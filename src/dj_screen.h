#ifndef _dj_screen_h
#define _dj_screen_h
#include "lvgl.h"
#include "globals.h"
#include "SD.h"

extern lv_obj_t * main_screen;

void dj_ui_init(Track * track);
void updateWaveformOffset(uint32_t waveformOffset);
extern File playFile;


#endif