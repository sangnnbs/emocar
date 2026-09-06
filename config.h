// config.h — every tunable in one place. Nothing else in the sketch hardcodes a number.
//
// Board: ESP32-C3 Super Mini
// https://www.espboards.dev/esp32/esp32-c3-super-mini/
// 4 MB flash | native USB CDC — enable "USB CDC On Boot"
// Arduino IDE: "ESP32C3 Dev Module" | PlatformIO: esp32-c3-devkitm-1
// I2C on board defaults: SDA GPIO8 (also blue LED), SCL GPIO9 (also BOOT button)
// Avoid for other peripherals: strapping pins GPIO2, 4, 5, 6, 7
#pragma once
#include <stdint.h>

// ---------- I2C bus ----------
#define PIN_SDA     8         // GPIO8, silkscreen "8"
#define PIN_SCL     9         // GPIO9, silkscreen "9"
#define I2C_HZ      400000
#define GYRO_ADDR   0x69      // GY-50 SDO open → 0x69; SDO→GND → 0x68
#define OLED_ADDR   0x3C      // SH1106 128x64

// ---------- display profile ----------
// One block per supported panel; exactly one enabled. DISP_PROFILE must match an id
// in server/profiles.json — /status reports it and the server refuses mismatched pushes.
// Pixel formats (qgif codes): 0 = mono1 (1 bpp, MSB=left, bit 1=dark),
// 1 = gray4 (2 px/byte, high nibble=left), 2 = rgb565 (2 B/px big-endian).
#define DISP_PROFILE      "sh1106_128x64"
#define DISP_W            128
#define DISP_H            64
#define DISP_FORMAT       0
#define DISP_DRIVER_U8G2_SH1106_I2C 1   // driver backend, see display.cpp

// derived — bytes of one animation frame in the profile's pixel format
#if DISP_FORMAT == 0
#define DISP_FRAME_BYTES  (((DISP_W + 7) / 8) * DISP_H)
#elif DISP_FORMAT == 1
#define DISP_FRAME_BYTES  (((DISP_W + 1) / 2) * DISP_H)
#else
#define DISP_FRAME_BYTES  (DISP_W * DISP_H * 2)
#endif

// ---------- serial ----------
#define SERIAL_BAUD 115200
#define PRINT_MS    250       // telemetry line period (0 = silent)

// ---------- gyro sampling ----------
// Constant sample period, set by the L3G4200D's own ODR and buffered in its FIFO.
// Allowed: 10000 (100 Hz), 5000 (200 Hz), 2500 (400 Hz), 1250 (800 Hz)
#define SAMPLE_US   5000
#define GYRO_FS_DPS 2000      // full scale: 250, 500 or 2000
#define CALIB_MS    1500      // zero-rate bias averaging time at boot (car must be still)

// ---------- animation ----------
#define ANIM_MIN_FRAME_MS 30  // floor for a frame delay read from the .qgif (0 in file → 100)
#define PEAK_HOLD_MS      3000 // peak readout reset period — only used by the Serial telemetry

// ---------- WiFi animation upload ----------
// The car is a WiFi hotspot; the Go server (server/) pushes converted .qgif blobs to it.
// One LittleFS file per CarState; a missing/invalid file falls back to the PROGMEM placeholder.
#define WIFI_AP_SSID    "emocar"
#define WIFI_AP_PASS    "emocar123"   // WPA2 needs >= 8 chars; "" makes an open network
#define HTTP_PORT       80
#define ANIM_PATH_FMT   "/anim%d.qgif"  // slot files: /anim0.qgif ... one per CarState
#define ANIM_TMP_PATH   "/anim.tmp"     // upload target, renamed over the slot after validation
#define ANIM_MAX_FRAMES 255
#define ANIM_MAX_BYTES  (11UL + 2 * ANIM_MAX_FRAMES + (uint32_t)DISP_FRAME_BYTES * ANIM_MAX_FRAMES)
#define NET_IO_TIMEOUT_MS 3000          // per-read deadline while receiving an upload

// ---------- motion state machine ----------
// Four states (PLANS.md P2): NORMAL / FAST / DIZZY / HAPPY. Every number below is a
// guess until the on-car tuning session (P5) — none of them have been measured.
// Measured on tinicar: rest rms ≈ 1, car rolling pk ≈ 6, hand shake pk ≈ 80, car spin |gz| ≈ 60
#define HOLD_MS          200    // a condition must persist this long before the state flips
#define RMS_WINDOW_MS    250    // vibration RMS window

// FAST — floor state: the car is being pushed/driven hard. Suppressed while in hand.
#define FAST_ENTER_RMS   4.0f   // vibration RMS (dps) to enter FAST
#define FAST_EXIT_RMS    2.5f   // vibration RMS below which FAST is left

// DIZZY — two entry paths into one state: a real spin, or a hard hand shake.
// A spin keeps one sign; a shake flips sign constantly, which is why the shake path
// keys on sheer magnitude instead (shaking by hand is more violent than driving).
#define SPIN_ENTER_DPS  40.0f   // |gz| to enter the spin path (one sign held for HOLD_MS)
#define SPIN_EXIT_DPS   25.0f   // |gz| below which the spin ends (a sign flip does not)
// The bar to clear is a pickup, not FAST: RMS includes *steady* rotation, so a
// smooth 30 dps lift-and-tilt already reads ~30 — a shake has to be visibly more
// violent than that (tinicar hand shake pk ≈ 80 dps ≈ RMS 55) or every pickup is 🥴.
#define SHAKE_ENTER_RMS 45.0f   // RMS to enter the shake path
#define SHAKE_EXIT_RMS  30.0f   // RMS below which the shake ends
#define SHAKE_HOLD_MS    300    // shake must persist this long (rejects a single jolt)

// DIZZY aftermath — dose = Σ|g|·dt over the spin/shake, in degrees of total rotation.
// Spin longer or shake harder → dizzier for longer.
#define DIZZY_MS_PER_DEG 8.0f   // wooze duration per degree accumulated
#define DIZZY_MIN_MS     1000   // ...clamped: a flick still earns a moment of 🥴
#define DIZZY_MAX_MS     8000   // ...and a long spin can't lock the face up

// Context bit — floor vs hand. Gates HAPPY on and FAST off; nothing else reads it.
// Enter on tilt: a car on wheels physically cannot hold a roll angle, a hand does it
// constantly. Leave on stillness: a set-down car settles, a hand never does.
#define STILL_RMS        2.0f   // RMS at rest; load-bearing — it decides put-down (P5)
#define HAND_TILT_DEG   25.0f   // accumulated |roll| that means "someone picked me up"
#define HAND_TILT_MS     2000   // ...within this window, else the accumulator restarts
#define HAND_QUIET_DPS   3.0f   // |gy| below this = lying flat: reset the accumulator
#define HAND_SETTLE_MS   2000   // stillness this long = set back down

// ---------- derived (don't edit) ----------
#define RMS_WINDOW  (RMS_WINDOW_MS * 1000 / SAMPLE_US)
