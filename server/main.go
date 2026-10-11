// emocar-server — web UI on the PC that converts emoji GIFs to .qgif and
// pushes them to the car over its USB cable (see serial.go). See README.md.
//
//	./emocar-server                  # opens http://localhost:8080, car auto-detected, GIFs in ./gifs
//	./emocar-server -port /dev/ttyACM0 -listen :8080 -lib ../emoji_lib,../downloads/00_Main
package main

import (
	_ "embed"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strconv"
	"strings"
)

//go:embed web/index.html
var indexHTML []byte

//go:embed web/dashboard.html
var dashboardHTML []byte

//go:embed web/configs.html
var configsHTML []byte

var (
	car        *carLink
	libDirs    []string
	uploadsDir string
)

// ---------------------------------------------------------------- assignments
// Which library gif each state was last given, persisted so the UI can show
// "1 gif for 1 state" at a glance (the car itself only stores anonymous frames).

type assignRec struct {
	Name   string `json:"name"`
	Frames int    `json:"frames"`
	Bytes  int    `json:"bytes"`
}

// numStates must match enum CarState in the firmware's motion.h: NORMAL, FAST,
// DIZZY, HAPPY. Slots are addressed by index, so states are append-only.
const numStates = 4

var assignments [numStates]*assignRec

func assignmentsPath() string { return filepath.Join(uploadsDir, "assignments.json") }

func loadAssignments() {
	data, err := os.ReadFile(assignmentsPath())
	if err != nil {
		return
	}
	json.Unmarshal(data, &assignments)
}

func saveAssignments() {
	os.MkdirAll(uploadsDir, 0o755)
	data, _ := json.Marshal(assignments)
	if err := os.WriteFile(assignmentsPath(), data, 0o644); err != nil {
		log.Printf("assignments: %v", err)
	}
}

// ---------------------------------------------------------------- library

type libEntry struct {
	Name  string `json:"name"` // basename, unique key
	Kind  string `json:"kind"` // "gif" (convertible) or "qgif" (ready-made)
	Bytes int64  `json:"bytes"`
	path  string
}

// Rescanned on every request so files dropped into the folders just appear.
func scanLibrary() []libEntry {
	var out []libEntry
	seen := map[string]bool{}
	for _, dir := range append(libDirs, uploadsDir) {
		files, err := os.ReadDir(dir)
		if err != nil {
			continue
		}
		for _, f := range files {
			name := f.Name()
			ext := strings.ToLower(filepath.Ext(name))
			if f.IsDir() || (ext != ".gif" && ext != ".qgif") || strings.HasPrefix(name, "preview_") {
				continue
			}
			if seen[name] {
				continue // same emoji in two dirs (first dir wins)
			}
			seen[name] = true
			info, err := f.Info()
			if err != nil {
				continue
			}
			out = append(out, libEntry{Name: name, Kind: ext[1:], Bytes: info.Size(), path: filepath.Join(dir, name)})
		}
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Name < out[j].Name })
	return out
}

func findEntry(name string) (libEntry, bool) {
	for _, e := range scanLibrary() {
		if e.Name == name {
			return e, true
		}
	}
	return libEntry{}, false
}

// Convert a library entry with the given knobs (a .qgif is used as-is).
func entryToAnim(e libEntry, p *Profile, o ConvOpts) (*Anim, error) {
	data, err := os.ReadFile(e.path)
	if err != nil {
		return nil, err
	}
	if e.Kind == "qgif" {
		return DecodeQGIF(data)
	}
	return ConvertGIF(data, p, o)
}

func optsFromQuery(q map[string][]string) (*Profile, ConvOpts, error) {
	get := func(k string) string {
		if v, ok := q[k]; ok && len(v) > 0 {
			return v[0]
		}
		return ""
	}
	p, err := findProfile(get("profile"))
	if err != nil {
		return nil, ConvOpts{}, err
	}
	t, _ := strconv.Atoi(get("threshold"))
	z, _ := strconv.Atoi(get("zoom"))
	px, _ := strconv.Atoi(get("panx"))
	py, _ := strconv.Atoi(get("pany"))
	return p, ConvOpts{Threshold: t, Invert: get("invert") == "1",
		Scale: get("scale"), Dither: get("dither"), Zoom: z, PanX: px, PanY: py}, nil
}

