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

  printf("\n%s\n", fails ? "FAILURES" : "all scenarios pass");
  return fails != 0;
}
