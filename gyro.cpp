// gyro.cpp — L3G4200D register-level driver.
#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "gyro.h"

// ---------- registers ----------
#define REG_WHO_AM_I   0x0F   // -> 0xD3
#define REG_CTRL_REG1  0x20   // DR/BW/PD/Zen/Yen/Xen
#define REG_CTRL_REG4  0x23   // BDU, FS
#define REG_CTRL_REG5  0x24   // bit6 FIFO_EN
#define REG_STATUS     0x27   // bit3 ZYXDA
#define REG_OUT_X_L    0x28
#define REG_FIFO_CTRL  0x2E   // bits7:5 FM: 010 = stream
#define REG_FIFO_SRC   0x2F   // bit6 OVRN, bit5 EMPTY, bits4:0 count
#define AUTO_INC       0x80   // bit7 of register address = auto-increment
#define WHO_AM_I_VAL   0xD3

// ---------- CTRL_REG1 from SAMPLE_US ----------
#if   SAMPLE_US == 10000
  #define CTRL1_VAL 0x3F   // 100 Hz, BW 25 Hz
#elif SAMPLE_US == 5000
  #define CTRL1_VAL 0x7F   // 200 Hz, BW 70 Hz
#elif SAMPLE_US == 2500
  #define CTRL1_VAL 0xBF   // 400 Hz, BW 110 Hz
#elif SAMPLE_US == 1250
  #define CTRL1_VAL 0xFF   // 800 Hz, BW 110 Hz
#else
  #error "SAMPLE_US must be 10000, 5000, 2500 or 1250"
#endif

// ---------- CTRL_REG4 / sensitivity from GYRO_FS_DPS ----------
#if   GYRO_FS_DPS == 250
  #define FS_BITS 0x00
  #define SENS_DPS 0.00875f
#elif GYRO_FS_DPS == 500
  #define FS_BITS 0x10
  #define SENS_DPS 0.0175f
#elif GYRO_FS_DPS == 2000
  #define FS_BITS 0x20
  #define SENS_DPS 0.070f
#else
  #error "GYRO_FS_DPS must be 250, 500 or 2000"
#endif
#define CTRL4_VAL (0x80 | FS_BITS)   // BDU on so high/low bytes are never torn

static const int FIFO_MAX_PER_READ = 21;   // 21 * 6 = 126 bytes < 128-byte Wire buffer

// ---------- low-level ----------
static bool regWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(GYRO_ADDR);
  Wire.write(reg); Wire.write(val);
  return Wire.endTransmission() == 0;
}

static uint8_t regRead(uint8_t reg) {
  Wire.beginTransmission(GYRO_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)GYRO_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

static void unpack(GyroRaw &s) {
  uint8_t b[6]; for (int k = 0; k < 6; k++) b[k] = Wire.read();
  s.x = (int16_t)(b[1] << 8 | b[0]);
  s.y = (int16_t)(b[3] << 8 | b[2]);
  s.z = (int16_t)(b[5] << 8 | b[4]);
}

// ---------- public ----------
float gyroSensitivity() { return SENS_DPS; }

const char *gyroErrorText(GyroError e) {
  switch (e) {
    case GYRO_OK:     return "ok";
    case GYRO_NO_ACK: return "no ACK — CS->3V3?";
    case GYRO_BAD_ID: return "WHO_AM_I != 0xD3";
  }
  return "?";
}

GyroError gyroBegin(uint8_t &whoAmI) {
  whoAmI = 0;
  Wire.beginTransmission(GYRO_ADDR);
  if (Wire.endTransmission() != 0) return GYRO_NO_ACK;

  whoAmI = regRead(REG_WHO_AM_I);
  if (whoAmI != WHO_AM_I_VAL) return GYRO_BAD_ID;

  regWrite(REG_CTRL_REG1, CTRL1_VAL);   // power on, ODR, XYZ enabled (reset default is power-down)
  regWrite(REG_CTRL_REG4, CTRL4_VAL);   // BDU + full scale
  // An ESP32 reset does not power-cycle the gyro, so it still holds whatever the last run
  // left: FIFO on, stream mode, 32 samples queued and OVRN set (dumped on the bench). Put
  // it back in the state the callers assume — FIFO off, so calibrate() reads live data,
  // and bypass, which also empties the FIFO so gyroFifoEnable() starts clean.
  regWrite(REG_CTRL_REG5, 0x00);        // FIFO_EN off
  regWrite(REG_FIFO_CTRL, 0x00);        // bypass mode: empties the FIFO, clears OVRN
  delay(100);
  return GYRO_OK;
}

bool gyroDataReady() { return regRead(REG_STATUS) & 0x08; }

bool gyroReadRaw(GyroRaw &s) {
  Wire.beginTransmission(GYRO_ADDR);
  Wire.write(REG_OUT_X_L | AUTO_INC);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)GYRO_ADDR, (uint8_t)6) != 6) return false;
  unpack(s);
  return true;
}

void gyroFifoEnable() {
  regWrite(REG_FIFO_CTRL, 0x40);   // FM = 010 stream mode
  regWrite(REG_CTRL_REG5, 0x40);   // FIFO_EN — starts empty, gyroBegin() left it in bypass
}

int gyroReadFifo(GyroRaw *out, int maxN, bool &overrun) {
  uint8_t src = regRead(REG_FIFO_SRC);
  overrun = src & 0x40;
  int n = src & 0x1F;
  if (n == 0) return 0;
  if (n > maxN) n = maxN;
  if (n > FIFO_MAX_PER_READ) n = FIFO_MAX_PER_READ;

  Wire.beginTransmission(GYRO_ADDR);
  Wire.write(REG_OUT_X_L | AUTO_INC);   // in FIFO mode the address wraps 0x2D -> 0x28
  if (Wire.endTransmission(false) != 0) return 0;
  int want = n * 6;
  if (Wire.requestFrom((uint8_t)GYRO_ADDR, (uint8_t)want) != want) return 0;
  for (int i = 0; i < n; i++) unpack(out[i]);
  return n;
}
