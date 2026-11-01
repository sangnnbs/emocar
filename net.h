// net.h — the car on WiFi. Built only with WIFI_ENABLED (config.h).
// Boot joins the saved WiFi. Nothing saved, or no join within WIFI_JOIN_MS: the setup
// hotspot opens (the printed QR joins it), the setup page picks the home WiFi, the car
// saves it, the hotspot closes and the OLED shows the IP.
#pragma once
#include <stdint.h>

void netBegin();              // join the saved WiFi or open setup; returns at once
void netTick(uint32_t now);   // call every loop: web server, join progress
