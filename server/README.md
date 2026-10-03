# emocar server

Web UI on the PC that turns emoji GIFs into `.qgif` animations and pushes them to the
car over its USB cable. Replaces the compile-time `mkanim.py` embed for real GIFs
(`mkanim.py` now only generates the PROGMEM placeholder faces).

```
browser ── localhost:8080 ──> emocar-server (Go, this dir)
                                   │  scans ../emoji_lib, ../downloads/00_Main
                                   │  GIF -> .qgif for the selected display profile (profiles.json)
                                   └── PUT <slot> <len> over /dev/ttyACM0 ──> car (USB serial)
```

`../gifs` is deliberately not scanned: it holds renamed copies of emoji_lib files
(mkanim placeholder sources, e.g. `spinning.gif` = `hoamat.gif`), which used to put the
same animation in the library twice and made state assignments look duplicated.

## Display profiles

`profiles.json` (embedded in the binary) is the master list of supported panels — id,
resolution, pixel format (`mono1` / `gray4` / `rgb565`) and conversion defaults. The UI's
**display** selector picks which profile to convert and preview for; it follows the
profile the car reports in `STATUS` until you choose one manually. The firmware's
`config.h` `DISP_*` block must match the entry for the panel it was compiled for — the
car (and the server, for ready-made `.qgif` files) refuses mismatched pushes.

To add a monitor: append an entry here, add a `DISP_*` profile block to `../config.h`
and a driver backend to `../display.cpp`.

## Run

```bash
cd ~/sang/esp32/emocar/server
make run                        # build + UI on http://localhost:8080
```

`make` alone lists the targets: `run`, `down`, `open`, `status`,
`assign STATE=2 GIF=hoamat.gif`, `vet`, `clean`.

Plug the car in over USB. The server finds it by its Espressif USB id (VID `303A`) —
`-port /dev/ttyACM0` (or `COM5`, `/dev/cu.usbmodem…`) overrides that when more than one
ESP32 is attached. Other flags: `-listen`, `-lib` (comma-separated library dirs),
`-uploads` (where browser-added GIFs are saved).

**GIF folder.** By default both the library and uploads are a `gifs` folder next to the
binary, created on first start — e.g. `C:\emocar\emocar-server.exe` uses `C:\emocar\gifs\`,
however it was started. Drop `.gif`/`.qgif` files there, or use **+ add GIF**. `make run`
overrides this with the repo's dev libraries (`DEV_LIB`, `DEV_UPLOADS` in the Makefile).

**The server holds the port open**, so it must be stopped before reflashing the car —
`make down`, flash, `make run`. It reconnects by itself whenever the cable is unplugged
and plugged back in. It never asserts DTR/RTS: on the C3 those lines are esptool's reset
sequence, so touching them would reboot the car every time the server connected.

## Using it

- **Four state cards** show what the car currently plays — including which library gif
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
- **+ add GIF** copies a new file into the uploads folder (`gifs/` next to the binary by
  default) and it appears in the library.

The car stores only the four active animations (LittleFS `/anim0..3.qgif`); the library
never leaves the PC. Uploads survive power cycles and firmware reflashes.

## Car serial protocol (cmd.cpp on the ESP32)

One command line, then a fixed-length raw payload — the serial equivalent of
`Content-Length`, and for the same reason: the `.qgif` is streamed straight to LittleFS,
never buffered in RAM.

| Command | Reply |
|---|---|
| `STATUS` | `OK {state, heap, uptime, profile, w, h, format, slots:[{file,frames,bytes}×4]}` |
| `GET <n>` | `OK <len>` + `<len>` raw bytes (`ERR placeholder in use` when the slot has no file) |
| `PUT <n> <len>` | `READY <chunk>`, then the payload one chunk at a time, each answered `ACK <got>` once it is on flash, and `OK <bytes>` at the end |

Replies are one line starting `OK` / `ERR` / `READY` / `ACK`. Current firmware prints
nothing else, but older builds also print telemetry on the same port, so a client reads
past any line that doesn't start with one of those four words.

**The chunking is not optional**, and any other client (a Web Serial page, say) has to do
the same. USB CDC looks reliable but isn't, end to end: the C3 driver's interrupt pushes
received bytes into a fixed-size queue and drops whatever doesn't fit, with nothing to
retransmit them. Pushing a 33 KB animation in one write lost two thirds of it — the losses
land wherever the firmware is busy erasing flash — and even a single 1 KB burst lost 369
bytes, because the queue is drained one byte at a time and the stock one holds 256. So the
firmware enlarges that queue to 4 KB, a chunk is a quarter of it, and the next chunk waits
for the `ACK` that says the last one reached flash. What this replaces is TCP, which was
doing all of it invisibly.

A PUT shows `UPDATING` on the OLED and blocks the motion loop for the transfer (~0.3 s).
Validation before the rename means a dropped transfer can't corrupt the active slot.

## Files

```
main.go        flags, library scan, HTTP API for the UI, proxy to the car
serial.go      the car link: port auto-detect, reconnect, STATUS / GET / PUT
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
