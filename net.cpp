// net.cpp — see net.h. The face never stops: WiFi.begin() returns at once, the core's
// WiFi task does the join and the reconnects, and netTick() only watches the status.
#include "config.h"
#if WIFI_ENABLED

#include <Arduino.h>
#include <WiFi.h>
#include "net.h"
#include "anim.h"
#include "motion.h"

#if !__has_include("wifi_secrets.h")
#error "WIFI_ENABLED needs wifi_secrets.h: copy wifi_secrets.example.h and fill it in"
#endif
#include "wifi_secrets.h"

#define DBG(...) do { if (DEBUG_LOG) Serial.printf(__VA_ARGS__); } while (0)

static char host[16];          // emocar-1a2b, from the chip ID: two cars never collide
static bool online;
static uint32_t ipShownAt;     // 0 = the IP is not on screen
static volatile uint8_t lastReason;   // why the last join attempt failed (wifi_err_reason_t)

void netBegin() {
  uint64_t mac = ESP.getEfuseMac();                    // MAC byte 0 in the low byte
  snprintf(host, sizeof host, "emocar-%02x%02x",       // the MAC's last two bytes
           (unsigned)(mac >> 32) & 0xff, (unsigned)(mac >> 40) & 0xff);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(host);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    lastReason = info.wifi_sta_disconnected.reason;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  DBG("wifi: %s joining \"%s\"\n", host, WIFI_SSID);
}

void netTick(uint32_t now) {
  static uint32_t lastLog;
  bool up = WiFi.status() == WL_CONNECTED;
  if (now - lastLog >= 5000) {
    lastLog = now;
    if (up) DBG("wifi: joined \"%s\" IP %s gw %s RSSI %d ch %d\n", WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(),
                WiFi.RSSI(), WiFi.channel());
    else DBG("wifi: not joined, status %d, last reason %u (%s)\n", WiFi.status(), lastReason,
             WiFi.disconnectReasonName((wifi_err_reason_t)lastReason));
  }
  if (up != online) {
    online = up;
    if (up) {
      String ip = WiFi.localIP().toString();
      DBG("wifi: joined, IP %s, RSSI %d\n", ip.c_str(), WiFi.RSSI());
      animMessage("WIFI", ip.c_str(), host);
      ipShownAt = now | 1;     // never 0
    } else {
      DBG("wifi: lost, rejoining\n");
    }
  }
  if (ipShownAt && now - ipShownAt >= WIFI_IP_SHOW_MS) {
    ipShownAt = 0;
    animShow(motionGet().state);
  }
}

#endif
