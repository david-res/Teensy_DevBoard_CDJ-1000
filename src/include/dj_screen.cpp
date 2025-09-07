#include "Arduino.h"
#include "dj_screen.h"
#include "imxrt.h"



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
static lv_obj_t *main_screen;
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
static lv_obj_t *waveform_canvas;
static lv_obj_t *static_waveform_canvas;
static lv_obj_t *cue_buttons[8];

static void draw_vertical_line(lv_color_t *buf, int buf_width, int buf_height, int x, int y1, int y2, lv_color_t color);

void create_top_container(void) {
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
    lv_label_set_text(title_label, "RELEASE YOURSELF (ORIGINAL CLUB MIX)");
    lv_obj_set_style_text_color(title_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title_label, &exo2_20, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 5);
    

    // BPM label (right side)
    bpm_label = lv_label_create(title_bpm_container);
    lv_label_set_text(bpm_label, "125");
    lv_obj_set_style_text_color(bpm_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(bpm_label, &exo2_32, 0);
    lv_obj_align(bpm_label, LV_ALIGN_TOP_RIGHT, 0, 0);
   

    // Key label (below BPM)
    key_label = lv_label_create(title_bpm_container);
    lv_label_set_text(key_label, "K");
    lv_obj_set_style_text_color(key_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(key_label, &exo2_20, 0);
    lv_obj_align_to(key_label, bpm_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);


    lv_obj_t *progress_line = lv_obj_create(title_bpm_container);
    lv_obj_set_size(progress_line, 700, 3);
    lv_obj_set_pos(progress_line, 0, 67);
    lv_obj_set_style_bg_color(progress_line, lv_color_hex(0x158EE2), 0);
    lv_obj_set_style_border_width(progress_line, 0, 0);
    
    // Artist label (below title)
    artist_label = lv_label_create(top_container);
    lv_label_set_text(artist_label, "RENE AMESZ");
    lv_obj_set_style_text_color(artist_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(artist_label, &exo2_20, 0);
    lv_obj_set_pos(artist_label, 0, 40);


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

    // Time label (center)
    time_label = lv_label_create(info_container);
    lv_label_set_text(time_label, "03:00.4");
    lv_obj_set_style_text_color(time_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(time_label, &exo2_28, 0);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, 0);
   

    // BPM info (right section)
    current_bpm_label = lv_label_create(info_container);
    lv_label_set_text(current_bpm_label, "125");
    lv_obj_set_style_text_color(current_bpm_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(current_bpm_label, &exo2_24, 0);
    lv_obj_align(current_bpm_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    // Additional tempo info labels can be added here
}

void create_middle_container(void) {
    middle_container = lv_obj_create(main_screen);
    lv_obj_set_size(middle_container, SCREEN_WIDTH, 160);
    lv_obj_set_pos(middle_container, 0, 160);
    lv_obj_set_style_bg_color(middle_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(middle_container, 1, 0);
    lv_obj_set_style_pad_all(middle_container, 10, 0);
    lv_obj_set_style_border_color(middle_container, lv_color_white(), 0);
    lv_obj_set_style_radius(middle_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(middle_container, LV_OBJ_FLAG_SCROLLABLE);
    

    // Dynamic waveform canvas
    waveform_canvas = lv_canvas_create(middle_container);
    lv_obj_set_size(waveform_canvas, SCREEN_WIDTH - 20, 140);
    lv_obj_center(waveform_canvas);
    lv_obj_clear_flag(waveform_canvas, LV_OBJ_FLAG_SCROLLABLE);

    // Create canvas buffer (16-bit RGB565)
    EXTMEM static lv_color_t canvas_buf[800 * 160];
    lv_canvas_set_buffer(waveform_canvas, canvas_buf, 800, 160, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(waveform_canvas, COLOR_BG, LV_OPA_COVER);

    // Draw sample waveform using custom function
    for (int i = 0; i < 800; i += 8) {
        int y_start = 35 - (i % 20);
        int y_end = 35 + (i % 20);
        draw_vertical_line(canvas_buf, 800, 160, i, y_start, y_end, COLOR_WAVEFORM_HIGH);
    }
}

void create_bottom_container(void) {
    bottom_container = lv_obj_create(main_screen);
    lv_obj_set_size(bottom_container, SCREEN_WIDTH, 160);
    lv_obj_set_pos(bottom_container, 0, 320);
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
    lv_obj_set_size(static_wave_container, SCREEN_WIDTH, 80);
    lv_obj_set_pos(static_wave_container, 0, 0);
    lv_obj_set_style_bg_color(static_wave_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(static_wave_container, 1, 0);
    lv_obj_set_style_border_color(static_wave_container, lv_color_white(), 0);
    lv_obj_set_style_radius(static_wave_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(static_wave_container, LV_OBJ_FLAG_SCROLLABLE);

    // Static waveform canvas
    static_waveform_canvas = lv_canvas_create(static_wave_container);
    lv_obj_set_size(static_waveform_canvas, SCREEN_WIDTH, 60);
    lv_obj_center(static_waveform_canvas);
    

    EXTMEM static lv_color_t static_canvas_buf[800 * 60];
    lv_canvas_set_buffer(static_waveform_canvas, static_canvas_buf, 800, 60, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(static_waveform_canvas, COLOR_BG, LV_OPA_COVER);

    // Draw static waveform overview using custom function
    for (int i = 0; i < 800; i += 4) {
        int y_start = 15 - (i % 10);
        int y_end = 15 + (i % 10);
        draw_vertical_line(static_canvas_buf, 800, 60, i, y_start, y_end, COLOR_WAVEFORM_LOW);
    }

    // Cue buttons container
    lv_obj_t *cue_container = lv_obj_create(bottom_container);
    lv_obj_set_size(cue_container, SCREEN_WIDTH, 60);
    lv_obj_set_pos(cue_container, 0, 90);
    lv_obj_set_style_bg_opa(cue_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cue_container, 0, 0);
    lv_obj_set_style_pad_all(cue_container, 0, 0);

    // Create 8 cue buttons (A-H)
    for (int i = 0; i < 8; i++) {
        cue_buttons[i] = lv_btn_create(cue_container);
        lv_obj_set_size(cue_buttons[i], 80, 50);
        lv_obj_set_pos(cue_buttons[i], i * 100 + 10, 5);
        lv_obj_set_style_bg_color(cue_buttons[i], cue_colors[i], 0);
        lv_obj_set_style_border_width(cue_buttons[i], 0, 0);
        lv_obj_set_style_radius(cue_buttons[i], 8, 0);

        // Button label
        lv_obj_t *btn_label = lv_label_create(cue_buttons[i]);
        char btn_text[2] = {'A' + i, '\0'};
        lv_label_set_text(btn_label, btn_text);
        lv_obj_set_style_text_color(btn_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(btn_label, &exo2_20, 0);
        lv_obj_center(btn_label);
    }
}

void dj_ui_init(void) {
    // Create main screen
    main_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(main_screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(main_screen, COLOR_BG, 0);
    lv_obj_set_style_border_width(main_screen, 0, 0);
    lv_obj_set_style_pad_all(main_screen, 0, 0);
    lv_obj_set_style_pad_row(main_screen, 0, 0);
    lv_obj_set_style_pad_column(main_screen, 0, 0);
    lv_obj_set_style_pad_gap(main_screen, 0, 0);
    lv_obj_set_style_radius(main_screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    // Load the screen
    //lv_scr_load(main_screen);

    // Create all containers
    create_top_container();
    create_middle_container();
    create_bottom_container();
}

// Update functions for dynamic content
void update_track_info(const char *title, const char *artist, int bpm, const char *key) {
    lv_label_set_text(title_label, title);
    lv_label_set_text(artist_label, artist);
    lv_label_set_text_fmt(bpm_label, "%d", "128");
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

void draw_vertical_line(lv_color_t *buf, int buf_width, int buf_height, int x, int y1, int y2, lv_color_t color) {
    if (x < 0 || x >= buf_width) return;
    
    // Ensure y1 <= y2
    if (y1 > y2) {
        int temp = y1;
        y1 = y2;
        y2 = temp;
    }
    
    // Clamp y values to buffer bounds
    if (y1 < 0) y1 = 0;
    if (y2 >= buf_height) y2 = buf_height - 1;
    
    // Draw vertical line
    for (int y = y1; y <= y2; y++) {
        buf[y * buf_width + x] = color;
    }
}

// Waveform update function (you'll implement with actual audio data)
void update_waveform(float *audio_data, int data_length) {
    // Clear canvas
    lv_canvas_fill_bg(waveform_canvas, COLOR_BG, LV_OPA_COVER);
    
    // Draw new waveform data
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.width = 2;
    
    for (int i = 0; i < data_length && i < 780; i++) {
        lv_point_t points[2];
        points[0].x = i;
        points[0].y = 70;
        points[1].x = i;
        
        // Use audio data amplitude
        int amplitude = (int)(audio_data[i] * 60);
        points[1].y = 70 + amplitude;
        
        // Color based on amplitude
        if (abs(amplitude) > 30) {
            line_dsc.color = COLOR_WAVEFORM_HIGH;
        } else {
            line_dsc.color = COLOR_WAVEFORM_LOW;
        }
        
        //lv_canvas_draw_line(waveform_canvas, points, 2, &line_dsc);
    }
}