#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include "stats/app_stats.h"

#define LCD_BUFFER_COUNT 2
#define SDRAM_SPEED 198      // 166, 198, 221
#define SD_CARD_SPEED 99'000 // 20'000, 33'000, 50'000 (default), 66'000, 99'000, 198'000 (usually doesn't work)
#define USE_EXTMEM_NOCACHE
#define USE_STATS
//#define IRQ_FROM_INT_TIMER
//#define USE_REM_DISP

#if defined(USE_EXTMEM_NOCACHE)
#define EXTMEM_NOCACHE __attribute__((section(".externalram_nocache")))
#define EXTMEM_NOCACHE_PCM __attribute__((section(".externalram_nocache_pcm")))
#else
#define EXTMEM_NOCACHE EXTMEM
#define EXTMEM_NOCACHE_PCM EXTMEM
#endif

#if (LVGL_VERSION_MAJOR == 9)
#define LV_IMG_CF_TRUE_COLOR LV_COLOR_FORMAT_NATIVE
#endif

extern AppStats appStats;

/////////////////////
//User defined params
/////////////////////
extern bool is_playing;
extern uint32_t all_long;
extern uint32_t baseSampPerWavePoint;
extern volatile uint32_t play_adr;
extern uint32_t slip_play_adr;
extern EXTMEM_NOCACHE_PCM uint16_t PCM[206][8192][2];
extern uint16_t PCM_2[2];
extern int16_t LR[2][4];
extern volatile uint8_t SAMPLE[4];
extern uint16_t start_adr_valid_data;
extern uint16_t end_adr_valid_data;
extern uint8_t filling_step;
extern uint8_t offset_adress;
extern uint8_t mem_offset_adress;
extern float c0, c1, c2, c3, r0, r1, r2, r3;
extern int32_t even1, even2, odd1, odd2;
extern float COEF[8];
extern uint8_t play_enable;
extern uint8_t slip_play_enable;
extern volatile uint8_t reverse;
extern volatile uint16_t pitch;
extern volatile uint32_t position;
extern uint32_t slip_position;
extern uint16_t pitch_for_slip;
extern float SAMPLE_BUFFER;
extern float T;
extern uint8_t QUANTIZE;
extern volatile uint8_t end_of_track;
extern uint8_t loop_active;
extern uint32_t LOOP_OUT;
extern uint8_t lock_control;
extern volatile bool dynamicBufferReady;

#define SCREEN_WIDTH 800 //1024
#define SCREEN_HEIGHT 480 //600
//#define SKIP_LVGL_RENDER_CANVAS //If defined, sets canvas to hidden and does 'manual' flush
#define BUFFER_MEM DMAMEM //DMAMEM //EXTMEM //<blank for ITCM>

extern uint16_t lcdBuffer[LCD_BUFFER_COUNT][SCREEN_WIDTH * SCREEN_HEIGHT] __attribute__((aligned(64)));

extern uint16_t dynamicCanvasBuffer[800 * 164];


const uint16_t chartWidth = 800;
const uint16_t chartHeight = 164;
const uint16_t overviewChartHeight = 64;
const uint16_t phaseMeterWidth = 75;
const uint16_t phaseMeterHeight = 22;
const uint8_t slopePoints = 1;
const uint8_t waveformScrollInc = 1;
const uint16_t middleContainerPos = 158;
const uint16_t bottomContainerPos = 322;

// For static buffer indicator
extern bool staticBufferReady;
extern uint16_t oldStaticBufferX;
extern uint16_t newStaticBufferX;
extern uint16_t overviewCanvasBuffer[800 * overviewChartHeight];

/////////////////////////
//End user defined params
/////////////////////////





typedef struct {
    char *trackLength;
    float bpmAnalyzed;
    char *filename;
    char *path;
    char *title;
    char *artist;
    char *fileType;
    uint16_t track_id;
    uint8_t star_rating;
    char *musical_key;
    double numberOfSamples;
    double sampleRate;
} Track;

struct KeyInfo {
    uint8_t numericValue;
    const char* key;
};

struct KeyInfoColor {
    uint8_t numericValue;
    const char* color;
};

constexpr KeyInfo keyLookup[] = {
    {0, "C"},
    {1, "Am"},
    {2, "G"},
    {3, "Em"},
    {4, "D"},
    {5, "Bm"},
    {6, "A"},
    {7, "F#m"},
    {8, "E"},
    {9, "Dbm"},
    {10, "B"},
    {11, "Abm"},
    {12, "F#"},
    {13, "Ebm"},
    {14, "Db"},
    {15, "Bbm"},
    {16, "Ab"},
    {17, "Fm"},
    {18, "Eb"},
    {19, "Cm"},
    {20, "Bb"},
    {21, "Gm"},
    {22, "F"},
    {23, "Dm"}
};

constexpr KeyInfo keyLookupColor[] = {
    {0, "#ee82d9"},
    {1, "#f2abe4"},
    {2, "#ce8fff"},
    {3, "#ddb4fd"},
    {4, "#9fb6ff"},
    {5, "#becdfd"},
    {6, "#56d9f9"},
    {7, "#8ee4f9"},
    {8, "#00ebeb"},
    {9, "#55f0f0"},
    {10, "#01edca"},
    {11, "#56f1da"},
    {12, "#3cee81"},
    {13, "#7df2aa"},
    {14, "#86f24f"},
    {15, "#aef589"},
    {16, "#dfca73"},
    {17, "#e8daa1"},
    {18, "#ffa07c"},
    {19, "#fdbfa7"},
    {20, "#ff8894"},
    {21, "#fdafb7"},
    {22, "#ff81b4"},
    {23, "#fdaacc"}
};


// Global beat lookup structure
// Global lookup table for entire track
typedef struct {
    int32_t *beatPixelPositions; // Array of pixel positions for each beat
    int64_t firstBeatIndex;      // Index of first beat in array
    uint16_t beatCount;          // Total number of beats
    uint32_t samplesPerPoint;    // Cached for quick conversion
} GlobalBeatLUT;


typedef struct {
    double sampleOffset;
    int64_t beatIndex;
    uint32_t beatsUntilNext;
    uint32_t unknown;
} BeatMarker;

typedef struct {
    double sampleRate;
    double numSamples;
    uint8_t hasGrid;
    BeatMarker *markers;
    int markerCount;
    double samplesPerWaveformPoint; // e.g., 420 for 44.1kHz
} Beatgrid;


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