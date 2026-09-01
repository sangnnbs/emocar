# emocar server — day-to-day targets. `make` alone lists them.
#
# Typical session (join the car's "emocar" WiFi first):
#   make run          # build + start the UI on http://localhost:8080
#   make status       # is the car there, which slots are loaded
#   make assign STATE=2 GIF=hoamat.gif

LISTEN    ?= :8080
UI        ?= http://localhost:8080
CAR       ?= http://192.168.4.1

JSON      := python3 -m json.tool

.PHONY: help build run down open vet clean status assign dist

help:
	@echo "build      compile ./emocar-server"
	@echo "run        build + serve the UI on $(UI)"
	@echo "down       stop a running emocar-server"
	@echo "open       open the UI in the browser"
	@echo "status     ask the car what it is playing (needs the car's WiFi)"
	@echo "assign     push one GIF: make assign STATE=2 GIF=hoamat.gif [PROFILE=sh1106_128x64] [THRESHOLD=128] [INVERT=1] [SCALE=fit] [DITHER=none]"
	@echo "           (needs the server running and the car's WiFi)"
	@echo "vet        go vet"
	@echo "clean      remove the binary and dist/"
	@echo "dist       cross-compile release archives into dist/ [VERSION=v0.1.0]"

build:
	go build -o emocar-server .

run: build
	./emocar-server -listen $(LISTEN)

down:
	@pkill -x emocar-server && echo "emocar-server stopped" || echo "emocar-server not running"

open:
	xdg-open $(UI)

vet:
	go vet ./...

clean:
	rm -f emocar-server
	rm -rf dist

# ---------------- release packaging ----------------

VERSION   ?= dev
# os/arch pairs; windows binaries get .exe, unix archives are .tar.gz, windows .zip
PLATFORMS := linux/amd64 linux/arm64 darwin/amd64 darwin/arm64 windows/amd64

dist:
	rm -rf dist && mkdir -p dist
	@set -e; for p in $(PLATFORMS); do \
	  os=$${p%/*}; arch=$${p#*/}; ext=; [ "$$os" = windows ] && ext=.exe; \
	  name=emocar-server_$(VERSION)_$${os}_$${arch}; \
	  echo "  $$os/$$arch"; \
	  mkdir -p dist/$$name; \
	  CGO_ENABLED=0 GOOS=$$os GOARCH=$$arch go build -trimpath -ldflags '-s -w' \
	    -o dist/$$name/emocar-server$$ext .; \
	  cp README.md dist/$$name/; \
	  if [ "$$os" = windows ]; then (cd dist && zip -qr $$name.zip $$name); \
	  else tar -czf dist/$$name.tar.gz -C dist $$name; fi; \
	  rm -rf dist/$$name; \
	done
	cd dist && sha256sum * > SHA256SUMS
	@ls -lh dist

# ---------------- talking to the car ----------------

status:
	@curl -sS -m 5 $(CAR)/status | $(JSON)

# STATE=0..2 (NORMAL FAST SPINNING), GIF=<library name>; knobs optional.
# PROFILE defaults to the first entry in profiles.json when empty.
PROFILE   ?=
THRESHOLD ?= 128
SCALE     ?= fit
DITHER    ?= none
assign:
	@test -n "$(STATE)" -a -n "$(GIF)" || { echo "usage: make assign STATE=0..2 GIF=name.gif"; exit 1; }
	@curl -sS -m 30 -X POST $(UI)/api/assign -H 'Content-Type: application/json' \
	  -d '{"state":$(STATE),"name":"$(GIF)","profile":"$(PROFILE)","threshold":$(THRESHOLD),"invert":$(if $(INVERT),true,false),"scale":"$(SCALE)","dither":"$(DITHER)"}' | $(JSON)
