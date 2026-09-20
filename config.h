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
// in server/profiles.json — STATUS reports it and the server refuses mismatched pushes.
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
#define CALIB_MS    1500      // zero-rate bias window at boot (car on the floor, not held)

// Boot calibration asks "is the car being *turned*?", not "did anything touch it?". A tap,
// a bump of the table or a rumble through the floor rattles the gyro but turns nothing: it
// is brief and averages to zero, so it cannot poison the bias. What poisons it is a hand
// turning the car (the +18.5 dps gz incident). So the window is cut into short blocks,
// each block is averaged (a rattle cancels inside it), and the bias is the mean of the
// blocks that agree with the median. A tap spoils a block or two and is simply outvoted;
// a hand turns the car at a different rate every moment, so its blocks never agree.
// The window rolls block by block, so a disturbance delays calibration, never restarts it.
#define CALIB_BLOCK_MS   100    // one block: long enough to average out a rattle
#define CALIB_BLOCKS     (CALIB_MS / CALIB_BLOCK_MS)
#define CALIB_AGREE_DPS  1.0f   // a block within this of the median on every axis agrees
#define CALIB_MIN_AGREE  10     // ...and this many of the CALIB_BLOCKS (15) must agree

// Stillness is judged by *jitter* — the wobble around the mean — never by the mean itself.
// A wrong bias moves the mean without moving the jitter: a bias poisoned by movement during
// calibration read +18.5 dps on gz while the sample-to-sample wobble stayed at ±0.5, so a
// level test sees a violently spinning car and a jitter test sees a perfectly still one.
// Continuous re-zeroing therefore asks about jitter (P4). Boot calibration asks a
// coarser question with the same idea — do the block means agree — see CALIB_* above.
#define REST_JITTER_DPS  1.5f // wobble below this = the car is not being moved
#define REZERO_AFTER_MS  3000 // ...and after this long at rest, start correcting the bias
#define REZERO_TAU_MS   20000 // exponential time constant of that correction (gentle on purpose)

// ---------- gyro watchdog ----------
// A wedged sensor is indistinguishable from a perfectly still car by any measure of the
// *data* — it has zero jitter and zero motion. Only the absence of NEW samples gives it
// away. Without this the car keeps showing whichever face it computed last, forever, which
// reads as a stuck emotion rather than a fault. Seen four times on the bench in one session.
#define GYRO_TIMEOUT_MS  1000   // no new samples for this long = the sensor is gone
#define GYRO_RETRY_MS    2000   // how often to try bringing it back (at boot too)

// ---------- animation ----------
#define ANIM_MIN_FRAME_MS 30  // floor for a frame delay read from the .qgif (0 in file → 100)
#define PEAK_HOLD_MS      3000 // peak readout reset period — only used by the Serial telemetry

// ---------- USB animation upload ----------
// The Go server (server/) opens the C3's USB serial port and pushes converted .qgif
// blobs over it (cmd.cpp). One LittleFS file per CarState; a missing or invalid file
// falls back to the PROGMEM placeholder. This replaced a WiFi AP + HTTP server whose
// ~80-100 mA cost half the battery life — see PLANS.md.
#define ANIM_PATH_FMT   "/anim%d.qgif"  // slot files: /anim0.qgif ... one per CarState
#define ANIM_TMP_PATH   "/anim.tmp"     // upload target, renamed over the slot after validation
#define ANIM_MAX_FRAMES 255
#define ANIM_MAX_BYTES  (11UL + 2 * ANIM_MAX_FRAMES + (uint32_t)DISP_FRAME_BYTES * ANIM_MAX_FRAMES)
#define SER_IO_TIMEOUT_MS 3000          // per-byte deadline during a transfer
#define CMD_LINE_MAX      64            // longest command line accepted
#define CMD_CHUNK         1024          // upload chunk; the car ACKs each one (see cmd.cpp)
#define CMD_RX_BUF        4096          // ...and the USB RX queue must hold one, 4x over

