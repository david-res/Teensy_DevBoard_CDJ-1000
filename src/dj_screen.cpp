
#include "dj_screen.h"
#include "../include/device_defines.h"
#include "globals.h"
#include "Arduino.h"
#include "lv_utils.h"
#include "database/db_manager.h"
#include "SD.h"
#include "file_viewer.h"
#include "T4_PXP.h"
#include <SDRAM_t4.h>
#include "DMAChannel.h"
#if defined(USE_BEAT_NUMBERS)
#include "utils/digit_renderer.h"
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
void drawBeatMarkers(uint32_t waveformOffset);
void updateOverviewWaveform(uint32_t waveformOffset);
bool useOpa = false;


uint64_t overviewSampleCount = 0;
uint64_t highResSampleCount = 0;

Beatgrid * beatgrid;


const uint16_t col_blue = 0x135D; //From Rezo, was 0x001F;
const uint16_t col_green = 0x15EA; //From Rezo, was 0x07E0;
const uint16_t col_white = 0xF7DE; //From Rezo, was 0xFFFF;

const uint16_t waveformColors[3] = {col_blue, col_green, col_white};
const float waveformUserGain[3] = {1.0, 0.66, 0.33};

uint8_t * dynamicWaveSampleData[6]; // 0=lo samples, 1=med samples, 2=hi samples
DMAMEM uint16_t dynamicCanvasBuffer[800 * 164];
uint64_t dynamicWaveformSampleCount = 0;
double samplesPerDaynamicPoint = 0;

//To store repeating group data of samples for the overview waveform
uint8_t * overViewWaveSampleData[3];
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

