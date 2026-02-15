
#include "dj_screen.h"
#include "../include/device_defines.h"
#include "globals.h"
#include "Arduino.h"
#include "lv_utils.h"
#include "SD.h"
#include "file_viewer.h"
#include "T4_PXP.h"
#include <SDRAM_t4.h>
#include "DMAChannel.h"
#include "rekordbox_anlz_api.h"
#if defined(USE_BEAT_NUMBERS)
#include "utils/digit_renderer.h"
#include "lv_utils.h"
#endif


#if defined(RDI_DEVELOPMENTS_REV3)
#include "SdFat.h"
#endif

#if defined(IRQ_FROM_INT_TIMER)
IntervalTimer irqTimer;
extern void SAI_IRQHandler();
#endif


FILE_TYPE playFile;


LV_FONT_DECLARE(exo2_16)
LV_FONT_DECLARE(exo2_18)
LV_FONT_DECLARE(exo2_20)
LV_FONT_DECLARE(exo2_24)
LV_FONT_DECLARE(exo2_28)
LV_FONT_DECLARE(exo2_32)

// Display dimensions
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 480

// Color definitions
#define COLOR_BG LV_COLOR_MAKE(0x1a, 0x1a, 0x1a)
#define COLOR_TEXT_PRIMARY LV_COLOR_MAKE(0xff, 0xff, 0xff)
#define COLOR_TEXT_SECONDARY LV_COLOR_MAKE(0xa0, 0xa0, 0xa0)
#define COLOR_WAVEFORM_HIGH LV_COLOR_MAKE(0x40, 0x80, 0xff)
#define COLOR_WAVEFORM_LOW LV_COLOR_MAKE(0x80, 0xff, 0x40)
#define COLOR_PROGRESS LV_COLOR_MAKE(0x80, 0xff, 0x40)

#define COLOR_GRAY          lv_color_hex(0xB0B0B0)

// Cue button colors (exact DJ controller colors)
static const lv_color_t cue_colors[8] = {
    LV_COLOR_MAKE(0xEA, 0xC5, 0x32), // 1 - EAC532 (Yellow)
    LV_COLOR_MAKE(0xEA, 0x8F, 0x32), // 2 - EA8F32 (Orange)
    LV_COLOR_MAKE(0xB8, 0x55, 0xBF), // 3 - B855BF (Purple)
    LV_COLOR_MAKE(0xBA, 0x2A, 0x41), // 4 - BA2A41 (Red)
    LV_COLOR_MAKE(0x86, 0xC6, 0x4B), // 5 - 86C64B (Light Green)
    LV_COLOR_MAKE(0x20, 0xC6, 0x7C), // 6 - 20C67C (Green)
    LV_COLOR_MAKE(0x00, 0xA8, 0xB1), // 7 - 00A8B1 (Teal)
    LV_COLOR_MAKE(0x15, 0x8E, 0xE2)  // 8 - 158EE2 (Blue)
};

// Or as an array for easier indexing:
const uint32_t HOTCUE_COLORS[8] = {
    0xFF1493,  // A - Hot Pink
    0x00BFFF,  // B - Cyan
    0x00FF00,  // C - Lime Green
    0x9D00FF,  // D - Purple
    0x00FF00,  // E - Bright Green
    0xFF8C00,  // F - Orange
    0x0080FF,  // G - Blue
    0xFFFF00   // H - Yellow
};

// Main containers
lv_obj_t * main_screen;
static lv_obj_t *top_container;
static lv_obj_t *middle_container;
static lv_obj_t *bottom_container;

// UI elements
static lv_obj_t *title_label;
static lv_obj_t *artist_label;
static lv_obj_t *bpm_label;
static lv_obj_t *key_label;
static lv_obj_t *bar_count_label;
static lv_obj_t *time_label;
static lv_obj_t *current_bpm_label;
static lv_obj_t *tempo_range_label;
static lv_obj_t *adjusted_tempo_label;
static lv_obj_t *progress_bars[4];
static lv_obj_t *daynamic_waveform_canvas;
static lv_obj_t *static_waveform_canvas;
static lv_obj_t *cue_buttons[8];




void drawFastVLine16Bit(uint16_t x, uint16_t y, uint16_t h, uint16_t color, uint16_t * buffer, uint16_t stride);
void drawFastVLine16BitOverview(uint16_t x, uint16_t y, uint16_t h, uint16_t color, uint16_t * buffer, uint16_t stride);
void drawSlope16Bit(uint16_t * buf, uint8_t p1, uint8_t p2, uint16_t x, uint16_t color, uint8_t opa);
void RedrawWaveforms(uint32_t position);
void updateDynamicWaveform(uint32_t waveformOffset);
void updateOverviewWaveform(uint32_t waveformOffset);
void ShowPhaseMeter(uint16_t phase);
void backButton(lv_event_t *e);
bool useOpa = false;


uint64_t overviewSampleCount = 0;
uint64_t highResSampleCount = 0;



const uint16_t col_blue = 0x135D; //From Rezo, was 0x001F;
const uint16_t col_green = 0x15EA; //From Rezo, was 0x15EA; //Amber 0xF547
const uint16_t col_white = 0xF7DE; //From Rezo, was 0xFFFF;

const uint16_t waveformColors[3] = {col_blue, col_green, col_white};
const float waveformUserGain[3] = {1.0, 0.66, 0.33};

uint8_t * dynamicWaveSampleData[3]; // 0=lo samples, 1=med samples, 2=hi samples
DMAMEM uint16_t dynamicCanvasBuffer[800 * 164];
uint64_t dynamicWaveformSampleCount = 0;
double samplesPerDaynamicPoint = 0;

//To store repeating group data of samples for the overview waveform
uint8_t overViewWaveSampleData[3][800];
DMAMEM uint16_t overviewCanvasBuffer[800 * overviewChartHeight];
uint64_t overviewWaveformSampleCount = 0;
double samplesPerOverviewPoint = 0;

