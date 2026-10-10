// motion.cpp — vibration RMS + the four-state machine.
//
//   want = dizzyUntil  -> DIZZY      shaken (any axis) or spun in yaw, in a hand or not
//        : handCtx     -> HAPPY      in a hand
//        : fast        -> FAST
//        :                NORMAL
//
// Each condition owns its hysteresis. An unregistered state (STATE_* in config.h) falls
// through; in a hand that is HAPPY if registered, else NORMAL — never FAST.
#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "motion.h"

static const char *NAMES[] = { "NORMAL", "FAST", "DIZZY", "HAPPY" };
const char *carStateName(CarState s) { return NAMES[s]; }

// Ladder rank per state. Separate from the enum, which is slot order and append-only.
static const uint8_t PRIORITY[] = { 0, 1, 3, 2 };   // NORMAL, FAST, DIZZY, HAPPY

static const float SAMPLE_S = SAMPLE_US / 1000000.0f;

static Motion  m;
static float   biasX, biasY, biasZ;

// sliding window: squared magnitude and per-axis values, with running sums
static float   sqBuf[RMS_WINDOW], xBuf[RMS_WINDOW], yBuf[RMS_WINDOW], zBuf[RMS_WINDOW];
static float   sqSum, xSum, ySum, zSum;
static int     winIdx;

static uint32_t restSince;
static CarState candidate; static uint32_t candidateSince;

static bool     fastCond;
static uint32_t fastSince;
static uint32_t dizzyUntil;               // DIZZY wanted until then (shake or spin)
static uint32_t rollSince, stillSince, quietSince, rollHoldSince;

// Shake, per axis: which side it is past (0 = re-armed), and when its last SWING_RING
// swings were.
#define SWING_RING 16
static int8_t   swingSide[3];
static uint32_t swingAt[3][SWING_RING];
static int      swingIdx[3];

// Spin: its direction (0 = none), when it started, when |gz| was last above SPIN_GZ_DPS.
static int8_t   spinDir;
static uint32_t spinStart, spinLast;

// DIZZY_AFTER_MS includes the step-down, so the wanted window is the rest of it.
static_assert(DIZZY_AFTER_MS >= EXIT_HOLD_MS, "DIZZY_AFTER_MS includes EXIT_HOLD_MS");
static const uint32_t DIZZY_WANT_MS = DIZZY_AFTER_MS - EXIT_HOLD_MS;

static GyroRaw  h1, h2;                   // the two raw samples before this one
static int      histN;

static int16_t med3(int16_t a, int16_t b, int16_t c) {
  return a < b ? (b < c ? b : (a < c ? c : a))
               : (a < c ? a : (b < c ? c : b));
}

static GyroRaw despike(const GyroRaw &raw) {
#if DESPIKE_MEDIAN3
  GyroRaw out = raw;
  if (histN >= 2) {
    out.x = med3(h2.x, h1.x, raw.x);
    out.y = med3(h2.y, h1.y, raw.y);
    out.z = med3(h2.z, h1.z, raw.z);
  } else histN++;
  h2 = h1; h1 = raw;          // keep raw history so a spike can't influence later output
  return out;
#else
  return raw;
#endif
}

void motionSetBias(float bx, float by, float bz) { biasX = bx; biasY = by; biasZ = bz; }

// Median per axis, then the bias is the mean of the blocks within CALIB_AGREE_DPS of it
// on every axis.
bool motionCalibFit(const float blk[][3], int n, float bias[3], int &agree) {
  if (n > CALIB_BLOCKS) n = CALIB_BLOCKS;
  float med[3];
  for (int a = 0; a < 3; a++) {
    float v[CALIB_BLOCKS];
    for (int i = 0; i < n; i++) {                 // insertion sort
      float x = blk[i][a]; int j = i;
      for (; j > 0 && v[j - 1] > x; j--) v[j] = v[j - 1];
      v[j] = x;
    }
    med[a] = v[n / 2];
  }
  double sum[3] = { 0, 0, 0 };
  agree = 0;
  for (int i = 0; i < n; i++) {
    bool ok = true;
    for (int a = 0; a < 3; a++) ok = ok && fabsf(blk[i][a] - med[a]) <= CALIB_AGREE_DPS;
    if (!ok) continue;
    for (int a = 0; a < 3; a++) sum[a] += blk[i][a];
    agree++;
  }
  if (agree < CALIB_MIN_AGREE) return false;
  for (int a = 0; a < 3; a++) bias[a] = sum[a] / agree;
  return true;
}

