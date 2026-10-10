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

static bool sawDizzy;                            // DIZZY shown since the last reset()

// Feed ms worth of samples at SAMPLE_US, advancing the fake clock.
static void feed(float gx, float gy, float gz, int ms) {
  int n = ms * 1000 / SAMPLE_US;
  for (int i = 0; i < n; i++) {
    fakeMillis += SAMPLE_US / 1000;
    motionUpdate(mk(gx, gy, gz));
    sawDizzy |= motionGet().state == DIZZY;
  }
}

// Shaken in a hand: every listed axis reversing each half-period, so each half-period is one
// gz swing. Measured 2026-10-10 on the dashboard: peaks 300-1095 on all three axes.
static void shakeFor(float ax, float ay, float az, int halfMs, int ms) {
  for (int t = 0, s = 1; t < ms; t += halfMs, s = -s) feed(s * ax, -s * ay, s * az, halfMs);
}

// One sample with `lsb` added to one axis, in raw counts — the torn FIFO read, reproduced
// exactly as the board delivers it (see DESPIKE_MEDIAN3).
static void feedSpike(float gx, float gy, float gz, int axis, int lsb) {
  GyroRaw s = mk(gx, gy, gz);
  if (axis == 0) s.x += lsb; else if (axis == 1) s.y += lsb; else s.z += lsb;
  fakeMillis += SAMPLE_US / 1000;
  motionUpdate(s);
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

// The bias survives motionReset() (calibration owns it) and the rest-time correction moves
// it, so zero it here or one scenario's long rest leaks into the next.
static void reset() { fakeMillis = 1000; motionSetBias(0, 0, 0); motionReset(); sawDizzy = false; }
static void expectNoDizzy(const char *what) {
  if (sawDizzy) fails++;
  printf("  %s %-46s %s\n", sawDizzy ? "FAIL" : "ok  ", what,
         sawDizzy ? "went DIZZY" : "never DIZZY");
}

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

  // --- 1b. a fingernail tap must not move the face (2026-09-20) -------------
  // The complaint from the car: touching it with a nail, or knocking the table, flipped it
  // to FAST. A tap is *louder* than anything the car does by itself (rms 40+ here), so no
  // threshold can reject it — only its shortness can. It lifts the RMS for one window and
  // is gone, well under FAST_HOLD_MS.
  printf("tapped with a fingernail\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  feed(150.0f, 200.0f, 120.0f, 10);       // one nail tap: huge, 10 ms long
  feed(0.5f, 0.5f, 0.5f, 400);
  expect("just after a tap", NORMAL);
  feed(0.5f, 0.5f, 0.5f, 1000);
  expect("a second later", NORMAL);
  expectCtx("a tap is not a pickup", false);

  printf("fidgeted with: eight taps over four seconds\n");
  for (int i = 0; i < 8; i++) {
    feed(150.0f, 200.0f, 120.0f, 10);
    feed(0.5f, 0.5f, 0.5f, 490);
  }
  expect("still nobody picked it up", NORMAL);
  expectCtx("...and rollAcc never walked to the tilt threshold", false);
  printf("  ..  rollAcc %.1f deg after eight taps (threshold %.0f)\n",
         motionGet().rollAcc, HAND_TILT_DEG);

  // --- 1b2. the torn FIFO read must not move the face (measured 2026-09-20) --
  // On the bench the car flipped to FAST five times in 30 s lying untouched, every time
  // from ONE sample exactly 256 LSB out on one axis: a low byte from one sample and a high
  // byte from the next. One such sample lifts a 50-sample RMS from 0.6 to 2.6 on its own.
  printf("torn FIFO read: one sample 256 LSB out, car untouched\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  for (int i = 0; i < 6; i++) {                 // one on each axis, both signs
    feedSpike(0.5f, 0.5f, 0.5f, i / 2, i % 2 ? 256 : -256);
    feed(0.5f, 0.5f, 0.5f, 400);
    if (motionGet().rms > 1.0f) {
      printf("  FAIL spike on axis %d survived the filter: rms %.2f\n", i / 2, motionGet().rms);
      fails++;
    }
  }
  expect("six torn samples, none of them believed", NORMAL);
  printf("  ..  rms %.2f after six spikes (a single 256 LSB spike alone reads 2.6)\n",
         motionGet().rms);

  // --- 1c. a trembling surface is not the car going fast --------------------
  printf("sitting on a faintly buzzing surface\n");
  reset();
  feed(1.8f, 1.8f, 1.8f, 4000);           // rms 3.1: above the old 2.5, below the new 4.0
  expect("low continuous tremble", NORMAL);

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

  // --- 5. shaken while held -> DIZZY, then back to HAPPY -------------------
  printf("shaken hard while held\n");
  feed(120.0f, 100.0f, 80.0f, 1500);      // loud handling, every axis well under 400
  expect("loud handling, no swings", HAPPY);
  shakeFor(450.0f, 350.0f, 450.0f, 125, 1500);   // 12 gx and 12 gz swings in 1.5 s
  expect("hard shake in a hand", DIZZY);
  feed(2.0f, 2.0f, 2.0f, 1800);
  expect("1.8 s after the last swing", DIZZY);
  feed(2.0f, 2.0f, 2.0f, 300);            // DIZZY_AFTER_MS, step-down included
  expect("2.1 s after: back to HAPPY, still held", HAPPY);

  // --- 6. put back down -> context clears ---------------------------------
  printf("put back down\n");
  feed(0.3f, 0.3f, 0.3f, 2500);
  expectCtx("after 2.5 s of stillness", false);
  feed(0.3f, 0.3f, 0.3f, 1200);           // HAPPY -> NORMAL is down the ladder: EXIT_HOLD_MS
  expect("resting on the desk", NORMAL);

  // --- 7. spinning in circles: |gz| > 300 one way for 5 s -------------------
  printf("driving in circles\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  feed(2.0f, 1.0f, 350.0f, 4800);
  expect("4.8 s of spin: not yet", FAST);
  feed(2.0f, 1.0f, 350.0f, 500);
  expect("5.3 s of spin", DIZZY);
  expectCtx("a floor spin must not read as a pickup", false);
  feed(0.5f, 0.5f, 0.5f, 1800);
  expect("1.8 s after the spin", DIZZY);
  feed(0.5f, 0.5f, 0.5f, 300);
  expect("2.1 s after the spin", NORMAL);
  feed(0.5f, 0.5f, 0.5f, 1000);
  printf("  %s %-46s gz reads %.2f\n", fabsf(motionGet().gz - 0.5f) < 0.2f ? "ok  " : "FAIL",
         "a steady spin is not learned as bias", motionGet().gz);
  if (fabsf(motionGet().gz - 0.5f) >= 0.2f) fails++;

  printf("circles with short dips under 300 (< SPIN_DIP_MS)\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  for (int i = 0; i < 6; i++) { feed(2.0f, 1.0f, 350.0f, 850); feed(2.0f, 1.0f, 250.0f, 150); }
  expect("6 s with 150 ms dips", DIZZY);

  printf("circles broken by a long dip\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  feed(2.0f, 1.0f, 350.0f, 3000);
  feed(2.0f, 1.0f, 250.0f, 300);          // longer than SPIN_DIP_MS: starts over
  feed(2.0f, 1.0f, 350.0f, 3000);
  expectNoDizzy("3 s + 300 ms dip + 3 s");

  printf("spinning too slowly\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  feed(2.0f, 1.0f, 250.0f, 8000);
  expectNoDizzy("250 dps for 8 s");

  printf("left for 3 s, then right for 3 s\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  feed(2.0f, 1.0f, -350.0f, 3000);
  feed(2.0f, 1.0f, 350.0f, 3000);
  expectNoDizzy("a reversal starts the spin over");

  // --- 7b. shaken, then set straight down: DIZZY plays out on the floor ----
  printf("shaken, then put down at once\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  feed(4.0f, 30.0f, 4.0f, 1200);          // picked up
  shakeFor(450.0f, 350.0f, 450.0f, 125, 1000);
  feed(0.3f, 0.3f, 0.3f, 1500);
  expect("on the desk, 1.5 s after", DIZZY);
  feed(0.3f, 0.3f, 0.3f, 2500);
  expect("4 s after", NORMAL);
  expectCtx("and back on the floor", false);
  {
    float r = motionGet().rms;
    bool ok = r < 1.0f;                   // NaN fails too
    if (!ok) fails++;
    printf("  %s %-46s rms %.2f\n", ok ? "ok  " : "FAIL", "rms back to rest after a 450 dps shake", r);
  }
  printf("one 1000 dps shake, then rest\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  shakeFor(1000.0f, 900.0f, 1000.0f, 100, 2000);
  feed(0.5f, 0.5f, 0.5f, 5000);
  {
    float r = motionGet().rms;
    bool ok = r < 1.0f;
    if (!ok) fails++;
    printf("  %s %-46s rms %.2f\n", ok ? "ok  " : "FAIL", "rms back to rest (was NaN before the resum)", r);
  }
  expect("and the face with it", NORMAL);

  // --- 7c. what must NOT be a shake (measured pickups: 0-1 gz swings past 400) -
  printf("the pickup from the 2026-10-10 recording\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  feed(120.0f, 90.0f, 200.0f, 300);
  feed(-40.0f, -30.0f, -60.0f, 300);
  feed(0.5f, 0.5f, 0.5f, 1500);
  expectNoDizzy("pickup, gz 200");
  printf("yanked out and back twice, gz 600\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  for (int i = 0; i < 2; i++) { feed(100.0f, 50.0f, 600.0f, 150); feed(100.0f, 50.0f, -600.0f, 150); }
  feed(0.5f, 0.5f, 0.5f, 1500);
  expectNoDizzy("4 swings: short of SHAKE_SWINGS");
  printf("hovering around 400 without falling back under 200\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  for (int i = 0; i < 20; i++) { feed(1.0f, 1.0f, 420.0f, 50); feed(1.0f, 1.0f, 380.0f, 50); }
  expectNoDizzy("one swing, not twenty");

  // --- 7d. counting: 5 swings in 2 s is not enough, 6 is -------------------
  printf("five swings, then six\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  shakeFor(0.0f, 0.0f, 450.0f, 300, 1500);       // 5 swings
  feed(0.5f, 0.5f, 0.5f, 2500);
  expectNoDizzy("5 swings in 1.5 s");
  shakeFor(0.0f, 0.0f, 450.0f, 300, 1800);       // 6 swings
  expect("6 swings in 1.8 s", DIZZY);
  printf("shaken on gx only, then gy only\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  shakeFor(450.0f, 0.0f, 0.0f, 125, 1000);       // 8 gx swings
  expect("gx-only shake", DIZZY);
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  shakeFor(0.0f, 450.0f, 0.0f, 125, 1000);       // 8 gy swings
  expect("gy-only shake", DIZZY);
  printf("4 swings on gx and 4 on gy at once: 8 in all, no axis at 6\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  shakeFor(450.0f, 450.0f, 0.0f, 125, 500);
  feed(0.5f, 0.5f, 0.5f, 2500);
  expectNoDizzy("axes are counted separately, never added");
  printf("six swings spread over 3 s\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  shakeFor(0.0f, 0.0f, 450.0f, 500, 3000);
  expectNoDizzy("6 swings, but never 6 inside 2 s");

  // --- 8. a wiggle is not a shake --------------------------------------------
  printf("wiggled in yaw: reversing, but under 400\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  for (int i = 0; i < 12; i++) feed(1.0f, 1.0f, i % 2 ? 60.0f : -60.0f, 100);
  expectNoDizzy("+-60 dps every 100 ms");
  expect("a wiggled car on the floor is not dizzy, just busy", FAST);

  // --- 8b. the ring of a knock is not a shake -----------------------------
  printf("knocked on the table\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  for (int i = 0; i < 6; i++) {
    float a = 600.0f / (1 << i);          // decaying ring, 50 ms per half-cycle
    feed(a * 0.5f, a * 0.5f, i % 2 ? a : -a, 50);
  }
  feed(0.5f, 0.5f, 0.5f, 1500);
  expectNoDizzy("ring from 600 dps: 1 swing past 400");
  expect("a knock leaves nothing behind", NORMAL);

  // --- 9. fast attack, slow decay (P1) ------------------------------------
  // The same threshold crossed in both directions must not take the same time: going up
  // the ladder is a reaction, coming down is a feeling wearing off.
  printf("attack vs decay across one threshold\n");
  reset();
  feed(0.5f, 0.5f, 0.5f, 1000);
  feed(6.0f, 1.5f, 3.0f, 400);            // up: the condition has not proved itself yet
  expect("400 ms after the motion started (FAST_HOLD_MS)", NORMAL);
  feed(6.0f, 1.5f, 3.0f, 400);            // ...FAST_HOLD_MS, then ENTER_HOLD_MS on top
  expect("800 ms after the motion started", FAST);
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
