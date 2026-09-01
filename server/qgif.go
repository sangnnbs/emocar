// qgif.go — GIF ⇄ QGIF conversion for any display profile.
//
// QGIF v2 container (little-endian):
//   [0..3]   "QGF2" magic
//   [4]      u8  format          0=mono1  1=gray4  2=rgb565
//   [5..6]   u16 width
//   [7..8]   u16 height
//   [9..10]  u16 frames          (1..255 — firmware delay table caps at 255)
//   [11..]   u16 delay_ms[frames]
//   [..]     frames × frameBytes(format, w, h)
//
// Frame layouts:
//   mono1  — ceil(w/8) B/row, MSB = leftmost pixel, bit 1 = DARK (firmware driver inverts)
//   gray4  — ceil(w/2) B/row, high nibble = leftmost pixel, 0 = black .. 15 = white
//   rgb565 — 2 B/pixel big-endian (RRRRRGGG GGGBBBBB), row-major
//
// Legacy v1 (decode only): u8 frames | u16 w=128 | u16 h=64 | u16 delays[] | 1024 B/frame,
// same mono1 bit layout. Everything new is encoded as v2.
package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"image"
	"image/color"
	"image/draw"
	"image/gif"
)

const (
	maxFrames = 255
	hdrV2     = 11
	zoomMax   = 400 // percent — past this the panel shows a handful of source pixels
)

var magicV2 = []byte("QGF2")

// ---------------------------------------------------------------- formats

type Format uint8

const (
	FmtMono1  Format = 0
	FmtGray4  Format = 1
	FmtRGB565 Format = 2
)

func ParseFormat(s string) (Format, error) {
	switch s {
	case "mono1":
		return FmtMono1, nil
	case "gray4":
		return FmtGray4, nil
	case "rgb565":
		return FmtRGB565, nil
	}
	return 0, fmt.Errorf("unknown pixel format %q", s)
}

func (f Format) String() string {
	switch f {
	case FmtMono1:
		return "mono1"
	case FmtGray4:
		return "gray4"
	case FmtRGB565:
		return "rgb565"
	}
	return fmt.Sprintf("format(%d)", int(f))
}

func (f Format) FrameBytes(w, h int) int {
	switch f {
	case FmtMono1:
		return (w + 7) / 8 * h
	case FmtGray4:
		return (w + 1) / 2 * h
	case FmtRGB565:
		return w * h * 2
	}
	return 0
}

// ---------------------------------------------------------------- Anim

type Anim struct {
	W, H    int
	Format  Format
	Delays  []uint16 // per frame, milliseconds
	Bitmaps [][]byte // FrameBytes each
}

type ConvOpts struct {
	Threshold int    // mono1: luminance below this = dark bit; default 128
	Invert    bool
	Scale     string // fit | stretch | fit_width | fit_height
	Dither    string // mono1: none | bayer | floyd
	Zoom      int    // percent applied after Scale, about the center; 100 = off
	PanX      int    // panel pixels the zoomed image is shifted right (negative = left)
	PanY      int    // panel pixels the zoomed image is shifted down (negative = up)
}

// ---------------------------------------------------------------- qgif encode/decode

func (a *Anim) EncodeQGIF() []byte {
	n := len(a.Bitmaps)
	fb := a.Format.FrameBytes(a.W, a.H)
	out := make([]byte, 0, hdrV2+2*n+fb*n)
	out = append(out, magicV2...)
	out = append(out, byte(a.Format))
	out = binary.LittleEndian.AppendUint16(out, uint16(a.W))
	out = binary.LittleEndian.AppendUint16(out, uint16(a.H))
	out = binary.LittleEndian.AppendUint16(out, uint16(n))
	for _, d := range a.Delays {
		out = binary.LittleEndian.AppendUint16(out, d)
	}
	for _, bm := range a.Bitmaps {
		out = append(out, bm...)
	}
	return out
}

func DecodeQGIF(data []byte) (*Anim, error) {
	if len(data) >= hdrV2 && bytes.Equal(data[:4], magicV2) {
		return decodeV2(data)
	}
	return decodeV1(data)
}

