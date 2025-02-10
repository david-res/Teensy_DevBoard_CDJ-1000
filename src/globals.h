#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>

/////////////////////
//User defined params
/////////////////////

#define SCREEN_WIDTH 800 //1024
#define SCREEN_HEIGHT 480 //600
#define SKIP_LVGL_RENDER_CANVAS //If defined, sets canvas to hidden and does 'manual' flush
#define BUFFER_MEM DMAMEM //DMAMEM //EXTMEM //<blank for ITCM>
#define WAVEFORM_FILE "bufferTest/blob_wave.txt"

const uint16_t chartWidth = 740; //964
const uint16_t chartHeight = 164;
const uint16_t overviewChartHeight = 50;
const uint16_t phaseMeterWidth = 75;
const uint16_t phaseMeterHeight = 22;
const uint8_t slopePoints = 4;
const uint8_t waveformScrollInc = 1;

/////////////////////////
//End user defined params
/////////////////////////

//////////////////
//Useful functions
//////////////////

//https://stackoverflow.com/questions/47491147/check-at-runtime-if-macro-was-defined
#define STRINGIZE_I(x) #x
#define MACRO_EXISTS(name) (#name [0] != STRINGIZE_I(name) [0])
#define ARRAY_SIZE(arr)	(sizeof(arr)/sizeof(*(arr)))

////////////////
//Console colors
/////////////////

#define USE_DEBUG_COLORS

#if defined (USE_DEBUG_COLORS)
//Foreground: reset = 0, black = 30, red = 31, green = 32, yellow = 33, blue = 34, magenta = 35, cyan = 36, and white = 37
//Background: reset = 0, black = 40, red = 41, green = 42, yellow = 43, blue = 44, magenta = 45, cyan = 46, and white = 47
#define SER_RED "\e[1;31m"
#define SER_GREEN "\e[1;32m"
#define SER_YELLOW "\e[1;33m"
#define SER_MAGENTA "\e[1;35m"
#define SER_CYAN "\e[1;36m"
#define SER_WHITE "\e[1;37m"
#define SER_RESET "\e[1;0m"

#define SER_TRACE "\033[38;2;182;222;215m"
#define SER_INFO "\033[38;2;200;200;200m"
#define SER_WARN "\033[38;2;221;230;112m"
#define SER_ERROR "\033[38;2;255;105;82m"
#define SER_USER "\033[38;2;55;255;28m"
#define SER_GREY "\033[38;2;128;128;128m"

#else

#define SER_RED ""
#define SER_GREEN ""
#define SER_YELLOW ""
#define SER_MAGENTA ""
#define SER_CYAN ""
#define SER_RESET ""

#define SER_TRACE ""
#define SER_INFO ""
#define SER_WARN ""
#define SER_ERROR ""
#define SER_USER ""
#define SER_GREY ""
#endif //USE_DEBUG_COLORS

#endif // GLOBALS_H