uint8_t * uncompressedBuffer;
uint8_t * highResBuffer;
uint8_t * overviewBuffer;

volatile bool dynamicBufferReady = false;

uint16_t oldIndicatorBuffer[2 * overviewChartHeight];  // 2px wide x 64px height
uint16_t newIndicatorBuffer[2 * overviewChartHeight];  // 2px wide x 64px height


GlobalBeatLUT globalBeats = {0};
uint32_t dynamicWaveformEntries = 0;
uint32_t BEATGRID[4096];    // beatgrid (0, 3, 7... )
uint16_t BPMGRID[4096];				// bpmgrit BPM*100
uint32_t hotCues[8];
uint8_t GRID_OFFSET = 0; // Default to 0 if not provided by parser
uint16_t beatGridLenth = 0;

uint32_t PreviousPositionDW = UINT32_MAX;
uint8_t Prev10m = 0xFF;
uint8_t Prev1m = 0xFF;
uint8_t Prev10s = 0xFF;
uint8_t Prev1s = 0xFF;
uint8_t Prev10f = 0xFF;
uint8_t Prev1f = 0xFF;
uint8_t PrevHf = 0xFF;


uint8_t DynamicWaveformZOOM = 1;

//#define USE_EXTMEM_FOR_WAVEFORM

#ifdef USE_EXTMEM_FOR_WAVEFORM
  #define WAVEFORM_MALLOC(size) extmem_malloc(size)
  #define WAVEFORM_FREE(ptr) extmem_free(ptr)
  #define WAVEFORM_LOCATION "EXTMEM"
#else
  #define WAVEFORM_MALLOC(size) malloc(size)
  #define WAVEFORM_FREE(ptr) free(ptr)
  #define WAVEFORM_LOCATION "internal RAM"
#endif


void loadTrackData(const char* dat_filepath) {
    BeatGridEntry* beat_grid = NULL;
    uint32_t num_beats;
    uint16_t original_bpm;
    uint8_t grid_offset;
    GRID_OFFSET = 0; // Default to 0 if not provided by parser
    
    // Extract beat grid and BPM data
    uint16_t err = extractBeatGrid(dat_filepath, &beat_grid, &num_beats, 
                                   &original_bpm, &grid_offset);
    
    if (err == 0) {
        // Store grid offset
        GRID_OFFSET = grid_offset;
        
        // Store beat grid length (limit to array size)
        beatGridLenth = (num_beats > 4096) ? 4096 : num_beats;
        
        // Copy beat positions and BPM values
        for (uint32_t i = 0; i < beatGridLenth; i++) {
            BEATGRID[i] = beat_grid[i].position;  // Beat position in samples/frames
            BPMGRID[i] = beat_grid[i].bpm;        // BPM * 100
        }
        
        Serial.printf("Loaded %d beats, BPM: %.2f, Grid offset: %d\n", 
                      beatGridLenth, original_bpm/100.0, GRID_OFFSET);
        
        // Free the allocated beat grid
        free(beat_grid);
    } else {
        Serial.printf("Error loading beat grid: %d\n", err);
    }
    
    // Extract hot cues
    CuePoint hot_cues[3];
    uint8_t num_hot_cues;
    
    err = extractHotCues(dat_filepath, hot_cues, &num_hot_cues);
    
    if (err == 0) {
        // Clear hot cue array first
        for (int i = 0; i < 8; i++) {
            hotCues[i] = 0xFFFFFFFF;  // Or 0, depending on your "empty" value
        }
        
        // Copy hot cues (only first 3 positions for A, B, C)
        for (int i = 0; i < num_hot_cues && i < 3; i++) {
            if (hot_cues[i].type & 0x2) {  // Is active?
                hotCues[i] = hot_cues[i].start_pos;
            }
        }
        
        Serial.printf("Loaded %d hot cues\n", num_hot_cues);
    } else {
        Serial.printf("Error loading hot cues: %d\n", err);
    }
}

void freeDynamicWaveform() {
    for (int i = 0; i < 3; i++) {
        if (dynamicWaveSampleData[i] != NULL) {
            WAVEFORM_FREE(dynamicWaveSampleData[i]);
            dynamicWaveSampleData[i] = NULL;
        }
    }
    dynamicWaveformEntries = 0;
    Serial.println("Dynamic waveform freed from EXTMEM");
}

bool loadDynamicWaveformForTrack(const char* filepath) {
    // Free any existing waveform first
    freeDynamicWaveform();
    
    uint8_t* waveform = NULL;
    uint32_t entries = 0;


    uint16_t err = extractDynamicWaveform(filepath, &waveform, &entries);
    
    if (err != ANLZ_OK) {
        Serial.printf("Failed to load dynamic waveform: error %d\n", err);
        return false;
    }
    Serial.printf("Dynamic waveform extracted successfully: %d entries\n", entries);
    
    dynamicWaveformEntries = entries;
    all_long = entries;
    
    Serial.printf("Allocating waveform in %s\n", WAVEFORM_LOCATION);
    
    // Allocate separate band buffers in EXTMEM
    for (int i = 0; i < 3; i++) {
      Serial.printf("Allocating band %d buffer for %d entries\n", i, entries);
        dynamicWaveSampleData[i] = (uint8_t*)WAVEFORM_MALLOC(entries);
        if (dynamicWaveSampleData[i] == NULL) {
            Serial.printf("Out of %s for band %d\n", WAVEFORM_LOCATION, i);
            // Cleanup
            for (int j = 0; j < i; j++) {
                WAVEFORM_FREE(dynamicWaveSampleData[j]);
                dynamicWaveSampleData[j] = NULL;
            }
            free(waveform);
            dynamicWaveformEntries = 0;
            return false;
        }
    }
    Serial.println("Waveform allocated successfully");
    // De-interleave to LOW-MID-HIGH order
    for (uint32_t i = 0; i < entries; i++) {
        dynamicWaveSampleData[0][i] = waveform[i * 3 + 0];  // LOW
        dynamicWaveSampleData[1][i] = waveform[i * 3 + 1];  // MID
        dynamicWaveSampleData[2][i] = waveform[i * 3 + 2];  // HIGH
    }
    
    Serial.println("Waveform de-interleaved successfully");
    free(waveform);
    
    Serial.printf("Dynamic waveform loaded in %s: %d entries, %d KB\n", 
                  WAVEFORM_LOCATION, entries, (entries * 3) / 1024);
    
    return true;
}

