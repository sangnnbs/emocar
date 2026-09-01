package main

import (
	"bytes"
	"encoding/binary"
	"testing"
)

func testAnim(f Format, w, h int) *Anim {
	fb := f.FrameBytes(w, h)
	a := &Anim{W: w, H: h, Format: f}
	for i := 0; i < 3; i++ {
		frame := make([]byte, fb)
		for j := range frame {
			frame[j] = byte(i*31 + j)
		}
		a.Bitmaps = append(a.Bitmaps, frame)
		a.Delays = append(a.Delays, uint16(100+i))
	}
	return a
}

func TestRoundTripV2(t *testing.T) {
	for _, tc := range []struct {
		f    Format
		w, h int
	}{{FmtMono1, 128, 64}, {FmtGray4, 128, 128}, {FmtRGB565, 240, 240}} {
		a := testAnim(tc.f, tc.w, tc.h)
		got, err := DecodeQGIF(a.EncodeQGIF())
		if err != nil {
			t.Fatalf("%s: %v", tc.f, err)
		}
		if got.W != a.W || got.H != a.H || got.Format != a.Format {
			t.Fatalf("%s: geometry mismatch: %+v", tc.f, got)
		}
		for i := range a.Bitmaps {
			if !bytes.Equal(got.Bitmaps[i], a.Bitmaps[i]) || got.Delays[i] != a.Delays[i] {
				t.Fatalf("%s: frame %d mismatch", tc.f, i)
			}
		}
	}
}

func TestDecodeLegacyV1(t *testing.T) {
	fb := FmtMono1.FrameBytes(128, 64)
	var buf []byte
	buf = append(buf, 2) // frames
	buf = binary.LittleEndian.AppendUint16(buf, 128)
	buf = binary.LittleEndian.AppendUint16(buf, 64)
	buf = binary.LittleEndian.AppendUint16(buf, 120)
	buf = binary.LittleEndian.AppendUint16(buf, 80)
	buf = append(buf, make([]byte, 2*fb)...)
	a, err := DecodeQGIF(buf)
	if err != nil {
		t.Fatal(err)
	}
	if a.Format != FmtMono1 || a.W != 128 || a.H != 64 || len(a.Bitmaps) != 2 ||
		a.Delays[0] != 120 || a.Delays[1] != 80 {
		t.Fatalf("bad legacy decode: %+v", a)
	}
}

func TestLayoutZoomCropsCenter(t *testing.T) {
	// 4×4 source: edges 10, center 2×2 block 200. At zoom 200% only the
	// center block survives the crop, so every output pixel must be 200.
	src := make([]uint8, 16)
	for i := range src {
		src[i] = 10
	}
	for _, i := range []int{5, 6, 9, 10} {
		src[i] = 200
	}
	out := layout(src, 4, 4, 1, 4, 4, "fit", 200, 0, 0)
	for i, v := range out {
		if v != 200 {
			t.Fatalf("pixel %d = %d, want 200 (center crop)", i, v)
		}
	}
	// zoom 100 (and 0 = unset) must reproduce the old behavior bit for bit
	if !bytes.Equal(layout(src, 4, 4, 1, 4, 4, "fit", 100, 0, 0), src) ||
		!bytes.Equal(layout(src, 4, 4, 1, 4, 4, "fit", 0, 0, 0), src) {
		t.Fatal("zoom 100/0 changed a same-size fit layout")
	}
}

func TestLayoutPanShifts(t *testing.T) {
	src := []uint8{1, 2, 3, 4} // 2×2
	// pan +1,0: image moves right one panel pixel; column 0 becomes black,
	// column 1 shows the source's left column.
	got := layout(src, 2, 2, 1, 2, 2, "fit", 100, 1, 0)
	want := []uint8{0, 1, 0, 3}
	if !bytes.Equal(got, want) {
		t.Fatalf("pan (1,0): got %v want %v", got, want)
	}
	// pan 0,-1: image moves up; bottom row becomes black.
	got = layout(src, 2, 2, 1, 2, 2, "fit", 100, 0, -1)
	want = []uint8{3, 4, 0, 0}
	if !bytes.Equal(got, want) {
		t.Fatalf("pan (0,-1): got %v want %v", got, want)
	}
}

func TestDecodeRejectsGarbage(t *testing.T) {
	for _, bad := range [][]byte{nil, {1, 2, 3}, append([]byte("QGF2"), 9, 0, 1, 0, 1, 0, 1)} {
		if _, err := DecodeQGIF(bad); err == nil {
			t.Fatalf("accepted garbage %v", bad)
		}
	}
}
