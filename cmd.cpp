// cmd.cpp — the animation upload link. A command line, then a fixed-length raw
// payload: the serial equivalent of Content-Length, and for the same reason — a
// .qgif is streamed straight to LittleFS, never buffered in RAM.
//
// A command is served start to finish inside cmdPoll(), so nothing here can
// interleave with the telemetry printing. The host still skips telemetry lines: one
// can be printed between its command and the reply.
#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "cmd.h"
#include "anim.h"
#include "motion.h"

static void replyErr(const char *msg) { Serial.printf("ERR %s\n", msg); }

static void handleStatus() {
  char body[560];   // header + one object per CarState
  int len = snprintf(body, sizeof body,
                     "{\"state\":\"%s\",\"heap\":%lu,\"uptime\":%lu,"
                     "\"profile\":\"%s\",\"w\":%d,\"h\":%d,\"format\":\"%s\",\"slots\":[",
                     carStateName(motionGet().state),
                     (unsigned long)ESP.getFreeHeap(), (unsigned long)(millis() / 1000),
                     DISP_PROFILE, DISP_W, DISP_H, dispFormatName());
  for (int i = 0; i < CAR_STATE_COUNT; i++) {
    uint16_t frames; uint32_t bytes;
    bool file = animSlotInfo(i, frames, bytes);
    len += snprintf(body + len, sizeof body - len, "%s{\"file\":%s,\"frames\":%u,\"bytes\":%lu}",
                    i ? "," : "", file ? "true" : "false",
                    file ? frames : 0, file ? (unsigned long)bytes : 0);
  }
  snprintf(body + len, sizeof body - len, "]}");
  Serial.printf("OK %s\n", body);
}

static void handleGetAnim(int slot) {
  char path[24]; snprintf(path, sizeof path, ANIM_PATH_FMT, slot);
  File f = LittleFS.open(path, "r");
  uint16_t frames; uint32_t bytes;
  if (!f || !animSlotInfo(slot, frames, bytes)) {
    if (f) f.close();
    replyErr("placeholder in use");
    return;
  }
  // setup()'s zero TX timeout keeps a car with no terminal from stalling in printf,
  // but it drops bytes on a payload this size. Let this one wait for the host.
  Serial.setTxTimeoutMs(SER_IO_TIMEOUT_MS);
  Serial.printf("OK %lu\n", (unsigned long)f.size());
  uint8_t buf[512];
  int n;
  while ((n = f.read(buf, sizeof buf)) > 0) Serial.write(buf, n);
  Serial.flush();
  Serial.setTxTimeoutMs(0);
  f.close();
}

static void handlePutAnim(int slot, long payloadLen) {
  if (payloadLen < QGIF_HDR_MAX + 2 + DISP_FRAME_BYTES || (unsigned long)payloadLen > ANIM_MAX_BYTES) {
    replyErr("bad length");
    return;
  }
  animMessage("UPDATING", "receiving anim", carStateName((CarState)slot));

  LittleFS.remove(ANIM_TMP_PATH);
  File f = LittleFS.open(ANIM_TMP_PATH, "w");
  if (!f) { replyErr("open tmp failed"); return; }

  // One chunk in flight, ACKed only once it is on flash. USB CDC is not reliable end
  // to end: the driver's ISR drops whatever doesn't fit its RX queue and nothing
  // retransmits it. Measured — 33 KB sent at once lost two thirds while LittleFS
  // erased, and even a lone 1 KB burst lost 369 bytes into the stock 256-byte queue.
  Serial.printf("READY %d\n", CMD_CHUNK);
  Serial.setTimeout(SER_IO_TIMEOUT_MS);   // readBytes gives each byte its own deadline

  uint8_t buf[CMD_CHUNK];
  long got = 0;
  size_t shortRead = 0, shortWant = 0;
  bool ok = true;
  while (got < payloadLen) {
    size_t want = min((long)CMD_CHUNK, payloadLen - got);
    size_t n = Serial.readBytes(buf, want);
    if (n != want) { ok = false; shortRead = n; shortWant = want; break; }  // host went quiet
    if (f.write(buf, n) != n) { ok = false; break; }        // out of flash space
    got += n;
    Serial.printf("ACK %ld\n", got);                        // ...now the host may send more
  }
  f.close();

  char err[96];
  if (!ok) {
    snprintf(err, sizeof err, "short transfer: %ld of %ld bytes (chunk gave %u of %u)",
             got, payloadLen, (unsigned)shortRead, (unsigned)shortWant);
  } else {
    // Validate before touching the live slot: geometry and pixel format must match
    // this build's display profile, and the size must be consistent.
    File t = LittleFS.open(ANIM_TMP_PATH, "r");
    uint8_t hdr[QGIF_HDR_MAX];
    QgifInfo info;
    ok = t && t.read(hdr, sizeof hdr) == sizeof hdr &&
         t.size() == (size_t)payloadLen && qgifParse(hdr, t.size(), info);
    if (t) t.close();
    if (!ok) snprintf(err, sizeof err, "not a %dx%d %s qgif",
                      DISP_W, DISP_H, dispFormatName());
  }

  if (!ok) {
    LittleFS.remove(ANIM_TMP_PATH);
    replyErr(err);
    return;
  }

  char path[24]; snprintf(path, sizeof path, ANIM_PATH_FMT, slot);
  LittleFS.remove(path);
  if (!LittleFS.rename(ANIM_TMP_PATH, path)) {
    replyErr("rename failed");
    return;
  }
  animReload(slot);
  Serial.printf("OK %ld\n", got);
}

static void handleLine(char *line) {
  char verb[8] = "";
  int slot = -1;
  long len = 0;
  int fields = sscanf(line, "%7s %d %ld", verb, &slot, &len);

  bool wasMessage = false;
  if (fields >= 1 && !strcmp(verb, "STATUS")) {
    handleStatus();
  } else if (slot < 0 || slot >= CAR_STATE_COUNT) {
    replyErr("bad slot");
  } else if (fields == 2 && !strcmp(verb, "GET")) {
    handleGetAnim(slot);
  } else if (fields == 3 && !strcmp(verb, "PUT")) {
    handlePutAnim(slot, len);
    wasMessage = true;
  } else {
    replyErr("unknown command");
  }

  if (wasMessage) animShow(motionGet().state);   // leave the UPDATING screen
}

void cmdPoll() {
  static char line[CMD_LINE_MAX];
  static size_t len = 0;

  while (Serial.available()) {
    int c = Serial.read();
    if (c != '\n' && c != '\r') {
      if (len + 1 < sizeof line) line[len++] = (char)c;
      continue;
    }
    if (len == 0) continue;            // blank line, or the second half of a CRLF
    line[len] = 0;
    len = 0;
    handleLine(line);
    return;                            // one command per loop — let the FIFO drain
  }
}
