// serial.go — the link to the car: one USB serial port, held open, reconnecting
// by itself when the cable is pulled or the car is reflashed.
//
// The port carries the car's telemetry printing (a line every 250 ms) as well as
// this protocol, so reading a reply means skipping lines until one starts with a
// protocol word.
//
//	STATUS         -> OK {json}
//	GET <n>        -> OK <len> + <len> raw bytes
//	PUT <n> <len>  -> READY <chunk>, then one chunk at a time, each answered
//	                  ACK <got> when it is on flash, and OK <bytes> at the end
package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"strconv"
	"strings"
	"sync"
	"time"

	"go.bug.st/serial"
	"go.bug.st/serial/enumerator"
)

// espressifVID — the C3's native USB, used to pick the port when none is given.
const espressifVID = "303A"

const (
	readPoll     = 100 * time.Millisecond // how often Read gives up and lets us check the clock
	replyTimeout = 5 * time.Second        // a command the car answers immediately
	writeTimeout = 30 * time.Second       // ...and one that erases flash first
)

var errTimeout = errors.New("car did not answer (is it running emocar firmware?)")

// ---------------------------------------------------------------- the link

type carLink struct {
	mu   sync.Mutex
	want string // -port flag; "" = auto-detect
	path string // what is actually open
	port serial.Port
	buf  []byte // bytes read from the port but not yet consumed
}

func newCarLink(want string) *carLink { return &carLink{want: want} }

func (l *carLink) Port() string {
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.path
}

func findPort() (string, error) {
	ports, err := enumerator.GetDetailedPortsList()
	if err != nil {
		return "", err
	}
	for _, p := range ports {
		if p.IsUSB && strings.EqualFold(p.VID, espressifVID) {
			return p.Name, nil
		}
	}
	return "", errors.New("no Espressif USB device found — plug the car in, or pass -port")
}

// open is a no-op when the port is already up; the device node disappears on every
// reflash and comes back, so opening is per-command rather than once at startup.
func (l *carLink) open() error {
	if l.port != nil {
		return nil
	}
	path := l.want
	if path == "" {
		p, err := findPort()
		if err != nil {
			return err
		}
		path = p
	}
	port, err := serial.Open(path, &serial.Mode{BaudRate: 115200})
	if err != nil {
		return fmt.Errorf("%s: %w", path, err)
	}
	// Both lines stay low: the C3 reads DTR/RTS as esptool's reset sequence, so
	// asserting them reboots the car the moment the server connects.
	port.SetDTR(false)
	port.SetRTS(false)
	port.SetReadTimeout(readPoll)
	l.port, l.path, l.buf = port, path, nil
	log.Printf("car: opened %s", path)
	return nil
}

// close drops the port so the next command reconnects. Every protocol failure ends
// here: a half-finished transfer leaves the stream misaligned.
func (l *carLink) close(why error) {
	if l.port != nil {
		log.Printf("car: closing %s (%v)", l.path, why)
		l.port.Close()
	}
	l.port, l.path, l.buf = nil, "", nil
}

// ---------------------------------------------------------------- reading

func (l *carLink) fill(deadline time.Time) error {
	tmp := make([]byte, 4096)
	for {
		n, err := l.port.Read(tmp)
		if err != nil {
			return err
		}
		if n > 0 {
			l.buf = append(l.buf, tmp[:n]...)
			return nil
		}
		if time.Now().After(deadline) {
			return errTimeout
		}
	}
}

func (l *carLink) line(deadline time.Time) (string, error) {
	for {
		if i := bytes.IndexByte(l.buf, '\n'); i >= 0 {
			s := strings.TrimRight(string(l.buf[:i]), "\r")
			l.buf = l.buf[i+1:]
			return s, nil
		}
		if err := l.fill(deadline); err != nil {
			return "", err
		}
	}
}

