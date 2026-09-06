// motion.cpp — vibration RMS + the four-state machine (PLANS.md P2).
//
// Two condition flags feed one want-chain. Each condition owns its own hysteresis
// so the chain itself stays a straight priority list:
//
//   want = handCtx     -> HAPPY      in a hand: nothing else applies
//        : spin        -> DIZZY      a real spin on the floor
//        : dizzyUntil  -> DIZZY      the wooze it earned afterwards
//        : fast        -> FAST
//        :                NORMAL
//
// The context bit decides the *situation* before any threshold is consulted, and being
// held is the whole answer: while in a hand the car is simply happy. That is what makes
// the remaining thresholds tractable — FAST and DIZZY only ever have to describe a car on
// a surface, so neither has to be chosen to avoid being tripped by handling. Separating
// handling from shaking by magnitude is what could never be made to work.
#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "motion.h"

static const char *NAMES[] = { "NORMAL", "FAST", "DIZZY", "HAPPY" };
const char *carStateName(CarState s) { return NAMES[s]; }

// Where each state sits on the ladder: HAPPY > DIZZY > FAST > NORMAL. Kept separate from
// the enum because the enum is *slot order* and must stay append-only. HAPPY is on top
// because it is not an intensity at all — it is the car reporting *where it is*, and that
// outranks anything it might otherwise say about how it is moving.
// Currently identical to the enum order, which is a coincidence worth keeping the map
// for: slots are append-only, so the next state added (STARTLED, say) would sit above
// FAST at slot 4 and break the coincidence immediately.
static const uint8_t PRIORITY[] = { 0, 1, 2, 3 };   // NORMAL, FAST, DIZZY, HAPPY

static const float SAMPLE_S = SAMPLE_US / 1000000.0f;

static Motion  m;
static float   biasX, biasY, biasZ;
static float   rmsBuf[RMS_WINDOW]; static int rmsIdx; static float rmsSum;
static float   xBuf[RMS_WINDOW], yBuf[RMS_WINDOW], zBuf[RMS_WINDOW];   // per-axis means
static float   xSum, ySum, zSum;
static uint32_t restSince;              // how long jitter has been at rest level
static uint32_t peakSince, rateSince, sampleCount;
static CarState candidate; static uint32_t condSince;

static bool     spinCond, fastCond;
static int      spinDir; static uint32_t spinSince;
static float    dose;                     // Σ|gz|·dt while spinning, i.e. degrees spun
static uint32_t dizzyUntil;               // wooze deadline armed when the spin stops
static uint32_t rollSince, stillSince, quietSince;   // context-bit windows
static bool     doseActive;               // the spin was running on the last sample

void motionSetBias(float bx, float by, float bz) { biasX = bx; biasY = by; biasZ = bz; }
void motionGetBias(float &bx, float &by, float &bz) { bx = biasX; by = biasY; bz = biasZ; }

void motionReset() {
  m = Motion{};
  for (int i = 0; i < RMS_WINDOW; i++) rmsBuf[i] = xBuf[i] = yBuf[i] = zBuf[i] = 0;
  rmsIdx = 0; rmsSum = xSum = ySum = zSum = 0; sampleCount = 0;
  peakSince = rateSince = condSince = rollSince = stillSince = quietSince = millis();
  restSince = millis();
  candidate = NORMAL;
  spinCond = fastCond = false;
  spinDir = 0; spinSince = millis();
  dose = 0; dizzyUntil = millis(); doseActive = false;
}

const Motion &motionGet() { return m; }

void motionTick() {
  if (millis() - rateSince >= 1000) {
    rateSince = millis();
    m.sampleRate = sampleCount; sampleCount = 0;
  }
}

