#ifndef DISPLAY_DRV_H
#define DISPLAY_DRV_H

#define NT35510
//#define SSD1963
//#define USE_TEAR

#if defined(NT35510)
#include <NT35510_t4p_conf.h>
#include <NT35510_t4p.h>
extern NT35510_t4p tft;
#endif

#if defined(SSD1963)
#include <SSD1963_t4p_conf.h>
#include <SSD1963_t4p.h>
extern SSD1963_t4p tft;
#endif

bool disp_init();
void disp_setBrightness(uint8_t brightness);
void disp_setAddrWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void disp_pushPixels16bit(uint16_t * pBuf, uint16_t * pBufEnd);
#if defined(SSD1963) && defined(USE_TEAR)
void disp_setTearingEffect(bool useTearing);
void disp_setTearingScanLine(uint16_t scanline);
#endif

#endif // DISPLAY_DRV_H