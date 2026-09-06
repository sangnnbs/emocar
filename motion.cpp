// motion.cpp — vibration RMS + the four-state machine (PLANS.md P2).
//
// Three condition flags feed one want-chain. Each condition owns its own hysteresis
// so the chain itself stays a straight priority list:
//
//   want = spin || shake  -> DIZZY     violent rotation, either kind
//        : dizzyUntil     -> DIZZY     the wooze it earned afterwards
//        : handCtx        -> HAPPY     someone is holding the car
//        : fast           -> FAST      floor only: handCtx short-circuits this
//        :                   NORMAL
//
// The context bit is what keeps FAST a floor state and HAPPY a hand state: they are
// two situations, not two magnitudes, so no threshold can separate them (pushing a
// car across a desk and holding it shake the gyro about equally hard).
#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "motion.h"

static const char *NAMES[] = { "NORMAL", "FAST", "DIZZY", "HAPPY" };
const char *carStateName(CarState s) { return NAMES[s]; }

static const float SAMPLE_S = SAMPLE_US / 1000000.0f;

static Motion  m;
static float   biasX, biasY, biasZ;
static float   rmsBuf[RMS_WINDOW]; static int rmsIdx; static float rmsSum;
static uint32_t peakSince, rateSince, sampleCount;
static CarState candidate; static uint32_t condSince;

static bool     spinCond, shakeCond, fastCond;
static int      spinDir; static uint32_t spinSince, shakeSince;
static float    dose;                     // Σ|g|·dt while spinning/shaking, degrees
static uint32_t dizzyUntil;               // wooze deadline armed when that stops
static uint32_t rollSince, stillSince, quietSince;   // context-bit windows
static bool     doseActive;               // spin/shake was running on the last sample

void motionSetBias(float bx, float by, float bz) { biasX = bx; biasY = by; biasZ = bz; }

void motionReset() {
  m = Motion{};
  for (int i = 0; i < RMS_WINDOW; i++) rmsBuf[i] = 0;
  rmsIdx = 0; rmsSum = 0; sampleCount = 0;
  peakSince = rateSince = condSince = rollSince = stillSince = quietSince = millis();
  candidate = NORMAL;
  spinCond = shakeCond = fastCond = false;
  spinDir = 0; spinSince = shakeSince = millis();
  dose = 0; dizzyUntil = millis(); doseActive = false;
}

const Motion &motionGet() { return m; }

void motionTick() {
  if (millis() - rateSince >= 1000) {
    rateSince = millis();
    m.sampleRate = sampleCount; sampleCount = 0;
  }
}

// A real spin holds one direction; a hand shake reverses every ~100 ms, which restarts
// the timer and never reaches HOLD_MS. Leaving takes slowing down, not a sign flip —
// otherwise every wobble mid-spin would end the state.
static void updateSpin(uint32_t now) {
  float az  = fabsf(m.gz);
  int   dir = (m.gz > 0) - (m.gz < 0);
  if (spinCond) {
    if (az < SPIN_EXIT_DPS) spinCond = false;
    return;
  }
  if (az < SPIN_ENTER_DPS || dir != spinDir) { spinDir = dir; spinSince = now; return; }
  if (now - spinSince >= HOLD_MS) spinCond = true;
}

// The shake path ignores direction entirely and keys on sheer magnitude: no reversal
// counting, no per-axis logic. SHAKE_HOLD_MS is what separates a shake from one jolt.
static void updateShake(uint32_t now) {
  if (shakeCond) {
    if (m.rms < SHAKE_EXIT_RMS) shakeCond = false;
    return;
  }
  if (m.rms < SHAKE_ENTER_RMS) { shakeSince = now; return; }
  if (now - shakeSince >= SHAKE_HOLD_MS) shakeCond = true;
}

// Floor -> hand on roll travel, hand -> floor on stillness. Sticky on purpose: holding
// the car level for a moment must not read as putting it down.
//
// Travel is unsigned. A signed angle cancels itself out — turning the car over and back
// nets zero, yet it is still a thing only a hand can do. And the accumulator survives
// quiet *samples*: gy crosses zero several times a second during real handling, so
// clearing on the first quiet sample wiped the gesture before it ever reached the
// threshold (measured: 8 s of handling never got past 5 deg). Only a continuously quiet
// HAND_QUIET_MS — the car lying flat, not a direction change — clears it.
static void updateContext(uint32_t now) {
  if (m.handCtx) {
    if (m.rms > STILL_RMS) stillSince = now;
    else if (now - stillSince >= HAND_SETTLE_MS) { m.handCtx = false; m.rollAcc = 0; }
    return;
  }
  if (fabsf(m.gy) >= HAND_QUIET_DPS) {
    quietSince = now;
    if (now - rollSince > HAND_TILT_MS) { m.rollAcc = 0; rollSince = now; }   // window expired
    m.rollAcc += fabsf(m.gy) * SAMPLE_S;                                      // dps * s = degrees
    if (m.rollAcc >= HAND_TILT_DEG) { m.handCtx = true; stillSince = now; }
  } else if (now - quietSince >= HAND_QUIET_MS) {   // lying flat, not just passing through 0
    m.rollAcc = 0; rollSince = now;
  }
}

// Dose is the whole aftermath mechanism: accumulate rotation while it is happening,
// spend it as screen time when it stops. Spin longer or shake harder -> dizzier.
static void updateDizzyDose(uint32_t now, float mag) {
  bool active = spinCond || shakeCond;
  if (active) {
    dose += mag * SAMPLE_S;
  } else if (doseActive) {
    uint32_t ms = (uint32_t)(dose * DIZZY_MS_PER_DEG);
    if (ms < DIZZY_MIN_MS) ms = DIZZY_MIN_MS;
    if (ms > DIZZY_MAX_MS) ms = DIZZY_MAX_MS;
    dizzyUntil = now + ms;
    dose = 0;
  }
  doseActive = active;
}

static bool updateState(uint32_t now, float mag) {
  updateSpin(now);
  updateShake(now);
  updateContext(now);
  updateDizzyDose(now, mag);
  fastCond = fastCond ? (m.rms > FAST_EXIT_RMS) : (m.rms > FAST_ENTER_RMS);

  CarState want = (spinCond || shakeCond)          ? DIZZY
                : (int32_t)(now - dizzyUntil) < 0  ? DIZZY
                : m.handCtx                        ? HAPPY
                : fastCond                         ? FAST
                :                                    NORMAL;

  if (want != candidate) { candidate = want; condSince = now; }
  if (candidate != m.state && now - condSince >= HOLD_MS) { m.state = candidate; return true; }
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

  uint32_t now = millis();
  if (now - peakSince > PEAK_HOLD_MS) { m.rmsPeak = 0; m.gzPeak = 0; peakSince = now; }
  if (m.rms > m.rmsPeak)      m.rmsPeak = m.rms;
  if (fabsf(m.gz) > m.gzPeak) m.gzPeak  = fabsf(m.gz);

  return updateState(now, sqrtf(sq));
}
