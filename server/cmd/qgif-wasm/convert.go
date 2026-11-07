// The conversion the browser runs: shared by the WASM build (main_js.go) and by
// compare_test.go, which checks the two give the same bytes.
package main

import "emocar-server/qgif"

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

func convert(gifData []byte, o Opts) (*qgif.Anim, error) {
	f, err := qgif.ParseFormat(o.Format)
	if err != nil {
		return nil, err
	}
	return qgif.ConvertGIF(gifData, o.W, o.H, f, qgif.ConvOpts{
		Threshold: o.Threshold, Invert: o.Invert, Scale: o.Scale, Dither: o.Dither,
		Zoom: o.Zoom, PanX: o.PanX, PanY: o.PanY,
	})
}
