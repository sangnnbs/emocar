# emocar — L3G4200D (GY-50) + SH1106 OLED ↔ ESP32-C3 Super Mini — Wiring

Board facts from <https://www.espboards.dev/esp32/esp32-c3-super-mini/> (the only source used):

- ESP32-C3, 4 MB flash, native USB CDC (enable **USB CDC On Boot** or `Serial` never appears)
- Arduino IDE: **"ESP32C3 Dev Module"** · PlatformIO: `esp32-c3-devkitm-1`
- Board default I²C: **SDA = GPIO8, SCL = GPIO9** — used here, so `Wire.begin()` needs no arguments
- Strapping pins: GPIO2, 4, 5, 6, 7, **8, 9**. GPIO8/9 are fine for I²C because the GY-50's
  pull-ups hold both lines HIGH at boot — exactly the level the boot logic expects
- Onboard blue LED shares **GPIO8** (SDA) → it flickers with I²C traffic. Harmless; a free "bus alive" indicator
- GPIO9 is also the **BOOT** button — don't hold it while the gyro is running

## Connections (I²C mode — 4 wires)

| GY-50 pin | Function | ESP32-C3 Super Mini | Notes |
|---|---|---|---|
| **VCC** | Power in (onboard LDO) | **3V3** | Tolerates 5 V, but use 3V3 so I²C logic matches the C3 |
| **GND** | Ground | **GND** | — |
| **SCL** | I²C clock | **GPIO9** (silkscreen `9`, header pin 13) | BOOT button pin — leave it alone at runtime |
| **SDA** | I²C data | **GPIO8** (silkscreen `8`, header pin 12) | Shares the blue LED → flickers with traffic |
| SDO | I²C address LSB | leave open | Module pulls up → address **0x69**; tie to GND for 0x68 |
| CS | I²C / SPI select | leave open | Must be HIGH for I²C; module pulls it up. If scan misses 0x69, tie **CS → 3V3** |
| INT1 | Threshold interrupt | leave open | Only for wake-on-motion (spare GPIO0/1/3/10 available) |
| INT2 / DRDY | Data-ready | leave open | Polling over I²C is sufficient |

Pull-ups: the GY-50 has its own SDA/SCL pull-ups — no external resistors needed.
Current: ~6 mA for the gyro; the 3V3 pin handles it easily.

## Diagram

```
 GY-50 (L3G4200D)              ESP32-C3 Super Mini
 ┌───────────┐                 ┌──────────────────────┐
 │ VCC  ●────┼─────────────────┼● 3V3                 │
 │ GND  ●────┼─────────────────┼● GND                 │
 │ SCL  ●────┼─────────────────┼● GPIO9  ("9", BOOT)  │
 │ SDA  ●────┼─────────────────┼● GPIO8  ("8", LED)   │
 │ SDO  ○    │  (open → 0x69)  │                      │
 │ CS   ○    │  (open = I²C)   │  GPIO0,1,3,10 spare  │
 │ INT1 ○    │                 │                      │
 │ INT2 ○    │                 │                      │
 └───────────┘                 └──────────────────────┘
```

## SH1106 OLED (128×64, I²C, 0x3C) — same bus

| SH1106 pin | ESP32-C3 Super Mini | Notes |
|---|---|---|
| **VCC** | **3V3** | Module has its own regulator/level-shift; 3V3 is fine |
| **GND** | **GND** | — |
| **SCL** | **GPIO9** | Shared with gyro SCL |
| **SDA** | **GPIO8** | Shared with gyro SDA |

Addresses **0x69** (gyro) and **0x3C** (OLED) don't collide. Both modules carry pull-ups;
two sets in parallel (~2.3 kΩ effective) is still fine at 400 kHz. Total bus current ≈ 6 mA gyro + ~20 mA OLED.

## Shared-bus diagram

```
                    3V3 ──┬──────────────┬──
                    GND ──┼──┬───────────┼──┬──
                          │  │           │  │
  ESP32-C3 GPIO9 (SCL) ───┼──┼──┬────────┼──┼──┬──
  ESP32-C3 GPIO8 (SDA) ───┼──┼──┼──┬─────┼──┼──┼──┬──
                          │  │  │  │     │  │  │  │
                        ┌─┴──┴──┴──┴─┐ ┌─┴──┴──┴──┴─┐
                        │VCC GND SCL SDA│ │VCC GND SCL SDA│
                        │  GY-50 0x69  │ │ SH1106 0x3C  │
                        │ SDO CS INT1  │ └──────────────┘
                        │  open open   │
                        └──────────────┘
```

Breadboard tip: put both modules on the same two power rails and daisy-chain SCL/SDA
with short jumpers — keep the total bus under ~20 cm.

## Code header

```cpp
// Board: ESP32-C3 Super Mini
// https://www.espboards.dev/esp32/esp32-c3-super-mini/
// 4 MB flash | native USB CDC — enable "USB CDC On Boot"
// Arduino IDE: "ESP32C3 Dev Module" | PlatformIO: esp32-c3-devkitm-1
// I2C on board defaults: SDA GPIO8 (also blue LED), SCL GPIO9 (also BOOT button)
// Avoid for other peripherals: strapping pins GPIO2, 4, 5, 6, 7
#define PIN_SDA 8        // GPIO8, silkscreen "8"
#define PIN_SCL 9        // GPIO9, silkscreen "9"
#define GYRO_ADDR 0x69   // SDO open (pulled up); 0x68 if SDO → GND

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

Wire.begin(PIN_SDA, PIN_SCL);    // = board defaults GPIO8/9
Wire.setClock(400000);
u8g2.setI2CAddress(0x3C << 1);   // U8g2 wants the 8-bit form
u8g2.begin();
```

## Bring-up checklist

1. I²C scan → expect **0x69** and **0x3C**. Missing → tie CS to 3V3. See 0x68 instead → SDO is low, change the address.
2. Read `WHO_AM_I` (0x0F) → must be **0xD3**.
3. Write `CTRL_REG1` (0x20) = **0x0F** (power on, 100 Hz, XYZ enabled). Reset default 0x07 is power-down — all-zero data means this step was skipped.
4. Write `CTRL_REG4` (0x23) with full-scale + **BDU** (bit 7) so high/low bytes aren't torn.
5. Read 6 bytes from `0x28 | 0x80` (auto-increment) → X, Y, Z little-endian int16.

## Gotchas specific to GPIO8/9

| Symptom | Cause | Fix |
|---|---|---|
| Board boots into download mode / no sketch runs | Something pulled GPIO9 low at reset | Only the GY-50 (pull-up) on the bus; don't press BOOT while resetting |
| Blue LED flickers constantly | Normal — LED is on SDA | Ignore, or use it as a bus-activity indicator |
| Bus works but a second device breaks boot | That device drags SDA/SCL low at power-up | Move I²C to GPIO0/1 (`Wire.begin(0, 1)`) |
