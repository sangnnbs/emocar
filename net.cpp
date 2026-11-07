#include "config.h"
#if WIFI_ENABLED

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <uri/UriBraces.h>
#include "net.h"
#include "anim.h"
#include "cmd.h"
#include "motion.h"
#include "setup_page.h"

enum Mode { JOINING, SETUP, ONLINE };
enum Join { J_IDLE, J_TRYING, J_OK, J_FAIL };

static WebServer server(80);
static DNSServer dns;

static char host[16];
static Mode mode = SETUP;
static Join join = J_IDLE;
static String trySsid, tryPass;
static const char *failWhy = "";
static uint32_t joinStart, doneAt;
static bool online, stationSeen;
static uint32_t msgShownAt;

static void showMsg(const char *l1, const char *l2, const char *l3, uint32_t now) {
  animMessage(l1, l2, l3);
  msgShownAt = now | 1;
}

static String jsonEscape(const String &s) {
  String o;
  o.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) { char u[7]; snprintf(u, sizeof u, "\\u%04x", c); o += u; }
    else o += c;
  }
  return o;
}

static void sendJson(const String &body) { server.send(200, "application/json", body); }

static void handleScan() {
  int n = WiFi.scanComplete();
  if (server.hasArg("again") && n != WIFI_SCAN_RUNNING) { WiFi.scanDelete(); n = WIFI_SCAN_FAILED; }
  if (n == WIFI_SCAN_FAILED) WiFi.scanNetworks(true);
  if (n < 0) { sendJson("{\"busy\":true}"); return; }
  String out = "{\"nets\":[";
  for (int i = 0; i < n; i++) {
    if (i) out += ',';
    out += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",\"rssi\":" + WiFi.RSSI(i);
    out += ",\"open\":";
    out += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false";
    out += '}';
  }
  out += "]}";
  sendJson(out);
}

static void handleSave() {
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  const char *bad = nullptr;
  if (ssid.length() == 0 || ssid.length() > 32) bad = "Wi-Fi name must be 1 to 32 characters";
  else if (pass.length() > 64 || (pass.length() > 0 && pass.length() < 8))
    bad = "Password must be 8 to 64 characters, or empty for an open Wi-Fi";
  else if (join == J_TRYING) bad = "Still trying the last one, wait a moment";
  if (bad) { server.send(400, "application/json", String("{\"error\":\"") + bad + "\"}"); return; }
  trySsid = ssid;
  tryPass = pass;
  failWhy = "";
  join = J_TRYING;
  joinStart = millis();
  sendJson("{\"ok\":true}");
  showMsg("WIFI", "joining", ssid.c_str(), joinStart);
  WiFi.begin(ssid.c_str(), pass.c_str());
}

static void handleJoin() {
  String out = "{\"state\":\"";
  out += join == J_IDLE ? "idle" : join == J_TRYING ? "trying" : join == J_OK ? "ok" : "fail";
  out += "\",\"ssid\":\"" + jsonEscape(trySsid) + "\",\"ip\":\"";
  if (join == J_OK) out += WiFi.localIP().toString();
  out += "\",\"why\":\"";
  out += failWhy;
  out += "\",\"host\":\"";
  out += host;
  out += "\"}";
  sendJson(out);
}

static void handleStatus() {
  char body[600];
  statusJson(body, sizeof body);
  server.sendHeader("Cache-Control", "no-store");
  sendJson(body);
}

static int animSlotArg() {
  String a = server.pathArg(0);
  int slot = a.length() == 1 ? a[0] - '0' : -1;
  return slot >= 0 && slot < CAR_STATE_COUNT ? slot : -1;
}

static void handleAnim() {
  int slot = animSlotArg();
  if (slot < 0) { server.send(400, "text/plain", "bad slot"); return; }
  server.sendHeader("Cache-Control", "no-store");
  uint16_t frames; uint32_t bytes;
  char path[24]; animSlotPath(slot, path, sizeof path);
  File f;
  if (animSlotInfo(slot, frames, bytes) && (f = LittleFS.open(path, "r"))) {
    server.streamFile(f, "application/octet-stream");
    f.close();
    return;
  }
  const uint8_t *data; uint32_t len;
  if (animBuiltin(slot, data, len)) server.send_P(200, "application/octet-stream", (PGM_P)data, len);
  else server.send(404, "text/plain", "no animation");
}

