// net.h — the car on WiFi. Built only with WIFI_ENABLED (config.h).
// Boot opens the setup hotspot (the printed QR joins it); the setup page picks the home
// WiFi; once joined, the hotspot closes and the OLED shows the IP.
#pragma once
#include <stdint.h>

void netBegin();              // open the setup hotspot; returns at once
void netTick(uint32_t now);   // call every loop: web server, join progress
