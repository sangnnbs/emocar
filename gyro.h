// gyro.h — L3G4200D driver (I2C, FIFO stream mode). No app logic here.
#pragma once
#include <stdint.h>

struct GyroRaw { int16_t x, y, z; };

// Returns 0 on success, otherwise an error code (see gyroErrorText).
enum GyroError { GYRO_OK = 0, GYRO_NO_ACK, GYRO_BAD_ID };

GyroError   gyroBegin(uint8_t &whoAmI);          // probe, WHO_AM_I, CTRL_REG1/4 from config.h, FIFO off
const char *gyroErrorText(GyroError e);

bool gyroDataReady();                             // STATUS_REG ZYXDA — used while FIFO is off
bool gyroReadRaw(GyroRaw &s);                     // single sample from OUT_X_L..OUT_Z_H

void gyroFifoEnable();                            // stream mode, from the empty FIFO gyroBegin() left
// Drain up to maxN samples (≤ 21 per call — ESP32 Wire buffer). Sets overrun if the FIFO wrapped.
int  gyroReadFifo(GyroRaw *out, int maxN, bool &overrun);

float gyroSensitivity();                          // dps per LSB for GYRO_FS_DPS
