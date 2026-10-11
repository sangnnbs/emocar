// config.go — /configs: the car's state conditions (firmware cfg.h), read and changed live
// over the serial link, and Export, which writes the car's values back into the sketch's
// config.h so the next flash starts from them.
//
//	GET  /api/car/config                     -> {"online", "config":{"cur","def"}, "export"}
//	POST /api/car/config {"set":{NAME:v}}    -> the same, after the car applied and saved them
//	POST /api/car/config {"reset":true}      -> the same, back to the sketch's defaults
//	POST /api/car/config/export              -> {"path", "changed":[NAME...]}
//
// Names are the config.h macro names end to end: page, server, car, file.
package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
)

var sketchDir string // -sketch: the folder holding the firmware's config.h

var cfgName = regexp.MustCompile(`^[A-Z][A-Z0-9_]*$`)

func configPath() string { return filepath.Join(sketchDir, "config.h") }

// exportInfo says where Export would write, or why it can't (a release build run outside
// the repo has no config.h next to it).
func exportInfo() map[string]any {
	p, _ := filepath.Abs(configPath())
	if _, err := os.Stat(p); err != nil {
		return map[string]any{"ok": false, "error": fmt.Sprintf("no config.h at %s (set -sketch)", p)}
	}
	return map[string]any{"ok": true, "path": p}
}

func handleConfig(w http.ResponseWriter, r *http.Request) {
	var args string
	switch r.Method {
	case http.MethodGet:
	case http.MethodPost:
		var req struct {
			Set   map[string]float64 `json:"set"`
			Reset bool               `json:"reset"`
		}
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			jsonErr(w, 400, "bad request body")
			return
		}
		if req.Reset {
			args = "RESET"
			break
		}
		if len(req.Set) == 0 {
			jsonErr(w, 400, "nothing to set")
			return
		}
		names := make([]string, 0, len(req.Set))
		for k := range req.Set {
			if !cfgName.MatchString(k) {
				jsonErr(w, 400, "bad setting name "+k)
				return
			}
			names = append(names, k)
		}
		sort.Strings(names)
		var b strings.Builder
		b.WriteString("SET")
		for _, k := range names {
			fmt.Fprintf(&b, " %s=%s", k, strconv.FormatFloat(req.Set[k], 'g', -1, 64))
		}
		args = b.String()
	default:
		jsonErr(w, 405, "GET or POST")
		return
	}

	cfg, err := car.Config(args)
	if err != nil {
		var refused carError
		switch {
		case errors.As(err, &refused):
			jsonErr(w, 400, string(refused))
		case r.Method == http.MethodGet:
			writeJSON(w, map[string]any{"online": false, "error": err.Error(), "export": exportInfo()})
		default:
			jsonErr(w, 502, err.Error())
		}
		return
	}
	writeJSON(w, map[string]any{"online": true, "port": car.Port(), "config": cfg, "export": exportInfo()})
}

// POST /api/car/config/export — the car's current values into config.h.
func handleConfigExport(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonErr(w, 405, "POST only")
		return
	}
	raw, err := car.Config("")
	if err != nil {
		jsonErr(w, 502, err.Error())
		return
	}
	var cfg struct {
		Cur map[string]float64 `json:"cur"`
	}
	if err := json.Unmarshal(raw, &cfg); err != nil || len(cfg.Cur) == 0 {
		jsonErr(w, 502, "car sent no settings")
		return
	}
	path, _ := filepath.Abs(configPath())
	st, err := os.Stat(path)
	if err != nil {
		jsonErr(w, 404, fmt.Sprintf("no config.h at %s (set -sketch)", path))
		return
	}
	src, err := os.ReadFile(path)
	if err != nil {
		jsonErr(w, 500, err.Error())
		return
	}
	out, changed, missing := rewriteConfig(string(src), cfg.Cur)
	if len(missing) > 0 {
		jsonErr(w, 500, "config.h has no plain #define for "+strings.Join(missing, ", ")+" — nothing written")
		return
	}
	if len(changed) > 0 {
		if err := os.WriteFile(path, []byte(out), st.Mode().Perm()); err != nil {
			jsonErr(w, 500, err.Error())
			return
		}
	}
	writeJSON(w, map[string]any{"path": path, "changed": changed})
}

var defineLine = regexp.MustCompile(`^(#define[ \t]+)([A-Z][A-Z0-9_]*)([ \t]+)(\S+)(.*)$`)

// rewriteConfig sets each named `#define NAME value` line to vals[NAME]. A value keeps
// its literal's style (4.0f stays a float, 500 an integer) and the comment after it keeps
// its column. Returns the new text, the names whose value changed, and the names with no
// such line (or a value that isn't a plain number).
func rewriteConfig(src string, vals map[string]float64) (string, []string, []string) {
	lines := strings.Split(src, "\n")
	seen := map[string]bool{}
	var changed []string
	for i, line := range lines {
		m := defineLine.FindStringSubmatch(strings.TrimRight(line, "\r"))
		if m == nil {
			continue
		}
		name, old, rest := m[2], m[4], m[5]
		v, ok := vals[name]
		if !ok {
			continue
		}
		oldV, err := strconv.ParseFloat(strings.TrimRight(old, "fF"), 64)
		if err != nil {
			continue // an expression, not a value: reported as missing
		}
		seen[name] = true
		if float32(oldV) == float32(v) {
			continue
		}
		tok := formatLike(old, v)
		if lead := len(rest) - len(strings.TrimLeft(rest, " ")); lead > 0 && lead < len(rest) {
			n := max(1, lead-(len(tok)-len(old)))
			rest = strings.Repeat(" ", n) + strings.TrimLeft(rest, " ")
		}
		cr := ""
		if strings.HasSuffix(line, "\r") {
			cr = "\r"
		}
		lines[i] = m[1] + name + m[3] + tok + rest + cr
		changed = append(changed, name)
	}
	var missing []string
	for name := range vals {
		if !seen[name] {
			missing = append(missing, name)
		}
	}
	sort.Strings(missing)
	return strings.Join(lines, "\n"), changed, missing
}

// formatLike writes v the way the literal `old` was written.
func formatLike(old string, v float64) string {
	if !strings.ContainsAny(old, ".fFeE") {
		return strconv.FormatInt(int64(math.Round(v)), 10)
	}
	s := strconv.FormatFloat(v, 'f', -1, 32)
	if !strings.Contains(s, ".") {
		s += ".0"
	}
	if strings.HasSuffix(old, "f") || strings.HasSuffix(old, "F") {
		s += "f"
	}
	return s
}
