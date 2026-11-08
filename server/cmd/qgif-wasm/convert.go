// The conversion the browser runs: shared by the WASM build (main_js.go) and by
// compare_test.go, which checks the two give the same bytes.
package main

import (
	"bytes"

	"emocar-server/qgif"
)

// Opts mirrors the JS options object; zero values mean the qgif defaults.
type Opts struct {
	W, H      int
	Format    string // mono1 | gray4 | rgb565, as the car's /status reports it
	Threshold int
	Invert    bool
	Scale     string
	Dither    string
	Zoom      int
	PanX      int
	PanY      int
}

// source decodes a GIF, or a .qgif (the face a car plays now) to lay out again,
// for a panel of the given format.
func source(data []byte, format string) (*qgif.Source, error) {
	f, err := qgif.ParseFormat(format)
	if err != nil {
		return nil, err
	}
	if bytes.HasPrefix(data, []byte("GIF8")) {
		return qgif.SourceGIF(data, f)
	}
	a, err := qgif.DecodeQGIF(data)
	if err != nil {
		return nil, err
	}
	return qgif.SourceAnim(a, f)
}

func (o Opts) conv() qgif.ConvOpts {
	return qgif.ConvOpts{
		Threshold: o.Threshold, Invert: o.Invert, Scale: o.Scale, Dither: o.Dither,
		Zoom: o.Zoom, PanX: o.PanX, PanY: o.PanY,
	}
}

func convert(data []byte, o Opts) (*qgif.Anim, error) {
	s, err := source(data, o.Format)
	if err != nil {
		return nil, err
	}
	return s.Convert(o.W, o.H, o.conv()), nil
}
