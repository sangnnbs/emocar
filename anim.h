// anim.h — non-blocking .qgif player. One animation per CarState.
// Each state plays /anim<N>.qgif from LittleFS when present and valid, else the
// PROGMEM placeholder from anim_data.h. Call animShow(state) on a state change
// and animTick() every loop. Frames go to the panel through display.h.
#pragma once
#include <stdint.h>
#include "motion.h"

// Parsed .qgif header, either container version:
//   v2: "QGF2" | u8 format | u16 w | u16 h | u16 frames | u16 delay[frames] | frames
//   v1 (legacy, mono1 128x64 only): u8 frames | u16 w | u16 h | u16 delay[frames] | frames
struct QgifInfo {
  uint16_t frames;
  uint32_t headerLen;    // bytes before the delay table (5 = v1, 11 = v2)
  uint32_t dataOff;      // headerLen + 2 * frames — first frame byte
};

// Parse + validate the first QGIF_HDR_MAX bytes against this build's display
// profile (DISP_W/H/FORMAT) and the total file length. False = not playable here.
#define QGIF_HDR_MAX 11
bool qgifParse(const uint8_t *hdr, uint32_t fileLen, QgifInfo &out);
const char *dispFormatName();                               // "mono1" | "gray4" | "rgb565"

void animBegin();                                           // display init + validate all slots
void animMessage(const char *l1, const char *l2 = "", const char *l3 = "");   // text screen
void animShow(CarState s);                                  // switch animation (restarts at frame 0)
void animTick();                                            // advance a frame when its delay elapsed
void animReload(int slot);                                  // revalidate a slot after an upload
bool animSlotInfo(int slot, uint16_t &frames, uint32_t &bytes); // true if a valid file backs the slot
