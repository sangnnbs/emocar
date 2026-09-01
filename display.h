// display.h — the one seam between the sketch and the physical panel.
// config.h picks the profile (DISP_W/H/FORMAT) and the driver backend; adding a
// new monitor = a new DISP_DRIVER_* block in display.cpp + a config.h profile
// block + an entry in server/profiles.json. Nothing else changes.
#pragma once
#include <stdint.h>

void displayBegin();
// Show one full frame, DISP_FRAME_BYTES long, in the profile's qgif pixel format
// (mono1: bit 1 = dark — the driver converts to its panel's polarity).
void displayFrame(const uint8_t *frame);
// Three lines of status text (title + two detail lines).
void displayMessage(const char *l1, const char *l2, const char *l3);
