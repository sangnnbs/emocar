// motion_test.cpp — runs the state machine on the host, against a fake clock.
//
// motion.cpp is pure logic (no I2C, no display), so it compiles unchanged here with a
// stub Arduino.h and a millis() we control. Each scenario feeds synthetic gyro samples
// at SAMPLE_US and asserts the state the car should be showing.
//
//   cd test && make test
//
// This is not a formality: it caught SHAKE_ENTER_RMS sitting below a pickup before the
// board was ever flashed. Its blind spot is the magnitudes themselves — a scenario is
// only as honest as the numbers fed in, and the first "hard shake" here was far gentler
// than a real one. Numbers marked "measured" come from the board; the rest are guesses.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

static uint32_t fakeMillis = 0;
uint32_t millis() { return fakeMillis; }
float gyroSensitivity() { return 0.07f; }        // FS 2000 dps

#include "motion.cpp"

static const float K = 0.07f;
static GyroRaw mk(float gx, float gy, float gz) {
  return GyroRaw{ (int16_t)lrintf(gx / K), (int16_t)lrintf(gy / K), (int16_t)lrintf(gz / K) };
}

// Feed ms worth of samples at SAMPLE_US, advancing the fake clock.
static void feed(float gx, float gy, float gz, int ms) {
  int n = ms * 1000 / SAMPLE_US;
  for (int i = 0; i < n; i++) {
    fakeMillis += SAMPLE_US / 1000;
    motionUpdate(mk(gx, gy, gz));
  }
}

static int fails = 0;
static void expect(const char *what, CarState want) {
  CarState got = motionGet().state;
  bool ok = got == want;
  if (!ok) fails++;
  printf("  %s %-46s want %-6s got %s\n", ok ? "ok  " : "FAIL", what,
         carStateName(want), carStateName(got));
}
static void expectCtx(const char *what, bool want) {
  bool got = motionGet().handCtx;
  bool ok = got == want;
  if (!ok) fails++;
  printf("  %s %-46s want %-6s got %s\n", ok ? "ok  " : "FAIL", what,
         want ? "hand" : "floor", got ? "hand" : "floor");
}

static void reset() { fakeMillis = 1000; motionReset(); }

// Boot calibration (P4.1). Samples are generated at SAMPLE_US from f(t) and averaged per
// CALIB_BLOCK_MS block, exactly as calibrate() does on the board, then fitted.
static const float BIAS[3] = { 0.8f, -1.2f, 0.4f };       // a plausible zero-rate offset
static bool calibOn(void (*f)(float t, float g[3]), float bias[3], int &agree) {
  float blk[CALIB_BLOCKS][3];
  const int per = CALIB_BLOCK_MS * 1000 / SAMPLE_US;
  for (int b = 0; b < CALIB_BLOCKS; b++) {
    double s[3] = { 0, 0, 0 };
    for (int i = 0; i < per; i++) {
      float g[3]; f((b * per + i) * SAMPLE_US / 1e6f, g);
      for (int a = 0; a < 3; a++) s[a] += BIAS[a] + g[a];
    }
    for (int a = 0; a < 3; a++) blk[b][a] = s[a] / per;
  }
  return motionCalibFit(blk, CALIB_BLOCKS, bias, agree);
}
static void expectCalib(const char *what, void (*f)(float, float[3]), bool wantOk) {
  float bias[3] = { 0, 0, 0 }; int agree;
  bool ok = calibOn(f, bias, agree);
  float err = 0;
  for (int a = 0; a < 3; a++) err = fmaxf(err, fabsf(bias[a] - BIAS[a]));
  bool pass = ok == wantOk && (!ok || err < 0.2f);   // accepted must also mean *right*
  if (!pass) fails++;
  printf("  %s %-46s %s (%2d/%d agree", pass ? "ok  " : "FAIL", what,
         ok ? "accepted" : "refused ", agree, CALIB_BLOCKS);
  if (ok) printf(", bias off by %.2f dps", err);
  printf(")\n");
}
// Deterministic noise: sensor noise ~0.3 dps per axis, as measured at rest.
static float noise(float t, int a) { return 0.3f * sinf(t * (977.0f + 131.0f * a)); }
static void still(float t, float g[3]) { for (int a = 0; a < 3; a++) g[a] = noise(t, a); }
// A tap at 0.6 s: a 60 dps kick that rings down at 8 Hz over ~250 ms, then nothing.
static void tap(float t, float g[3]) {
  still(t, g);
  float d = t - 0.6f;
  if (d >= 0 && d < 0.25f) {
    float r = 60.0f * expf(-d / 0.08f) * cosf(6.2832f * 8.0f * d);
    g[0] += r; g[1] += 0.5f * r; g[2] -= 0.3f * r;
  }
}
static void twoTaps(float t, float g[3]) { tap(t, g); float h[3]; tap(t - 0.7f, h);
  if (t >= 1.3f) for (int a = 0; a < 3; a++) g[a] += h[a] - noise(t - 0.7f, a); }
