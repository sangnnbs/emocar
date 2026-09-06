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
  float rmsPeak, gzPeak;  // peak hold over PEAK_HOLD_MS — for threshold tuning
  float rollAcc;          // accumulated roll angle (deg) — the context bit's input
  bool  handCtx;          // true = in a hand; gates HAPPY on and FAST off
  CarState state;
  uint32_t sampleRate;    // samples processed in the last second
};

void        motionSetBias(float bx, float by, float bz);
void        motionReset();
// Returns true if the state changed on this sample.
bool        motionUpdate(const GyroRaw &raw);
const Motion &motionGet();
void        motionTick();   // call every loop: maintains the 1-s sample-rate counter