func (l *carLink) exact(n int, deadline time.Time) ([]byte, error) {
	for len(l.buf) < n {
		if err := l.fill(deadline); err != nil {
			return nil, err
		}
	}
	out := l.buf[:n:n]
	l.buf = l.buf[n:]
	return out, nil
}

// reply reads past the telemetry printing to the first protocol line.
func (l *carLink) reply(deadline time.Time) (string, string, error) {
	for {
		s, err := l.line(deadline)
		if err != nil {
			return "", "", err
		}
		verb, rest, _ := strings.Cut(s, " ")
		switch verb {
		case "OK", "READY", "ACK":
			return verb, rest, nil
		case "ERR":
			return "", "", fmt.Errorf("car: %s", rest)
		}
		// anything else is telemetry
	}
}

// drain throws away whatever the car printed since the last command, so a reply is
// never matched against a stale line.
func (l *carLink) drain() {
	l.buf = nil
	tmp := make([]byte, 4096)
	for {
		n, err := l.port.Read(tmp)
		if n == 0 || err != nil {
			return
		}
	}
}

func (l *carLink) send(cmd string) error {
	l.drain()
	_, err := l.port.Write([]byte(cmd + "\n"))
	return err
}

// ---------------------------------------------------------------- commands

// do runs one command with the port locked, and drops the port on any failure.
func (l *carLink) do(f func() error) error {
	l.mu.Lock()
	defer l.mu.Unlock()
	if err := l.open(); err != nil {
		return err
	}
	if err := f(); err != nil {
		l.close(err)
		return err
	}
	return nil
}

func (l *carLink) Status() (json.RawMessage, error) {
	var out json.RawMessage
	err := l.do(func() error {
		if err := l.send("STATUS"); err != nil {
			return err
		}
		_, rest, err := l.reply(time.Now().Add(replyTimeout))
		if err != nil {
			return err
		}
		if !json.Valid([]byte(rest)) {
			return fmt.Errorf("bad status payload: %.60q", rest)
		}
		out = json.RawMessage(rest)
		return nil
	})
	return out, err
}

func (l *carLink) Get(slot int) ([]byte, error) {
	var out []byte
	err := l.do(func() error {
		if err := l.send(fmt.Sprintf("GET %d", slot)); err != nil {
			return err
		}
		deadline := time.Now().Add(replyTimeout)
		_, rest, err := l.reply(deadline)
		if err != nil {
			return err
		}
		n, err := strconv.Atoi(strings.TrimSpace(rest))
		if err != nil || n <= 0 {
			return fmt.Errorf("bad length in reply: %.30q", rest)
		}
		out, err = l.exact(n, time.Now().Add(writeTimeout))
		return err
	})
	return out, err
}

func (l *carLink) Put(slot int, blob []byte) error {
	return l.do(func() error {
		if err := l.send(fmt.Sprintf("PUT %d %d", slot, len(blob))); err != nil {
			return err
		}
		verb, rest, err := l.reply(time.Now().Add(replyTimeout))
		if err != nil {
			return err
		}
		if verb != "READY" {
			return fmt.Errorf("car did not say READY")
		}
		// Nothing is sent until the car ACKs the last chunk onto flash: its USB
		// driver drops whatever doesn't fit the RX queue. See cmd.cpp.
		chunk, err := strconv.Atoi(strings.TrimSpace(rest))
		if err != nil || chunk <= 0 {
			return fmt.Errorf("car offered no chunk size: %.30q", rest)
		}
		for off := 0; off < len(blob); off += chunk {
			end := min(off+chunk, len(blob))
			if _, err := l.port.Write(blob[off:end]); err != nil {
				return err
			}
			if err := l.port.Drain(); err != nil {
				return err
			}
			if verb, _, err := l.reply(time.Now().Add(writeTimeout)); err != nil {
				return err
			} else if verb != "ACK" {
				return fmt.Errorf("car said %s where an ACK was due", verb)
			}
		}
		_, _, err = l.reply(time.Now().Add(writeTimeout))
		return err
	})
}
