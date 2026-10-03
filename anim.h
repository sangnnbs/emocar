// anim.h — non-blocking .qgif player, one animation per CarState.
// Each state plays /anim<N>.qgif from LittleFS when present and valid, else the
// PROGMEM placeholder from anim_data.h.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "motion.h"

// .qgif header, either container version:
//   v2: "QGF2" | u8 format | u16 w | u16 h | u16 frames | u16 delay[frames] | frames
//   v1 (mono1 only): u8 frames | u16 w | u16 h | u16 delay[frames] | frames
struct QgifInfo {
  uint16_t frames;
  uint32_t headerLen;    // bytes before the delay table (5 = v1, 11 = v2)
  uint32_t dataOff;      // first frame byte
};

// Validate a header against this build's display profile and the total file length.
#define QGIF_HDR_MAX 11
bool qgifParse(const uint8_t *hdr, uint32_t fileLen, QgifInfo &out);
// Same, for a LittleFS file. `size` is the file length when it opened.
bool qgifCheckFile(const char *path, QgifInfo &out, uint32_t &size);
const char *dispFormatName();                               // "mono1" | "gray4" | "rgb565"
void animSlotPath(int slot, char *buf, size_t n);

void animBegin();                                           // display init + validate all slots
void animMessage(const char *l1, const char *l2 = "", const char *l3 = "");
void animShow(CarState s);                                  // switch animation (restarts at frame 0)
void animTick();                                            // call every loop
void animReload(int slot);                                  // revalidate a slot after an upload
bool animSlotInfo(int slot, uint16_t &frames, uint32_t &bytes); // true if a valid file backs the slot
