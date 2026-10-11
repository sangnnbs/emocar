// cfg.cpp — see cfg.h. One table names every field, so parsing, range checks and the JSON
// all come from the same list.
#include <Arduino.h>
#include <LittleFS.h>
#include <math.h>
#include "cfg.h"

MotionCfg cfg = CFG_DEFAULTS;

struct Field { const char *name; bool isFloat; uint16_t off; float lo, hi; };

#define FI(name, member, lo, hi) { #name, false, offsetof(MotionCfg, member), lo, hi }
#define FF(name, member, lo, hi) { #name, true,  offsetof(MotionCfg, member), lo, hi }

// Ranges only keep a typo from making the car unusable; the real tuning happens inside them.
static const Field FIELDS[] = {
  FI(STATE_FAST,      stateFast,     0, 1),
  FI(STATE_DIZZY,     stateDizzy,    0, 1),
  FI(STATE_HAPPY,     stateHappy,    0, 1),
  FI(ENTER_HOLD_MS,   enterHoldMs,   0, 5000),
  FI(EXIT_HOLD_MS,    exitHoldMs,    0, 10000),
  FF(FAST_ENTER_RMS,  fastEnterRms,  0.5f, 500),
  FF(FAST_EXIT_RMS,   fastExitRms,   0, 500),
  FI(FAST_HOLD_MS,    fastHoldMs,    0, 10000),
  FF(SHAKE_DPS,       shakeDps,      50, GYRO_FS_DPS),
  FF(SHAKE_REARM_DPS, shakeRearmDps, 0, GYRO_FS_DPS),
  FI(SHAKE_SWINGS,    shakeSwings,   1, SWING_RING),
  FI(SHAKE_WINDOW_MS, shakeWindowMs, 100, 10000),
  FF(SPIN_GZ_DPS,     spinGzDps,     10, GYRO_FS_DPS),
  FI(SPIN_HOLD_MS,    spinHoldMs,    0, 60000),
  FI(SPIN_DIP_MS,     spinDipMs,     0, 5000),
  FI(DIZZY_AFTER_MS,  dizzyAfterMs,  0, 60000),
  FF(HAND_TILT_DEG,   handTiltDeg,   1, 360),
  FI(HAND_TILT_MS,    handTiltMs,    100, 10000),
  FI(HAND_ROLL_MS,    handRollMs,    0, 2000),
  FF(HAND_QUIET_DPS,  handQuietDps,  0, 100),
  FI(HAND_QUIET_MS,   handQuietMs,   0, 5000),
  FF(STILL_RMS,       stillRms,      0.1f, 50),
  FI(HAND_SETTLE_MS,  handSettleMs,  0, 30000),
};
static const int NFIELDS = sizeof FIELDS / sizeof FIELDS[0];
static_assert(sizeof(MotionCfg) == sizeof FIELDS / sizeof FIELDS[0] * 4,
              "every MotionCfg field needs a row in FIELDS");

static float get(const MotionCfg &c, const Field &f) {
  const uint8_t *p = (const uint8_t *)&c + f.off;
  return f.isFloat ? *(const float *)p : (float)*(const uint32_t *)p;
}
static void put(MotionCfg &c, const Field &f, float v) {
  uint8_t *p = (uint8_t *)&c + f.off;
  if (f.isFloat) *(float *)p = v; else *(uint32_t *)p = (uint32_t)v;
}

// The cross-field rules: each one, broken, makes a state flicker or never end.
static bool check(const MotionCfg &c, char *err, size_t errLen) {
  for (int i = 0; i < NFIELDS; i++) {
    float v = get(c, FIELDS[i]);
    if (!(v >= FIELDS[i].lo && v <= FIELDS[i].hi)) {
      snprintf(err, errLen, "%s must be %g..%g", FIELDS[i].name, FIELDS[i].lo, FIELDS[i].hi);
      return false;
    }
  }
  const char *why = !(c.fastExitRms < c.fastEnterRms)  ? "FAST_EXIT_RMS must be below FAST_ENTER_RMS"
                  : !(c.shakeRearmDps < c.shakeDps)    ? "SHAKE_REARM_DPS must be below SHAKE_DPS"
                  : !(c.dizzyAfterMs >= c.exitHoldMs)  ? "DIZZY_AFTER_MS must be at least EXIT_HOLD_MS"
                  : nullptr;
  if (why) snprintf(err, errLen, "%s", why);
  return !why;
}

