// anim.cpp — plays one .qgif per CarState without ever blocking.
//
// Source per slot: /anim<N>.qgif in LittleFS (uploaded over WiFi, see net.cpp),
// falling back to the PROGMEM placeholder in anim_data.h when the file is
// missing or invalid. File frames are streamed one frame at a time, so
// animation size is bounded by flash, not RAM.
//
// Both .qgif container versions play (see anim.h); a file only validates when
// its geometry and pixel format match this build's display profile (config.h).
// Frames are handed to displayFrame() in qgif polarity — the panel driver
// converts (SH1106: bit 1 = dark, U8g2 wants bit 1 = lit, so it inverts).
#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "display.h"
#include "anim.h"
#include "anim_data.h"

static uint8_t  frameBuf[DISP_FRAME_BYTES];
static bool     pmValid[ANIM_COUNT];     // PROGMEM placeholder parses cleanly
static bool     fsValid[ANIM_COUNT];     // slot file exists and parses cleanly
static QgifInfo fsInfo[ANIM_COUNT];
static uint32_t fsBytes[ANIM_COUNT];
static QgifInfo pmInfo[ANIM_COUNT];

// ---- playback state (one animation at a time) ----
static int      cur = -1;                // slot being played, -1 = nothing (text screen)
static bool     curFromFile;
static File     curFile;                 // open while a file animation plays
static QgifInfo curInfo;
static uint16_t delays[ANIM_MAX_FRAMES]; // delay table of the current file animation
static uint16_t frame;                   // next frame to draw
static uint32_t nextAt;                  // millis() at which to draw it

static void slotPath(int i, char *buf, size_t n) { snprintf(buf, n, ANIM_PATH_FMT, i); }

static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }

const char *dispFormatName() {
  return DISP_FORMAT == 0 ? "mono1" : DISP_FORMAT == 1 ? "gray4" : "rgb565";
}

// Validate a v1/v2 header against this display profile and the file length.
bool qgifParse(const uint8_t *hdr, uint32_t fileLen, QgifInfo &out) {
  uint16_t n, w, h;
  uint8_t fmt;
  if (!memcmp(hdr, "QGF2", 4)) {         // v2
    fmt = hdr[4];
    w = rd16(hdr + 5); h = rd16(hdr + 7); n = rd16(hdr + 9);
    out.headerLen = 11;
  } else {                               // legacy v1: mono1 only, no format field
    fmt = 0;
    n = hdr[0]; w = rd16(hdr + 1); h = rd16(hdr + 3);
    out.headerLen = 5;
  }
  out.frames = n;
  out.dataOff = out.headerLen + 2u * n;
  return n > 0 && n <= ANIM_MAX_FRAMES && w == DISP_W && h == DISP_H && fmt == DISP_FORMAT &&
         fileLen == out.dataOff + (uint32_t)DISP_FRAME_BYTES * n;
}

static bool qgifParsePm(const AnimEntry &e, QgifInfo &out) {
  if (e.len < QGIF_HDR_MAX) return false;
  uint8_t hdr[QGIF_HDR_MAX];
  memcpy_P(hdr, e.data, sizeof hdr);
  return qgifParse(hdr, e.len, out);
}

static void closeCurFile() { if (curFile) curFile.close(); }

// (Re)check the slot file; called at boot and after every upload.
static void probeSlot(int i) {
  char path[24]; slotPath(i, path, sizeof path);
  fsValid[i] = false; fsBytes[i] = 0;
  File f = LittleFS.open(path, "r");
  if (!f) return;
  uint8_t hdr[QGIF_HDR_MAX];
  if (f.size() >= QGIF_HDR_MAX && f.read(hdr, sizeof hdr) == sizeof hdr &&
      qgifParse(hdr, f.size(), fsInfo[i])) {
    fsValid[i] = true;
    fsBytes[i] = f.size();
  }
  f.close();
}

// Open the best source for slot i and load its delay table. False = nothing playable.
static bool openSlot(int i) {
  closeCurFile();
  if (fsValid[i]) {
    char path[24]; slotPath(i, path, sizeof path);
    curFile = LittleFS.open(path, "r");
    if (curFile) {
      curInfo = fsInfo[i];
      curFile.seek(curInfo.headerLen);
      if (curFile.read((uint8_t *)delays, 2u * curInfo.frames) == 2u * curInfo.frames) {
        curFromFile = true;
        return true;
      }
      curFile.close();
    }
    fsValid[i] = false;                  // file went bad underneath us
  }
  if (pmValid[i]) { curFromFile = false; curInfo = pmInfo[i]; return true; }
  return false;
}

static void drawFrame(uint16_t i) {
  if (curFromFile) {
    curFile.seek(curInfo.dataOff + (uint32_t)i * DISP_FRAME_BYTES);
    if (curFile.read(frameBuf, DISP_FRAME_BYTES) != DISP_FRAME_BYTES) return;
  } else {
    memcpy_P(frameBuf, ANIM[cur].data + curInfo.dataOff + (uint32_t)i * DISP_FRAME_BYTES,
             DISP_FRAME_BYTES);
  }
  displayFrame(frameBuf);
}

static uint16_t frameDelay(uint16_t i) {
  uint16_t ms;
  if (curFromFile) {
    ms = delays[i];
  } else {
    uint8_t d[2];
    memcpy_P(d, ANIM[cur].data + curInfo.headerLen + 2u * i, 2);
    ms = rd16(d);
  }
  if (ms == 0) ms = 100;
  return ms < ANIM_MIN_FRAME_MS ? ANIM_MIN_FRAME_MS : ms;
}

void animBegin() {
  displayBegin();
  for (int i = 0; i < ANIM_COUNT; i++) {
    pmValid[i] = qgifParsePm(ANIM[i], pmInfo[i]);
    probeSlot(i);
    Serial.printf("anim %d %-9s file:%s placeholder:%s\n", i, ANIM[i].name,
                  fsValid[i] ? "ok" : "-", pmValid[i] ? "ok" : "INVALID");
  }
}

void animMessage(const char *l1, const char *l2, const char *l3) {
  cur = -1;                              // a later animShow() must redraw
  closeCurFile();
  displayMessage(l1, l2, l3);
}

void animShow(CarState s) {
  int i = (int)s;
  if (i == cur) return;
  if (i < 0 || i >= ANIM_COUNT || !openSlot(i)) { animMessage("bad anim", carStateName(s)); return; }
  cur = i; frame = 0; nextAt = millis();
  animTick();                            // draw frame 0 right now, no wait
}

void animTick() {
  if (cur < 0) return;
  uint32_t now = millis();
  if ((int32_t)(now - nextAt) < 0) return;
  drawFrame(frame);
  nextAt = now + frameDelay(frame);      // delay counted from the start of the draw (~25 ms)
  frame = (frame + 1) % curInfo.frames;
}

void animReload(int slot) {
  if (slot < 0 || slot >= ANIM_COUNT) return;
  bool replaying = (slot == cur);
  if (replaying) { cur = -1; closeCurFile(); }   // release the old file before probing
  probeSlot(slot);
  if (replaying) animShow((CarState)slot);
}

bool animSlotInfo(int slot, uint16_t &frames, uint32_t &bytes) {
  if (slot < 0 || slot >= ANIM_COUNT || !fsValid[slot]) return false;
  frames = fsInfo[slot].frames; bytes = fsBytes[slot];
  return true;
}
