// net.cpp — see net.h. Nothing here blocks or reboots (a reboot recalibrates the gyro):
// WiFi.begin() returns at once, the core's WiFi task joins, netTick() watches.
#include "config.h"
#if WIFI_ENABLED

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "net.h"
#include "anim.h"
#include "motion.h"
#include "setup_page.h"

#define DBG(...) do { if (DEBUG_LOG) Serial.printf(__VA_ARGS__); } while (0)

enum Mode { SETUP, ONLINE };
enum Join { J_IDLE, J_TRYING, J_OK, J_FAIL };

static WebServer server(80);
static DNSServer dns;

static char host[16];          // emocar-1a2b: hotspot name and hostname, from the MAC
static Mode mode = SETUP;
static Join join = J_IDLE;
static String trySsid;
static const char *failWhy = "";
static uint32_t joinStart, doneAt;
static bool online, stationSeen;
static uint32_t msgShownAt;    // 0 = no WiFi message on screen
static volatile uint8_t lastReason;   // why the last join attempt failed (wifi_err_reason_t)

static void showMsg(const char *l1, const char *l2, const char *l3, uint32_t now) {
  animMessage(l1, l2, l3);
  msgShownAt = now | 1;        // never 0
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
  failWhy = "";
  lastReason = 0;
  join = J_TRYING;
  joinStart = millis();
  // Answer before joining: the hotspot moves to the router's channel and the phone may drop.
  sendJson("{\"ok\":true}");
  DBG("wifi: trying \"%s\"\n", ssid.c_str());
  showMsg("WIFI", "joining", ssid.c_str(), joinStart);
  WiFi.begin(ssid.c_str(), pass.c_str());
}

static void handleStatus() {
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

// Anything else, incl. the phone's internet check, lands on the setup page: that's the popup.
static void handleOther() {
  DBG("http: %s %s%s\n", server.method() == HTTP_GET ? "GET" : "other",
      server.hostHeader().c_str(), server.uri().c_str());
  if (mode != SETUP) { server.send(404, "text/plain", "Not found"); return; }
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

void netBegin() {
  uint64_t mac = ESP.getEfuseMac();                    // MAC byte 0 in the low byte
  snprintf(host, sizeof host, "emocar-%02x%02x",       // the MAC's last two bytes
           (unsigned)(mac >> 32) & 0xff, (unsigned)(mac >> 40) & 0xff);
  WiFi.persistent(false);      // the core must not keep its own copy of the WiFi
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    lastReason = info.wifi_sta_disconnected.reason;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname(host);
  WiFi.softAP(host, WIFI_AP_PASS);
  // The core's DHCP hands out no DNS server unless one is given here. Without it the phone's
  // internet check never reaches the car, so Android says "no internet", not "sign in".
  const IPAddress ap(192, 168, 4, 1);
  WiFi.softAPConfig(ap, ap, IPAddress(255, 255, 255, 0), IPAddress(0, 0, 0, 0), ap);
  // DHCP option 114 (RFC 8910): the phone is told the setup page's address when it joins,
  // so Android 11+ / iOS 14+ show it without guessing from their internet check.
  WiFi.AP.enableDhcpCaptivePortal();
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());

  server.on("/", HTTP_GET, [] {
    DBG("http: setup page\n");
    server.send_P(200, "text/html", SETUP_HTML);
  });
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/status", HTTP_GET, handleStatus);
  server.onNotFound(handleOther);
  server.begin();
  WiFi.scanNetworks(true);     // ready by the time a phone opens the page

  mode = SETUP;
  showMsg("SCAN QR", host, "", millis());
  DBG("wifi: setup hotspot %s up, page at http://%s/\n", host, WiFi.softAPIP().toString().c_str());
}

static void tickSetup(uint32_t now) {
  dns.processNextRequest();
  if (!stationSeen && WiFi.softAPgetStationNum() > 0) {   // a phone joined: back to the face
    stationSeen = true;
    if (msgShownAt) msgShownAt = now - WIFI_IP_SHOW_MS;
  }

  if (join == J_TRYING) {
    wl_status_t s = WiFi.status();
    // Skip the first 3 s: the status can still hold the previous attempt's failure.
    bool gaveUp = now - joinStart > 3000 && (s == WL_NO_SSID_AVAIL || s == WL_CONNECT_FAILED);
    if (s == WL_CONNECTED) {
      join = J_OK;
      doneAt = now;
      DBG("wifi: joined \"%s\", IP %s\n", trySsid.c_str(), WiFi.localIP().toString().c_str());
    } else if (gaveUp || now - joinStart > WIFI_JOIN_MS) {
      failWhy = s == WL_NO_SSID_AVAIL ? "notfound" : s == WL_CONNECT_FAILED ? "refused" : "timeout";
      WiFi.disconnect();
      join = J_FAIL;
      DBG("wifi: join failed: %s, reason %u (%s)\n", failWhy, lastReason,
          WiFi.disconnectReasonName((wifi_err_reason_t)lastReason));
      showMsg("SCAN QR", host, "join failed", now);
    }
  }

  // Hotspot stays up a little so the page can read "ok" and show the IP.
  if (join == J_OK && now - doneAt > WIFI_DONE_MS) {
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    mode = ONLINE;
    DBG("wifi: hotspot closed\n");
  }
}

static void tickOnline(uint32_t now) {
  bool up = WiFi.status() == WL_CONNECTED;
  if (up == online) return;
  online = up;
  if (up) {
    String ip = WiFi.localIP().toString();
    DBG("wifi: online, IP %s, RSSI %d\n", ip.c_str(), WiFi.RSSI());
    showMsg("WIFI", ip.c_str(), host, now);
  } else {
    DBG("wifi: lost, rejoining, reason %u (%s)\n", lastReason,
        WiFi.disconnectReasonName((wifi_err_reason_t)lastReason));
  }
}

void netTick(uint32_t now) {
  if (mode == SETUP) tickSetup(now);
  else tickOnline(now);
  server.handleClient();
  if (msgShownAt && now - msgShownAt >= WIFI_IP_SHOW_MS) {
    msgShownAt = 0;
    animShow(motionGet().state);
  }
}

#endif
