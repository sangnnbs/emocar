#!/usr/bin/env python3
"""
mkanim.py — build emocar/anim_data.h: one animation per CarState for the compiled
display profile, index 0 = NORMAL, 1 = FAST, 2 = DIZZY, 3 = HAPPY.

NAMES below is the single source of the count: append a state there and to enum
CarState (never reorder — uploaded /anim<N>.qgif keep their meaning by index).

  ./mkanim.py                                   # built-in placeholder faces
  ./mkanim.py gifs/normal.gif - - - --invert    # one real GIF, placeholders for the rest
  ./mkanim.py gifs/normal.gif gifs/fast.gif gifs/dizzy.gif -
  ./mkanim.py --preview                         # also write gifs/preview_*.gif (4x)
  ./mkanim.py --profile sh1106_128x64           # pick the target from server/profiles.json
Source GIFs and previews live in gifs/.

GIF conversion reuses ../qgif_player/tools/gif2qbit.py (flags --threshold / --invert / --scale
/ --zoom are passed through; --scale and --zoom default to the profile's "defaults" block in
server/profiles.json, same as the web UI). Run with the qgif_player venv:
../qgif_player/venv/bin/python mkanim.py

Output is a QGIF v2 container: "QGF2" | u8 format | u16 w | u16 h | u16 frames |
u16 delay[frames] | frames. mono1 frames: ceil(w/8) B/row, MSB = leftmost pixel,
bit 1 = DARK (the display driver inverts). The firmware also still reads legacy v1.
Placeholder drawing currently targets mono1 128x64 only — extend when a new
profile lands in server/profiles.json.
"""
import argparse, json, math, struct, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / ".." / "qgif_player" / "tools"))
from PIL import Image, ImageDraw               # noqa: E402
import gif2qbit                                # noqa: E402

GIFS = HERE / "gifs"                            # source GIFs + preview output
PROFILES = json.loads((HERE / "server" / "profiles.json").read_text())["profiles"]
W, H = 128, 64                                  # set from the profile in main()
NAMES = ("normal", "fast", "dizzy", "happy")
MAGIC = b"QGF2"
FMT_MONO1 = 0

# ---------------------------------------------------------------- placeholder faces
# Drawn as black ink on white, then packed with invert=True: ink -> bit 0 -> the sketch's
# ~ makes it LIT, so the OLED shows a bright face on a black background.

def canvas():
    im = Image.new("L", (W, H), 255)
    return im, ImageDraw.Draw(im)

def eye(d, cx, cy, rx, ry, fill=0):
    d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=fill)

def face_normal(blink):                       # 🙂  blink = 0..1 (1 = closed)
    im, d = canvas()
    ry = max(1, round(11 * (1 - blink)))
    for cx in (44, 84):
        eye(d, cx, 26, 9, ry)
    d.arc((40, 30, 88, 56), 20, 160, fill=0, width=3)          # smile
    return im

def face_fast(shift):                          # 😮  wide eyes + motion lines, wobbling
    im, d = canvas()
    for cx in (44, 84):
        eye(d, cx + shift, 22, 12, 12)
        eye(d, cx + shift, 22, 6, 6, fill=255)
        eye(d, cx + shift - 2, 19, 2, 2)
    d.ellipse((54 + shift, 38, 74 + shift, 60), outline=0, width=3)   # "O" mouth
    for y in (14, 32, 50):                                            # speed lines
        d.line((4, y, 20, y), fill=0, width=2)
        d.line((108, y, 124, y), fill=0, width=2)
    return im

def face_dizzy(phase):                         # 🥴  spiral eyes rotating, wavy mouth
    im, d = canvas()
    for cx in (44, 84):
        for k in range(3):                     # three arcs = a crude spiral
            r = 4 + k * 3
            a = (phase * 60 + k * 40) % 360
            d.arc((cx - r, 24 - r, cx + r, 24 + r), a, a + 220, fill=0, width=2)
    pts = [(40 + i * 6, 50 + (3 if (i + phase) % 2 else -3)) for i in range(9)]
    d.line(pts, fill=0, width=3)
    return im

