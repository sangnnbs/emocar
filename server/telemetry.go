// telemetry.go — the /dashboard's live feed. While at least one browser listens, a
// goroutine keeps reading the port between commands and fans the car's `T ...` lines
// (format in the firmware's telem.h) out to every listener. With nobody listening the
// port is only touched by commands, as before.
package main

import (
	"bytes"
	"encoding/json"
	"strings"
	"sync"
	"time"
)

const (
	subQueue  = 64          // lines buffered per listener; a slow one loses lines, not the feed
	maxBuffer = 64 << 10    // drop unterminated junk past this
	retryLink = time.Second // between attempts to find the car
)

type feed struct {
	mu      sync.Mutex
	subs    map[chan string]struct{}
	pumping bool
	link    string // last "link" event, replayed to each new listener
}

// Subscribe returns a stream of ready-to-send SSE events and a cancel func.
func (l *carLink) Subscribe() (<-chan string, func()) {
	ch := make(chan string, subQueue)
	f := &l.feed
	f.mu.Lock()
	if f.subs == nil {
		f.subs = map[chan string]struct{}{}
	}
	f.subs[ch] = struct{}{}
	if f.link != "" {
		ch <- f.link
	}
	if !f.pumping {
		f.pumping = true
		go l.pump()
	}
	f.mu.Unlock()
	return ch, func() {
		f.mu.Lock()
		delete(f.subs, ch)
		f.mu.Unlock()
	}
}

func (f *feed) publish(ev string) {
	f.mu.Lock()
	defer f.mu.Unlock()
	for ch := range f.subs {
		select {
		case ch <- ev:
		default:
		}
	}
}

// listening ends the pump once the last listener has gone.
func (f *feed) listening() bool {
	f.mu.Lock()
	defer f.mu.Unlock()
	if len(f.subs) == 0 {
		f.pumping = false
		return false
	}
	return true
}

func (f *feed) setLink(port string, err error) {
	v := map[string]any{"online": err == nil, "port": port}
	if err != nil {
		v["error"] = err.Error()
	}
	b, _ := json.Marshal(v)
	ev := "event: link\ndata: " + string(b)
	f.mu.Lock()
	changed := ev != f.link
	f.link = ev
	f.mu.Unlock()
	if changed {
		f.publish(ev)
	}
}

func (l *carLink) pump() {
	for l.feed.listening() {
		port, err := l.readTelemetry()
		l.feed.setLink(port, err)
		if err != nil {
			time.Sleep(retryLink)
		}
	}
}

// readTelemetry holds the port for one read (readPoll at most), so a command waiting
// on the lock gets in between reads.
func (l *carLink) readTelemetry() (string, error) {
	l.mu.Lock()
	defer l.mu.Unlock()
	if err := l.open(); err != nil {
		return "", err
	}
	if err := l.fill(time.Now()); err != nil && err != errTimeout {
		l.close(err)
		return "", err
	}
	for {
		i := bytes.IndexByte(l.buf, '\n')
		if i < 0 {
			break
		}
		s := strings.TrimRight(string(l.buf[:i]), "\r")
		l.buf = l.buf[i+1:]
		if strings.HasPrefix(s, "T ") {
			l.feed.publish("event: t\ndata: " + s[2:])
		}
	}
	if len(l.buf) > maxBuffer {
		l.buf = nil
	}
	return l.path, nil
}

// Zero makes the car's current pose the dashboard's reference orientation.
func (l *carLink) Zero() error {
	return l.do(func() error {
		if err := l.send("ZERO"); err != nil {
			return err
		}
		_, _, err := l.reply(time.Now().Add(replyTimeout))
		return err
	})
}
