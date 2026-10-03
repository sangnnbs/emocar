// gyro.cpp — L3G4200D register-level driver.
#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "gyro.h"

#define REG_WHO_AM_I   0x0F
#define REG_CTRL_REG1  0x20   // DR/BW/PD/Zen/Yen/Xen
#define REG_CTRL_REG4  0x23   // BDU, FS
#define REG_CTRL_REG5  0x24   // bit6 FIFO_EN
#define REG_STATUS     0x27   // bit3 ZYXDA
#define REG_OUT_X_L    0x28
#define REG_FIFO_CTRL  0x2E   // bits7:5 FM
#define REG_FIFO_SRC   0x2F   // bit6 OVRN, bit5 EMPTY, bits4:0 count
#define AUTO_INC       0x80
#define WHO_AM_I_VAL   0xD3

#define CTRL5_FIFO_EN   0x40
#define FIFO_MODE_BYPASS 0x00
#define FIFO_MODE_STREAM 0x40
#define STATUS_ZYXDA    0x08
#define FIFO_SRC_OVRN   0x40
#define FIFO_SRC_COUNT  0x1F

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
#define CTRL4_VAL (0x80 | FS_BITS)   // BDU on

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

// Read n samples starting at OUT_X_L (in FIFO mode the address wraps 0x2D -> 0x28).
static bool readSamples(GyroRaw *out, int n) {
  Wire.beginTransmission(GYRO_ADDR);
  Wire.write(REG_OUT_X_L | AUTO_INC);
  if (Wire.endTransmission(false) != 0) return false;
  int want = n * 6;
  if (Wire.requestFrom((uint8_t)GYRO_ADDR, (uint8_t)want) != want) return false;
  for (int i = 0; i < n; i++) {
    uint8_t b[6]; for (int k = 0; k < 6; k++) b[k] = Wire.read();
    out[i].x = (int16_t)(b[1] << 8 | b[0]);
    out[i].y = (int16_t)(b[3] << 8 | b[2]);
    out[i].z = (int16_t)(b[5] << 8 | b[4]);
  }
  return true;
}

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

  regWrite(REG_CTRL_REG1, CTRL1_VAL);
  regWrite(REG_CTRL_REG4, CTRL4_VAL);
  // An ESP32 reset does not power-cycle the gyro, which may still be streaming into a
  // full FIFO. FIFO off + bypass mode empties it and clears OVRN.
  regWrite(REG_CTRL_REG5, 0x00);
  regWrite(REG_FIFO_CTRL, FIFO_MODE_BYPASS);
  delay(100);
  return GYRO_OK;
}

bool gyroDataReady() { return regRead(REG_STATUS) & STATUS_ZYXDA; }

bool gyroReadRaw(GyroRaw &s) { return readSamples(&s, 1); }

void gyroFifoEnable() {
  regWrite(REG_FIFO_CTRL, FIFO_MODE_STREAM);
  regWrite(REG_CTRL_REG5, CTRL5_FIFO_EN);
}

int gyroReadFifo(GyroRaw *out, int maxN, bool &overrun) {
  uint8_t src = regRead(REG_FIFO_SRC);
  overrun = src & FIFO_SRC_OVRN;
  int n = src & FIFO_SRC_COUNT;
  if (n > maxN) n = maxN;
  if (n > GYRO_FIFO_MAX_READ) n = GYRO_FIFO_MAX_READ;
  if (n == 0) return 0;
  return readSamples(out, n) ? n : 0;
}
