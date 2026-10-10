// config.h — every tunable in one place.
//
// Board: ESP32-C3 Super Mini — https://www.espboards.dev/esp32/esp32-c3-super-mini/
// Arduino IDE: "ESP32C3 Dev Module" with "USB CDC On Boot" | PlatformIO: esp32-c3-devkitm-1
// SDA GPIO8 is also the blue LED, SCL GPIO9 the BOOT button. Strapping pins: 2, 4-7.
#pragma once
#include <stdint.h>

// ---------- I2C bus ----------
#define PIN_SDA     8
#define PIN_SCL     9
#define I2C_HZ      400000
#define GYRO_ADDR   0x69      // GY-50 SDO open → 0x69; SDO→GND → 0x68
#define OLED_ADDR   0x3C      // SH1106 128x64

// ---------- display profile ----------
// DISP_PROFILE must match an id in server/profiles.json.
// Pixel formats: 0 = mono1 (1 bpp, MSB=left, bit 1=dark),
// 1 = gray4 (2 px/byte, high nibble=left), 2 = rgb565 (2 B/px big-endian).
#define DISP_PROFILE      "sh1106_128x64"
#define DISP_W            128
#define DISP_H            64
#define DISP_FORMAT       0
#define DISP_DRIVER_U8G2_SH1106_I2C 1   // driver backend, see display.cpp

#if DISP_FORMAT == 0
#define DISP_FRAME_BYTES  (((DISP_W + 7) / 8) * DISP_H)
#elif DISP_FORMAT == 1
#define DISP_FRAME_BYTES  (((DISP_W + 1) / 2) * DISP_H)
#else
#define DISP_FRAME_BYTES  (DISP_W * DISP_H * 2)
#endif

// ---------- serial ----------
#define SERIAL_BAUD 115200

// ---------- debug log ----------
// 1 = print debug lines on serial: boot, calibration, gyro faults, state changes and a
// gyro line every DEBUG_PRINT_MS. The server skips any line that isn't a protocol reply.
#define DEBUG_LOG        1
#define DEBUG_PRINT_MS   200

// ---------- telemetry ----------
// 1 = print a `T ...` sensor line every TELEMETRY_MS for the server's /dashboard (format in
// telem.h). Lines nobody reads are dropped, never blocking the loop. Needs the gyro on.
#define TELEMETRY        1
#define TELEMETRY_MS     40

// ---------- gyro sampling ----------
#define SAMPLE_US   5000      // 10000, 5000, 2500 or 1250 (the gyro's own ODR)
#define GYRO_FS_DPS 2000      // full scale: 250, 500 or 2000
#define CALIB_MS    1500      // zero-rate bias window at boot (car on the floor, not held)

// Boot calibration averages short blocks and takes the bias from the blocks that agree
// with the median: a tap spoils a block or two and is outvoted, a turning hand never agrees.
#define CALIB_BLOCK_MS   100
#define CALIB_BLOCKS     (CALIB_MS / CALIB_BLOCK_MS)
#define CALIB_AGREE_DPS  1.0f   // a block within this of the median on every axis agrees
#define CALIB_MIN_AGREE  10     // ...and this many of the CALIB_BLOCKS must agree
#define CALIB_UI         0      // 1 = show "CALIBRATING" on the OLED

// Stillness is judged by jitter (wobble around the mean), never the mean: a bad bias
// moves the mean but not the jitter.
#define REST_JITTER_DPS  1.5f   // wobble below this = the car is not being moved
#define REZERO_AFTER_MS  3000   // ...and after this long at rest, start correcting the bias
#define REZERO_TAU_MS   20000   // time constant of that correction
#define REZERO_MAX_DPS   30.0f  // ...but only on a mean below this: a steady spin has no
                                // jitter either, and must not be learned as bias

// Median-of-3 filter against the torn FIFO reads (one axis off by exactly 256 LSB for one
// sample). BDU does not cover samples read through the FIFO.
#define DESPIKE_MEDIAN3  1

// ---------- gyro watchdog ----------
// A wedged sensor looks like a perfectly still car; only the absence of samples shows it.
#define GYRO_TIMEOUT_MS  1000   // no new samples for this long = the sensor is gone
#define GYRO_RETRY_MS    2000   // how often to try bringing it back (at boot too)

// ---------- animation ----------
#define ANIM_MIN_FRAME_MS 30    // floor for a frame delay read from the .qgif (0 in file → 100)

