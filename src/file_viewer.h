#ifndef FILE_VIEWER_H
#define FILE_VIEWER_H
#include "lvgl.h"
#include "sqlite3.h"


extern lv_obj_t * filesScreen;

void createListScreen();

extern sqlite3 * mdb;
extern sqlite3 * pdb;

#endif