// ---------- motion state machine ----------
// Four states (PLANS.md P2): NORMAL / FAST / DIZZY / HAPPY.
// Measured 2026-09-06 on the bare board over serial (not yet on the car):
//   at rest on a desk      rms 0.5-0.6, per-axis |g| < 0.5   (bias variance, not motion)
//   held in a palm, casual rms p50 35, p90 75, spikes to 255
//   a pickup/yank burst    rms 150-380, gz pk 400-730
// Older tinicar figures, still unconfirmed here: car rolling pk ≈ 6, car spin |gz| ≈ 60
#define RMS_WINDOW_MS    250    // vibration RMS window

// Fast attack, slow decay (P1). A startle is instant; calming down lingers. So the
// commit hold depends on which way the state moves along the priority ladder
// (DIZZY > HAPPY > FAST > NORMAL, see PRIORITY[] in motion.cpp): going *up* is quick,
// coming back *down* is slow, which is what keeps a big reaction from snapping off
// as if nothing had happened. These gate the state flip, not the conditions below.
#define ENTER_HOLD_MS    200    // moving up the ladder: a reaction should feel immediate
#define EXIT_HOLD_MS    1000    // moving down: the feeling decays instead of cutting

// FAST and DIZZY are *floor* states. Neither can fire while the car is in a hand — the
// context bit decides the situation first, so no threshold below has to be chosen to
// avoid being tripped by handling. That is the whole reason the shake path is gone: it
// keyed on RMS magnitude, which handling and shaking share, and no value ever separated
// them (spec'd 8, then 45, then 120, never confirmed).
// Lowered 4.0 -> 2.5 once the context bit took over: the old value had to sit above a
// hand, and no longer does, because FAST cannot fire while the car is held at all. What
// it must now clear is only the resting noise floor, measured at 0.5-0.6 on every surface
// (it is the sensor, not the surface). The headroom buys soft surfaces: a bed, a sofa or
// a carpet damps most of the rattle a table transmits, and at 4.0 a car pushed across one
// would never have reached FAST. Remaining false positive: a surface that vibrates by
// itself. Per-surface adjustment without a reflash is P6.
#define FAST_ENTER_RMS   2.5f   // vibration RMS (dps) to enter FAST
#define FAST_EXIT_RMS    1.5f   // vibration RMS below which FAST is left

// DIZZY — a real spin on the floor. One sign held, which a wobble or a bump cannot fake.
#define SPIN_ENTER_DPS  40.0f   // |gz| to enter the spin (one sign held for SPIN_HOLD_MS)
#define SPIN_EXIT_DPS   25.0f   // |gz| below which the spin ends (a sign flip does not)
#define SPIN_HOLD_MS     200    // spin must hold one sign this long

// DIZZY aftermath — dose = Σ|gz|·dt over the spin, i.e. degrees actually spun.
// Spin longer or faster → dizzier for longer.
#define DIZZY_MS_PER_DEG 8.0f   // wooze duration per degree accumulated
#define DIZZY_MIN_MS     1000   // ...clamped: a flick still earns a moment of 🥴
#define DIZZY_MAX_MS     8000   // ...and a long spin can't lock the face up

// Context bit — floor vs hand. Gates HAPPY on and FAST off; nothing else reads it.
// Enter on roll travel: a car on wheels cannot turn about its long axis, a hand does it
// constantly. Leave on stillness: a set-down car settles (rms 0.5), a hand does not
// (rms never fell below ~10 while held, so 2 s of quiet really does mean "put down").
//
// Travel is UNSIGNED: tilting one way and back nets to zero as a signed angle, but it
// is still something only a hand can do. 25 deg within the window needs |gy| averaging
// 12.5 dps, and a car rolling on the floor peaks around 6 — so the floor cannot fake it.
#define STILL_RMS        2.0f   // RMS at rest; load-bearing — it decides put-down (P5)
#define HAND_TILT_DEG   25.0f   // roll travel that means "someone picked me up"
#define HAND_TILT_MS     2000   // ...within this window, else the accumulator restarts
#define HAND_QUIET_DPS   3.0f   // |gy| below this is noise: don't integrate it
#define HAND_QUIET_MS     300   // ...and only clear the accumulator after this much quiet,
                                // or every zero-crossing mid-gesture wipes it (measured bug)
#define HAND_SETTLE_MS   2000   // stillness this long = set back down

// ---------- derived (don't edit) ----------
#define RMS_WINDOW  (RMS_WINDOW_MS * 1000 / SAMPLE_US)