def face_happy(bounce):                        # 🥰  closed happy arcs, blush, gentle bob
    im, d = canvas()
    y = 26 + bounce
    for cx in (44, 84):                        # ^ ^ eyes: upward arcs, the "content" read
        d.arc((cx - 10, y - 9, cx + 10, y + 9), 200, 340, fill=0, width=3)
    d.arc((44, y + 6, 84, y + 30), 20, 160, fill=0, width=3)                 # wide smile
    for cx in (26, 102):                                                     # blush marks
        d.ellipse((cx - 6, y + 8, cx + 6, y + 16), outline=0, width=2)
    return im

def placeholder_frames(name):
    """-> [(delay_ms, PIL 'L' image)]"""
    if name == "normal":
        return [(1400, face_normal(0)), (40, face_normal(0.5)), (70, face_normal(1)),
                (40, face_normal(0.5)), (1900, face_normal(0)), (40, face_normal(0.5)),
                (70, face_normal(1)), (40, face_normal(0.5))]
    if name == "fast":
        return [(70, face_fast(s)) for s in (-2, 0, 2, 0)]
    if name == "dizzy":
        return [(60, face_dizzy(p)) for p in range(6)]
    return [(220, face_happy(b)) for b in (0, -1, -2, -1, 0, 1)]   # slow contented bob

# ---------------------------------------------------------------- packing

def frames_to_qgif(frames, threshold=128, invert=False):
    out = bytearray(MAGIC + struct.pack("<BHHH", FMT_MONO1, W, H, len(frames)))
    for ms, _ in frames:
        out += struct.pack("<H", ms)
    for _, im in frames:
        out += gif2qbit.frame_to_bitmap(im, threshold, invert)
    return bytes(out)

def gif_to_qgif(path, threshold, invert, scale, zoom):
    img = Image.open(path)
    frames = []
    try:
        while True:
            ms = img.info.get("duration", 100) or 100
            frames.append((ms, gif2qbit.resize_frame(img.convert("RGBA"), scale, zoom)))
            img.seek(img.tell() + 1)
    except EOFError:
        pass
    if len(frames) > 255:
        sys.exit(f"{path}: {len(frames)} frames, max 255")
    return frames_to_qgif(frames, threshold, invert)

def qgif_parse(name, data):
    """-> (frames, delay_table_offset). Accepts v2 and legacy v1 mono1 containers."""
    fb = (W + 7) // 8 * H
    if data[:4] == MAGIC:
        fmt, w, h, n = struct.unpack_from("<BHHH", data, 4)
        ok = fmt == FMT_MONO1 and n > 0 and (w, h) == (W, H) and len(data) == 11 + 2 * n + fb * n
        off = 11
    else:                                       # legacy v1
        n = data[0]
        w, h = struct.unpack_from("<HH", data, 1)
        ok = n > 0 and (w, h) == (W, H) and len(data) == 5 + 2 * n + fb * n
        off = 5
    if not ok:
        sys.exit(f"{name}: not a valid {W}x{H} mono1 .qgif")
    return n, off

def upgrade_v1(data):
    """Rewrap a legacy v1 container as v2 (same delays and frames)."""
    if data[:4] == MAGIC:
        return data
    return MAGIC + struct.pack("<BHHH", FMT_MONO1, W, H, data[0]) + data[5:]

def write_preview(name, data, scale=4):
    """Same polarity as the OLED: bit 1 = dark pixel, so invert PIL's '1' mode (bit 1 = white)."""
    n, off = qgif_parse(name, data)
    fb = (W + 7) // 8 * H
    delays = struct.unpack_from(f"<{n}H", data, off)
    base = off + 2 * n
    ims = []
    for i in range(n):
        im = Image.frombytes("1", (W, H), data[base + i * fb: base + (i + 1) * fb])
        im = im.convert("L").point(lambda v: 255 - v)
        ims.append(im.resize((W * scale, H * scale), Image.NEAREST).convert("P"))
    GIFS.mkdir(exist_ok=True)
    ims[0].save(GIFS / f"preview_{name}.gif", save_all=True, append_images=ims[1:],
                duration=list(delays), loop=0)

