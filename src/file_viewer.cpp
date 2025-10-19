#include "lvgl.h"
#include "file_viewer.h"
#include "sqlite3.h"
#include "SD.h"
#include "inflate.h"
//#include "waveform.h"
#include "dj_screen.h"
#include "globals.h"
#include "lv_utils.h"

sqlite3 * mdb;
sqlite3 * pdb;


LV_FONT_DECLARE(exo2_16)
LV_FONT_DECLARE(exo2_18)
LV_FONT_DECLARE(exo2_20)
LV_FONT_DECLARE(exo2_24)
LV_FONT_DECLARE(exo2_28)
LV_FONT_DECLARE(exo2_32)



lv_obj_t * filesScreen;


lv_obj_t * add_track_item(lv_obj_t *parent, int track_id);
static void update_scroll(lv_obj_t * obj);
static void scroll_cb(lv_event_t * e);

static int32_t top_num;
static int32_t bottom_num;
static bool update_scroll_running = false;
int16_t track_count =0;

#define LIST_WIDTH 800
#define ITEM_HEIGHT 48
#define RESERVED_WIDTH 400


int get_track_by_id(sqlite3 *db, int track_id);
int get_track_count(sqlite3 *db);




// Color definitions to match the dark theme
#define COLOR_BACKGROUND    lv_color_hex(0x2A2A2A)
#define COLOR_TRACK_BG      lv_color_hex(0x3A3A3A)
#define COLOR_TRACK_HOVER   lv_color_hex(0x4A4A4A)
#define COLOR_WHITE         lv_color_hex(0xFFFFFF)
#define COLOR_GRAY          lv_color_hex(0xB0B0B0)
#define COLOR_BORDER        lv_color_hex(0x555555)





FLASHMEM void createListScreen(){
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

    // Database operations

    Serial.println("Calling sqlite3_open for m.db");
    int dbOpenResult = sqlite3_open("databases/m.db", &mdb);
    Serial.printf("Result sqlite3_open for m.db: %d\n", dbOpenResult);

    if (dbOpenResult != SQLITE_OK) {
        int extended_err = sqlite3_extended_errcode(mdb);
        fprintf(stderr, "SQL error: %s (Primary Code: %d, Extended Code: %d)\n", sqlite3_errmsg(mdb), dbOpenResult, extended_err);
    }

    track_count = get_track_count(mdb);
    sqlite3_stmt *stmt;
    int first_id = -1;

    const char *sql = "SELECT id FROM Track ORDER BY id ASC LIMIT 1";

    if (sqlite3_prepare_v2(mdb, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            first_id = sqlite3_column_int(stmt, 0);
        }
    }

    sqlite3_finalize(stmt);
    //sqlite3_close(mdb);

    add_track_item(filesScreen, first_id);
    top_num = 1;
    bottom_num = 1;

    lv_obj_update_layout(filesScreen);
    update_scroll(filesScreen);
    lv_obj_add_event_cb(filesScreen, scroll_cb, LV_EVENT_SCROLL, NULL);
}

FLASHMEM void load_track(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        Track * track = (Track *)lv_event_get_user_data(e);
        
        //waveformView(track);
        dj_ui_init(track);
        lv_scr_load_anim(main_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    }
}



// Function to retrieve track data
Track *getTrackData(sqlite3 *db, int track_id) {
    Track *track = (Track *)calloc(1, sizeof(Track));
    if (!track) return NULL;
    Serial.printf("Track ID: %d\n", track_id);

    sqlite3_stmt *stmt;

    // Adjust query to match your schema
    const char *sqlTrack =
        "SELECT length, bpmAnalyzed, filename, path, title, artist, key, rating FROM Track WHERE id = ?";

    if (sqlite3_prepare_v2(db, sqlTrack, -1, &stmt, NULL) != SQLITE_OK) {
        free(track);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return NULL;
    }

    sqlite3_bind_int(stmt, 1, track_id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        // length is stored as integer → convert to string for your struct
        int lenVal = sqlite3_column_int(stmt, 0);
        char buf[164];
        snprintf(buf, sizeof(buf), "%d", lenVal);
        track->trackLength = strdup(buf);

        track->bpmAnalyzed = (float)sqlite3_column_double(stmt, 1);

        const char *filename = (const char *)sqlite3_column_text(stmt, 2);
        const char *path     = (const char *)sqlite3_column_text(stmt, 3);
        const char *title    = (const char *)sqlite3_column_text(stmt, 4);
        const char *artist   = (const char *)sqlite3_column_text(stmt, 5);
        const char *key = (const char *)sqlite3_column_text(stmt, 6);
        uint8_t rating = (uint8_t)sqlite3_column_int(stmt, 7);

        if (filename) track->filename = strdup(filename);
        if (path)     track->path     = strdup(path);
        if (title)    track->title    = strdup(title);
        if (artist)   track->artist   = strdup(artist);
        if (key)      track->musical_key = strdup(key);
        if (rating)   track->star_rating = lookupValue(rating);
        Serial.println("crumble 7");
        
    }
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    track->track_id = track_id;
    // fileType, star_rating, musical_key not present in your table → keep NULL/0

    return track;
}