func decodeV2(data []byte) (*Anim, error) {
	f := Format(data[4])
	w := int(binary.LittleEndian.Uint16(data[5:]))
	h := int(binary.LittleEndian.Uint16(data[7:]))
	n := int(binary.LittleEndian.Uint16(data[9:]))
	fb := f.FrameBytes(w, h)
	if fb == 0 || n == 0 || n > maxFrames || len(data) != hdrV2+2*n+fb*n {
		return nil, fmt.Errorf("qgif v2: corrupt header")
	}
	a := &Anim{W: w, H: h, Format: f}
	for i := 0; i < n; i++ {
		a.Delays = append(a.Delays, binary.LittleEndian.Uint16(data[hdrV2+2*i:]))
	}
	base := hdrV2 + 2*n
	for i := 0; i < n; i++ {
		a.Bitmaps = append(a.Bitmaps, data[base+i*fb:base+(i+1)*fb])
	}
	return a, nil
}

// decodeV1 reads the legacy 128×64 mono format (no magic).
func decodeV1(data []byte) (*Anim, error) {
	if len(data) < 5 {
		return nil, fmt.Errorf("qgif: too short")
	}
	n := int(data[0])
	w := int(binary.LittleEndian.Uint16(data[1:]))
	h := int(binary.LittleEndian.Uint16(data[3:]))
	fb := FmtMono1.FrameBytes(w, h)
	if n == 0 || w != 128 || h != 64 || len(data) != 5+2*n+fb*n {
		return nil, fmt.Errorf("qgif: neither a v2 container nor a legacy 128x64 animation")
	}
	a := &Anim{W: w, H: h, Format: FmtMono1}
	for i := 0; i < n; i++ {
		a.Delays = append(a.Delays, binary.LittleEndian.Uint16(data[5+2*i:]))
	}
	base := 5 + 2*n
	for i := 0; i < n; i++ {
		a.Bitmaps = append(a.Bitmaps, data[base+i*fb:base+(i+1)*fb])
	}
	return a, nil
}

// ---------------------------------------------------------------- GIF -> Anim

func ConvertGIF(data []byte, p *Profile, o ConvOpts) (*Anim, error) {
	g, err := gif.DecodeAll(bytes.NewReader(data))
	if err != nil {
		return nil, fmt.Errorf("gif decode: %w", err)
	}
	if len(g.Image) == 0 {
		return nil, fmt.Errorf("gif: no frames")
	}
	if len(g.Image) > maxFrames {
		return nil, fmt.Errorf("gif: %d frames, max %d", len(g.Image), maxFrames)
	}
	if o.Threshold <= 0 {
		o.Threshold = 128
	}
	if o.Zoom <= 0 {
		o.Zoom = 100
	} else if o.Zoom > zoomMax {
		o.Zoom = zoomMax
	}
	o.PanX = min(max(o.PanX, -2*p.W), 2*p.W)
	o.PanY = min(max(o.PanY, -2*p.H), 2*p.H)

	// Composite frames the way PIL's GIF seek does: onto an opaque black canvas,
	// honoring per-frame disposal.
	lw, lh := g.Config.Width, g.Config.Height
	if lw == 0 || lh == 0 {
		b := g.Image[0].Bounds()
		lw, lh = b.Max.X, b.Max.Y
	}
	canvas := image.NewRGBA(image.Rect(0, 0, lw, lh))
	draw.Draw(canvas, canvas.Bounds(), image.NewUniform(color.Black), image.Point{}, draw.Src)

	a := &Anim{W: p.W, H: p.H, Format: p.Format}
	for i, pf := range g.Image {
		var backup *image.RGBA
		if i < len(g.Disposal) && g.Disposal[i] == gif.DisposalPrevious {
			backup = image.NewRGBA(canvas.Bounds())
			copy(backup.Pix, canvas.Pix)
		}
		draw.Draw(canvas, pf.Bounds(), pf, pf.Bounds().Min, draw.Over)

		var frame []byte
		if p.Format == FmtRGB565 {
			rgb := layout(toRGB(canvas), lw, lh, 3, p.W, p.H, o.Scale, o.Zoom, o.PanX, o.PanY)
			frame = packRGB565(rgb, p.W, p.H, o.Invert)
		} else {
			gray := layout(toGray(canvas), lw, lh, 1, p.W, p.H, o.Scale, o.Zoom, o.PanX, o.PanY)
			if p.Format == FmtGray4 {
				frame = packGray4(gray, p.W, p.H, o.Invert)
			} else {
				frame = packMono(gray, p.W, p.H, o.Threshold, o.Invert, o.Dither)
			}
		}
		a.Bitmaps = append(a.Bitmaps, frame)

		ms := 100
		if i < len(g.Delay) && g.Delay[i] > 0 {
			ms = g.Delay[i] * 10 // GIF delay unit is 10 ms
		}
		if ms > 0xFFFF {
			ms = 0xFFFF
		}
		a.Delays = append(a.Delays, uint16(ms))

		if i < len(g.Disposal) {
			switch g.Disposal[i] {
			case gif.DisposalBackground:
				draw.Draw(canvas, pf.Bounds(), image.NewUniform(color.Black), image.Point{}, draw.Src)
			case gif.DisposalPrevious:
				canvas = backup
			}
		}
	}
	return a, nil
}

