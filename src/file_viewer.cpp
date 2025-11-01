#include "lvgl.h"
#include "file_viewer.h"
#include "database/db_manager.h"
#include "SD.h"
//#include "waveform.h"
#include "dj_screen.h"
#include "globals.h"
#include "lv_utils.h"

LV_FONT_DECLARE(exo2_16)
LV_FONT_DECLARE(exo2_18)
LV_FONT_DECLARE(exo2_20)
LV_FONT_DECLARE(exo2_24)
LV_FONT_DECLARE(exo2_28)
LV_FONT_DECLARE(exo2_32)

lv_obj_t * filesScreen;

lv_obj_t * add_track_item(lv_obj_t *parent, Track *track);
static void update_scroll(lv_obj_t * obj);
static void scroll_cb(lv_event_t * e);

// These track array indices, not database IDs
static int32_t top_index;      // Index of topmost visible track in array
static int32_t bottom_index;   // Index of bottommost visible track in array
static bool update_scroll_running = false;

// Store the tracks array globally so update_scroll can access it
static Track** g_tracks = nullptr;
static int16_t g_track_count = 0;

#define LIST_WIDTH 800
#define ITEM_HEIGHT 48
#define RESERVED_WIDTH 400

// Color definitions to match the dark theme
#define COLOR_BACKGROUND    lv_color_hex(0x2A2A2A)
#define COLOR_TRACK_BG      lv_color_hex(0x3A3A3A)
#define COLOR_TRACK_HOVER   lv_color_hex(0x4A4A4A)
#define COLOR_WHITE         lv_color_hex(0xFFFFFF)
#define COLOR_GRAY          lv_color_hex(0xB0B0B0)
#define COLOR_BORDER        lv_color_hex(0x555555)

