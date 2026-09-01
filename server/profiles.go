// profiles.go — display profile registry, loaded from the embedded profiles.json.
// One profile per supported panel; the firmware's config.h DISP_* block must
// match the entry for the display it is compiled for.
package main

import (
	_ "embed"
	"encoding/json"
	"fmt"
	"log"
)

//go:embed profiles.json
var profilesJSON []byte

type ConvDefaults struct {
	Threshold int    `json:"threshold"`
	Invert    bool   `json:"invert"`
	Scale     string `json:"scale"`
	Dither    string `json:"dither"`
	Zoom      int    `json:"zoom"`
}

type Profile struct {
	ID       string       `json:"id"`
	Label    string       `json:"label"`
	W        int          `json:"width"`
	H        int          `json:"height"`
	FmtName  string       `json:"format"`
	Defaults ConvDefaults `json:"defaults"`
	Format   Format       `json:"-"`
}

var profiles []Profile

func loadProfiles() {
	var doc struct {
		Profiles []Profile `json:"profiles"`
	}
	if err := json.Unmarshal(profilesJSON, &doc); err != nil {
		log.Fatalf("profiles.json: %v", err)
	}
	if len(doc.Profiles) == 0 {
		log.Fatal("profiles.json: no profiles")
	}
	for i := range doc.Profiles {
		p := &doc.Profiles[i]
		f, err := ParseFormat(p.FmtName)
		if err != nil {
			log.Fatalf("profiles.json: %s: %v", p.ID, err)
		}
		p.Format = f
		if p.W <= 0 || p.H <= 0 {
			log.Fatalf("profiles.json: %s: bad dimensions", p.ID)
		}
	}
	profiles = doc.Profiles
}

// findProfile returns the profile by id; "" means the first (default) profile.
func findProfile(id string) (*Profile, error) {
	if id == "" {
		return &profiles[0], nil
	}
	for i := range profiles {
		if profiles[i].ID == id {
			return &profiles[i], nil
		}
	}
	return nil, fmt.Errorf("unknown display profile %q", id)
}