// ITU-R 601 luminance, same weights as PIL's convert("L").
func toGray(img *image.RGBA) []uint8 {
	b := img.Bounds()
	out := make([]uint8, b.Dx()*b.Dy())
	for y := 0; y < b.Dy(); y++ {
		row := img.Pix[y*img.Stride:]
		for x := 0; x < b.Dx(); x++ {
			r, g, bl := int(row[x*4]), int(row[x*4+1]), int(row[x*4+2])
			out[y*b.Dx()+x] = uint8((299*r + 587*g + 114*bl) / 1000)
		}
	}
	return out
}

func toRGB(img *image.RGBA) []uint8 {
	b := img.Bounds()
	out := make([]uint8, b.Dx()*b.Dy()*3)
	for y := 0; y < b.Dy(); y++ {
		row := img.Pix[y*img.Stride:]
		for x := 0; x < b.Dx(); x++ {
			o := (y*b.Dx() + x) * 3
			out[o], out[o+1], out[o+2] = row[x*4], row[x*4+1], row[x*4+2]
		}
	}
	return out
}

// Resize with integer box sampling (fine for downscale, nearest for upscale),
// ch interleaved channels per pixel.
func resizeBox(src []uint8, sw, sh, ch, nw, nh int) []uint8 {
	out := make([]uint8, nw*nh*ch)
	for dy := 0; dy < nh; dy++ {
		sy0, sy1 := dy*sh/nh, (dy+1)*sh/nh
		if sy1 <= sy0 {
			sy1 = sy0 + 1
		}
		for dx := 0; dx < nw; dx++ {
			sx0, sx1 := dx*sw/nw, (dx+1)*sw/nw
			if sx1 <= sx0 {
				sx1 = sx0 + 1
			}
			for c := 0; c < ch; c++ {
				sum, cnt := 0, 0
				for y := sy0; y < sy1; y++ {
					for x := sx0; x < sx1; x++ {
						sum += int(src[(y*sw+x)*ch+c])
						cnt++
					}
				}
				out[(dy*nw+dx)*ch+c] = uint8(sum / cnt)
			}
		}
	}
	return out
}