FLASHMEM void drawOverviewCanvas()
{
  //Clear overview canvas
  memset(overviewCanvasBuffer, 0, chartWidth * overviewChartHeight * 2);

  for (uint16_t x = 0; x < chartWidth; x++) {
    // Stack from bottom to top: MID (white) → HIGH (orange) → LOW (blue)
    // overViewWaveSampleData[0] = MID
    // overViewWaveSampleData[1] = HIGH  
    // overViewWaveSampleData[2] = LOW
    
    uint8_t mid_height = overViewWaveSampleData[0][x];
    uint8_t high_height = overViewWaveSampleData[1][x];
    uint8_t low_height = overViewWaveSampleData[2][x];
    
    // Calculate cumulative heights for stacking
    uint8_t base = 0;  // Start from bottom
    
    // Draw MID (white) at bottom
    drawFastVLine16BitOverview(x, overviewChartHeight - (base + mid_height), mid_height, waveformColors[0], overviewCanvasBuffer, chartWidth);
    base += mid_height;
    
    // Draw HIGH (orange) on top of MID
    drawFastVLine16BitOverview(x, overviewChartHeight - (base + high_height), high_height, waveformColors[1], overviewCanvasBuffer, chartWidth);
    base += high_height;
    
    // Draw LOW (blue) on top of HIGH
    drawFastVLine16BitOverview(x, overviewChartHeight - (base + low_height), low_height, waveformColors[2], overviewCanvasBuffer, chartWidth);
  }
  
  //Invalidate canvas, as this is updated done rarely
  lv_obj_invalidate(static_waveform_canvas);
}

