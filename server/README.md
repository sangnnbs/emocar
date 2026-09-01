# emocar server

Web UI on the PC that turns emoji GIFs into `.qgif` animations and pushes them to the
car over its WiFi hotspot. Replaces the compile-time `mkanim.py` embed for real GIFs
(`mkanim.py` now only generates the PROGMEM placeholder faces).

```
browser ── localhost:8080 ──> emocar-server (Go, this dir)
                                   │  scans ../emoji_lib, ../downloads/00_Main
                                   │  GIF -> .qgif for the selected display profile (profiles.json)
                                   └── PUT http://192.168.4.1/anim/<0|1|2> ──> car (WiFi AP "emocar")
```

`../gifs` is deliberately not scanned: it holds renamed copies of emoji_lib files
(mkanim placeholder sources, e.g. `spinning.gif` = `hoamat.gif`), which used to put the
same animation in the library twice and made state assignments look duplicated.

## Display profiles

`profiles.json` (embedded in the binary) is the master list of supported panels — id,
resolution, pixel format (`mono1` / `gray4` / `rgb565`) and conversion defaults. The UI's
**display** selector picks which profile to convert and preview for; it follows the
profile the car reports in `/status` until you choose one manually. The firmware's
`config.h` `DISP_*` block must match the entry for the panel it was compiled for — the
car (and the server, for ready-made `.qgif` files) refuses mismatched pushes.

To add a monitor: append an entry here, add a `DISP_*` profile block to `../config.h`
and a driver backend to `../display.cpp`.

## Run

```bash
cd ~/sang/esp32/emocar/server
make run                        # build + UI on http://localhost:8080
```

`make` alone lists the targets: `run`, `open`, `status`, `assign STATE=2 GIF=hoamat.gif`,
`vet`, `clean`.

Join the `emocar` WiFi network (password in `../config.h`, default `emocar123`) so the
server can reach the car at `192.168.4.1`. Flags: `-listen`, `-car`, `-lib` (comma-separated
library dirs), `-uploads` (where browser-added GIFs are saved).

## Using it

- **Three state cards** show what the car currently plays — including which library gif
  it is (the server remembers every assignment in `<uploads>/assignments.json`; the name
  is only shown when the byte size still matches what's on the car). The live state is
  highlighted; "placeholder" = no file uploaded yet. Click a card to target that state.
- **One gif per state, visibly**: library entries are badged with the state they're on
  ("on NORMAL"), and the tune panel says what the target state currently has plus a ⚠
  warning when the gif is already assigned to a different state.
- **Library** lists every `.gif`/`.qgif` in the library dirs (previews are the real
  conversion for the selected display profile — exactly what the panel will show).
  Click one to open the tune panel: scale, zoom (50–400%) and pan — drag the live
  preview to pan or type exact x/y panel pixels (+ is right/down), scroll on it to
  zoom, double-click or **reset** to recenter — so e.g. a face GIF can be cropped
  down to just its eyes for a Cars-style windshield. Mono panels
  additionally get threshold / invert / dither (`none` / `bayer` / `floyd`) knobs.
  Then **Send to car**.
  Ready-made `.qgif` files are sent as-is, no knobs.
- **+ add GIF** copies a new file into `../downloads/uploads/` and it appears in the library.

The car stores only the three active animations (LittleFS `/anim0..2.qgif`); the library
never leaves the PC. Uploads survive power cycles and firmware reflashes.

## Car HTTP API (net.cpp on the ESP32)

| Route | Effect |
|---|---|
| `GET /status` | `{state, heap, uptime, profile, w, h, format, slots:[{file,frames,bytes}×3]}` |
| `GET /anim/<n>` | the slot's `.qgif` (404 while the placeholder is in use) |
| `PUT /anim/<n>` | replace slot: stream to temp file, validate header+length, rename, reload |

A PUT shows `UPDATING` on the OLED and blocks the motion loop for the transfer (~1–2 s).
Validation before the rename means a dropped transfer can't corrupt the active slot.

## Files

```
main.go        flags, library scan, HTTP API for the UI, proxy to the car
profiles.go    display profile registry (embeds profiles.json)
qgif.go        GIF -> .qgif conversion per profile (composite, box resize, pack) + preview render
web/index.html the whole UI, embedded into the binary
```

## .qgif container (v2)

```
"QGF2" | u8 format | u16 w | u16 h | u16 frames | u16 delay_ms[frames] | frames
format 0 = mono1  (ceil(w/8) B/row, MSB = leftmost, bit 1 = DARK)
format 1 = gray4  (ceil(w/2) B/row, high nibble = leftmost, 0=black 15=white)
format 2 = rgb565 (2 B/px big-endian)
```

Delays in ms (0 → 100 ms), max 255 frames. The legacy v1 layout
(`u8 frames | u16 w | u16 h | delays | 1024 B/frame`, mono1 128×64 only) still decodes
everywhere but everything new is written as v2. mono1 packing matches
`qgif_player/tools/gif2qbit.py`; only the resampling filter differs
(box vs Lanczos, ~0.3 % of pixels at edges).