// Scale per mode, apply zoom (percent) about the center, then place on a black
// dw×dh canvas — centered plus the pan offset, cropping whatever falls outside.
func layout(px []uint8, sw, sh, ch, dw, dh int, mode string, zoom, panX, panY int) []uint8 {
	var nw, nh int
	switch mode {
	case "stretch":
		nw, nh = dw, dh
	case "fit_width":
		nw = dw
		nh = max(1, sh*dw/sw)
	case "fit_height":
		nh = dh
		nw = max(1, sw*dh/sh)
	default: // fit
		if sw*dh >= sh*dw { // width is the binding constraint
			nw = dw
			nh = max(1, sh*dw/sw)
		} else {
			nh = dh
			nw = max(1, sw*dh/sh)
		}
	}
	if zoom > 0 && zoom != 100 {
		nw = max(1, nw*zoom/100)
		nh = max(1, nh*zoom/100)
	}
	resized := resizeBox(px, sw, sh, ch, nw, nh)
	out := make([]uint8, dw*dh*ch) // black
	xo, yo := (dw-nw)/2+panX, (dh-nh)/2+panY
	for y := 0; y < nh; y++ {
		ty := y + yo
		if ty < 0 || ty >= dh {
			continue
		}
		for x := 0; x < nw; x++ {
			tx := x + xo
			if tx < 0 || tx >= dw {
				continue
			}
			copy(out[(ty*dw+tx)*ch:(ty*dw+tx)*ch+ch], resized[(y*nw+x)*ch:])
		}
	}
	return out
}

// ---------------------------------------------------------------- packers

// bayer8 is the standard 8×8 ordered-dither matrix (values 0..63). Ordered
// dithering is temporally stable — the pattern doesn't shimmer between frames.
var bayer8 = [8][8]int{
	{0, 32, 8, 40, 2, 34, 10, 42},
	{48, 16, 56, 24, 50, 18, 58, 26},
	{12, 44, 4, 36, 14, 46, 6, 38},
	{60, 28, 52, 20, 62, 30, 54, 22},
	{3, 35, 11, 43, 1, 33, 9, 41},
	{51, 19, 59, 27, 49, 17, 57, 25},
	{15, 47, 7, 39, 13, 45, 5, 37},
	{63, 31, 55, 23, 61, 29, 53, 21},
}

// packMono packs 1-bit, bit 1 = dark, MSB = leftmost. threshold biases all
// dither modes the same way: higher threshold → more dark bits.
func packMono(gray []uint8, w, h, threshold int, invert bool, dither string) []byte {
	stride := (w + 7) / 8
	bm := make([]byte, stride*h)
	bias := 128 - threshold // shift midpoint so the slider works in every mode

	var lit func(x, y int) bool
	switch dither {
	case "bayer":
		lit = func(x, y int) bool {
			return int(gray[y*w+x])+bias >= bayer8[y%8][x%8]*4+2
		}
	case "floyd":
		buf := make([]int, len(gray))
		for i, g := range gray {
			buf[i] = int(g) + bias
		}
		for y := 0; y < h; y++ {
			for x := 0; x < w; x++ {
				old := buf[y*w+x]
				var val int
				if old >= 128 {
					val = 255
				}
				err := old - val
				buf[y*w+x] = val
				if x+1 < w {
					buf[y*w+x+1] += err * 7 / 16
				}
				if y+1 < h {
					if x > 0 {
						buf[(y+1)*w+x-1] += err * 3 / 16
					}
					buf[(y+1)*w+x] += err * 5 / 16
					if x+1 < w {
						buf[(y+1)*w+x+1] += err * 1 / 16
					}
				}
			}
		}
		lit = func(x, y int) bool { return buf[y*w+x] != 0 }
	default: // hard threshold
		lit = func(x, y int) bool { return int(gray[y*w+x]) >= threshold }
	}

	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			dark := !lit(x, y)
			if invert {
				dark = !dark
			}
			if dark {
				bm[y*stride+x/8] |= 1 << (7 - x%8)
			}
		}
	}
	return bm
}

// packGray4 packs 4-bit luminance, high nibble = leftmost pixel, 0=black 15=white.
func packGray4(gray []uint8, w, h int, invert bool) []byte {
	stride := (w + 1) / 2
	bm := make([]byte, stride*h)
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			v := gray[y*w+x] >> 4
			if invert {
				v = 15 - v
			}
			if x%2 == 0 {
				bm[y*stride+x/2] |= v << 4
			} else {
				bm[y*stride+x/2] |= v
			}
		}
	}
	return bm
}

