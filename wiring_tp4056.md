# emocar — battery power: LiPo 402030 + TP4056 + Schottky diode → ESP32-C3 Super Mini

Goal: the car runs on its LiPo **and** the C3's own USB-C stays usable (flash, serial) with
the battery connected. Without the diode, a LiPo wired to the 5V pin gets USB's 5 V pushed
straight into it the moment a cable is plugged in — uncontrolled charging.

Board facts from <https://www.espboards.dev/esp32/esp32-c3-super-mini/> (the only source used):

- The **5V** pin is the board's "5V power input"; deep sleep ≈ 43 µA
- The page does **not** say whether the 5V pin is tied straight to USB VBUS or through an
  onboard diode, nor which regulator is fitted. This design works either way: D1 blocks any
  current from the 5V rail back into the battery, whatever sits between VBUS and the 5V pin.

## Parts

| Ref | Part | Notes |
|---|---|---|
| BT1 | LiPo 402030, 3.7 V 200 mAh | 4.2 V full, ~3.0 V empty |
| U1 | TP4056 module **with protection** (DW01 + 8205A, pads B+/B−/OUT+/OUT−) | R3 replaced — see charge current below |
| SW1 | slide switch | power on/off; charging still works when off |
| D1 | Schottky, 1 A or more: 1N5819, SS14, SS34 | cathode band toward the ESP32; bigger part = less voltage drop |
| C1 | 470–1000 µF electrolytic, ≥ 6.3 V | rides out the current steps a 200 mAh cell sags on |

## Diagram

```
   charger USB
        │
  ┌─────▼──────────────────┐
  │ TP4056 + protection    │          BT1  LiPo 402030
  │ (R3 replaced)          │               3.7 V 200 mAh
  │                 B+  ●──┼── red ─────── (+)
  │                 B−  ●──┼── black ───── (−)
  │                        │
  │   OUT+ ●     OUT− ●    │
  └─────┬──────────┬───────┘
        │          │
       SW1         │
        │          │
       ─┬─         │
        ▼  D1      │          Schottky, band (cathode) on the ESP32 side
       ─┴─         │
        │          │
        ├──(+)C1(−)┤          470–1000 µF
        │          │
  ┌─────┴──────────┴───────┐
  │   5V          GND      │
  │  ESP32-C3 Super Mini   │
  │              USB-C ◀───┼── PC: flash / serial, safe with the battery on
  └────────────────────────┘
```

| From | To | Why |
|---|---|---|
| BT1 red (+) | U1 **B+** | cell |
| BT1 black (−) | U1 **B−** | cell |
| U1 **OUT+** | SW1 → D1 anode | load side of the protection |
| D1 cathode (band) | C3 **5V** | one-way: battery → board only |
| U1 **OUT−** | C3 **GND** | ⚠️ OUT−, not B− — B− bypasses the over-discharge cut-off |
| C1 + / − | C3 5V / GND | as close to the C3 as it fits |

GY-50 and SH1106 stay on the C3's **3V3** pin exactly as in [wiring.md](wiring.md).

## What happens when you plug in

Whichever side has the higher voltage powers the ESP32. The C3's USB puts about 5 V on the
5V pin, always above the battery (4.2 V at most), so D1 blocks and the battery is left alone.

| Plugged in | ESP32 runs from | Battery | D1 |
|---|---|---|---|
| **1** C3 USB only | PC USB | idle — the C3's USB never charges it | blocked |
| **2** TP4056 USB only, SW1 on | charger, through the battery pads | charges slowly — gets only what the car leaves; DONE may never light | conducts |
| **2** TP4056 USB only, SW1 off | — (car off) | charges fully, DONE lights | no current |
| **3** Both | PC USB | charges fully, DONE lights — SW1 doesn't matter | blocked |
| Nothing, SW1 on | battery (≈ Vcell − 0.3 V) | draining, roughly 4–5 h | conducts |

Developing while charging: plug in both. Just charging: SW1 off, or plug in both.
No switch at all behaves like SW1 on.

## Charge current (R3 = the TP4056's R_PROG)

`I_charge ≈ 1200 / R3` (amps, R3 in ohms). A 200 mAh cell wants ≤ 200 mA (1C); 0.5C is kinder.

| R3 | Charge current | For 200 mAh |
|---|---|---|
| 1.2 kΩ (stock) | 1000 mA | ❌ 5C — never on this cell |
| 5.1 kΩ | ~235 mA | ⚠️ just over 1C |
| 6.8 kΩ | ~175 mA | OK |
| 10 kΩ | ~120 mA | ✅ gentle |
| 12 kΩ | ~100 mA | ✅ 0.5C |

## Voltage budget

The 5V pin sees the cell minus D1's drop (~0.3 V), and the board's regulator makes 3V3 from
that. So below a cell voltage of roughly 3.6 V the 3V3 rail starts sagging with the cell.
The ESP32-C3 chip keeps running down to about 3.0 V, and most of a LiPo's capacity sits above
3.5 V, so the loss is small. If it ever matters, swap D1 for a P-MOSFET power-path (near-zero drop).

Rough runtime on 200 mAh: ~4–5 h. It was half that until the WiFi hotspot was removed —
an AP the C3 can never sleep off costs 80–100 mA, more than the rest of the car together.
Estimated from datasheet figures, not yet measured on the cell.

## Bring-up checklist (multimeter, ESP32 not connected yet)

1. Charge the cell: charger USB in, red LED → blue/green when done. Measure B+ to B− ≈ 4.2 V.
2. Measure OUT+ to OUT− ≈ cell voltage. 0 V → protection tripped; plug the charger once to reset it.
3. SW1 on, measure D1 cathode to OUT− ≈ cell − 0.3 V.
4. Connect 5V/GND to the C3, SW1 on → it boots on battery (OLED splash).
5. Plug the C3's USB with SW1 still on. Measure across D1: the **cathode must read higher than
   the anode** (reverse-biased). That is the proof USB isn't reaching the cell.
6. Flash over USB with the battery on — it must work like on the bench.

## Gotchas

| Symptom | Cause | Fix |
|---|---|---|
| Charger LED never turns "done" | The ESP32 draws current from the cell while it charges, so the TP4056 never sees the charge current fall to its end point | Charge with SW1 off, or with the C3's USB also plugged in |
| Board resets under a current step | A flash write or the OLED waking sags a small cell | C1 close to the C3; check the cell is charged |
| Cell hot or swelling while charging | R3 too small (charge current too high) | Check R3 against the table — stop using the cell if it swelled |
| Nothing powers up, cell is fine | Load wired to B− instead of OUT−, or protection tripped | Rewire to OUT−; plug the charger once |