// A real spin holds one direction, which a wobble or a bump cannot fake. Leaving takes
// slowing down, not a sign flip — otherwise every wobble mid-spin would end the state.
static void updateSpin(uint32_t now) {
  float az  = fabsf(m.gz);
  int   dir = (m.gz > 0) - (m.gz < 0);
  if (spinCond) {
    if (az < SPIN_EXIT_DPS) spinCond = false;
    return;
  }
  if (az < SPIN_ENTER_DPS || dir != spinDir) { spinDir = dir; spinSince = now; return; }
  if (now - spinSince >= SPIN_HOLD_MS) spinCond = true;
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

// Dose is the whole aftermath mechanism: accumulate the rotation while it is happening,
// spend it as screen time when it stops. Spin longer or faster -> dizzier for longer.
static void updateDizzyDose(uint32_t now) {
  bool active = spinCond;
  if (active) {
    dose += fabsf(m.gz) * SAMPLE_S;       // degrees actually spun
  } else if (doseActive) {
    uint32_t ms = (uint32_t)(dose * DIZZY_MS_PER_DEG);
    if (ms < DIZZY_MIN_MS) ms = DIZZY_MIN_MS;
    if (ms > DIZZY_MAX_MS) ms = DIZZY_MAX_MS;
    dizzyUntil = now + ms;
    dose = 0;
  }
  doseActive = active;
}

static bool updateState(uint32_t now) {
  updateSpin(now);
  updateContext(now);
  updateDizzyDose(now);
  fastCond = fastCond ? (m.rms > FAST_EXIT_RMS) : (m.rms > FAST_ENTER_RMS);

  CarState want = m.handCtx                        ? HAPPY   // in a hand: that is the answer
                : spinCond                         ? DIZZY
                : (int32_t)(now - dizzyUntil) < 0  ? DIZZY   // aftermath, ∝ degrees spun
                : fastCond                         ? FAST
                :                                    NORMAL;

  if (want != candidate) { candidate = want; condSince = now; }
  if (candidate == m.state) return false;
  uint32_t hold = PRIORITY[candidate] > PRIORITY[m.state] ? ENTER_HOLD_MS : EXIT_HOLD_MS;
  if (now - condSince >= hold) { m.state = candidate; return true; }
  return false;
}

bool motionUpdate(const GyroRaw &raw) {
  const float k = gyroSensitivity();
  m.gx = raw.x * k - biasX;
  m.gy = raw.y * k - biasY;
  m.gz = raw.z * k - biasZ;

  float sq = m.gx * m.gx + m.gy * m.gy + m.gz * m.gz;
  rmsSum += sq     - rmsBuf[rmsIdx];          // running sums, O(1)
  xSum   += m.gx   - xBuf[rmsIdx];
  ySum   += m.gy   - yBuf[rmsIdx];
  zSum   += m.gz   - zBuf[rmsIdx];
  rmsBuf[rmsIdx] = sq;
  xBuf[rmsIdx] = m.gx; yBuf[rmsIdx] = m.gy; zBuf[rmsIdx] = m.gz;
  rmsIdx = (rmsIdx + 1) % RMS_WINDOW;
  m.rms = sqrtf(rmsSum / RMS_WINDOW);
  sampleCount++;

  // Jitter = spread about the window mean, i.e. RMS with any constant offset removed
  // (var = E[g²] - E[g]²). This is the only honest "is it being moved?" test, because a
  // bad bias inflates the mean and leaves the spread untouched.
  float mx = xSum / RMS_WINDOW, my = ySum / RMS_WINDOW, mz = zSum / RMS_WINDOW;
  float var = rmsSum / RMS_WINDOW - (mx * mx + my * my + mz * mz);
  m.jitter = var > 0 ? sqrtf(var) : 0;

  uint32_t now = millis();

  // Continuous re-zeroing (P4.2). While the car is demonstrably not being moved, leak the
  // residual mean into the bias. This corrects temperature drift, and — the reason it
  // exists — a bias poisoned by a car that was moving at boot: the car recovers on its own
  // once it is set down, instead of being wrong until the next reset.
  if (m.jitter > REST_JITTER_DPS) restSince = now;
  else if (now - restSince >= REZERO_AFTER_MS) {
    const float k = SAMPLE_S / (REZERO_TAU_MS / 1000.0f);
    biasX += mx * k; biasY += my * k; biasZ += mz * k;
  }
  if (now - peakSince > PEAK_HOLD_MS) { m.rmsPeak = 0; m.gzPeak = 0; peakSince = now; }
  if (m.rms > m.rmsPeak)      m.rmsPeak = m.rms;
  if (fabsf(m.gz) > m.gzPeak) m.gzPeak  = fabsf(m.gz);

  return updateState(now);
}