void motionReset() {
  uint32_t now = millis();
  m = Motion{};
  for (int i = 0; i < RMS_WINDOW; i++) sqBuf[i] = xBuf[i] = yBuf[i] = zBuf[i] = 0;
  sqSum = xSum = ySum = zSum = 0; winIdx = 0;
  restSince = candidateSince = now;
  candidate = NORMAL;
  fastCond = false;
  fastSince = dizzyUntil = now;
  for (int a = 0; a < 3; a++) {
    swingSide[a] = 0; swingIdx[a] = 0;
    for (int i = 0; i < SWING_RING; i++) swingAt[a][i] = 0;
  }
  spinDir = 0; spinStart = spinLast = now;
  rollSince = stillSince = quietSince = rollHoldSince = now;
  h1 = h2 = GyroRaw{}; histN = 0;
}

const Motion &motionGet() { return m; }

// Enter only after FAST_HOLD_MS of continuous loudness (a tap never lasts); leave on
// level alone — EXIT_HOLD_MS decides how long the face stays.
static void updateFast(uint32_t now) {
  if (fastCond) { if (m.rms <= FAST_EXIT_RMS) fastCond = false; return; }
  if (m.rms <= FAST_ENTER_RMS) { fastSince = now; return; }
  if (now - fastSince >= FAST_HOLD_MS) fastCond = true;
}

// A swing = an axis crossing ±SHAKE_DPS; that side re-arms below SHAKE_REARM_DPS. Each axis
// counts on its own: enough swings on any one inside the window, and every further swing
// on it keeps DIZZY going.
static void updateShakeAxis(int a, float g, uint32_t now) {
  int8_t side = g > SHAKE_DPS ? 1 : g < -SHAKE_DPS ? -1 : 0;
  bool swung = side && side != swingSide[a];
  if (swung) {
    swingAt[a][swingIdx[a]] = now;
    swingIdx[a] = (swingIdx[a] + 1) % SWING_RING;
  }
  if (side) swingSide[a] = side;
  else if (fabsf(g) < SHAKE_REARM_DPS) swingSide[a] = 0;

  int n = 0;
  for (int i = 0; i < SWING_RING; i++)
    if (swingAt[a][i] && now - swingAt[a][i] < SHAKE_WINDOW_MS) n++;
  m.swings[a] = n;
  if (swung && n >= SHAKE_SWINGS) dizzyUntil = now + DIZZY_WANT_MS;
}

static void updateShake(uint32_t now) {
  updateShakeAxis(0, m.gx, now);
  updateShakeAxis(1, m.gy, now);
  updateShakeAxis(2, m.gz, now);
}

// |gz| held above SPIN_GZ_DPS one way; a reversal starts over, a dip longer than SPIN_DIP_MS
// ends it.
static void updateSpin(uint32_t now) {
  int8_t dir = m.gz > SPIN_GZ_DPS ? 1 : m.gz < -SPIN_GZ_DPS ? -1 : 0;
  if (dir && dir == spinDir) spinLast = now;
  else if (dir) { spinDir = dir; spinStart = spinLast = now; }
  else if (spinDir && now - spinLast > SPIN_DIP_MS) spinDir = 0;

  m.spinMs = spinDir ? spinLast - spinStart : 0;
  if (dir && m.spinMs >= SPIN_HOLD_MS) dizzyUntil = now + DIZZY_WANT_MS;
}