// packRGB565 packs 2 B/pixel big-endian, the byte order SPI TFT drivers push.
func packRGB565(rgb []uint8, w, h int, invert bool) []byte {
	bm := make([]byte, w*h*2)
	for i := 0; i < w*h; i++ {
		r, g, b := rgb[i*3], rgb[i*3+1], rgb[i*3+2]
		if invert {
			r, g, b = 255-r, 255-g, 255-b
		}
		v := uint16(r>>3)<<11 | uint16(g>>2)<<5 | uint16(b>>3)
		bm[i*2] = byte(v >> 8)
		bm[i*2+1] = byte(v)
	}
	return bm
}

// ---------------------------------------------------------------- Anim -> preview GIF

// PreviewGIF renders the animation exactly as the display will show it, per format.
func (a *Anim) PreviewGIF() ([]byte, error) {
	var pal color.Palette
	var index func(bm []byte, x, y int) uint8

	switch a.Format {
	case FmtMono1:
		stride := (a.W + 7) / 8
		pal = color.Palette{color.Black, color.White} // bit 1 = dark → index 0
		index = func(bm []byte, x, y int) uint8 {
			if bm[y*stride+x/8]&(1<<(7-x%8)) == 0 {
				return 1
			}
			return 0
		}
	case FmtGray4:
		stride := (a.W + 1) / 2
		for i := 0; i < 16; i++ {
			g := uint8(i * 255 / 15)
			pal = append(pal, color.RGBA{g, g, g, 255})
		}
		index = func(bm []byte, x, y int) uint8 {
			b := bm[y*stride+x/2]
			if x%2 == 0 {
				return b >> 4
			}
			return b & 0x0F
		}
	case FmtRGB565:
		// 6×6×6 color cube + 16 extra grays; nearest-of-two lookup per pixel.
		for r := 0; r < 6; r++ {
			for g := 0; g < 6; g++ {
				for b := 0; b < 6; b++ {
					pal = append(pal, color.RGBA{uint8(r * 51), uint8(g * 51), uint8(b * 51), 255})
				}
			}
		}
		grayBase := len(pal)
		for i := 0; i < 16; i++ {
			g := uint8(i * 17)
			pal = append(pal, color.RGBA{g, g, g, 255})
		}
		index = func(bm []byte, x, y int) uint8 {
			v := uint16(bm[(y*a.W+x)*2])<<8 | uint16(bm[(y*a.W+x)*2+1])
			r := int(v>>11) * 255 / 31
			g := int(v>>5&0x3F) * 255 / 63
			b := int(v&0x1F) * 255 / 31
			cube := ((r+25)/51)*36 + ((g+25)/51)*6 + (b+25)/51
			gray := (299*r + 587*g + 114*b) / 1000
			gi := grayBase + min(15, (gray+8)/17)
			if dist(pal[cube], r, g, b) <= dist(pal[gi], r, g, b) {
				return uint8(cube)
			}
			return uint8(gi)
		}
	default:
		return nil, fmt.Errorf("preview: unsupported format %s", a.Format)
	}

	g := &gif.GIF{LoopCount: 0}
	for i, bm := range a.Bitmaps {
		im := image.NewPaletted(image.Rect(0, 0, a.W, a.H), pal)
		for y := 0; y < a.H; y++ {
			for x := 0; x < a.W; x++ {
				im.SetColorIndex(x, y, index(bm, x, y))
			}
		}
		g.Image = append(g.Image, im)
		g.Delay = append(g.Delay, int(a.Delays[i])/10)
	}
	var buf bytes.Buffer
	if err := gif.EncodeAll(&buf, g); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

func dist(c color.Color, r, g, b int) int {
	cr, cg, cb, _ := c.RGBA()
	dr, dg, db := int(cr>>8)-r, int(cg>>8)-g, int(cb>>8)-b
	return dr*dr + dg*dg + db*db
}
