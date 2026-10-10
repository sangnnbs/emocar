// cmd.cpp — the animation upload link. Payloads are streamed straight to LittleFS,
// never buffered in RAM.
#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "cmd.h"
#include "anim.h"
#include "motion.h"
#include "telem.h"

static void replyErr(const char *msg) { Serial.printf("ERR %s\n", msg); }

static void handleStatus() {
  char body[560];
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
  uint16_t frames; uint32_t bytes;
  char path[24]; animSlotPath(slot, path, sizeof path);
  File f;
  if (!animSlotInfo(slot, frames, bytes) || !(f = LittleFS.open(path, "r"))) {
    replyErr("placeholder in use");
    return;
  }
  // The TX timeout is 0 so telemetry never stalls the loop, but that drops bytes on a
  // payload this size.
  Serial.setTxTimeoutMs(SER_IO_TIMEOUT_MS);
  Serial.printf("OK %lu\n", (unsigned long)f.size());
  uint8_t buf[512];
  int n;
  while ((n = f.read(buf, sizeof buf)) > 0) Serial.write(buf, n);
  Serial.flush();
  Serial.setTxTimeoutMs(0);
  f.close();
}

// Receive into ANIM_TMP_PATH. Returns false with `err` filled on failure.
//
// One chunk in flight, ACKed only once it is on flash: the USB CDC driver drops whatever
// doesn't fit its RX queue, and nothing retransmits it.
static bool receivePayload(long payloadLen, char *err, size_t errLen) {
  LittleFS.remove(ANIM_TMP_PATH);
  File f = LittleFS.open(ANIM_TMP_PATH, "w");
  if (!f) { snprintf(err, errLen, "open tmp failed"); return false; }

  Serial.printf("READY %d\n", CMD_CHUNK);
  Serial.setTimeout(SER_IO_TIMEOUT_MS);

  uint8_t buf[CMD_CHUNK];
  long got = 0;
  while (got < payloadLen) {
    size_t want = min((long)CMD_CHUNK, payloadLen - got);
    size_t n = Serial.readBytes(buf, want);
    if (n != want) {
      f.close();
      snprintf(err, errLen, "short transfer: %ld of %ld bytes (chunk gave %u of %u)",
               got, payloadLen, (unsigned)n, (unsigned)want);
      return false;
    }
    if (f.write(buf, n) != n) {
      f.close();
      snprintf(err, errLen, "flash write failed at %ld of %ld bytes", got, payloadLen);
      return false;
    }
    got += n;
    Serial.printf("ACK %ld\n", got);
  }
  f.close();

  QgifInfo info; uint32_t size;
  if (!qgifCheckFile(ANIM_TMP_PATH, info, size) || size != (uint32_t)payloadLen) {
    snprintf(err, errLen, "not a %dx%d %s qgif", DISP_W, DISP_H, dispFormatName());
    return false;
  }
  return true;
}

static void handlePutAnim(int slot, long payloadLen) {
  if (payloadLen < QGIF_HDR_MAX + 2 + DISP_FRAME_BYTES || (unsigned long)payloadLen > ANIM_MAX_BYTES) {
    replyErr("bad length");
    return;
  }
  animMessage("UPDATING", "receiving anim", carStateName((CarState)slot));

  char err[96];
  if (!receivePayload(payloadLen, err, sizeof err)) {
    LittleFS.remove(ANIM_TMP_PATH);
    replyErr(err);
    return;
  }

  char path[24]; animSlotPath(slot, path, sizeof path);
  LittleFS.remove(path);
  if (!LittleFS.rename(ANIM_TMP_PATH, path)) {
    replyErr("rename failed");
    return;
  }
  animReload(slot);
  Serial.printf("OK %ld\n", payloadLen);
}

static void handleLine(char *line) {
  char verb[8] = "";
  int slot = -1;
  long len = 0;
  int fields = sscanf(line, "%7s %d %ld", verb, &slot, &len);

  if (fields >= 1 && !strcmp(verb, "STATUS")) {
    handleStatus();
  } else if (fields == 2 && !strcmp(verb, "TELEM")) {
    telemLease(slot);                  // no reply: the T lines are the answer
  } else if (fields >= 1 && !strcmp(verb, "ZERO")) {
    telemZero();
    Serial.printf("OK\n");
  } else if (slot < 0 || slot >= CAR_STATE_COUNT) {
    replyErr("bad slot");
  } else if (fields == 2 && !strcmp(verb, "GET")) {
    handleGetAnim(slot);
  } else if (fields == 3 && !strcmp(verb, "PUT")) {
    handlePutAnim(slot, len);
    animShow(motionGet().state);       // leave the UPDATING screen
  } else {
    replyErr("unknown command");
  }
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
    return;                            // one command per loop
  }
}