// ---------- USB animation upload ----------
#define ANIM_PATH_FMT   "/anim%d.qgif"  // one slot file per CarState
#define ANIM_TMP_PATH   "/anim.tmp"     // upload target, renamed over the slot after validation
#define ANIM_MAX_FRAMES 255
#define ANIM_MAX_BYTES  (11UL + 2 * ANIM_MAX_FRAMES + (uint32_t)DISP_FRAME_BYTES * ANIM_MAX_FRAMES)
#define SER_IO_TIMEOUT_MS 3000          // per-byte deadline during a transfer
#define CMD_LINE_MAX      64            // longest command line accepted
#define CMD_CHUNK         1024          // upload chunk; the car ACKs each one
#define CMD_RX_BUF        4096          // USB RX queue, must hold a chunk

// ---------- registered states ----------
// An unregistered state is never shown and its detector does not run. NORMAL is always on.
// The host tests override these to 1 (test/Makefile).
#ifndef STATE_FAST
#define STATE_FAST   0
#endif
#ifndef STATE_DIZZY
#define STATE_DIZZY  1
#endif
#ifndef STATE_HAPPY
#define STATE_HAPPY  1
#endif

// ---------- motion state machine ----------
// Measured on the bare board:
//   at rest on a desk      rms 0.5-0.6
//   held in a palm         rms p50 35, p90 75, spikes to 255
//   a pickup/yank burst    rms 150-380, gz pk 400-730
#define RMS_WINDOW_MS    250    // vibration RMS window

// Fast attack, slow decay along the priority ladder (DIZZY > HAPPY > FAST > NORMAL).
#define ENTER_HOLD_MS    200    // moving up the ladder
#define EXIT_HOLD_MS    1000    // moving down

// FAST — only on a surface. A tap spikes RMS but never lasts, so FAST_HOLD_MS rejects it.
// On soft surfaces (bed, carpet) FAST_ENTER_RMS may need to come down toward 3.0.
#define FAST_ENTER_RMS   4.0f   // vibration RMS (dps) to enter FAST
#define FAST_EXIT_RMS    2.5f   // vibration RMS below which FAST is left
#define FAST_HOLD_MS     500    // rms above FAST_ENTER_RMS this long = really moving

// DIZZY — in a hand or not; outranks HAPPY. Two triggers:
//   shake  SHAKE_SWINGS swings past ±SHAKE_DPS within SHAKE_WINDOW_MS on any one axis (gx, gy
//          and gz count separately, never added). A swing is the axis crossing the line; it
//          must fall back under SHAKE_REARM_DPS before the same side counts again.
//          Measured 2026-10-10, best 2 s of a hard shake (40 ms peaks, so the car sees
//          more): gx 10, gy 11, gz 5 swings; pickups 0 on gx/gy, 1 on gz; holding 0.
//   spin   |gz| above SPIN_GZ_DPS one way for SPIN_HOLD_MS — the car driving in circles.
//          Dips shorter than SPIN_DIP_MS don't break it. Not yet measured on the car.
#define SHAKE_DPS       400.0f
#define SHAKE_REARM_DPS 200.0f
#define SHAKE_SWINGS        6
#define SHAKE_WINDOW_MS  2000
#define SPIN_GZ_DPS     300.0f
#define SPIN_HOLD_MS     5000
#define SPIN_DIP_MS       200

// How long DIZZY shows after the last swing / the end of the spin, step-down
// (EXIT_HOLD_MS) included.
#define DIZZY_AFTER_MS   2000

// Context bit — floor vs hand. Enter on unsigned roll travel (a car on wheels peaks at
// ~6 dps roll, a hand turns it constantly), leave on stillness.
#define STILL_RMS        2.0f   // RMS at rest — decides put-down
#define HAND_TILT_DEG   25.0f   // roll travel that means "picked up"
#define HAND_TILT_MS     2000   // ...within this window, else the accumulator restarts
#define HAND_QUIET_DPS   6.0f   // |gy| below this is noise: don't integrate it
#define HAND_QUIET_MS     300   // quiet this long clears the accumulator (not each zero-crossing)
#define HAND_ROLL_MS      150   // |gy| must be sustained this long before it counts (rejects taps)
#define HAND_SETTLE_MS   2000   // stillness this long = set back down

// ---------- derived ----------
#define RMS_WINDOW  (RMS_WINDOW_MS * 1000 / SAMPLE_US)
// With only NORMAL registered the gyro is never started.
#define USE_GYRO    (STATE_FAST || STATE_DIZZY || STATE_HAPPY)