// ---------------------------------------------------------------- handlers

func jsonErr(w http.ResponseWriter, code int, msg string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	json.NewEncoder(w).Encode(map[string]string{"error": msg})
}

func writeJSON(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(v)
}

func handleLibrary(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, scanLibrary())
}

// GET /api/profiles — the display profile registry, first entry = default.
func handleProfiles(w http.ResponseWriter, r *http.Request) {
	type out struct {
		ID       string       `json:"id"`
		Label    string       `json:"label"`
		W        int          `json:"width"`
		H        int          `json:"height"`
		Format   string       `json:"format"`
		Defaults ConvDefaults `json:"defaults"`
	}
	var list []out
	for _, p := range profiles {
		list = append(list, out{p.ID, p.Label, p.W, p.H, p.Format.String(), p.Defaults})
	}
	writeJSON(w, list)
}

func handlePreview(w http.ResponseWriter, r *http.Request) {
	e, ok := findEntry(r.URL.Query().Get("name"))
	if !ok {
		jsonErr(w, 404, "not in library")
		return
	}
	p, opts, err := optsFromQuery(r.URL.Query())
	if err != nil {
		jsonErr(w, 400, err.Error())
		return
	}
	a, err := entryToAnim(e, p, opts)
	if err != nil {
		jsonErr(w, 400, err.Error())
		return
	}
	g, err := a.PreviewGIF()
	if err != nil {
		jsonErr(w, 500, err.Error())
		return
	}
	w.Header().Set("Content-Type", "image/gif")
	w.Write(g)
}

func handleCarStatus(w http.ResponseWriter, r *http.Request) {
	status, err := car.Status()
	if err != nil {
		writeJSON(w, map[string]any{"online": false, "error": err.Error()})
		return
	}
	writeJSON(w, map[string]any{"online": true, "port": car.Port(), "status": status})
}

// GET /api/telemetry — server-sent events for /dashboard: `t` carries one telemetry
// line from the car (fields in the firmware's telem.h), `link` the connection state.
func handleTelemetry(w http.ResponseWriter, r *http.Request) {
	fl, ok := w.(http.Flusher)
	if !ok {
		jsonErr(w, 500, "streaming unsupported")
		return
	}
	ch, cancel := car.Subscribe()
	defer cancel()
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-store")
	for {
		select {
		case <-r.Context().Done():
			return
		case ev := <-ch:
			fmt.Fprintf(w, "%s\n\n", ev)
			fl.Flush()
		}
	}
}

// POST /api/car/zero — the car's current pose becomes the dashboard's reference.
func handleZero(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonErr(w, 405, "POST only")
		return
	}
	if err := car.Zero(); err != nil {
		jsonErr(w, 502, err.Error())
		return
	}
	writeJSON(w, map[string]any{"ok": true})
}

// GET /api/car/preview/{slot} — what the car currently plays, rendered for the browser.
func handleCarPreview(w http.ResponseWriter, r *http.Request) {
	n, err := strconv.Atoi(strings.TrimPrefix(r.URL.Path, "/api/car/preview/"))
	if err != nil || n < 0 || n >= numStates {
		jsonErr(w, 404, "bad slot")
		return
	}
	data, err := car.Get(n)
	if err != nil {
		jsonErr(w, 502, err.Error())
		return
	}
	a, err := DecodeQGIF(data)
	if err != nil {
		jsonErr(w, 502, err.Error())
		return
	}
	g, err := a.PreviewGIF()
	if err != nil {
		jsonErr(w, 500, err.Error())
		return
	}
	w.Header().Set("Content-Type", "image/gif")
	w.Header().Set("Cache-Control", "no-store")
	w.Write(g)
}

