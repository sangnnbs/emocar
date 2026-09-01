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
#define ANIM_PATH_FMT   "/anim%d.qgif"  // slot files: /anim0.qgif /anim1.qgif /anim2.qgif
#define ANIM_TMP_PATH   "/anim.tmp"     // upload target, renamed over the slot after validation
#define ANIM_MAX_FRAMES 255
#define ANIM_MAX_BYTES  (11UL + 2 * ANIM_MAX_FRAMES + (uint32_t)DISP_FRAME_BYTES * ANIM_MAX_FRAMES)
#define NET_IO_TIMEOUT_MS 3000          // per-read deadline while receiving an upload

// ---------- motion state machine ----------
// Measured on tinicar: rest rms ≈ 1, car rolling pk ≈ 6, hand shake pk ≈ 80, car spin |gz| ≈ 60
#define SPIN_ENTER_DPS  40.0f   // |gz| to enter SPINNING (must keep one sign for HOLD_MS)
#define SPIN_EXIT_DPS   25.0f   // |gz| below which SPINNING is left
#define FAST_ENTER_RMS   4.0f   // vibration RMS (dps) to enter FAST
#define FAST_EXIT_RMS    2.5f   // vibration RMS below which FAST is left
#define HOLD_MS          200    // a condition must persist this long before the state flips
#define RMS_WINDOW_MS    250    // vibration RMS window

// ---------- derived (don't edit) ----------
#define RMS_WINDOW  (RMS_WINDOW_MS * 1000 / SAMPLE_US)
