// gyro.h — L3G4200D driver (I2C, FIFO stream mode).
#pragma once
#include <stdint.h>

struct GyroRaw { int16_t x, y, z; };

enum GyroError { GYRO_OK = 0, GYRO_NO_ACK, GYRO_BAD_ID };

// Max samples per gyroReadFifo(): 21 * 6 = 126 bytes fits the 128-byte Wire buffer.
#define GYRO_FIFO_MAX_READ 21

GyroError   gyroBegin(uint8_t &whoAmI);           // probe, configure, leave the FIFO off and empty
const char *gyroErrorText(GyroError e);

bool gyroDataReady();                             // new sample available (FIFO off)
bool gyroReadRaw(GyroRaw &s);                     // single sample (FIFO off)

void gyroFifoEnable();                            // switch to stream mode
int  gyroReadFifo(GyroRaw *out, int maxN, bool &overrun);

float gyroSensitivity();                          // dps per LSB for GYRO_FS_DPS