FLASHMEM void createListScreen(Track** tracks, int16_t track_count)
{
    // Store tracks globally for scroll callbacks
    g_tracks = tracks;
    g_track_count = track_count;
    
    static lv_style_t fileScreen_style;
    filesScreen = lv_obj_create(NULL);
    lv_obj_set_size(filesScreen, 800, 480);
    lv_obj_align(filesScreen, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_flex_flow(filesScreen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_opa(filesScreen, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    lv_obj_set_scroll_dir(filesScreen, LV_DIR_VER);
    lv_style_set_border_width(&fileScreen_style, 0);
    lv_style_set_bg_color(&fileScreen_style, COLOR_BACKGROUND);
    lv_obj_add_style(filesScreen, &fileScreen_style, 0);

    // Create header bar
    lv_obj_t * header = lv_obj_create(filesScreen);
    lv_obj_set_size(header, 800, 40);
    lv_obj_set_flex_grow(header, 0); // Don't grow with flex
    
    // Style the header
    lv_obj_set_style_bg_color(header, lv_color_hex(0x404040), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(header, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // Create flex row layout for header
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    
    // Track title header
    lv_obj_t * lbl_track_header = lv_label_create(header);
    lv_label_set_text(lbl_track_header, "Track");
    lv_obj_set_flex_grow(lbl_track_header, 1);
    lv_obj_set_style_text_color(lbl_track_header, COLOR_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_track_header, &exo2_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // Key header
    lv_obj_t * lbl_key_header = lv_label_create(header);
    lv_label_set_text(lbl_key_header, "Key");
    lv_obj_set_style_text_align(lbl_key_header, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(lbl_key_header, 40);
    lv_obj_set_style_text_color(lbl_key_header, COLOR_GRAY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_key_header, &exo2_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // BPM header
    lv_obj_t * lbl_bpm_header = lv_label_create(header);
    lv_label_set_text(lbl_bpm_header, "BPM");
    lv_obj_set_style_text_align(lbl_bpm_header, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(lbl_bpm_header, 60);
    lv_obj_set_style_text_color(lbl_bpm_header, COLOR_GRAY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_bpm_header, &exo2_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Load initial batch of tracks from array
    if (tracks != nullptr && track_count > 0) {
        Serial.printf("Loading initial tracks into list screen (total: %d)\n", track_count);
        
        // Load first visible batch (e.g., first 10-15 tracks)
        int16_t initial_load_count = (track_count < 15) ? track_count : 15;
        
        for (int16_t i = 0; i < initial_load_count; i++) {
            if (tracks[i] != nullptr) {
                add_track_item(filesScreen, tracks[i]);
            } else {
                Serial.printf("Warning: tracks[%d] is null, skipping\n", i);
            }
        }
        
        // Initialize indices (these are array indices, not track IDs)
        top_index = 0;
        bottom_index = initial_load_count - 1;
        
        Serial.printf("Initially loaded tracks [%d-%d]\n", top_index, bottom_index);
    } else {
        Serial.println("No tracks provided or track_count is 0");
        top_index = 0;
        bottom_index = -1;
    }

    lv_obj_update_layout(filesScreen);
    update_scroll(filesScreen);
    lv_obj_add_event_cb(filesScreen, scroll_cb, LV_EVENT_SCROLL, NULL);
}

FLASHMEM void select_track_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        Track * track = (Track *)lv_event_get_user_data(e);
        load_dj_screen_with_track(track);
        // Note: Don't free the track here - it's still in the global array
        // Only free when the entire tracks array is freed
    }
}

FLASHMEM void load_dj_screen_with_track(Track * track)
{
    //waveformView(track);
    dj_ui_init(track);
    lv_scr_load_anim(main_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

void setFlexContainerProperties(lv_obj_t * cont, int32_t pad_row, int32_t pad_col, lv_flex_flow_t flow){
    lv_obj_remove_style_all(cont);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_flex_flow(cont, flow);

    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_pad_hor(cont, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(cont, pad_row, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(cont, pad_col, LV_PART_MAIN | LV_STATE_DEFAULT);
}

lv_obj_t * add_track_item(lv_obj_t *parent, Track *track)
{
    if (!track) {
        Serial.println("Error: null track passed to add_track_item");
        return nullptr;
    }
    
    lv_obj_t * cont_outer = lv_obj_create(parent);
    setFlexContainerProperties(cont_outer, 0, 0, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(cont_outer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cont_outer, select_track_cb, LV_EVENT_CLICKED, track);
    
    // Style outer container
    lv_obj_set_style_bg_color(cont_outer, COLOR_TRACK_BG, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(cont_outer, COLOR_TRACK_HOVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(cont_outer, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(cont_outer, COLOR_BORDER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(cont_outer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(cont_outer, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_gap(cont_outer, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    //Top row
    lv_obj_t * cont_topRow = lv_obj_create(cont_outer);
    setFlexContainerProperties(cont_topRow, 0, 4, LV_FLEX_FLOW_ROW);
    
    // Make top row transparent
    lv_obj_set_style_bg_opa(cont_topRow, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cont_topRow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(cont_topRow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * lbl_title = lv_label_create(cont_topRow);
    lv_label_set_text(lbl_title, track->title);
    lv_obj_set_flex_grow(lbl_title, 1);
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_SCROLL);
    // Style title - larger, white text
    lv_obj_set_style_text_font(lbl_title, &exo2_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl_title, COLOR_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * lbl_key = lv_label_create(cont_topRow);
    lv_label_set_text(lbl_key, (char*)getKey(atoi(track->musical_key)));
    lv_obj_set_style_text_align(lbl_key, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(lbl_key, 40);
    // Style key with color coding
    lv_obj_set_style_text_font(lbl_key, &exo2_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    int key_numeric = atoi(track->musical_key); 
    lv_obj_set_style_text_color(lbl_key, getKeyColor(key_numeric), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * lbl_bpm = lv_label_create(cont_topRow);
    lv_label_set_text_fmt(lbl_bpm, "%.1f", track->bpmAnalyzed); 
    lv_obj_set_style_text_align(lbl_bpm, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(lbl_bpm, 60);
    // Style BPM
    lv_obj_set_style_text_font(lbl_bpm, &exo2_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl_bpm, COLOR_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);

    //Second row
    lv_obj_t * cont_bottomRow = lv_obj_create(cont_outer);
    setFlexContainerProperties(cont_bottomRow, 0, 4, LV_FLEX_FLOW_ROW);
    
    // Make bottom row transparent
    lv_obj_set_style_bg_opa(cont_bottomRow, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cont_bottomRow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(cont_bottomRow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * lbl_artist = lv_label_create(cont_bottomRow);
    lv_label_set_text(lbl_artist, track->artist);
    lv_obj_set_flex_grow(lbl_artist, 1);
    lv_label_set_long_mode(lbl_artist, LV_LABEL_LONG_SCROLL);
    // Style artist - smaller, gray text
    lv_obj_set_style_text_font(lbl_artist, &exo2_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl_artist, COLOR_GRAY, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * lbl_rating = lv_label_create(cont_bottomRow);
    lv_label_set_text_fmt(lbl_rating, "%.*s", track->star_rating, "**");
    lv_obj_set_style_text_align(lbl_rating, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(lbl_rating, 80);
    // Style rating - gold stars
    lv_obj_set_style_text_font(lbl_rating, &exo2_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl_rating, lv_color_hex(0xFFD700), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * lbl_duration = lv_label_create(cont_bottomRow);
    lv_label_set_text(lbl_duration, formatDuration(track->trackLength));
    lv_obj_set_style_text_align(lbl_duration, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(lbl_duration, 60);
    // Style duration
    lv_obj_set_style_text_font(lbl_duration, &exo2_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl_duration, COLOR_GRAY, LV_PART_MAIN | LV_STATE_DEFAULT);

    //Set sizes. Use LV_SIZE_CONTENT for minimum size, or set fixed height for topRow / bottomRow (eg 30)
    lv_obj_set_size(cont_topRow, 800 - 16, LV_SIZE_CONTENT);
    lv_obj_set_size(cont_bottomRow, 800 - 16, LV_SIZE_CONTENT);
    lv_obj_set_size(cont_outer, 800, LV_SIZE_CONTENT);

    return cont_outer;
}

FASTRUN static void update_scroll(lv_obj_t * obj)
{
    if(update_scroll_running) return;
    if(!g_tracks || g_track_count == 0) return;
    
    update_scroll_running = true;

    /* Load items when scrolling down */
    while(bottom_index < g_track_count - 1 && lv_obj_get_scroll_bottom(obj) < 200) {
        bottom_index += 1;
        if (g_tracks[bottom_index] != nullptr) {
            add_track_item(obj, g_tracks[bottom_index]);
            LV_LOG_USER("Loaded bottom track at index: %" PRId32 " (ID: %d)", 
                       bottom_index, g_tracks[bottom_index]->track_id);
        }
        lv_obj_update_layout(obj);
    }

    /* Load items when scrolling up */
    while(top_index > 0 && lv_obj_get_scroll_top(obj) < 200) {  
        top_index -= 1;
        if (g_tracks[top_index] != nullptr) {
            lv_obj_t * new_item = add_track_item(obj, g_tracks[top_index]);
            lv_obj_move_to_index(new_item, 1); // Move to top (after header)
            LV_LOG_USER("Loaded top track at index: %" PRId32 " (ID: %d)", 
                       top_index, g_tracks[top_index]->track_id);
        }
        lv_obj_update_layout(obj);
    }

    /* Delete far items to save memory */
    while(lv_obj_get_scroll_bottom(obj) > 600 && bottom_index > top_index) {
        bottom_index -= 1;
        lv_obj_t * child = lv_obj_get_child(obj, -1);
        if (child) {
            lv_obj_del(child);
            LV_LOG_USER("Deleted bottom track at index: %" PRId32, bottom_index + 1);
        }
        lv_obj_update_layout(obj);
    }
    
    while(lv_obj_get_scroll_top(obj) > 600 && top_index < bottom_index) {
        top_index += 1;
        // Child 0 is the header, child 1 is the first track
        lv_obj_t * child = lv_obj_get_child(obj, 1);
        if (child) {
            lv_obj_del(child);
            LV_LOG_USER("Deleted top track at index: %" PRId32, top_index - 1);
        }
        lv_obj_update_layout(obj);
    }

    update_scroll_running = false;
}

FASTRUN static void scroll_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    update_scroll(obj);
}