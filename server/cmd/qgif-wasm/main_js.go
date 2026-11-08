//go:build js && wasm

// qgif.wasm — the GIF → .qgif converter for the web app. Exposes two functions:
//
//	qgifConvert(gif or qgif: Uint8Array, {w, h, format, threshold, invert, scale, dither, zoom, panX, panY})
//	  → {qgif: Uint8Array, frames} or {error}
//	qgifDecode(qgif: Uint8Array) → {w, h, format, delays: [ms], frames: [Uint8Array], qgif: v2 bytes} or {error}
package main

import (
	"syscall/js"

	"emocar-server/qgif"
)

func main() {
	js.Global().Set("qgifConvert", js.FuncOf(jsConvert))
	js.Global().Set("qgifDecode", js.FuncOf(jsDecode))
	select {}
}

func fromJS(v js.Value) []byte {
	b := make([]byte, v.Get("length").Int())
	js.CopyBytesToGo(b, v)
	return b
}

func toJS(b []byte) js.Value {
	u := js.Global().Get("Uint8Array").New(len(b))
	js.CopyBytesToJS(u, b)
	return u
}

func fail(msg string) any { return map[string]any{"error": msg} }

func num(o js.Value, k string) int {
	if v := o.Get(k); v.Type() == js.TypeNumber {
		return v.Int()
	}
	return 0
}

func str(o js.Value, k string) string {
	if v := o.Get(k); v.Type() == js.TypeString {
		return v.String()
	}
	return ""
}

// The last source qgifConvert decoded, keyed by the JS array it came from.
var cached struct {
	bytes  js.Value
	format string
	src    *qgif.Source
}

func jsConvert(_ js.Value, args []js.Value) any {
	if len(args) < 2 || args[1].Type() != js.TypeObject {
		return fail("qgifConvert(gifBytes, options)")
	}
	o := args[1]
	opts := Opts{
		W: num(o, "w"), H: num(o, "h"), Format: str(o, "format"),
		Threshold: num(o, "threshold"), Invert: o.Get("invert").Truthy(),
		Scale: str(o, "scale"), Dither: str(o, "dither"),
		Zoom: num(o, "zoom"), PanX: num(o, "panX"), PanY: num(o, "panY"),
	}
	// Slider moves call this again and again with the same Uint8Array: decode it once.
	if !args[0].Equal(cached.bytes) || opts.Format != cached.format {
		s, err := source(fromJS(args[0]), opts.Format)
		if err != nil {
			return fail(err.Error())
		}
		cached.bytes, cached.format, cached.src = args[0], opts.Format, s
	}
	a := cached.src.Convert(opts.W, opts.H, opts.conv())
	return map[string]any{"qgif": toJS(a.EncodeQGIF()), "frames": len(a.Bitmaps)}
}

func jsDecode(_ js.Value, args []js.Value) any {
	if len(args) < 1 {
		return fail("qgifDecode(qgifBytes)")
	}
	a, err := qgif.DecodeQGIF(fromJS(args[0]))
	if err != nil {
		return fail(err.Error())
	}
	delays := make([]any, len(a.Delays))
	frames := make([]any, len(a.Bitmaps))
	for i := range a.Bitmaps {
		delays[i] = int(a.Delays[i])
		frames[i] = toJS(a.Bitmaps[i])
	}
	// qgif = the same animation as a v2 container, so a legacy v1 file can go to the car.
	return map[string]any{"w": a.W, "h": a.H, "format": a.Format.String(), "delays": delays, "frames": frames,
		"qgif": toJS(a.EncodeQGIF())}
}