// FNV-1a over the built-in defaults: changes whenever config.h's numbers or the struct do.
static uint32_t defaultsHash() {
  uint32_t h = 2166136261u;
  const uint8_t *p = (const uint8_t *)&CFG_DEFAULTS;
  for (size_t i = 0; i < sizeof CFG_DEFAULTS; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

struct CfgFile { uint32_t magic, defHash, size; MotionCfg c; };
static const uint32_t CFG_MAGIC = 0x47464345;   // "ECFG"

void cfgLoad() {
  cfg = CFG_DEFAULTS;
  File f = LittleFS.open(CFG_PATH, "r");
  if (!f) return;
  CfgFile in;
  bool ok = f.read((uint8_t *)&in, sizeof in) == sizeof in;
  f.close();
  char err[64];
  if (ok && in.magic == CFG_MAGIC && in.size == sizeof(MotionCfg) &&
      in.defHash == defaultsHash() && check(in.c, err, sizeof err)) {
    cfg = in.c;
    return;
  }
  LittleFS.remove(CFG_PATH);          // another sketch's values, or damaged: the defaults win
}

// Written beside and renamed over, so a power cut leaves the old file or the new one.
static bool save() {
  CfgFile out = { CFG_MAGIC, defaultsHash(), sizeof(MotionCfg), cfg };
  File f = LittleFS.open(CFG_TMP_PATH, "w");
  if (!f) return false;
  bool ok = f.write((const uint8_t *)&out, sizeof out) == sizeof out;
  f.close();
  LittleFS.remove(CFG_PATH);
  return ok && LittleFS.rename(CFG_TMP_PATH, CFG_PATH);
}

bool cfgSet(char *args, char *err, size_t errLen) {
  MotionCfg next = cfg;
  char *save_ptr;
  for (char *tok = strtok_r(args, " ", &save_ptr); tok; tok = strtok_r(nullptr, " ", &save_ptr)) {
    char *eq = strchr(tok, '=');
    if (!eq) { snprintf(err, errLen, "expected NAME=value, got %.24s", tok); return false; }
    *eq = 0;
    const Field *f = nullptr;
    for (int i = 0; i < NFIELDS && !f; i++) if (!strcmp(FIELDS[i].name, tok)) f = &FIELDS[i];
    if (!f) { snprintf(err, errLen, "unknown setting %.24s", tok); return false; }
    char *end;
    float v = strtof(eq + 1, &end);
    if (end == eq + 1 || *end || !isfinite(v)) {
      snprintf(err, errLen, "%s: not a number", f->name);
      return false;
    }
    if (!f->isFloat && v != floorf(v)) {
      snprintf(err, errLen, "%s must be a whole number", f->name);
      return false;
    }
    put(next, *f, v);
  }
  if (!check(next, err, errLen)) return false;
  cfg = next;
  if (!save()) { snprintf(err, errLen, "applied, but saving to flash failed"); return false; }
  return true;
}

void cfgReset() {
  cfg = CFG_DEFAULTS;
  LittleFS.remove(CFG_PATH);
}

static void printBlock(const MotionCfg &c) {
  Serial.print('{');
  for (int i = 0; i < NFIELDS; i++)
    Serial.printf("%s\"%s\":%g", i ? "," : "", FIELDS[i].name, get(c, FIELDS[i]));
  Serial.print('}');
}

void cfgPrintJson() {
  Serial.print("{\"cur\":");
  printBlock(cfg);
  Serial.print(",\"def\":");
  printBlock(CFG_DEFAULTS);
  Serial.print('}');
}

bool cfgAnyState(const MotionCfg &c) { return c.stateFast || c.stateDizzy || c.stateHappy; }
