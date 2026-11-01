// net.h — the car on WiFi. Built only with WIFI_ENABLED (config.h).
// Step A1 of PLANS.md: join the WiFi in wifi_secrets.h at boot, show the IP on the OLED.
#pragma once
#include <stdint.h>

void netBegin();              // start joining; returns at once, the radio joins on its own
void netTick(uint32_t now);   // call every loop: reports a join / a loss
