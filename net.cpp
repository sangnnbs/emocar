// net.cpp — hand-rolled HTTP over WiFiServer, small enough to audit.
// No ESPAsyncWebServer / WebServer dependency: the only tricky requirement is
// streaming a PUT body straight to LittleFS without buffering it in RAM, which
// the stock WebServer can't do for a raw body.
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include "config.h"
#include "net.h"
#include "anim.h"
#include "motion.h"

static WiFiServer httpServer(HTTP_PORT);

void netBegin() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  httpServer.begin();
  Serial.printf("WiFi AP \"%s\" up, http://%s:%d\n",
                WIFI_AP_SSID, WiFi.softAPIP().toString().c_str(), HTTP_PORT);
}

// ---- small blocking helpers with a millis() deadline ----

static bool readByte(WiFiClient &c, uint8_t &b) {
  uint32_t t0 = millis();
  while (c.connected()) {
    if (c.available()) { b = c.read(); return true; }
    if (millis() - t0 > NET_IO_TIMEOUT_MS) return false;
    delay(1);
  }
  return false;
}

// Read one CRLF/LF-terminated line, truncating at n-1. False on timeout/close.
static bool readLine(WiFiClient &c, char *buf, size_t n) {
  size_t len = 0;
  uint8_t b;
  while (readByte(c, b)) {
    if (b == '\n') { buf[len] = 0; if (len && buf[len - 1] == '\r') buf[len - 1] = 0; return true; }
    if (len + 1 < n) buf[len++] = b;
  }
  return false;
}

static void reply(WiFiClient &c, int code, const char *text, const char *body) {
  c.printf("HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\n"
           "Connection: close\r\n\r\n%s", code, text, (int)strlen(body), body);
}

// ---- handlers ----

static void handleStatus(WiFiClient &c) {
  char body[420];
  int len = snprintf(body, sizeof body,
                     "{\"state\":\"%s\",\"heap\":%lu,\"uptime\":%lu,"
                     "\"profile\":\"%s\",\"w\":%d,\"h\":%d,\"format\":\"%s\",\"slots\":[",
                     carStateName(motionGet().state),
                     (unsigned long)ESP.getFreeHeap(), (unsigned long)(millis() / 1000),
                     DISP_PROFILE, DISP_W, DISP_H, dispFormatName());
  for (int i = 0; i < 3; i++) {
    uint16_t frames; uint32_t bytes;
    bool file = animSlotInfo(i, frames, bytes);
    len += snprintf(body + len, sizeof body - len, "%s{\"file\":%s,\"frames\":%u,\"bytes\":%lu}",
                    i ? "," : "", file ? "true" : "false",
                    file ? frames : 0, file ? (unsigned long)bytes : 0);
  }
  snprintf(body + len, sizeof body - len, "]}");
  reply(c, 200, "OK", body);
}

static void handleGetAnim(WiFiClient &c, int slot) {
  char path[24]; snprintf(path, sizeof path, ANIM_PATH_FMT, slot);
  File f = LittleFS.open(path, "r");
  uint16_t frames; uint32_t bytes;
  if (!f || !animSlotInfo(slot, frames, bytes)) {
    if (f) f.close();
    reply(c, 404, "Not Found", "{\"error\":\"placeholder in use\"}");
    return;
  }
  c.printf("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
           "Content-Length: %lu\r\nConnection: close\r\n\r\n", (unsigned long)f.size());
  uint8_t buf[512];
  int n;
  while ((n = f.read(buf, sizeof buf)) > 0) c.write(buf, n);
  f.close();
}

static void handlePutAnim(WiFiClient &c, int slot, long contentLen) {
  if (contentLen < QGIF_HDR_MAX + 2 + DISP_FRAME_BYTES || (unsigned long)contentLen > ANIM_MAX_BYTES) {
    reply(c, 400, "Bad Request", "{\"error\":\"bad content-length\"}");
    return;
  }
  animMessage("UPDATING", "receiving anim", carStateName((CarState)slot));

  LittleFS.remove(ANIM_TMP_PATH);
  File f = LittleFS.open(ANIM_TMP_PATH, "w");
  if (!f) { reply(c, 500, "Error", "{\"error\":\"open tmp failed\"}"); return; }

  uint8_t buf[1024];
  long got = 0;
  bool ok = true;
  while (got < contentLen) {
    uint32_t t0 = millis();
    while (!c.available()) {
      if (!c.connected() || millis() - t0 > NET_IO_TIMEOUT_MS) { ok = false; break; }
      delay(1);
    }
    if (!ok) break;
    int n = c.read(buf, min((long)sizeof buf, contentLen - got));
    if (n <= 0) { ok = false; break; }
    if ((int)f.write(buf, n) != n) { ok = false; break; }   // out of flash space
    got += n;
  }
  f.close();

  // Validate before touching the live slot: the header must match this build's
  // display profile (geometry + pixel format) and the size must be consistent.
  if (ok) {
    File t = LittleFS.open(ANIM_TMP_PATH, "r");
    uint8_t hdr[QGIF_HDR_MAX];
    QgifInfo info;
    ok = t && t.read(hdr, sizeof hdr) == sizeof hdr &&
         t.size() == (size_t)contentLen && qgifParse(hdr, t.size(), info);
    if (t) t.close();
  }

  if (!ok) {
    LittleFS.remove(ANIM_TMP_PATH);
    char err[96];
    snprintf(err, sizeof err, "{\"error\":\"transfer failed or not a %dx%d %s qgif\"}",
             DISP_W, DISP_H, dispFormatName());
    reply(c, 400, "Bad Request", err);
    return;
  }

  char path[24]; snprintf(path, sizeof path, ANIM_PATH_FMT, slot);
  LittleFS.remove(path);
  if (!LittleFS.rename(ANIM_TMP_PATH, path)) {
    reply(c, 500, "Error", "{\"error\":\"rename failed\"}");
    return;
  }
  animReload(slot);
  char body[64];
  snprintf(body, sizeof body, "{\"ok\":true,\"slot\":%d,\"bytes\":%ld}", slot, got);
  reply(c, 200, "OK", body);
  Serial.printf("anim %d replaced over WiFi (%ld bytes)\n", slot, got);
}

// ---- request dispatch ----

static void handleClient(WiFiClient &c) {
  char line[128];
  if (!readLine(c, line, sizeof line)) return;
  char method[8] = "", path[32] = "";
  sscanf(line, "%7s %31s", method, path);

  long contentLen = 0;
  while (readLine(c, line, sizeof line) && line[0])        // headers until blank line
    if (!strncasecmp(line, "Content-Length:", 15)) contentLen = atol(line + 15);

  int slot = -1;
  if (!strncmp(path, "/anim/", 6) && path[6] >= '0' && path[6] <= '2' && !path[7])
    slot = path[6] - '0';

  bool wasMessage = false;
  if (!strcmp(method, "GET") && !strcmp(path, "/status")) handleStatus(c);
  else if (!strcmp(method, "GET") && slot >= 0)           handleGetAnim(c, slot);
  else if (!strcmp(method, "PUT") && slot >= 0)           { handlePutAnim(c, slot, contentLen); wasMessage = true; }
  else reply(c, 404, "Not Found", "{\"error\":\"unknown route\"}");

  c.flush();
  if (wasMessage) animShow(motionGet().state);   // leave the UPDATING screen
}

void netPoll() {
  WiFiClient c = httpServer.accept();
  if (!c) return;
  handleClient(c);
  c.stop();
}
