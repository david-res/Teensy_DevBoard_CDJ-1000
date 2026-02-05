#ifndef FILE_VIEWER_H
#define FILE_VIEWER_H
#include "lvgl.h"
#include "globals.h"
//#include "RekordboxParser.h"


extern lv_obj_t * filesScreen;

void createListScreen(Track** tracks, int16_t track_count);
void load_dj_screen_with_track(Track * track);

#endif