// Floor -> hand on unsigned roll travel, hand -> floor on stillness. The accumulator
// survives zero-crossings; only HAND_QUIET_MS of continuous quiet clears it.
static void updateContext(uint32_t now) {
  if (m.handCtx) {
    if (m.rms > STILL_RMS) stillSince = now;
    else if (now - stillSince >= HAND_SETTLE_MS) { m.handCtx = false; m.rollAcc = 0; }
    return;
  }
  if (fabsf(m.gy) >= HAND_QUIET_DPS) {
    quietSince = now;
    if (now - rollSince > HAND_TILT_MS) { m.rollAcc = 0; rollSince = now; }
    if (now - rollHoldSince >= HAND_ROLL_MS) m.rollAcc += fabsf(m.gy) * SAMPLE_S;
    if (m.rollAcc >= HAND_TILT_DEG) { m.handCtx = true; stillSince = now; }
  } else {
    rollHoldSince = now;
    if (now - quietSince >= HAND_QUIET_MS) { m.rollAcc = 0; rollSince = now; }
  }
}

static bool updateState(uint32_t now) {
  // Plain ifs, not #if, so disabled states keep compiling.
  updateShake(now); updateSpin(now);               // always: the dashboard shows them
  if (USE_GYRO) updateContext(now);
  if (STATE_FAST) updateFast(now);

  CarState want = STATE_DIZZY && (int32_t)(now - dizzyUntil) < 0 ? DIZZY
                : m.handCtx                        ? (STATE_HAPPY ? HAPPY : NORMAL)
                : STATE_FAST && fastCond           ? FAST
                :                                    NORMAL;

  if (want != candidate) { candidate = want; candidateSince = now; }
  if (candidate == m.state) return false;
  uint32_t hold = PRIORITY[candidate] > PRIORITY[m.state] ? ENTER_HOLD_MS : EXIT_HOLD_MS;
  if (now - candidateSince >= hold) { m.state = candidate; return true; }
  return false;
}

bool motionUpdate(const GyroRaw &in) {
  const GyroRaw raw = despike(in);
  const float k = gyroSensitivity();
  m.gx = raw.x * k - biasX;
  m.gy = raw.y * k - biasY;
  m.gz = raw.z * k - biasZ;

  float sq = m.gx * m.gx + m.gy * m.gy + m.gz * m.gz;
  sqSum += sq   - sqBuf[winIdx];
  xSum  += m.gx - xBuf[winIdx];
  ySum  += m.gy - yBuf[winIdx];
  zSum  += m.gz - zBuf[winIdx];
  sqBuf[winIdx] = sq;
  xBuf[winIdx] = m.gx; yBuf[winIdx] = m.gy; zBuf[winIdx] = m.gz;
  winIdx = (winIdx + 1) % RMS_WINDOW;
  // Running sums drift: a hard shake's squares (~1e6) leave a float rounding residue that
  // never washes out — rms read NaN at rest and froze FAST on. Re-add once per window.
  if (winIdx == 0) {
    sqSum = xSum = ySum = zSum = 0;
    for (int i = 0; i < RMS_WINDOW; i++) {
      sqSum += sqBuf[i]; xSum += xBuf[i]; ySum += yBuf[i]; zSum += zBuf[i];
    }
  }
  m.rms = sqrtf(fmaxf(sqSum, 0) / RMS_WINDOW);

  // jitter = RMS with the window mean removed: var = E[g²] - E[g]²
  float mx = xSum / RMS_WINDOW, my = ySum / RMS_WINDOW, mz = zSum / RMS_WINDOW;
  float var = sqSum / RMS_WINDOW - (mx * mx + my * my + mz * mz);
  m.jitter = var > 0 ? sqrtf(var) : 0;

  uint32_t now = millis();

  // At rest, leak the residual mean into the bias: fixes drift and a bias poisoned at boot.
  // Steady and fast is a spin, not an offset.
  bool steadySpin = fabsf(mx) > REZERO_MAX_DPS || fabsf(my) > REZERO_MAX_DPS ||
                    fabsf(mz) > REZERO_MAX_DPS;
  if (m.jitter > REST_JITTER_DPS || steadySpin) restSince = now;
  else if (now - restSince >= REZERO_AFTER_MS) {
    const float leak = SAMPLE_S / (REZERO_TAU_MS / 1000.0f);
    biasX += mx * leak; biasY += my * leak; biasZ += mz * leak;
  }

  return updateState(now);
}