FLASHMEM void drawOverviewCanvas()
{
  //Clear overview canvas
  memset(overviewCanvasBuffer, 0, chartWidth * overviewChartHeight * 2);

  for (uint16_t x = 0; x < chartWidth; x++) {
    for (uint8_t i = 0; i < 3; i++) {
      drawFastVLine16BitOverview(x, overviewChartHeight - (overViewWaveSampleData[i][x]), (overViewWaveSampleData[i][x]), waveformColors[i], overviewCanvasBuffer, chartWidth);
      //Serial.printf("drawing x=%d, height=%d\n", x, (overViewWaveSampleData[i][x]));
    }
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

    // Title label (left side)
    title_label = lv_label_create(title_bpm_container);
    lv_label_set_text(title_label, track->title);
    lv_obj_set_style_text_color(title_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title_label, &exo2_20, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 5);
    lv_obj_clear_flag(title_label, LV_OBJ_FLAG_SCROLLABLE);
    

    // BPM label (right side)
    bpm_label = lv_label_create(title_bpm_container);
    lv_label_set_text_fmt(bpm_label,"%.1f", track->bpmAnalyzed);
    lv_obj_set_style_text_color(bpm_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(bpm_label, &exo2_32, 0);
    lv_obj_align(bpm_label, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_clear_flag(bpm_label, LV_OBJ_FLAG_SCROLLABLE);
   

    // Key label (below BPM)
    key_label = lv_label_create(title_bpm_container);
    lv_label_set_text(key_label, (char*)getKey(atoi(track->musical_key)));
    lv_obj_set_style_text_font(key_label, &exo2_20, 0);
    lv_obj_align_to(key_label, bpm_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    int key_numeric = atoi(track->musical_key); 
    lv_obj_set_style_text_color(key_label, getKeyColor(key_numeric), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(key_label, LV_OBJ_FLAG_SCROLLABLE);


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
        
        if (i < 3) {
            lv_obj_set_style_bg_color(progress_bars[i], COLOR_TEXT_SECONDARY, 0);
        } else {
            lv_obj_set_style_bg_color(progress_bars[i], COLOR_PROGRESS, 0);
        }
        lv_obj_set_style_border_width(progress_bars[i], 0, 0);
    }

    // Bar count label
    bar_count_label = lv_label_create(info_container);
    lv_label_set_text(bar_count_label, "94 || 0.34");
    lv_obj_set_style_text_color(bar_count_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(bar_count_label, &exo2_18, 0);
    lv_obj_set_pos(bar_count_label, 0, 35);
    lv_obj_clear_flag(bar_count_label, LV_OBJ_FLAG_SCROLLABLE);

    // Time label (center)
    time_label = lv_label_create(info_container);
    lv_label_set_text(time_label, "03:00.4");
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

void create_middle_container(void) {
    middle_container = lv_obj_create(main_screen);
    lv_obj_set_size(middle_container, chartWidth, middleContainerPos);
    lv_obj_set_pos(middle_container, 0, middleContainerPos);
    lv_obj_set_style_bg_color(middle_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(middle_container, 0, 0);
    //lv_obj_set_style_pad_all(middle_container, 10, 0);
    lv_obj_set_style_border_color(middle_container, lv_color_white(), 0);
    lv_obj_set_style_radius(middle_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(middle_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(middle_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(middle_container, LV_OBJ_FLAG_IGNORE_LAYOUT);
    

    // Dynamic waveform canvas
    daynamic_waveform_canvas = lv_canvas_create(middle_container);
    lv_obj_set_size(daynamic_waveform_canvas, chartWidth, chartHeight);
    lv_obj_center(daynamic_waveform_canvas);
    lv_obj_clear_flag(daynamic_waveform_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(daynamic_waveform_canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(daynamic_waveform_canvas, LV_OBJ_FLAG_IGNORE_LAYOUT);

    // Create canvas buffer (16-bit RGB565)
    lv_canvas_set_buffer(daynamic_waveform_canvas, dynamicCanvasBuffer, chartWidth, chartHeight, LV_IMG_CF_TRUE_COLOR);
}

void create_bottom_container(void) {
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
    
    db_load_dynamic_waveform_data(track->track_id, dynamicWaveSampleData, &dynamicWaveformSampleCount, (uint32_t*)&baseSampPerWavePoint);
    
    double tempSamplesPerOverviewPoint = 0;
    db_load_overview_waveform_data(track->track_id, overViewWaveSampleData, &overviewSampleCount, &tempSamplesPerOverviewPoint, overviewChartHeight);
    
    beatgrid = (Beatgrid*)malloc(sizeof(Beatgrid));
    
    db_load_beatgrid_data(track->track_id, beatgrid, baseSampPerWavePoint, (uint32_t*)&all_long);
  
    // Create all containers
    create_top_container(track);
    create_middle_container();
    create_bottom_container();

    //PXP_overlay_buffer((uint16_t*)dynamicCanvasBuffer, 2, SCREEN_WIDTH, 164);
    //PXP_overlay_position(0, 158, 799, 321);

    char full_path[256];  // Adjust size as needed

    // Combine folder + filename
    snprintf(full_path, sizeof(full_path), "mixxx-export/%s", track->filename);


     //const char * fName = "mixxx-export/86 - raise_your_hands.wav"; // track->path
#if defined(RDI_DEVELOPMENTS_REV3)
    playFile.open(full_path, FILE_READ);
#else
    playFile = SD.open(full_path, FILE_READ);
#endif
    if (!playFile) {
      Serial.printf("Failed trying to open file: %s\n", full_path);
    }
    else{
      Serial.printf("Opened audio file: %s\n", full_path);
      is_playing = true;
      playFile.seek(44);
      audio.startI2SInterrupt();
      updateDynamicWaveform(0); 
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
  if (h <= 0) return;
  if(y+h >= chartHeight ) return;
    uint16_t *p = buffer + y * stride + x;
    for (int i = 0; i < h; ++i)
    {
        *p = color;
        p += stride; // move one row down
    }
}

FASTRUN void drawFastVLine16BitOverview(uint16_t x, uint16_t y, uint16_t h, uint16_t color, uint16_t * buffer, uint16_t stride)
{
  if (h <= 0) return;
  //if(y+h >= overviewChartHeight ) return;
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

int DynamicWaveformZOOM = 1;
uint32_t nextLabelMs = 0;
FASTRUN void updateDynamicWaveform(uint32_t waveformOffset)
{
  if (dynamicBufferReady == true) {
    return;
  }
  
  // Start time for stats
  appStats.start(DYNAMIC_RENDER);
  
  //Clear canvas
  //memset(dynamicCanvasBuffer, 0, chartWidth * chartHeight * 2);
  
  uint32_t pos = (waveformOffset / baseSampPerWavePoint) / DynamicWaveformZOOM; 
  /*If zoom is bigger than 1, need to find the highest value in between each jump*/
  //Serial.printf("Waveform offset: %d, pos: %d sample count: %d \n", waveformOffset, pos, sampleCount);

  //Draw waveforms - expanded, interpolated
  const uint16_t chartHeightHalf = chartHeight / 2;

  for (uint16_t x = 0; x < chartWidth; x++) {
    // Clear single line here, rather than memset whole canvas - no performance hit and later, this becomes loop color, etc
    drawFastVLine16Bit(x, 0, chartHeight - 1, 0x00, dynamicCanvasBuffer, chartWidth);

    int64_t index = DynamicWaveformZOOM * (x + pos - (chartWidth / 2));
    if (index < 0 || index >= all_long) continue;
    for (uint8_t i = 0; i < 3; i++) {
        uint8_t sampleValue = (uint8_t)(dynamicWaveSampleData[i][index]);
        drawFastVLine16Bit(x, (chartHeightHalf - (sampleValue >> 1)), sampleValue, waveformColors[i], dynamicCanvasBuffer, chartWidth);
    }
  }
  // Draw horizontal mid-canvas line
  int midOffset = chartWidth * chartHeightHalf; 
  memset((dynamicCanvasBuffer + midOffset), 0xFFFF, chartWidth * 2);
  // Draw vertical mid-canvas play head line
  drawFastVLine16Bit(chartWidth / 2, 0, chartHeight - 1, col_white, dynamicCanvasBuffer, chartWidth);
  //lv_obj_invalidate(daynamic_waveform_canvas);
  //memcpy(lcdBuffer1+(800*158),dynamicCanvasBuffer,(800*164*2));
  //arm_dcache_flush_delete((uint16_t*)dynamicCanvasBuffer, chartWidth * chartHeight * 2);
  //PXP_process();

  // Draw beat grid
  if (beatgrid && beatgrid->markerCount > 0) {
    drawBeatMarkers(waveformOffset);
  }

  if (millis() > nextLabelMs) {
    lv_label_set_text_fmt(time_label, "%ld", play_adr);
    nextLabelMs = millis() + 90;
  }

  dynamicBufferReady = true;
 
  // Finish stats
  appStats.end(DYNAMIC_RENDER);
}

// TODO these 'wobble' as they scroll, due to float math being rounded. Apply fix
FASTRUN void drawBeatMarkers(uint32_t waveformOffset) {
  appStats.start(BEAT_GRID_RENDER);
    // TODO check if these are correct values to use
    uint16_t beatCount = beatgrid->markers[0].beatsUntilNext;
    float firstSample = beatgrid->markers[0].sampleOffset;
    float samplesPerBeat = (beatgrid->markers[beatgrid->markerCount - 1].sampleOffset - firstSample) / beatCount;
    
    // waveformOffset is in the center sample on screen, so calculate left most sample index
    int64_t centerSample = waveformOffset;
    int64_t leftmostSample = centerSample - ((chartWidth / 2) * baseSampPerWavePoint * DynamicWaveformZOOM);
    
    // Beat grid starts 4 beats before the first sample (I think?)
    float beatZeroSample = firstSample - (4 * samplesPerBeat);
    
    // Calculate which beat number would be at the left edge of the screen
    int16_t firstVisibleBeat = (int16_t)((leftmostSample - beatZeroSample) / samplesPerBeat);
    if (firstVisibleBeat < -4) firstVisibleBeat = -4;
    
    const uint16_t white_190 = 0xBDF7;
    const uint16_t tickHeight = 10;
    const uint16_t tickBottomStart = chartHeight - tickHeight - 1;
    const float sampleToPixel = 1.0 / (float)(baseSampPerWavePoint * DynamicWaveformZOOM);
    const int16_t halfWidth = chartWidth / 2;
    
    // Draw beats starting from the first visible one
    for (int16_t beat = firstVisibleBeat; beat <= beatCount + 4; beat++) {
      // Calculate beat position in pixels from center
      int16_t beatX = (int16_t)(((beatZeroSample + (beat * samplesPerBeat) - centerSample) * sampleToPixel) + halfWidth);
      
      // Stop early, if we've gone past the right edge
      if (beatX >= chartWidth - 1) break;

      // Draw beat line with bounds checking (need room for ±1 pixel)
      if (beatX >= 1) {
        uint16_t tickColor = (beat % 4 == 0) ? 0xF800 : white_190;
        
        // Draw top tick (3 pixels wide)
        drawFastVLine16Bit(beatX - 1, 0, tickHeight, tickColor, dynamicCanvasBuffer, chartWidth);
        drawFastVLine16Bit(beatX, 0, tickHeight, tickColor, dynamicCanvasBuffer, chartWidth);
        drawFastVLine16Bit(beatX + 1, 0, tickHeight, tickColor, dynamicCanvasBuffer, chartWidth);
        
        // Draw middle section (1 pixel wide, dim white)
        drawFastVLine16Bit(beatX, tickHeight, chartHeight - (2 * tickHeight), white_190, dynamicCanvasBuffer, chartWidth);
        
        // Draw bottom tick (3 pixels wide)
        drawFastVLine16Bit(beatX - 1, tickBottomStart, tickHeight, tickColor, dynamicCanvasBuffer, chartWidth);
        drawFastVLine16Bit(beatX, tickBottomStart, tickHeight, tickColor, dynamicCanvasBuffer, chartWidth);
        drawFastVLine16Bit(beatX + 1, tickBottomStart, tickHeight, tickColor, dynamicCanvasBuffer, chartWidth);

#if defined(USE_BEAT_NUMBERS)
        // Draw beat numbers
        if (beat % 4 == 0) {
          
          uint16_t beatVal = beat / 4;
          int16_t beatDigitOffset = beatX - (beatVal < 10 ? DIGIT_WIDTH : DIGIT_WIDTH * 2);
          if (beatDigitOffset > 0) {
            appStats.start(BEAT_DIGIT_RENDER);
            blit_number_to_canvas(dynamicCanvasBuffer, chartWidth, beatDigitOffset, chartHeight - 1 - DIGIT_HEIGHT, beatVal);
            appStats.end(BEAT_DIGIT_RENDER);
          }
        }
#endif        
      }
    }
    appStats.end(BEAT_GRID_RENDER);
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