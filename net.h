// net.h — WiFi hotspot + tiny HTTP endpoint for animation uploads.
// The car broadcasts WIFI_AP_SSID; the Go server (server/) connects and does:
//   GET /status    -> JSON: slot files, heap, uptime
//   GET /anim/<n>  -> the slot's .qgif (404 if the placeholder is in use)
//   PUT /anim/<n>  -> replace the slot: write temp, validate, rename, reload
// Call netBegin() in setup and netPoll() every loop. A PUT blocks the loop for
// the transfer (~1-2 s); motion detection just misses that window.
#pragma once

void netBegin();
void netPoll();
