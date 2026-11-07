package main

import (
	"bytes"
	"encoding/json"
	"image"
	"image/color"
	"image/color/palette"
	"image/gif"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"testing"
)

// Node loads qgif.wasm with Go's own wasm_exec.js, converts every job, and writes the
// results; this file holds the native side of the comparison.
const nodeRunner = `
const fs = require("fs");
require(process.env.WASM_EXEC);
const go = new Go();
WebAssembly.instantiate(fs.readFileSync(process.env.WASM), go.importObject).then(r => {
  go.run(r.instance);
  for (const j of JSON.parse(fs.readFileSync(process.env.JOBS))) {
    const res = qgifConvert(fs.readFileSync(j.gif), j.opts);
    if (res.error) { fs.writeFileSync(j.out + ".err", res.error); continue; }
    fs.writeFileSync(j.out, res.qgif);
    const back = qgifDecode(res.qgif);
    if (back.error || back.frames.length !== res.frames) fs.writeFileSync(j.out + ".err", "decode: " + (back.error || "frame count"));
  }
  process.exit(0);
});
`

type job struct {
	GIF  string         `json:"gif"`
	Opts map[string]any `json:"opts"`
	Out  string         `json:"out"`
	opts Opts
}

// synthGIF draws a moving gradient over a few frames, with every disposal mode.
func synthGIF(t *testing.T, path string) {
	g := &gif.GIF{}
	for i := 0; i < 4; i++ {
		img := image.NewPaletted(image.Rect(0, 0, 200, 120), palette.Plan9)
		for y := 0; y < 120; y++ {
			for x := 0; x < 200; x++ {
				img.Set(x, y, color.RGBA{uint8(x + 40*i), uint8(y * 2), uint8(x ^ y), 255})
			}
		}
		g.Image = append(g.Image, img)
		g.Delay = append(g.Delay, 3+i)
		g.Disposal = append(g.Disposal, []byte{gif.DisposalNone, gif.DisposalBackground, gif.DisposalPrevious, 0}[i])
	}
	var b bytes.Buffer
	if err := gif.EncodeAll(&b, g); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, b.Bytes(), 0o644); err != nil {
		t.Fatal(err)
	}
}

func TestWasmMatchesNative(t *testing.T) {
	node, err := exec.LookPath("node")
	if err != nil {
		t.Skip("node not installed")
	}
	dir := t.TempDir()
	wasm := filepath.Join(dir, "qgif.wasm")
	build := exec.Command("go", "build", "-o", wasm, ".")
	build.Env = append(os.Environ(), "GOOS=js", "GOARCH=wasm")
	if out, err := build.CombinedOutput(); err != nil {
		t.Fatalf("wasm build: %v\n%s", err, out)
	}

	gifs := []string{filepath.Join(dir, "synth.gif")}
	synthGIF(t, gifs[0])
	lib, _ := filepath.Glob("../../../emoji_lib/*.gif") // personal library, not in git
	gifs = append(gifs, lib...)

	optSets := []Opts{
		{W: 128, H: 64, Format: "mono1"},
		{W: 128, H: 64, Format: "mono1", Threshold: 90, Invert: true, Dither: "floyd", Scale: "stretch"},
		{W: 128, H: 64, Format: "mono1", Dither: "bayer", Scale: "fit_width", Zoom: 150, PanX: -12, PanY: 7},
		{W: 128, H: 128, Format: "gray4", Scale: "fit_height"},
		{W: 160, H: 80, Format: "rgb565", Invert: true, Zoom: 250},
	}
	var jobs []job
	for _, g := range gifs {
		for oi, o := range optSets {
			jobs = append(jobs, job{GIF: g, Out: filepath.Join(dir, "out",
				filepath.Base(g)+"."+string(rune('a'+oi))+".qgif"), opts: o, Opts: map[string]any{
				"w": o.W, "h": o.H, "format": o.Format, "threshold": o.Threshold, "invert": o.Invert,
				"scale": o.Scale, "dither": o.Dither, "zoom": o.Zoom, "panX": o.PanX, "panY": o.PanY}})
		}
	}
	os.MkdirAll(filepath.Join(dir, "out"), 0o755)
	jb, _ := json.Marshal(jobs)
	jobsPath := filepath.Join(dir, "jobs.json")
	os.WriteFile(jobsPath, jb, 0o644)
	runner := filepath.Join(dir, "run.cjs")
	os.WriteFile(runner, []byte(nodeRunner), 0o644)

	cmd := exec.Command(node, runner)
	cmd.Env = append(os.Environ(), "WASM="+wasm, "JOBS="+jobsPath,
		"WASM_EXEC="+filepath.Join(runtime.GOROOT(), "lib", "wasm", "wasm_exec.js"))
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("node: %v\n%s", err, out)
	}

	same, rejected := 0, 0
	for _, j := range jobs {
		data, _ := os.ReadFile(j.GIF)
		a, nerr := convert(data, j.opts)
		werrb, werr := os.ReadFile(j.Out + ".err")
		name := filepath.Base(j.Out)
		switch {
		case nerr != nil && werr == nil:
			t.Errorf("%s: native failed (%v) but wasm gave %q", name, nerr, werrb)
		case nerr != nil:
			if string(werrb) != nerr.Error() {
				t.Errorf("%s: errors differ: native %q, wasm %q", name, nerr, werrb)
			}
			rejected++
		case werr == nil:
			t.Errorf("%s: wasm failed: %s", name, werrb)
		default:
			got, _ := os.ReadFile(j.Out)
			if !bytes.Equal(got, a.EncodeQGIF()) {
				t.Errorf("%s: wasm output differs from native", name)
			}
			same++
		}
	}
	t.Logf("%d GIFs × %d option sets: %d identical, %d rejected by both", len(gifs), len(optSets), same, rejected)
}
