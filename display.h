// display.h — the seam between the sketch and the physical panel.
// New monitor = a DISP_DRIVER_* backend in display.cpp + a profile block in config.h +
// an entry in server/profiles.json.
#pragma once
#include <stdint.h>

void displayBegin();
// One full frame, DISP_FRAME_BYTES long, in the profile's qgif pixel format.
void displayFrame(const uint8_t *frame);
// Title + two detail lines.
void displayMessage(const char *l1, const char *l2, const char *l3);
