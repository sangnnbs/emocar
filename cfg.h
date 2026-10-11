// cfg.h — the state conditions, changeable at runtime from the server's /configs page.
//
// Every field is named after its config.h macro, and that macro stays its default ("the
// sketch"). The live values are kept in CFG_PATH, stamped with a hash of the defaults the
// firmware was built with: flash a sketch whose defaults differ and the saved values are
// dropped, so a new config.h always wins.
//
// Only the numbers are configurable — the want-chain in motion.cpp stays hand-written.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "config.h"

#define CFG_PATH     "/cfg.bin"
#define CFG_TMP_PATH "/cfg.tmp"

// motion.cpp remembers this many swings per axis, so SHAKE_SWINGS can't ask for more.
#define SWING_RING   16
static_assert(SHAKE_SWINGS <= SWING_RING, "SHAKE_SWINGS is limited by SWING_RING");

// All fields are 4 bytes, so the struct has no padding and hashes as plain bytes.
struct MotionCfg {
  uint32_t stateFast, stateDizzy, stateHappy;            // 0 = never shown
  uint32_t enterHoldMs, exitHoldMs;
  float    fastEnterRms, fastExitRms;   uint32_t fastHoldMs;
  float    shakeDps, shakeRearmDps;     uint32_t shakeSwings, shakeWindowMs;
  float    spinGzDps;                   uint32_t spinHoldMs, spinDipMs, dizzyAfterMs;
  float    handTiltDeg;                 uint32_t handTiltMs, handRollMs;
  float    handQuietDps;                uint32_t handQuietMs;
  float    stillRms;                    uint32_t handSettleMs;
};

static const MotionCfg CFG_DEFAULTS = {
  STATE_FAST, STATE_DIZZY, STATE_HAPPY,
  ENTER_HOLD_MS, EXIT_HOLD_MS,
  FAST_ENTER_RMS, FAST_EXIT_RMS, FAST_HOLD_MS,
  SHAKE_DPS, SHAKE_REARM_DPS, SHAKE_SWINGS, SHAKE_WINDOW_MS,
  SPIN_GZ_DPS, SPIN_HOLD_MS, SPIN_DIP_MS, DIZZY_AFTER_MS,
  HAND_TILT_DEG, HAND_TILT_MS, HAND_ROLL_MS,
  HAND_QUIET_DPS, HAND_QUIET_MS,
  STILL_RMS, HAND_SETTLE_MS,
};

// The rules cfgSet() enforces at runtime, checked on the defaults at compile time.
static_assert(FAST_EXIT_RMS < FAST_ENTER_RMS, "FAST_EXIT_RMS must be below FAST_ENTER_RMS");
static_assert(SHAKE_REARM_DPS < SHAKE_DPS, "SHAKE_REARM_DPS must be below SHAKE_DPS");
static_assert(DIZZY_AFTER_MS >= EXIT_HOLD_MS, "DIZZY_AFTER_MS includes EXIT_HOLD_MS");

extern MotionCfg cfg;                 // what motion.cpp runs on

void cfgLoad();                       // at boot, after LittleFS; defaults if nothing valid saved
// Apply "NAME=value NAME=value ..." (config.h names) as one change: all of it is validated
// first, then applied and saved. On false, err says why and nothing changed.
bool cfgSet(char *args, char *err, size_t errLen);
void cfgReset();                      // back to the defaults, saved file removed
void cfgPrintJson();                  // {"cur":{NAME:value,...},"def":{...}} on Serial, no newline
bool cfgAnyState(const MotionCfg &c); // any state besides NORMAL on = the gyro is needed
