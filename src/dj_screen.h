#ifndef _dj_screen_h
#define _dj_screen_h
#include "lvgl.h"
#include "globals.h"
#include "SD.h"

extern lv_obj_t * main_screen;

void dj_ui_init(Track * track);
void updateDynamicWaveform(uint32_t waveformOffset);
void updatePlaybackPosition(uint16_t newX);
#if defined(RDI_DEVELOPMENTS_REV3)
extern FsFile playFile;
#else
extern File playFile;
#endif


#endif