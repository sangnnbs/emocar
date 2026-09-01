# Plans — GIF upload tool for ESP32 devices

Where the `server/` tool is headed. Decided 2026-09-01.

**Now (local testing): Option A — desktop Go binary.**
**Production (when we need real users): Option C — cloud static page + Web Serial.**

The goal is a tool *any* ESP32 owner can use to push styled GIFs (zoom/pan/threshold/
dither) to their device. emocar is the first supported device, not the only one.

## Option A — desktop Go binary (current)

What we have in `server/`: Go binary, UI embedded, converts GIF → `.qgif`, pushes to
the car over its WiFi AP (`PUT http://192.168.4.1/anim/<n>`).

Used for local testing as-is. When it moves to its own repo, the packaging pass is:

- [ ] Split `server/` into its own repo; emocar keeps firmware/hardware only
- [ ] `go:embed` a curated starter library (subset of `emoji_lib/`) so the binary
      works outside this checkout (defaults currently assume `../emoji_lib` etc.)
- [ ] Uploads + `assignments.json` → per-user dir (`os.UserConfigDir()/…`)
- [ ] Auto-open browser on launch; fall back to `:0` if 8080 is taken
- [ ] Cross-compile releases (windows/amd64, darwin/arm64, linux/amd64)
- [ ] De-emocar the code: `-car`/`carURL`/`/api/car/*` → "device"; slot count and
      names from the device's `/status`, not hardcoded `0..2`
- [ ] Publish `DEVICE_API.md`: `GET /status`, `GET|PUT /anim/<n>`, `.qgif` container
      spec (v2, see `server/README.md`)

Known friction (accepted for now): download + unsigned-binary warnings
(SmartScreen/Gatekeeper), stale copies in the wild, users must download before
joining the car's AP (no internet on the AP).

## Option C — cloud page + Web Serial (production target)

Fully static site on Cloudflare Pages. No binary, no server infra.

```
Cloudflare Pages (HTTPS, static)
  ├─ UI (index.html, evolved from server/web/)
  ├─ converter: qgif.go compiled to WASM (GOOS=js GOARCH=wasm) — runs in-browser
  └─ device link: Web Serial API over plugged USB
```

Why this shape: an HTTPS page can never fetch `http://192.168.4.1` (mixed content),
but Web Serial from an HTTPS page to a plugged-in ESP32 works — same pattern as
Espressif's web flasher and the ESPHome web installer.

Build steps, in order (each is independently useful):

1. [ ] Compile `qgif.go` to WASM; static page with file picker, embedded starter
       library, local previews (all tune knobs run in-browser — no round trips).
       Deployable to Pages immediately as a converter-only tool.
2. [ ] Firmware: serial protocol mirroring the HTTP API — length-prefixed frames
       `STATUS` / `PUT <slot> <len> <qgif bytes>` / `GET <slot>`, reusing net.cpp's
       validate-to-temp-then-rename path (~150 lines, shared with HTTP handlers)
3. [ ] Wire Web Serial into the page (port picker, transfer progress)
4. [ ] Deploy to Cloudflare Pages as the primary tool; binary (A) stays as the
       fallback for Firefox/Safari users

Accepted trade-offs: Chromium desktop only (no Safari/Firefox/iOS); USB cable only;
one serial-port permission prompt. Everything built for A carries over — same repo,
same conversion code, same UI, same device spec.

## Rejected / deferred

- **Serve UI from the ESP32 (LittleFS)** — rejected. Nice for emocar owners but
  inverts the "any device" goal (every device maker would embed/update our UI in
  firmware), loses the browsable library, and still needs the JS/WASM converter
  that C needs anyway.
- **Go binary on Cloudflare Pages as-is** — impossible. Pages is static hosting;
  and no cloud host can reach a device on the user's local network.
- **Cloud relay for wireless (D)** — deferred, not rejected. Device joins home WiFi
  and dials out `wss://` to a broker (Cloudflare Worker + Durable Objects); browser
  bridges through it. Adds infra, pairing, and requires device internet. Layers
  cleanly on top of C if cable-free pushing is ever demanded.

## Decision rule

Stay on A while the audience is us. Trigger for building C: we want strangers using
this without installing anything — then start at step 1 above, since it's useful
before any firmware work lands.
