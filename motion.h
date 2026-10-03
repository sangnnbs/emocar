// motion.h — turns bias-corrected gyro rates into NORMAL / FAST / DIZZY / HAPPY.
// Pure logic: no I2C, no display. Feed it one sample per SAMPLE_US.
//
// Enum value = animation slot number. APPEND ONLY, NEVER REORDER: uploaded
// /anim<N>.qgif files and the server's assignments keep their meaning by index.
#pragma once
#include <stdint.h>
#include "gyro.h"

enum CarState { NORMAL, FAST, DIZZY, HAPPY };
#define CAR_STATE_COUNT 4
const char *carStateName(CarState s);

struct Motion {
  float gx, gy, gz;       // bias-corrected, dps
  float rms;              // 3-axis vibration RMS over RMS_WINDOW_MS
  float jitter;           // wobble around the mean
  float rollAcc;          // accumulated roll travel (deg)
  bool  handCtx;          // true = in a hand
  CarState state;
};

void        motionSetBias(float bx, float by, float bz);
void        motionReset();
// Given the mean rate of n calibration blocks, returns true and the bias if at least
// CALIB_MIN_AGREE of them agree. `agree` is the count.
bool        motionCalibFit(const float blk[][3], int n, float bias[3], int &agree);
bool        motionUpdate(const GyroRaw &raw);   // true if the state changed
const Motion &motionGet();
