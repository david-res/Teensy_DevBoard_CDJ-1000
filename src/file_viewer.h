#ifndef FILE_VIEWER_H
#define FILE_VIEWER_H
#include "lvgl.h"
#include "database/db_manager.h"


extern lv_obj_t * filesScreen;

void createListScreen();
void load_dj_screen_with_track(Track * track);

#endif