// Function to freememory
void freeTrack(Track *track) {
    if (!track) return;
    free(track->title);
    free(track->artist);
    free(track->trackLength);
    free(track->fileType);
    free(track->path);
    free(track->filename);
    free(track->musical_key);
    free(track);
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

lv_obj_t * add_track_item(lv_obj_t *parent, int track_id){
    Track *track = getTrackData(mdb, track_id);
    
    lv_obj_t * cont_outer = lv_obj_create(parent);
    setFlexContainerProperties(cont_outer, 0, 0, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(cont_outer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cont_outer, load_track, LV_EVENT_CLICKED, track);
    
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

    printf("Track ID: %d\n", track_id);
    printf("Length: %s\n", track->trackLength);
    printf("BPM Analyzed: %.2f\n", track->bpmAnalyzed);
    printf("Title: %s\n", track->title);
    printf("Artist: %s\n", track->artist);

    return cont_outer;
}

/*
lv_obj_t * add_track_item(lv_obj_t *parent, int track_id) {
    
    
    Track *track = getTrackData(mdb, track_id);
    lv_obj_t *item = lv_obj_create(parent);
    lv_obj_set_size(item, 700, 96);
    //lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    //lv_obj_set_style_pad_all(item, 0, LV_PART_MAIN);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * id = lv_label_create(item);
    //lv_obj_set_size(track_title, 400, 48); // Ensure label is 400px wide
    char id_buf[6];
    snprintf(id_buf, sizeof(id_buf), "%d", track->track_id);
    lv_label_set_text(id, id_buf);
    lv_label_set_long_mode(id, LV_LABEL_LONG_SCROLL_CIRCULAR); // Enables scrolling if text overflows
    lv_obj_set_style_text_align(id, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(id, 0, 24);
    
    

    // Track Title (Top Left)
    lv_obj_t * track_title = lv_label_create(item);
    lv_obj_set_size(track_title, 400, 48); // Ensure label is 400px wide
    lv_label_set_text(track_title, track->title);
    lv_label_set_long_mode(track_title, LV_LABEL_LONG_SCROLL_CIRCULAR); // Enables scrolling if text overflows
    lv_obj_set_style_text_align(track_title, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(track_title, 40, 0);

    // Track BPM (Top Right)
    lv_obj_t * track_bpm = lv_label_create(item);
    lv_obj_set_size(track_bpm, 200, 48); // Ensure label is 400px wide
    char bpm_buf[6];
    snprintf(bpm_buf, sizeof(bpm_buf), "%.2f", track->bpmAnalyzed);
    lv_label_set_text(track_bpm, bpm_buf);
    lv_obj_set_style_text_align(track_bpm, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(track_bpm, 400, 0);

    
    // Track Artist (Bottom Left)
    lv_obj_t * track_artist = lv_label_create(item);
    lv_obj_set_size(track_artist, 400, 48); // Ensure label is 400px wide
    lv_label_set_text(track_artist, track->artist);
    //lv_label_set_long_mode(track_artist, LV_LABEL_LONG_SCROLL_CIRCULAR); // Enables scrolling if text overflows
    lv_obj_set_style_text_align(track_artist, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(track_artist, 40, 48);
    
    
    // Track Length (Bottom Right)
    lv_obj_t * track_length = lv_label_create(item);
    lv_obj_set_size(track_length, 200, 48); // Ensure label is 400px wide
    lv_label_set_text(track_length, track->trackLength);
    lv_obj_set_style_text_align(track_length, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(track_length, 400, 48);
    



    
    // Print the extracted values
    printf("Track ID: %d\n", track_id);
    printf("Length: %s\n", track->trackLength);
    printf("BPM Analyzed: %.2f\n", track->bpmAnalyzed);
    printf("Title: %s\n", track->title);
    printf("Artist: %s\n", track->artist);

    freeTrack(track);

    return item;
    
}
*/



FASTRUN static void update_scroll(lv_obj_t * obj)
{
    if(update_scroll_running) return;
    update_scroll_running = true;

    /* Load items when scrolling down */
    while(bottom_num < track_count && lv_obj_get_scroll_bottom(obj) < 200) {
        bottom_num += 1; 
        add_track_item(obj, bottom_num);
        LV_LOG_USER("Loaded bottom track ID: %" PRId32, bottom_num);
         // ✅move forward
        lv_obj_update_layout(obj);
    }

    /* Load items when scrolling up */
    while(top_num > 1 && lv_obj_get_scroll_top(obj) < 200) {  
        top_num -= 1;  // ✅move backward
        lv_obj_t * new_item = add_track_item(obj, top_num);
        lv_obj_move_to_index(new_item, 0); //move to top
        lv_obj_update_layout(obj);
        LV_LOG_USER("Loaded top track ID: %" PRId32, top_num);
    }

    /* Delete far items to savememory */
    while(lv_obj_get_scroll_bottom(obj) > 600) {
        bottom_num -= 1;
        lv_obj_t * child = lv_obj_get_child(obj, -1);
        lv_obj_del(child);
        lv_obj_update_layout(obj);
        LV_LOG_USER("Deleted bottom track ID: %" PRId32, bottom_num);
    }
    while(lv_obj_get_scroll_top(obj) > 600) {
        top_num += 1;
        lv_obj_t * child = lv_obj_get_child(obj, 0);
        lv_obj_del(child);
        lv_obj_update_layout(obj);
        LV_LOG_USER("Deleted top track ID: %" PRId32, top_num);
    }

    update_scroll_running = false;
}


FASTRUN static void scroll_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    update_scroll(obj);
}


FLASHMEM int get_track_count(sqlite3 *db) {
    sqlite3_stmt *stmt;  // Prepared statement pointer
    int count = 0;

    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM Track;", -1, &stmt, NULL) != SQLITE_OK) {
        Serial.print("SQL error: ");
        Serial.println(sqlite3_errmsg(db));
        return -1;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
        Serial.printf("Total tracks: %d\n", count);
    }

    sqlite3_finalize(stmt);
    return count;
}