void create_top_container(Track * track) {
    // Main top container
    top_container = lv_obj_create(main_screen);
    lv_obj_set_size(top_container, SCREEN_WIDTH, 160);
    lv_obj_set_pos(top_container, 0, 0);
    lv_obj_set_style_bg_color(top_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(top_container, 0, 0);
    lv_obj_set_style_pad_all(top_container, 0, 0);
    lv_obj_set_style_radius(top_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(top_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(top_container, backButton, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(top_container, LV_OBJ_FLAG_CLICKABLE);
    
    // Title and BPM/Key container
    //LV_COLOR_MAKE(0x53, 0x53, 0x53)
    lv_obj_t *title_bpm_container = lv_obj_create(top_container);
    lv_obj_set_size(title_bpm_container, SCREEN_WIDTH, 70);
    lv_obj_set_pos(title_bpm_container, 0, 0);
    //lv_obj_set_style_bg_opa(title_bpm_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(title_bpm_container, LV_COLOR_MAKE(0x53, 0x53, 0x53), 0);
    lv_obj_set_style_border_width(title_bpm_container, 0, 0);
    lv_obj_set_style_pad_all(title_bpm_container, 0, 0);
     lv_obj_set_style_radius(title_bpm_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(title_bpm_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(title_bpm_container, LV_OBJ_FLAG_CLICKABLE);

    // Title label (left side)
    title_label = lv_label_create(title_bpm_container);
    lv_label_set_text(title_label, track->title);
    lv_obj_set_style_text_color(title_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title_label, &exo2_20, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 5);
    lv_obj_clear_flag(title_label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(title_label, LV_OBJ_FLAG_CLICKABLE);
    

    // BPM label (right side)
    bpm_label = lv_label_create(title_bpm_container);
    lv_label_set_text_fmt(bpm_label,"%.1f", track->bpm);
    lv_obj_set_style_text_color(bpm_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(bpm_label, &exo2_32, 0);
    lv_obj_align(bpm_label, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_clear_flag(bpm_label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(bpm_label, LV_OBJ_FLAG_CLICKABLE);
   

    // Key label (below BPM)
    key_label = lv_label_create(title_bpm_container);
    lv_label_set_text(key_label, rbParser.getKeyName(track->key_id));
    lv_obj_set_style_text_font(key_label, &exo2_20, 0);
    lv_obj_align_to(key_label, bpm_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    const char* keyName = rbParser.getKeyName(track->key_id);
    const char* keyColor = getKeyColor(keyName);
    lv_obj_set_style_text_color(key_label,lv_color_hex(strtol(keyColor + 1, nullptr, 16)), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(key_label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(key_label, LV_OBJ_FLAG_CLICKABLE);


    lv_obj_t *progress_line = lv_obj_create(title_bpm_container);
    lv_obj_set_size(progress_line, 700, 3);
    lv_obj_set_pos(progress_line, 0, 67);
    lv_obj_set_style_bg_color(progress_line, lv_color_hex(0x158EE2), 0);
    lv_obj_set_style_border_width(progress_line, 0, 0);
    lv_obj_clear_flag(progress_line, LV_OBJ_FLAG_SCROLLABLE);
    
    // Artist label (below title)
    artist_label = lv_label_create(top_container);
    lv_label_set_text(artist_label, track->artist);
    lv_obj_set_style_text_color(artist_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(artist_label, &exo2_20, 0);
    lv_obj_set_pos(artist_label, 0, 40);
    lv_obj_clear_flag(artist_label, LV_OBJ_FLAG_SCROLLABLE);


    // Bottom info container (bars, time, BPM info)
    lv_obj_t *info_container = lv_obj_create(top_container);
    lv_obj_set_size(info_container, LV_PCT(100), 60);
    lv_obj_set_pos(info_container, 0, 80);
    lv_obj_set_style_bg_opa(info_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(info_container, 0, 0);
    lv_obj_set_style_pad_all(info_container, 0, 0);
    lv_obj_clear_flag(info_container, LV_OBJ_FLAG_SCROLLABLE);


    // Progress bars (left section)
    
    for (int i = 0; i < 4; i++) {
        progress_bars[i] = lv_obj_create(info_container);
        lv_obj_set_size(progress_bars[i], 60, 16);
        lv_obj_set_pos(progress_bars[i], i * 65, 15);
        lv_obj_set_style_radius(progress_bars[i], 0, LV_PART_MAIN);
        lv_obj_clear_flag(progress_bars[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(progress_bars[i], 0, 0);
        lv_obj_set_style_bg_color(progress_bars[i], lv_color_hex(0xA6C8FF), LV_PART_MAIN);
    }

    // Bar count label
    bar_count_label = lv_label_create(info_container);
    //lv_label_set_text(bar_count_label, "94 || 0.34");
    lv_obj_set_style_text_color(bar_count_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(bar_count_label, &exo2_18, 0);
    lv_obj_set_pos(bar_count_label, 0, 35);
    lv_obj_clear_flag(bar_count_label, LV_OBJ_FLAG_SCROLLABLE);

    // Time label (center)
    time_label = lv_label_create(info_container);
    //lv_label_set_text(time_label, "03:00.4");
    lv_obj_set_style_text_color(time_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(time_label, &exo2_28, 0);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(time_label, LV_OBJ_FLAG_SCROLLABLE);
   

    // BPM info (right section)
    current_bpm_label = lv_label_create(info_container);
    lv_label_set_text(current_bpm_label, "125");
    lv_obj_set_style_text_color(current_bpm_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(current_bpm_label, &exo2_24, 0);
    lv_obj_align(current_bpm_label, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_clear_flag(current_bpm_label, LV_OBJ_FLAG_SCROLLABLE);

    // Additional tempo info labels can be added here
}

void create_middle_container(Track * track) {
    middle_container = lv_obj_create(main_screen);
    lv_obj_set_size(middle_container, chartWidth, middleContainerPos);
    lv_obj_set_pos(middle_container, 0, middleContainerPos);
    lv_obj_set_style_bg_color(middle_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(middle_container, 0, 0);
    //lv_obj_set_style_pad_all(middle_container, 10, 0);
    lv_obj_set_style_border_color(middle_container, lv_color_white(), 0);
    lv_obj_set_style_radius(middle_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(middle_container, LV_OBJ_FLAG_SCROLLABLE);
    //lv_obj_add_flag(middle_container, LV_OBJ_FLAG_HIDDEN);
    //lv_obj_add_flag(middle_container, LV_OBJ_FLAG_IGNORE_LAYOUT);
    

    // Dynamic waveform canvas
    daynamic_waveform_canvas = lv_canvas_create(middle_container);
    lv_obj_set_size(daynamic_waveform_canvas, chartWidth, chartHeight);
    lv_obj_center(daynamic_waveform_canvas);
    lv_obj_clear_flag(daynamic_waveform_canvas, LV_OBJ_FLAG_SCROLLABLE);
    //lv_obj_add_flag(daynamic_waveform_canvas, LV_OBJ_FLAG_HIDDEN);
    //lv_obj_add_flag(daynamic_waveform_canvas, LV_OBJ_FLAG_IGNORE_LAYOUT);

    // Create canvas buffer (16-bit RGB565)
    lv_canvas_set_buffer(daynamic_waveform_canvas, dynamicCanvasBuffer, chartWidth, chartHeight, LV_IMG_CF_TRUE_COLOR);

    String correctedPath = String(track->anlz_ex2_path);
    if (correctedPath.startsWith("Y/")) {
        correctedPath = correctedPath.substring(2);  // Remove "Y/"
    }

    loadDynamicWaveformForTrack(correctedPath.c_str());

}

void create_bottom_container(Track * track) {
    bottom_container = lv_obj_create(main_screen);
    lv_obj_set_size(bottom_container, chartWidth, 158);
    lv_obj_set_pos(bottom_container, 0, bottomContainerPos);
    lv_obj_set_style_bg_color(bottom_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(bottom_container, 0, 0);
    lv_obj_set_style_pad_all(bottom_container, 0, 0);
    lv_obj_set_style_border_width(bottom_container, 0, 0);
    lv_obj_set_style_pad_all(bottom_container, 0, 0);
    lv_obj_set_style_pad_row(bottom_container, 0, 0);
    lv_obj_set_style_pad_column(bottom_container, 0, 0);
    lv_obj_set_style_pad_gap(bottom_container, 0, 0);
    lv_obj_clear_flag(bottom_container, LV_OBJ_FLAG_SCROLLABLE);

    // Static waveform container
    lv_obj_t *static_wave_container = lv_obj_create(bottom_container);
    lv_obj_set_size(static_wave_container, chartWidth, overviewChartHeight);
    lv_obj_set_pos(static_wave_container, 0, 0);
    lv_obj_set_style_bg_color(static_wave_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(static_wave_container, 1, 0);
    lv_obj_set_style_border_color(static_wave_container, lv_color_white(), 0);
    lv_obj_set_style_radius(static_wave_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(static_wave_container, LV_OBJ_FLAG_SCROLLABLE);

    // Static waveform canvas
    static_waveform_canvas = lv_canvas_create(static_wave_container);
    lv_canvas_set_buffer(static_waveform_canvas, (lv_color_t *)overviewCanvasBuffer, chartWidth, overviewChartHeight, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(static_waveform_canvas, chartWidth, overviewChartHeight);
    lv_obj_center(static_waveform_canvas);
    lv_obj_clear_flag(static_waveform_canvas, LV_OBJ_FLAG_SCROLLABLE);
    //lv_obj_clear_flag(static_waveform_canvas, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);

    Serial.printf("path to anlz: %s\n", track->anlz_ex2_path);
    String correctedPath = String(track->anlz_ex2_path);
    if (correctedPath.startsWith("Y/")) {
        correctedPath = correctedPath.substring(2);  // Remove "Y/"
    }
    
    
    uint16_t err = extractPreviewWaveform(correctedPath.c_str(), overViewWaveSampleData);
    //uint16_t err = extractPreviewWaveform(track->anlz_ex2_path, overViewWaveSampleData);
    if (err == ANLZ_OK) {
        Serial.println("Preview loaded!");
    }
    else {
        Serial.printf("Error loading preview waveform: %d\n", err);
    }
    
  //lv_canvas_fill_bg(static_waveform_canvas, COLOR_BG, LV_OPA_COVER);
  drawOverviewCanvas();
    //lv_obj_invalidate(static_waveform_canvas);

    
   


    // Cue buttons container
    /*
    lv_obj_t *cue_container = lv_obj_create(bottom_container);
    lv_obj_set_size(cue_container, SCREEN_WIDTH, 94);
    lv_obj_set_pos(cue_container, 0, 90);
    lv_obj_set_style_bg_opa(cue_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cue_container, 0, 0);
    lv_obj_set_style_pad_all(cue_container, 0, 0);
    lv_obj_clear_flag(cue_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create 8 cue buttons (A-H)
    for (int i = 0; i < 8; i++) {
        cue_buttons[i] = lv_btn_create(cue_container);
        lv_obj_set_size(cue_buttons[i], 80, 50);
        lv_obj_set_pos(cue_buttons[i], i * 100 + 10, 5);
        lv_obj_set_style_bg_color(cue_buttons[i], cue_colors[i], 0);
        lv_obj_set_style_border_width(cue_buttons[i], 0, 0);
        lv_obj_set_style_radius(cue_buttons[i], 8, 0);
        lv_obj_clear_flag(cue_buttons[i], LV_OBJ_FLAG_SCROLLABLE);

        // Button label
        lv_obj_t *btn_label = lv_label_create(cue_buttons[i]);
        char btn_text[2] = {'A' + i, '\0'};
        lv_label_set_text(btn_label, btn_text);
        lv_obj_set_style_text_color(btn_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(btn_label, &exo2_20, 0);
        lv_obj_center(btn_label);
    }
        */
}

uint8_t findBestRefreshRate(uint16_t samplesPerWavepoint, uint8_t minHz = 30, uint8_t maxHz = 68) {
  const float sampleRate = 44100.0;
  float pixelsPerSecond = sampleRate / samplesPerWavepoint;
  
  uint8_t bestHz = 60;
  float bestError = 999.0;
  
  // Check each integer Hz value
  for (uint8_t hz = minHz; hz <= maxHz; hz++) {
    float pixelsPerFrame = pixelsPerSecond / hz;
    float error = fabs(pixelsPerFrame - round(pixelsPerFrame));
    
    if (error <= bestError) {
      bestError = error;
      bestHz = hz;
    }
  }
  
  float pixelsPerFrame = pixelsPerSecond / bestHz;
  Serial.printf("Best match: %d Hz -> %.2f pixels/frame (error: %.4f)\n", bestHz, pixelsPerFrame, bestError);
  
  return bestHz;
}

void dj_ui_init(Track * track) {
    // Create main screen
    main_screen = lv_obj_create(NULL);
    lv_obj_set_size(main_screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(main_screen, COLOR_BG, 0);
    lv_obj_set_style_border_width(main_screen, 0, 0);
    lv_obj_set_style_pad_all(main_screen, 0, 0);
    lv_obj_set_style_pad_row(main_screen, 0, 0);
    lv_obj_set_style_pad_column(main_screen, 0, 0);
    lv_obj_set_style_pad_gap(main_screen, 0, 0);
    lv_obj_set_style_radius(main_screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);
    
  

  
    // Create all containers
    Serial.printf("anlz path: %s\n", track->anlz_path);
    loadTrackData(track->anlz_path);
    create_top_container(track);
    create_middle_container(track);
    Serial.println("Creating bottom container...");
    create_bottom_container(track); 
    PreviousPhase = 0xFFFF;
    PreviousPositionDW = UINT32_MAX;
    bars = 0; 
    RedrawWaveforms(500);

    //PXP_overlay_buffer((uint16_t*)dynamicCanvasBuffer, 2, SCREEN_WIDTH, 164);
    //PXP_overlay_position(0, 158, 799, 321);

    char full_path[256];  // Adjust size as needed

    // Combine folder + filename



     //const char * fName = "mixxx-export/86 - raise_your_hands.wav"; // track->path
#if defined(RDI_DEVELOPMENTS_REV3)
    playFile.open(full_path, FILE_READ);
#else
    playFile = SD.open(track->audio_path, FILE_READ);
#endif
    if (!playFile) {
      Serial.printf("Failed trying to open file: %s\n", full_path);
    }
    else{
      Serial.printf("Opened audio file: %s\n", full_path);
      is_playing = true;
      playFile.seek(44);
      audio.startI2SInterrupt();
      
      track_play_now = track->id;
      pitch = 0;	
	    play_enable = 0;
      play_adr = 0;	
      slip_play_adr = 0;
      loop_active = 0;
      LOOP_OUT = 0;
    }
}

// Update functions for dynamic content

void update_track_info(const char *title, const char *artist, int bpm, const char *key) {
    lv_label_set_text(title_label, title);
    lv_label_set_text(artist_label, artist);
    lv_label_set_text_fmt(bpm_label, "%d", 128);
    lv_label_set_text(key_label, key);
}

void update_time_info(const char *time, int bars, float beat_pos) {
    lv_label_set_text(time_label, time);
    lv_label_set_text_fmt(bar_count_label, "%d || %.2f", bars, beat_pos);
}

void update_progress_bars(int active_bar) {
    for (int i = 0; i < 4; i++) {
        if (i <= active_bar) {
            lv_obj_set_style_bg_color(progress_bars[i], COLOR_PROGRESS, 0);
        } else {
            lv_obj_set_style_bg_color(progress_bars[i], COLOR_TEXT_SECONDARY, 0);
        }
    }
}

FASTRUN void drawFastVLine16Bit(uint16_t x, uint16_t y, uint16_t h, uint16_t color, uint16_t * buffer, uint16_t stride)
{
    uint16_t *p = buffer + y * stride + x;
    for (int i = 0; i < h; ++i)
    {
        *p = color;
        p += stride; // move one row down
    }
}

FASTRUN void drawFastVLine16BitOverview(uint16_t x, uint16_t y, uint16_t h, uint16_t color, uint16_t * buffer, uint16_t stride)
{
    uint16_t *p = buffer + y * stride + x;
    for (int i = 0; i < h; ++i)
    {
        *p = color;
        p += stride; // move one row down
    }
}

FASTRUN uint16_t fastBlend( uint32_t fg, uint32_t bg, uint8_t opa)
{
    //Dont blend if canvas background, or opa is max
    if ((bg == 0x0000) || (fg == 0x0000) || (opa == 0xFF)) {
      return fg;
    }

    opa = ( opa + 4 ) >> 3;
    bg = (bg | (bg << 16)) & 0b00000111111000001111100000011111;
    fg = (fg | (fg << 16)) & 0b00000111111000001111100000011111;
    uint32_t result = ((((fg - bg) * opa) >> 5) + bg) & 0b00000111111000001111100000011111;
    return (uint16_t)((result >> 16) | result);
}

FASTRUN void drawSlope16Bit(uint16_t * buf, uint8_t p1, uint8_t p2, uint16_t x, uint16_t color, uint8_t opa)
{
  if (opa > 0) {
    //Calculate increment, simple lerp between two points
    float delta = (p2 - p1) / slopePoints;

    for (uint16_t i = 0; i < slopePoints; i++) {
      uint8_t height = p1 + (i * delta);
      drawFastVLine16Bit((x * slopePoints) + i, (chartHeight - height) / 2, height, color, buf, chartWidth);
    }
  }
}

void RedrawWaveforms(uint32_t position){

	if(position>all_long)
		{
		return;	
		}
	uint32_t clock_pos;	

	if(REMAIN_ENABLE)
		{
		clock_pos = all_long - position;	
		}	
	else
		{
		clock_pos	= position;
		}


    static uint32_t prev_clock_pos = UINT32_MAX;
    if (clock_pos != prev_clock_pos) {
    prev_clock_pos = clock_pos;

    char time_str[16];
        uint32_t min10  = (clock_pos / 90000) % 10;
        uint32_t min1   = (clock_pos / 9000) % 10;
        uint32_t sec10  = (clock_pos / 1500) % 6;
        uint32_t sec1   = (clock_pos / 150) % 10;
        uint32_t frm10  = ((clock_pos / 2) % 75) / 10;
        uint32_t frm1   = ((clock_pos / 2) % 75) % 10;
        uint32_t hf     = (clock_pos % 2) * 5;

        lv_snprintf(time_str, sizeof(time_str), "%s%lu%lu:%lu%lu:%lu%lu.%lu",
                    REMAIN_ENABLE ? "-" : "",
                    min10, min1, sec10, sec1, frm10, frm1, hf);
        lv_label_set_text(time_label, time_str);
    }
    /*
    if(needle_enable || (Tbuffer[23]&0x20))						//detecting touch on sensor or touch on jog 
		{
		if(RED_VERTICAL_LINE==0)
			{
			RED_VERTICAL_LINE = 1;	
			forcibly_redraw = 1;	
			}
		}
	else
		{
		if(RED_VERTICAL_LINE)
			{
			RED_VERTICAL_LINE = 0;	
			forcibly_redraw = 1;	
			}
		}

    */
   //updateOverviewWaveform((position*799)/all_long);
   position = position/DynamicWaveformZOOM;

   if(position != PreviousPositionDW ) { // ||forcibly_redraw==1)
        PreviousPositionDW = position;
        updateDynamicWaveform(position);
        ShowPhaseMeter(bars);
        if(originalBPM != BPMGRID[bars])										//������� �������� �� ������� (dSHOW==WAVEFORM)!!!
				{
				originalBPM = BPMGRID[bars];
				tempo_need_update = 2;		
				}	
   } //

}



uint32_t nextLabelMs = 0;
/*
FASTRUN void updateDynamicWaveform(uint32_t waveformOffset)
{
  if (dynamicBufferReady == true) {
    return;
  }
  
  // Start time for stats
  //appStats.start(DYNAMIC_RENDER);
  
  //Clear canvas
  //memset(dynamicCanvasBuffer, 0, chartWidth * chartHeight * 2);
  
  uint32_t pos = (waveformOffset / baseSampPerWavePoint) / DynamicWaveformZOOM; 
  //If zoom is bigger than 1, need to find the highest value in between each jump
  //Serial.printf("Waveform offset: %d, pos: %d sample count: %d \n", waveformOffset, pos, sampleCount);

  //Draw waveforms - expanded, interpolated
  const uint16_t chartHeightHalf = chartHeight / 2;

  for (uint16_t x = 0; x < chartWidth; x++) {
    // Clear single line here, rather than memset whole canvas - no performance hit and later, this becomes loop color, etc
    drawFastVLine16Bit(x, 0, chartHeight, 0x00, dynamicCanvasBuffer, chartWidth);

    int64_t index = DynamicWaveformZOOM * (x + pos - (chartWidth / 2));
    //if (index < 0 || index >= all_long) continue;
    
    //for (uint8_t i = 2; i > 0; i--) {
     //   uint8_t sampleValue = (uint8_t)(dynamicWaveSampleData[i][index]);
      //  drawFastVLine16Bit(x, (chartHeightHalf - (sampleValue >> 1)), sampleValue, waveformColors[i], dynamicCanvasBuffer, chartWidth);

    //}
    
    drawFastVLine16Bit(x, (chartHeightHalf - (dynamicWaveSampleData[0][index] >> 1)), dynamicWaveSampleData[0][index], waveformColors[0], dynamicCanvasBuffer, chartWidth);
    drawFastVLine16Bit(x, (chartHeightHalf - (dynamicWaveSampleData[1][index] >> 1)), dynamicWaveSampleData[1][index], waveformColors[1], dynamicCanvasBuffer, chartWidth);
    drawFastVLine16Bit(x, (chartHeightHalf - (dynamicWaveSampleData[2][index] >> 1)), dynamicWaveSampleData[2][index], waveformColors[2], dynamicCanvasBuffer, chartWidth);
  }

  

  // Draw horizontal mid-canvas line
  int midOffset = chartWidth * chartHeightHalf; 
  memset((dynamicCanvasBuffer + midOffset), 0xFFFF, chartWidth * 2);

  // Draw vertical mid-canvas play head line
  drawFastVLine16Bit(chartWidth / 2, 0, chartHeight, col_white, dynamicCanvasBuffer, chartWidth);
  lv_obj_invalidate(daynamic_waveform_canvas);
  //memcpy(lcdBuffer1+(800*158),dynamicCanvasBuffer,(800*164*2));
  //arm_dcache_flush_delete((uint16_t*)dynamicCanvasBuffer, chartWidth * chartHeight * 2);
  //PXP_process();

  if (millis() > nextLabelMs) {
    //lv_label_set_text_fmt(time_label, "%ld", play_adr);
    nextLabelMs = millis() + 90;
  }

  //dynamicBufferReady = true;
 
  // Finish stats
  //appStats.end(DYNAMIC_RENDER);
}
*/

void ShowPhaseMeter(uint16_t phase)
{
    if (PreviousPhase == phase) {
        return;
    }

    uint16_t int_offset;

    if (phase == 0xFFFF) {
        // Reset state - all bars to default gradient, show "--.-"
        for (int i = 0; i < 4; i++) {
            lv_obj_set_style_bg_color(progress_bars[i], lv_color_hex(0x8080FF), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(progress_bars[i], LV_OPA_60, LV_PART_MAIN);
            lv_obj_set_style_border_color(progress_bars[i], lv_color_hex(0xA6C8FF), LV_PART_MAIN);
        }
        lv_label_set_text(bar_count_label, "--.-");
    }
    else {
        int_offset = (( GRID_OFFSET-1 ) & 0x0003);

        // Reset previous active bar back to default
        uint8_t prev_bar = (PreviousPhase + int_offset) % 4;
        lv_obj_set_style_bg_opa(progress_bars[prev_bar], LV_OPA_60, LV_PART_MAIN);
        lv_obj_set_style_bg_color(progress_bars[prev_bar], lv_color_hex(0x8080FF), LV_PART_MAIN);
        lv_obj_set_style_border_color(progress_bars[prev_bar], lv_color_hex(0xA6C8FF), LV_PART_MAIN);

        int_offset += phase;
        uint8_t active_bar = int_offset % 4;
        //Serial.printf("Phase: %d, Int offset: %d, Active bar: %d\n", phase, int_offset, active_bar);

        if (active_bar == 0) {
            // Beat 1 - RED highlight
            lv_obj_set_style_bg_color(progress_bars[active_bar], lv_color_hex(0xFF4040), LV_PART_MAIN);
        } else {
            // Other beats - BLUE highlight
            lv_obj_set_style_bg_color(progress_bars[active_bar], lv_color_hex(0xA6C8FF), LV_PART_MAIN);
        }
        lv_obj_set_style_bg_opa(progress_bars[active_bar], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(progress_bars[active_bar], lv_color_hex(0xA6C8FF), LV_PART_MAIN);

        // Update bar/beat label: "BB.B"
        char bar_str[8];
        lv_snprintf(bar_str, sizeof(bar_str), "%lu%lu.%lu",
                    (int_offset / 40) % 10,
                    (int_offset >> 2) % 10,
                    (int_offset % 4) + 1);
        lv_label_set_text(bar_count_label, bar_str);
    }

    PreviousPhase = phase;
}

#define LOOP_INACTIVE_COLOR 0x0000 // Green
#define LOOP_ACTIVE_COLOR 0xF800 // Red

FASTRUN void updateDynamicWaveform(uint32_t waveformOffset)
{
  if (dynamicBufferReady == true) {
    return;
  }
  memset(dynamicCanvasBuffer, 0, chartWidth * chartHeight * 2);
  uint32_t pos = waveformOffset;
  const uint16_t chartHeightHalf = chartHeight / 2;
  const uint16_t playHeadX = chartWidth / 2;

  // --- Beat grid index: find starting beat index for visible range ---
  uint16_t beatIdx = 0;
  uint32_t rangeStart = 0;
  if (pos >= playHeadX) {
    rangeStart = DynamicWaveformZOOM * (pos - playHeadX);
    while (beatIdx < beatGridLenth &&
           (BEATGRID[beatIdx] - (BEATGRID[beatIdx] % DynamicWaveformZOOM)) < rangeStart) {
      beatIdx++;
    }
  }
  uint16_t beatX = 0; // local beat counter within visible range

  for (uint16_t x = 0; x < chartWidth; x++) {
    int64_t index = DynamicWaveformZOOM * (x + pos - playHeadX);

    // --- Determine background color (loop region, default black) ---
    uint16_t bgColor = 0x0000; // black
    if (CUE_ADR < LOOP_OUT) {
      if (index >= 0 && (uint32_t)index >= CUE_ADR && (uint32_t)index < LOOP_OUT) {
        bgColor = loop_active ? LOOP_ACTIVE_COLOR : LOOP_INACTIVE_COLOR;
      }
    }

    // Clear column with background color
    //drawFastVLine16Bit(x, 0, chartHeight, bgColor, dynamicCanvasBuffer, chartWidth);

    if (index < 0 || (uint32_t)index > all_long) continue;
    //if (index < 0 ) continue;

    uint32_t adr = (uint32_t)index;
    /*
    // --- Draw memory cue markers (top, small triangles) ---
    if (number_of_memory_cue_points > 0) {
      for (uint8_t j = 0; j < number_of_memory_cue_points; j++) {
        if ((MEMORY_adr[0][j] - (MEMORY_adr[0][j] % DynamicWaveformZOOM)) == adr) {
          // Draw red marker: small vertical line at top
          drawFastVLine16Bit(x, 0, 6, col_red, dynamicCanvasBuffer, chartWidth);
        }
      }
    }
  
    // --- Draw hot cue markers (top + bottom) ---
    if (number_of_hot_cue_points > 0) {
      for (uint8_t j = 0; j < 3; j++) {
        if (HCUE_adr[0][j] != 0xFFFF) {
          if ((HCUE_adr[0][j] - (HCUE_adr[0][j] % DynamicWaveformZOOM)) == adr) {
            uint16_t hcColor = (HCUE_type[j] & 0x1) ? CUE_COLOR : col_green;
            drawFastVLine16Bit(x, 0, 6, hcColor, dynamicCanvasBuffer, chartWidth);
            drawFastVLine16Bit(x, chartHeight - 6, 6, hcColor, dynamicCanvasBuffer, chartWidth);
          }
        }
      }
    }

    // --- Draw cue marker (bottom) ---
    if ((CUE_ADR - (CUE_ADR % DynamicWaveformZOOM)) == adr) {
      drawFastVLine16Bit(x, chartHeight - 6, 6, CUE_COLOR, dynamicCanvasBuffer, chartWidth);
    }
    */
    // --- Draw beat grid lines (top + bottom gutters) ---
    
        if ((beatIdx + beatX) < beatGridLenth &&
                (BEATGRID[beatIdx + beatX] - (BEATGRID[beatIdx + beatX] % DynamicWaveformZOOM)) == adr) {
            uint16_t beatColor;
            if (((beatIdx + beatX) % 4) == ((GRID_OFFSET) & 0x03)) {
                beatColor = 0xF800; // red downbeat
            } else if (DynamicWaveformZOOM < 8) {
                beatColor = col_white;
            } else {
                beatX++;
                continue;
            } 

            // Full height line
            drawFastVLine16Bit(x, 0, chartHeight, 0x8410 , dynamicCanvasBuffer, chartWidth);

            // Top tick: 6px tall, 2px wide to the right
            if ((x) < chartWidth) {
                drawFastVLine16Bit(x, 0, 16, beatColor, dynamicCanvasBuffer, chartWidth);
            }

            // Bottom tick: 6px tall, 2px wide to the right
            if ((x) < chartWidth) {
                drawFastVLine16Bit(x , chartHeight - 16, 16, beatColor, dynamicCanvasBuffer, chartWidth);
            }

            beatX++;
            }
            if(adr<=all_long){
            // --- Draw 3 waveform bands (back to front so band 0 is on top) --
                drawFastVLine16Bit(x, (chartHeightHalf - (dynamicWaveSampleData[0][index] >> 1)), dynamicWaveSampleData[0][index], waveformColors[0], dynamicCanvasBuffer, chartWidth);
                drawFastVLine16Bit(x, (chartHeightHalf - (dynamicWaveSampleData[1][index] >> 1)), dynamicWaveSampleData[1][index], waveformColors[1], dynamicCanvasBuffer, chartWidth);
                drawFastVLine16Bit(x, (chartHeightHalf - (dynamicWaveSampleData[2][index] >> 1)), dynamicWaveSampleData[2][index], waveformColors[2], dynamicCanvasBuffer, chartWidth);
            }

            // --- Track bar position at play head ---
            if (x == playHeadX) {
            bars = beatIdx + beatX;
            }
    
  }

  // --- Draw horizontal center line ---
  int midOffset = chartWidth * chartHeightHalf;
  memset((dynamicCanvasBuffer + midOffset), 0xFF, chartWidth * 2); // white line

  // --- Draw vertical play head ---
  //uint16_t playHeadColor = RED_VERTICAL_LINE ? col_red : col_white;
  drawFastVLine16Bit(playHeadX, 0, chartHeight, col_white, dynamicCanvasBuffer, chartWidth);
  drawFastVLine16Bit(playHeadX + 1, 0, chartHeight, col_white, dynamicCanvasBuffer, chartWidth);

  lv_obj_invalidate(daynamic_waveform_canvas);

}

// Add these as global/static variables
static uint16_t oldX = 0; // Initialize to invalid position
// New function to update position efficiently

FASTRUN void updatePlaybackPosition_new(uint16_t newX)
{
  // If position changed, if past first pixel (border), if inside last pixel (border), still playing and not already waiting to send
  if((oldX != newX) && (newX > 0) && (newX < (chartWidth - 1) && (end_of_track == 0) && (staticBufferReady == false))) {
    staticBufferReady = true;
    oldStaticBufferX = oldX;
    newStaticBufferX = newX;
    oldX = newX;
  }

  return;
}


void backButton(lv_event_t *e){
    Serial.println("Back button clicked, returning to browser");
    create_dj_browser_ui();
    populate_track_list(all_tracks, track_count);
	lv_scr_load_anim(filesScreen, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}