// display.cpp — panel driver backends; exactly one DISP_DRIVER_* is enabled in config.h.
#include <Arduino.h>
#include "config.h"
#include "display.h"

#if DISP_DRIVER_U8G2_SH1106_I2C
// SH1106 over I2C (U8g2). qgif mono1 bit 1 = dark, U8g2 bit 1 = lit: invert on copy.
#include <U8g2lib.h>

static U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
static uint8_t scratch[DISP_FRAME_BYTES];

void displayBegin() {
  u8g2.setI2CAddress(OLED_ADDR << 1);   // U8g2 wants the 8-bit form
  u8g2.begin();
}

void displayFrame(const uint8_t *frame) {
  for (uint32_t b = 0; b < DISP_FRAME_BYTES; b++) scratch[b] = ~frame[b];
  u8g2.clearBuffer();
  u8g2.drawBitmap(0, 0, DISP_W / 8, DISP_H, scratch);
  u8g2.sendBuffer();
}

void displayMessage(const char *l1, const char *l2, const char *l3) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tr);
  u8g2.drawStr(0, 14, l1);
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(0, 32, l2);
  u8g2.drawStr(0, 46, l3);
  u8g2.sendBuffer();
}

#else
#error "config.h must enable exactly one DISP_DRIVER_* backend"
#endif
