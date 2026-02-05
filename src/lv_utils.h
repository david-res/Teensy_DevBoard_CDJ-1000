
#ifndef dj_utils_H
#define dj_utils_H

#include "globals.h"
#include "lvgl.h"
#include <cstring>
#include <cstdlib>

// Color definitions to match the dark theme
#define COLOR_BACKGROUND    lv_color_hex(0x2A2A2A)
#define COLOR_TRACK_BG      lv_color_hex(0x3A3A3A)
#define COLOR_TRACK_HOVER   lv_color_hex(0x4A4A4A)
#define COLOR_WHITE         lv_color_hex(0xFFFFFF)
#define COLOR_GRAY          lv_color_hex(0xB0B0B0)
#define COLOR_BORDER        lv_color_hex(0x555555)

// Function declarations (prototypes)
const char* getKey(uint8_t numericValue);
uint8_t lookupValue(uint8_t input);
lv_color_t hex_string_to_color(const char* hex);
lv_color_t getKeyColor(uint8_t numericValue);
const char* formatDuration(uint16_t total_seconds);
uint32_t get_voltage_mv();
float getPSRamSpeed();
void setPSRamSpeed(int mhz);

#endif // LV_UTILS_H