static void handleOther() {
  if (mode != SETUP) { server.send(404, "text/plain", "Not found"); return; }
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

static WebServer::THandlerFunction setupOnly(void (*h)()) {
  return [h] { if (mode == SETUP) h(); else handleOther(); };
}

static bool loadSaved(String &ssid, String &pass) {
  Preferences p;
  p.begin("wifi", true);
  ssid = p.getString("ssid", "");
  pass = p.getString("pass", "");
  p.end();
  return ssid.length() > 0;
}

static void save(const String &ssid, const String &pass) {
  Preferences p;
  p.begin("wifi", false);
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  p.end();
}

static void startSetup() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(host, WIFI_AP_PASS);
  const IPAddress ap(192, 168, 4, 1);
  WiFi.softAPConfig(ap, ap, IPAddress(255, 255, 255, 0), IPAddress(0, 0, 0, 0), ap);
  WiFi.AP.enableDhcpCaptivePortal();
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  WiFi.scanNetworks(true);

  mode = SETUP;
  showMsg("SCAN QR", host, "", millis());
}

void netBegin() {
  uint64_t mac = ESP.getEfuseMac();
  snprintf(host, sizeof host, "emocar-%02x%02x",
           (unsigned)(mac >> 32) & 0xff, (unsigned)(mac >> 40) & 0xff);
  WiFi.persistent(false);
  WiFi.setHostname(host);

  String ssid, pass;
  if (loadSaved(ssid, pass)) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    joinStart = millis();
    mode = JOINING;
  } else {
    startSetup();
  }

  server.on("/", HTTP_GET, setupOnly([] { server.send_P(200, "text/html", SETUP_HTML); }));
  server.on("/scan", HTTP_GET, setupOnly(handleScan));
  server.on("/save", HTTP_POST, setupOnly(handleSave));
  server.on("/join", HTTP_GET, setupOnly(handleJoin));
  server.on("/status", HTTP_GET, handleStatus);
  server.on(UriBraces("/anim/{}"), HTTP_GET, handleAnim);
  server.onNotFound(handleOther);
  server.begin();
}

static void tickJoining(uint32_t now) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setAutoReconnect(true);
    mode = ONLINE;
  } else if (now - joinStart > WIFI_JOIN_MS) {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect();
    startSetup();
  }
}

static void tickSetup(uint32_t now) {
  dns.processNextRequest();
  if (!stationSeen && WiFi.softAPgetStationNum() > 0) {
    stationSeen = true;
    if (msgShownAt) msgShownAt = now - WIFI_IP_SHOW_MS;
  }

  if (join == J_TRYING) {
    wl_status_t s = WiFi.status();
    bool gaveUp = now - joinStart > 3000 && (s == WL_NO_SSID_AVAIL || s == WL_CONNECT_FAILED);
    if (s == WL_CONNECTED) {
      save(trySsid, tryPass);
      tryPass = "";
      join = J_OK;
      doneAt = now;
    } else if (gaveUp || now - joinStart > WIFI_JOIN_MS) {
      failWhy = s == WL_NO_SSID_AVAIL ? "notfound" : s == WL_CONNECT_FAILED ? "refused" : "timeout";
      WiFi.disconnect();
      join = J_FAIL;
      showMsg("SCAN QR", host, "join failed", now);
    }
  }

  if (join == J_OK && now - doneAt > WIFI_DONE_MS) {
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    mode = ONLINE;
  }
}

static void tickOnline(uint32_t now) {
  bool up = WiFi.status() == WL_CONNECTED;
  if (up == online) return;
  online = up;
  if (up) showMsg("WIFI", WiFi.localIP().toString().c_str(), host, now);
}

void netTick(uint32_t now) {
  if (mode == JOINING) tickJoining(now);
  else if (mode == SETUP) tickSetup(now);
  else tickOnline(now);
  server.handleClient();
  if (msgShownAt && now - msgShownAt >= WIFI_IP_SHOW_MS) {
    msgShownAt = 0;
    animShow(motionGet().state);
  }
}

#endif
