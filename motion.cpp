// motion.cpp — vibration RMS + hysteresis/hold state machine (emocar doc §6).
#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "motion.h"

static const char *NAMES[] = { "NORMAL", "FAST", "SPINNING" };
const char *carStateName(CarState s) { return NAMES[s]; }

static Motion  m;
static float   biasX, biasY, biasZ;
static float   rmsBuf[RMS_WINDOW]; static int rmsIdx; static float rmsSum;
static uint32_t peakSince, rateSince, sampleCount;
static CarState candidate; static int candDir; static uint32_t condSince;

void motionSetBias(float bx, float by, float bz) { biasX = bx; biasY = by; biasZ = bz; }

void motionReset() {
  m = Motion{};
  for (int i = 0; i < RMS_WINDOW; i++) rmsBuf[i] = 0;
  rmsIdx = 0; rmsSum = 0; sampleCount = 0;
  peakSince = rateSince = condSince = millis();
  candidate = NORMAL; candDir = 0;
}

const Motion &motionGet() { return m; }

void motionTick() {
  if (millis() - rateSince >= 1000) {
    rateSince = millis();
    m.sampleRate = sampleCount; sampleCount = 0;
  }
}

// Priority: SPINNING > FAST > NORMAL (spinning also vibrates).
// A real spin keeps one direction; hand shaking flips sign every ~100 ms, which
// restarts the hold timer and keeps it in FAST instead of SPINNING.
static bool updateState() {
  float az  = fabsf(m.gz);
  int   dir = (m.gz > 0) - (m.gz < 0);
  CarState want;
  if (m.state == SPINNING)      want = (az > SPIN_EXIT_DPS) ? SPINNING
                                     : (m.rms > FAST_ENTER_RMS ? FAST : NORMAL);
  else if (az > SPIN_ENTER_DPS) want = SPINNING;
  else if (m.state == FAST)     want = (m.rms > FAST_EXIT_RMS) ? FAST : NORMAL;
  else if (m.rms > FAST_ENTER_RMS) want = FAST;
  else                          want = NORMAL;

  bool dirChanged = (want == SPINNING && dir != candDir);
  if (want != candidate || dirChanged) { candidate = want; candDir = dir; condSince = millis(); }
  if (candidate != m.state && millis() - condSince >= HOLD_MS) { m.state = candidate; return true; }
  return false;
}

bool motionUpdate(const GyroRaw &raw) {
  const float k = gyroSensitivity();
  m.gx = raw.x * k - biasX;
  m.gy = raw.y * k - biasY;
  m.gz = raw.z * k - biasZ;

  float sq = m.gx * m.gx + m.gy * m.gy + m.gz * m.gz;
  rmsSum += sq - rmsBuf[rmsIdx];              // running sum, O(1)
  rmsBuf[rmsIdx] = sq;
  rmsIdx = (rmsIdx + 1) % RMS_WINDOW;
  m.rms = sqrtf(rmsSum / RMS_WINDOW);
  sampleCount++;

  if (millis() - peakSince > PEAK_HOLD_MS) { m.rmsPeak = 0; m.gzPeak = 0; peakSince = millis(); }
  if (m.rms > m.rmsPeak)      m.rmsPeak = m.rms;
  if (fabsf(m.gz) > m.gzPeak) m.gzPeak  = fabsf(m.gz);

  return updateState();
}
