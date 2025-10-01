#include "lv_utils.h"

const char* getKey(uint8_t numericValue) {
    if (numericValue < sizeof(keyLookup) / sizeof(keyLookup[0])) {
        return keyLookup[numericValue].key;
    }
    return "N/A";
}

uint8_t lookupValue(uint8_t input) {
    switch (input) {
        case 0: return 0;
        case 20: return 1;
        case 40: return 2;
        case 60: return 3;
        case 80: return 4;
        case 100: return 5;
        case 120: return 5;
        default: return 255; // Invalid input
    }
}

// Convert hex string to lv_color_t
lv_color_t hex_string_to_color(const char* hex) {
    if (!hex) {
        return COLOR_GRAY;
    }
    
    if (hex[0] == '#') hex++; // Skip # if present
    
    uint32_t color_val = strtoul(hex, NULL, 16);
    return lv_color_hex(color_val);
}

lv_color_t getKeyColor(uint8_t numericValue) {
    if (numericValue < sizeof(keyLookupColor) / sizeof(keyLookupColor[0])) {
        return hex_string_to_color(keyLookupColor[numericValue].key);
    }
    return COLOR_GRAY; // Default color for invalid keys
}

const char* formatDuration(const char* seconds_str) {
    static char formatted_time[8]; // Static buffer for "MM:SS\0"
    
    if (!seconds_str || strlen(seconds_str) == 0) {
        strcpy(formatted_time, "0:00");
        return formatted_time;
    }
    
    int total_seconds = atoi(seconds_str);
    int minutes = total_seconds / 60;
    int seconds = total_seconds % 60;
    
    snprintf(formatted_time, sizeof(formatted_time), "%d:%02d", minutes, seconds);
    return formatted_time;
}