// POST /api/assign {"state":0..2,"name":"x.gif","profile":"sh1106_128x64",
//                   "threshold":128,"invert":false,"scale":"fit","dither":"none",
//                   "zoom":100,"panx":0,"pany":0}
func handleAssign(w http.ResponseWriter, r *http.Request) {
	var req struct {
		State     int    `json:"state"`
		Name      string `json:"name"`
		Profile   string `json:"profile"`
		Threshold int    `json:"threshold"`
		Invert    bool   `json:"invert"`
		Scale     string `json:"scale"`
		Dither    string `json:"dither"`
		Zoom      int    `json:"zoom"`
		PanX      int    `json:"panx"`
		PanY      int    `json:"pany"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		jsonErr(w, 400, "bad request body")
		return
	}
	if req.State < 0 || req.State >= numStates {
		jsonErr(w, 400, fmt.Sprintf("state must be 0..%d", numStates-1))
		return
	}
	p, err := findProfile(req.Profile)
	if err != nil {
		jsonErr(w, 400, err.Error())
		return
	}
	e, ok := findEntry(req.Name)
	if !ok {
		jsonErr(w, 404, "not in library")
		return
	}
	a, err := entryToAnim(e, p, ConvOpts{Threshold: req.Threshold, Invert: req.Invert,
		Scale: req.Scale, Dither: req.Dither, Zoom: req.Zoom, PanX: req.PanX, PanY: req.PanY})
	if err != nil {
		jsonErr(w, 400, err.Error())
		return
	}
	// A ready-made .qgif may carry any geometry — refuse a mismatch before the car does.
	if a.W != p.W || a.H != p.H || a.Format != p.Format {
		jsonErr(w, 400, fmt.Sprintf("%s is %dx%d %s but profile %s wants %dx%d %s",
			req.Name, a.W, a.H, a.Format, p.ID, p.W, p.H, p.Format))
		return
	}
	blob := a.EncodeQGIF()

	if err := car.Put(req.State, blob); err != nil {
		jsonErr(w, 502, err.Error())
		return
	}
	assignments[req.State] = &assignRec{Name: req.Name, Frames: len(a.Bitmaps), Bytes: len(blob)}
	saveAssignments()
	log.Printf("assigned %s -> slot %d (%d frames, %d bytes)", req.Name, req.State, len(a.Bitmaps), len(blob))
	writeJSON(w, map[string]any{"ok": true, "frames": len(a.Bitmaps), "bytes": len(blob)})
}

// GET /api/assignments — one {name,frames,bytes} or null per state, index = state.
func handleAssignments(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, assignments)
}

// POST /api/upload — multipart "file"; lands in uploadsDir and shows up in the library.
func handleUpload(w http.ResponseWriter, r *http.Request) {
	f, hdr, err := r.FormFile("file")
	if err != nil {
		jsonErr(w, 400, "no file field")
		return
	}
	defer f.Close()
	name := filepath.Base(hdr.Filename)
	ext := strings.ToLower(filepath.Ext(name))
	if ext != ".gif" && ext != ".qgif" {
		jsonErr(w, 400, "only .gif or .qgif")
		return
	}
	if err := os.MkdirAll(uploadsDir, 0o755); err != nil {
		jsonErr(w, 500, err.Error())
		return
	}
	out, err := os.Create(filepath.Join(uploadsDir, name))
	if err != nil {
		jsonErr(w, 500, err.Error())
		return
	}
	defer out.Close()
	if _, err := io.Copy(out, io.LimitReader(f, 16<<20)); err != nil {
		jsonErr(w, 500, err.Error())
		return
	}
	log.Printf("uploaded %s", name)
	writeJSON(w, map[string]any{"ok": true, "name": name})
}

// ---------------------------------------------------------------- main

// gifsDir is the default library + uploads folder: "gifs" next to the binary, so
// C:\emocar\emocar-server.exe reads and writes C:\emocar\gifs no matter how it was
// started. Relative to the working directory would move with it — a double-click and a
// terminal in another folder would each see a different (usually empty) library.
func gifsDir() string {
	exe, err := os.Executable()
	if err != nil {
		return "gifs"
	}
	if p, err := filepath.EvalSymlinks(exe); err == nil {
		exe = p
	}
	return filepath.Join(filepath.Dir(exe), "gifs")
}

func main() {
	gifs := gifsDir()
	listen := flag.String("listen", ":8080", "web UI address")
	port := flag.String("port", "", "car serial port (default: auto-detect an Espressif USB device)")
	lib := flag.String("lib", gifs, "comma-separated GIF library dirs")
	uploads := flag.String("uploads", gifs, "where browser uploads are stored")
	flag.StringVar(&sketchDir, "sketch", "..", "firmware folder whose config.h /configs Export rewrites")
	open := flag.Bool("open", true, "open the web UI in the default browser once the server is up")
	flag.Parse()
	car = newCarLink(*port)
	libDirs = strings.Split(*lib, ",")
	uploadsDir = *uploads
	// Create it up front, so a first-time user finds the folder to drop GIFs into
	// before ever pressing "+ add GIF".
	if err := os.MkdirAll(uploadsDir, 0o755); err != nil {
		log.Printf("cannot create %s: %v", uploadsDir, err)
	}
	loadProfiles()
	loadAssignments()

	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/" {
			http.NotFound(w, r)
			return
		}
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Write(indexHTML)
	})
	http.HandleFunc("/dashboard", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Write(dashboardHTML)
	})
	http.HandleFunc("/configs", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Write(configsHTML)
	})
	http.HandleFunc("/api/car/config", handleConfig)
	http.HandleFunc("/api/car/config/export", handleConfigExport)
	http.HandleFunc("/api/telemetry", handleTelemetry)
	http.HandleFunc("/api/car/zero", handleZero)
	http.HandleFunc("/api/library", handleLibrary)
	http.HandleFunc("/api/profiles", handleProfiles)
	http.HandleFunc("/api/preview", handlePreview)
	http.HandleFunc("/api/car/status", handleCarStatus)
	http.HandleFunc("/api/car/preview/", handleCarPreview)
	http.HandleFunc("/api/assign", handleAssign)
	http.HandleFunc("/api/assignments", handleAssignments)
	http.HandleFunc("/api/upload", handleUpload)

	where := *port
	if where == "" {
		where = "auto-detect"
	}
	log.Printf("emocar server on %s, car port %s, library: %s, uploads: %s", *listen, where, *lib, uploadsDir)
	// Listen before opening the browser, so the page never races the server.
	ln, err := net.Listen("tcp", *listen)
	if err != nil {
		log.Fatal(err)
	}
	if *open {
		openBrowser(uiURL(ln.Addr()))
	}
	log.Fatal(http.Serve(ln, nil))
}

// uiURL turns the listen address into one a browser can open: ":8080" and
// "0.0.0.0:8080" both become http://localhost:8080.
func uiURL(addr net.Addr) string {
	port := "8080"
	if a, ok := addr.(*net.TCPAddr); ok {
		port = strconv.Itoa(a.Port)
		if !a.IP.IsUnspecified() {
			return "http://" + net.JoinHostPort(a.IP.String(), port)
		}
	}
	return "http://localhost:" + port
}

// openBrowser hands the URL to the OS's default browser. Failing is fine: the URL is
// in the log line above, and a headless box has no browser to open.
func openBrowser(url string) {
	var cmd *exec.Cmd
	switch runtime.GOOS {
	case "windows":
		cmd = exec.Command("rundll32", "url.dll,FileProtocolHandler", url)
	case "darwin":
		cmd = exec.Command("open", url)
	default:
		cmd = exec.Command("xdg-open", url)
	}
	if err := cmd.Start(); err != nil {
		log.Printf("open %s in a browser (%v)", url, err)
		return
	}
	go cmd.Wait() // reap it; xdg-open exits as soon as the browser has the URL
}
