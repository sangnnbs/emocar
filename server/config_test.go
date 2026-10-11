package main

import (
	"os"
	"strings"
	"testing"
)

func TestRewriteConfigKeepsStyleAndComments(t *testing.T) {
	src := "#define SHAKE_DPS       400.0f\n" +
		"#define FAST_HOLD_MS     500    // rms above FAST_ENTER_RMS this long\n" +
		"#define STILL_RMS        2.0f   // RMS at rest\n" +
		"#ifndef STATE_FAST\n#define STATE_FAST   0\n#endif\n" +
		"#define RMS_WINDOW  (RMS_WINDOW_MS * 1000 / SAMPLE_US)\n"
	out, changed, missing := rewriteConfig(src, map[string]float64{
		"SHAKE_DPS": 350, "FAST_HOLD_MS": 12000, "STILL_RMS": 2, "STATE_FAST": 1,
	})
	want := "#define SHAKE_DPS       350.0f\n" +
		"#define FAST_HOLD_MS     12000  // rms above FAST_ENTER_RMS this long\n" +
		"#define STILL_RMS        2.0f   // RMS at rest\n" +
		"#ifndef STATE_FAST\n#define STATE_FAST   1\n#endif\n" +
		"#define RMS_WINDOW  (RMS_WINDOW_MS * 1000 / SAMPLE_US)\n"
	if out != want {
		t.Errorf("got\n%s\nwant\n%s", out, want)
	}
	if strings.Join(changed, ",") != "SHAKE_DPS,FAST_HOLD_MS,STATE_FAST" {
		t.Errorf("changed = %v", changed)
	}
	if len(missing) != 0 {
		t.Errorf("missing = %v", missing)
	}
}

func TestRewriteConfigReportsMissing(t *testing.T) {
	src := "#define RMS_WINDOW  (RMS_WINDOW_MS * 1000 / SAMPLE_US)\n"
	_, _, missing := rewriteConfig(src, map[string]float64{"RMS_WINDOW": 1, "NOPE": 2})
	if strings.Join(missing, ",") != "NOPE,RMS_WINDOW" {
		t.Errorf("missing = %v", missing)
	}
}

func TestFormatLike(t *testing.T) {
	for _, c := range []struct {
		old  string
		v    float64
		want string
	}{
		{"4.0f", 4.5, "4.5f"}, {"4.0f", 3, "3.0f"}, {"0.5f", 0.1, "0.1f"},
		{"500", 600, "600"}, {"1.5", 2, "2.0"},
	} {
		if got := formatLike(c.old, c.v); got != c.want {
			t.Errorf("formatLike(%q, %v) = %q, want %q", c.old, c.v, got, c.want)
		}
	}
}

// Every setting the car reports must be a plain #define in the real config.h, or Export
// would refuse. Values equal to the file's own change nothing.
func TestRealConfigHasEverySetting(t *testing.T) {
	src, err := os.ReadFile("../config.h")
	if err != nil {
		t.Skip("no ../config.h")
	}
	names := []string{"STATE_FAST", "STATE_DIZZY", "STATE_HAPPY", "ENTER_HOLD_MS", "EXIT_HOLD_MS",
		"FAST_ENTER_RMS", "FAST_EXIT_RMS", "FAST_HOLD_MS", "SHAKE_DPS", "SHAKE_REARM_DPS",
		"SHAKE_SWINGS", "SHAKE_WINDOW_MS", "SPIN_GZ_DPS", "SPIN_HOLD_MS", "SPIN_DIP_MS",
		"DIZZY_AFTER_MS", "HAND_TILT_DEG", "HAND_TILT_MS", "HAND_ROLL_MS", "HAND_QUIET_DPS",
		"HAND_QUIET_MS", "STILL_RMS", "HAND_SETTLE_MS"}
	vals := map[string]float64{}
	for _, n := range names {
		vals[n] = -12345 // anything: only "found or not" matters here
	}
	_, _, missing := rewriteConfig(string(src), vals)
	if len(missing) != 0 {
		t.Errorf("config.h lacks plain #defines for %v", missing)
	}
}
