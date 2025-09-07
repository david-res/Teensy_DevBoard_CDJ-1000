#ifndef WAVEFORM_H
#define WAVEFORM_H
#include <lvgl.h>
#include "globals.h"


extern lv_obj_t * waveform_scr;

void waveformView(Track * track);
bool loadWaveformData(uint16_t track_id);
void drawOverviewCanvas();
void updateWaveformOffset(uint32_t newVal);
void incrementPhaseMeter();
void drawPhaseMeterRect(uint8_t index, uint16_t * colorBuffer);
void drawMainWaveform();
void drawLabels();
void flushBuffer(uint16_t x1, uint16_t x2, uint16_t y1, uint16_t y2, uint16_t * buffer);
void drawFastVLine16Bit(int16_t x, int16_t y, int16_t h, uint16_t color, uint8_t opa, uint16_t * buffer, uint16_t stride);
void drawSlope16Bit(uint8_t p1, uint8_t p2, uint16_t x, uint16_t color, uint8_t opa);
uint16_t blend(uint16_t fg, uint16_t bg, uint8_t opa);
uint16_t fastBlend( uint32_t fg, uint32_t bg, uint8_t opa);
void waveform_cb_event_cb(lv_event_t * e);
void waveform_slider_event_cb(lv_event_t * e);
void lv_display_event_cb(lv_event_t * e);

#endif //WAVEFORM_H