def write_header(blobs, sources):
    lines = ["// Generated by mkanim.py — do not edit. Index = CarState ("
             + ", ".join(n.upper() for n in NAMES) + ").",
             f"// QGIF v2 containers for profile {W}x{H} mono1.",
             "#pragma once", "#include <pgmspace.h>"]
    for i, (name, data) in enumerate(zip(NAMES, blobs)):
        n, _ = qgif_parse(name, data)
        lines.append(f"// {i}: {name} <- {sources[i]} ({n} frames, {len(data)} bytes)")
        lines.append(f"static const uint8_t anim_{name}[] PROGMEM = {{")
        for off in range(0, len(data), 16):
            lines.append("  " + ", ".join(f"0x{b:02X}" for b in data[off:off + 16]) + ",")
        lines.append("};")
    lines.append("struct AnimEntry { const uint8_t *data; uint32_t len; const char *name; };")
    lines.append(f"#define ANIM_COUNT {len(NAMES)}")
    lines.append("static const AnimEntry ANIM[ANIM_COUNT] = {")
    for name in NAMES:
        lines.append(f'  {{ anim_{name}, sizeof(anim_{name}), "{name}" }},')
    lines.append("};")
    (HERE / "anim_data.h").write_text("\n".join(lines) + "\n")

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*",
                    help=" ".join(NAMES) + " (.gif or .qgif, or - for the placeholder); none = placeholders")
    ap.add_argument("-t", "--threshold", type=int, default=128)
    ap.add_argument("--invert", action="store_true")
    ap.add_argument("--scale", default=None, choices=("fit", "stretch", "fit_width", "fit_height"),
                    help="scale mode (default: the profile's defaults.scale)")
    ap.add_argument("--zoom", type=int, default=None,
                    help="zoom percent applied after --scale, about the centre; overflow is "
                         "cropped (default: the profile's defaults.zoom)")
    ap.add_argument("--preview", action="store_true", help="write gifs/preview_<state>.gif at 4x")
    ap.add_argument("--profile", default=PROFILES[0]["id"],
                    help="display profile id from server/profiles.json (default: %(default)s)")
    a = ap.parse_args()
    if a.files and len(a.files) != len(NAMES):
        sys.exit(f"give exactly {len(NAMES)} files: {' '.join(NAMES)}")

    global W, H
    prof = next((p for p in PROFILES if p["id"] == a.profile), None)
    if prof is None:
        sys.exit(f"unknown profile {a.profile!r}; see server/profiles.json")
    if prof["format"] != "mono1" or (prof["width"], prof["height"]) != (128, 64):
        sys.exit(f"{a.profile}: placeholder generation only supports mono1 128x64 so far")
    W, H = prof["width"], prof["height"]
    d = prof.get("defaults", {})                 # unset knobs follow the profile, like the web UI
    if a.scale is None:
        a.scale = d.get("scale", "fit")
    if a.zoom is None:
        a.zoom = d.get("zoom", 100)

    blobs, sources = [], []
    for i, name in enumerate(NAMES):
        if a.files and a.files[i] != "-":
            p = Path(a.files[i])
            data = upgrade_v1(p.read_bytes()) if p.suffix == ".qgif" \
                else gif_to_qgif(p, a.threshold, a.invert, a.scale, a.zoom)
            sources.append(p.name)
        else:
            data = frames_to_qgif(placeholder_frames(name), invert=True)   # lit face, black bg
            sources.append("placeholder")
        n, _ = qgif_parse(name, data)
        blobs.append(data)
        print(f"{name:9s} {sources[-1]:20s} {n:3d} frames {len(data):6d} bytes")
        if a.preview:
            write_preview(name, data)
    write_header(blobs, sources)
    print(f"wrote anim_data.h ({sum(map(len, blobs))} bytes total)")

if __name__ == "__main__":
    main()