// A rumble through the floor the whole window: 3 dps at 35 Hz, every axis.
static void rumble(float t, float g[3]) {
  still(t, g);
  for (int a = 0; a < 3; a++) g[a] += 3.0f * sinf(6.2832f * 35.0f * t + a);
}
// Held in a palm: slow wandering turns, rms ~35 dps (measured p50), never settles.
static void palm(float t, float g[3]) {
  still(t, g);
  g[0] += 30.0f * sinf(6.2832f * 0.9f * t);
  g[1] += 35.0f * sinf(6.2832f * 1.3f * t + 1.0f);
  g[2] += 25.0f * sinf(6.2832f * 0.7f * t + 2.0f);
}
// Picked up for the first 0.9 s of the window, then set down: most blocks are spoiled.
static void setDownLate(float t, float g[3]) { if (t < 0.9f) palm(t, g); else still(t, g); }

int main() {
  // --- 1. idle on a desk stays NORMAL -------------------------------------
  printf("idle on a desk\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 3000);
  expect("still, 3 s", NORMAL);

  // --- 2. pushed across the floor: FAST, never HAPPY ------------------------
  printf("pushed across the floor (the case that used to read HAPPY)\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  feed(6.0f, 1.5f, 3.0f, 2000);           // rolling: pitch + yaw, roll stays small
  expect("rolling hard", FAST);
  expectCtx("context while rolling", false);
  feed(3.0f, 1.0f, 1.0f, 2000);           // slows into the old 2.5-4.0 "hand band"
  expect("coasting through the old hand band", FAST);   // hysteresis: 3.3 > FAST_EXIT_RMS
  expectCtx("context while coasting", false);

  // --- 3. picked up: tilt enters hand context ------------------------------
  printf("picked up off the desk\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  feed(4.0f, 30.0f, 4.0f, 1200);          // lift + tilt: 30 dps roll for 1.2 s = 36 deg
  expectCtx("after a pickup tilt", true);
  feed(2.0f, 2.0f, 2.0f, 1000);
  expect("held gently", HAPPY);

  // --- 4. held and waved briskly stays HAPPY (FAST is suppressed) ----------
  printf("waved briskly while held\n");
  feed(6.0f, 5.0f, 5.0f, 2000);
  expect("brisk waving in hand", HAPPY);
  expectCtx("context while waving", true);

  // --- 5. nothing interrupts HAPPY while the car is held -------------------
  // In a hand the car is simply happy: no magnitude, however violent, changes that. This
  // is what deleted the shake path — handling and shaking overlap on every RMS threshold
  // that was tried (8, then 45, then 120), and none of them ever separated the two.
  printf("shaken hard while held\n");
  feed(120.0f, 100.0f, 80.0f, 1500);      // far above anything the old shake path wanted
  expect("violent shaking, still in a hand", HAPPY);
  feed(2.0f, 2.0f, 2.0f, 1000);
  expect("and afterwards", HAPPY);

  // --- 6. put back down -> context clears ---------------------------------
  printf("put back down\n");
  feed(0.3f, 0.3f, 0.3f, 2500);
  expectCtx("after 2.5 s of stillness", false);
  feed(0.3f, 0.3f, 0.3f, 1200);           // HAPPY -> NORMAL is down the ladder: EXIT_HOLD_MS
  expect("resting on the desk", NORMAL);

  // --- 7. a real spin on the floor -> DIZZY, dose scales the tail ----------
  printf("spun on the floor\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  feed(2.0f, 1.0f, 120.0f, 3000);         // one sign, 120 dps = 360 deg spun
  expect("spinning", DIZZY);
  feed(0.5f, 0.5f, 0.5f, 1500);           // measured rest is 0.5-0.6 per axis
  expect("1.5 s after the spin (dose ~360 deg -> ~2.9 s)", DIZZY);
  feed(0.5f, 0.5f, 0.5f, 3000);
  expect("after the tail is spent", NORMAL);
  expectCtx("a floor spin must not read as a pickup", false);

  // --- 7b. picked up mid-wooze: being held outranks the aftermath ----------
  printf("picked up while still dizzy\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  feed(2.0f, 1.0f, 120.0f, 3000);         // spin, arming a long tail
  feed(0.5f, 0.5f, 0.5f, 500);
  expect("wooze running", DIZZY);
  feed(4.0f, 30.0f, 4.0f, 1200);          // lifted off the floor
  expect("in a hand now, wooze or not", HAPPY);

  // --- 8. hand shake must not be read as a spin ---------------------------
  printf("hand shake in yaw must not enter the spin path\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  for (int i = 0; i < 12; i++) feed(1.0f, 1.0f, i % 2 ? 60.0f : -60.0f, 100);
  printf("  %s spin path stayed closed (sign flips every 100 ms)\n", spinCond ? "FAIL" : "ok  ");
  if (spinCond) fails++;
  expect("a shaken car on the floor is not dizzy, just busy", FAST);

  // --- 9. fast attack, slow decay (P1) ------------------------------------
  // The same threshold crossed in both directions must not take the same time: going up
  // the ladder is a reaction, coming down is a feeling wearing off.
  printf("attack vs decay across one threshold\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  feed(6.0f, 1.5f, 3.0f, 400);            // up: NORMAL -> FAST inside ENTER_HOLD_MS
  expect("400 ms after the motion started", FAST);
  feed(0.5f, 0.5f, 0.5f, 600);            // down: well past ENTER_HOLD_MS...
  expect("600 ms after the motion stopped (still decaying)", FAST);
  feed(0.5f, 0.5f, 0.5f, 900);            // ...and past EXIT_HOLD_MS
  expect("1.5 s after the motion stopped", NORMAL);

  // --- 10. a poisoned bias heals itself (P4.2) -----------------------------
  // Reproduces what happened on the bench twice: the car booted while being handled, so
  // calibration baked an offset into the bias. Every later reading carries it, RMS never
  // falls below stillRms, and the car can never say it is resting on the floor again.
  // Boot now refuses such a window (P4.1); this checks the car also recovers on its own.
  printf("bias poisoned by 18 dps, car left sitting on the floor\n");
  reset();
  feed(0.4f, 0.3f, 18.0f, 2000);          // still, but reading a phantom 18 dps of yaw
  expect("phantom rotation is believed at first", FAST);
  printf("  ..  jitter %.2f dps while 'spinning' at 18 — level lies, spread does not\n",
         motionGet().jitter);
  feed(0.4f, 0.3f, 18.0f, 60000);         // left alone on the floor for a minute
  expect("after a minute at rest, back to itself", NORMAL);
  expectCtx("and on the floor, not in a hand", false);

  // --- 11. boot calibration tolerates a touch, refuses a hand (P4.1) ------
  // The old test demanded every sample steady, so one tap failed the whole window. What
  // actually poisons a bias is the car being *turned*; a tap, a bump or a rumble is not.
  printf("boot calibration\n");
  expectCalib("untouched on the floor", still, true);
  expectCalib("one light tap mid-window", tap, true);
  expectCalib("two taps", twoTaps, true);
  expectCalib("floor rumble the whole window", rumble, true);
  expectCalib("held in a palm", palm, false);
  expectCalib("still in the hand for 0.9 s of 1.5 s", setDownLate, false);

  printf("\n%s\n", fails ? "FAILURES" : "all scenarios pass");
  return fails != 0;
}
