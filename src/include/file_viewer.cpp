#include "lvgl.h"
#include "file_viewer.h"
#include "sqlite3.h"
#include "SD.h"
#include "inflate.h"

sqlite3 *mdb;

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
} Track;

struct KeyInfo {
    uint8_t numericValue;
    const char* key;
};

/*
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
*/

constexpr KeyInfo keyLookup[] = {
    {0, "#ee82d9 C"},
    {1, "#f2abe4 Am"},
    {2, "#ce8fff G"},
    {3, "#ddb4fd Em"},
    {4, "#9fb6ff D"},
    {5, "#becdfd Bm"},
    {6, "#56d9f9 A"},
    {7, "#8ee4f9 F#m"},
    {8, "#00ebeb E"},
    {9, "#55f0f0 Dbm"},
    {10, "#01edca B"},
    {11, "#56f1da Abm"},
    {12, "#3cee81 F#"},
    {13, "#7df2aa Ebm"},
    {14, "#86f24f Db"},
    {15, "#aef589 Bbm"},
    {16, "#dfca73 Ab"},
    {17, "#e8daa1 Fm"},
    {18, "#ffa07c Eb"},
    {19, "#fdbfa7 Cm"},
    {20, "#ff8894 Bb"},
    {21, "#fdafb7 Gm"},
    {22, "#ff81b4 F"},
    {23, "#fdaacc Dm"}
};


FASTRUN const char* getKey(uint8_t numericValue) {
    if (numericValue < sizeof(keyLookup) / sizeof(keyLookup[0])) {
        return keyLookup[numericValue].key;
    }
    return "Invalid Key";
}

constexpr uint8_t lookupValue(uint8_t input) {
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


FLASHMEM void createListScreen(){
    static lv_style_t fileScreen_style;
    lv_obj_t * filesScreen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(filesScreen, 700, 480);
    lv_obj_align(filesScreen, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_flex_flow(filesScreen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_opa(filesScreen, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_scrollbar_mode(filesScreen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(filesScreen, LV_DIR_VER);
    lv_style_set_border_width(&fileScreen_style, 0);
    lv_style_set_bg_color(&fileScreen_style, lv_color_white());
    lv_obj_add_style(filesScreen, &fileScreen_style, 0);


    sqlite3_open("Engine Library/m.db", &mdb);
    track_count = get_track_count(mdb);

    add_track_item(filesScreen, 1);
    top_num = 1;
    bottom_num = 1;

    lv_obj_update_layout(filesScreen);
    update_scroll(filesScreen);
    lv_obj_add_event_cb(filesScreen, scroll_cb, LV_EVENT_SCROLL, NULL);
    

}

FLASHMEM void show_user_data(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        // Clear the current screen (transition to an empty screen)
        //lv_scr_load(NULL);

        // Create a new screen
        lv_obj_t * new_screen = lv_obj_create(NULL);  // Create a new screen (parent is NULL)
        lv_obj_set_size(new_screen, 800, 480);
        //lv_scr_load(new_screen);  // Load the new screen


        // Create a label to display the data
        lv_obj_t *label = lv_label_create(new_screen);
        lv_label_set_text(label, "1");  // Set the label text to display the user data
        lv_obj_align(label,  LV_ALIGN_CENTER, 0, 0);  // Align the label to the center

        lv_screen_load_anim(new_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    }
}



// Function to retrieve track data
Track *getTrackData(sqlite3 *db, int track_id) {
    Track *track = (Track *)calloc(1, sizeof(Track));  // Zero-initialized struct
    if (!track) return NULL;  //memory allocation failure
    track->track_id = track_id;

    sqlite3_stmt *stmt;
    
    // QuerymetaData for specific type values
    const char *sqlMeta = "SELECT text, type FROM metaData WHERE id = ? AND type IN (1, 2, 10, 13)";

    if (sqlite3_prepare_v2(db, sqlMeta, -1, &stmt, NULL) != SQLITE_OK) {
        free(track);
        return NULL;
    }

    sqlite3_bind_int(stmt, 1, track_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *data = (const char *)sqlite3_column_text(stmt, 0);
        int type = sqlite3_column_int(stmt, 1);
        int len = sqlite3_column_bytes(stmt, 0);
        
        if (!data) continue;

        // Allocatememory
        char *copy = (char *)malloc(len + 1);
        if (!copy) continue;
       memcpy(copy, data, len);
        copy[len] = '\0';

        // Assign based on type column
        switch (type) {
            case 1:  track->title = copy; break;
            case 2:  track->artist = copy; break;
            case 10: track->trackLength = copy; break;
            case 13: track->fileType = copy; break;
            default: free(copy); break; // Should not happen, but safe practice
        }
    }
    sqlite3_finalize(stmt);

    // Query Track table
    const char *sqlTrack = "SELECT path, bpmAnalyzed, filename FROM Track WHERE id = ?";
    if (sqlite3_prepare_v2(db, sqlTrack, -1, &stmt, NULL) != SQLITE_OK) {
        free(track);
        return NULL;
    }

    sqlite3_bind_int(stmt, 1, track_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *path = (const char *)sqlite3_column_text(stmt, 0);
        track->bpmAnalyzed = (float)sqlite3_column_double(stmt, 1);
        const char *filename = (const char *)sqlite3_column_text(stmt, 2);

        if (path) track->path = strdup(path);
        if (filename) track->filename = strdup(filename);
    }
    sqlite3_finalize(stmt);


    // QuerymetaDataInteger for specific type values
    const char *sqlMetaInteger = "SELECT value, type FROM MetaDataInteger WHERE id = ? AND type IN (4, 5)";

    if (sqlite3_prepare_v2(db, sqlMetaInteger, -1, &stmt, NULL) != SQLITE_OK) {
        free(track);
        return NULL;
    }

    sqlite3_bind_int(stmt, 1, track_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *data = (const char *)sqlite3_column_text(stmt, 0);
        int type = sqlite3_column_int(stmt, 1);
        int len = sqlite3_column_bytes(stmt, 0);
        
        if (!data) continue;

        // Allocatememory
        char *copy = (char *)malloc(len + 1);
        if (!copy) continue;
       memcpy(copy, data, len);
        copy[len] = '\0';

        // Assign based on type column
        switch (type) {
            case 4:  track->musical_key = (char*)getKey(atoi(copy)); break;
            case 5:  track->star_rating = lookupValue(atoi(copy)); break;
            default: free(copy); break; // Should not happen, but safe practice
        }
    }
    sqlite3_finalize(stmt);

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

const uint16_t trackContainerWidth = 600;

void setFlexContainerProperties(lv_obj_t * cont, int32_t pad_row, int32_t pad_col, lv_flex_flow_t flow){
    lv_obj_remove_style_all(cont);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_flex_flow(cont, flow);

    lv_obj_remove_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_pad_hor(cont, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(cont, pad_row, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(cont, pad_col, LV_PART_MAIN | LV_STATE_DEFAULT);
}

lv_obj_t * add_track_item(lv_obj_t *parent, int track_id){
    Track *track = getTrackData(mdb, track_id);

    lv_obj_t * cont_outer = lv_obj_create(parent);
    setFlexContainerProperties(cont_outer, 0, 0, LV_FLEX_FLOW_COLUMN);

    //Top row
    lv_obj_t * cont_topRow = lv_obj_create(cont_outer);
    setFlexContainerProperties(cont_topRow, 0, 4, LV_FLEX_FLOW_ROW);

    lv_obj_t * lbl_title = lv_label_create(cont_topRow);
    lv_label_set_text(lbl_title, track->title);
    lv_obj_set_flex_grow(lbl_title, 1);
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_SCROLL);

    lv_obj_t * lbl_key = lv_label_create(cont_topRow);
    lv_label_set_recolor(lbl_key, en);
    lv_label_set_text(lbl_key, track->musical_key);
    lv_obj_set_style_text_align(lbl_key, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(lbl_key, 40);

    lv_obj_t * lbl_bpm = lv_label_create(cont_topRow);
    lv_label_set_text_fmt(lbl_bpm, "%.2f", track->bpmAnalyzed);
    lv_obj_set_style_text_align(lbl_bpm, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(lbl_bpm, 60);

    //Second row
    lv_obj_t * cont_bottomRow = lv_obj_create(cont_outer);
    setFlexContainerProperties(cont_bottomRow, 10, 4, LV_FLEX_FLOW_ROW);

    lv_obj_t * lbl_artist = lv_label_create(cont_bottomRow);
    lv_label_set_text(lbl_artist, track->artist);
    lv_obj_set_flex_grow(lbl_artist, 1);
    lv_label_set_long_mode(lbl_artist, LV_LABEL_LONG_SCROLL);

    lv_obj_t * lbl_rating = lv_label_create(cont_bottomRow);
    lv_label_set_text_fmt(lbl_rating, "%.*s", track->star_rating, "********");
    lv_obj_set_style_text_align(lbl_rating, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(lbl_rating, 40);

    lv_obj_t * lbl_duration = lv_label_create(cont_bottomRow);
    lv_label_set_text_fmt(lbl_duration, "%s", track->trackLength);
    lv_obj_set_style_text_align(lbl_duration, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(lbl_duration, 60);

    //Set sizes. Use LV_SIZE_CONTENT forminimum size, or set fixed height for topRow / bottomRow (eg 30)
    lv_obj_set_size(cont_topRow, trackContainerWidth, 30);
    lv_obj_set_size(cont_bottomRow, trackContainerWidth, 30);
    lv_obj_set_size(cont_outer, trackContainerWidth, LV_SIZE_CONTENT);

    //Adds a 1 pixel border around the track container
    lv_obj_set_style_border_color(cont_outer, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cont_outer, 1, LV_PART_MAIN | LV_STATE_DEFAULT);

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
        lv_obj_delete(child);
        lv_obj_update_layout(obj);
        LV_LOG_USER("Deleted bottom track ID: %" PRId32, bottom_num);
    }
    while(lv_obj_get_scroll_top(obj) > 600) {
        top_num += 1;
        lv_obj_t * child = lv_obj_get_child(obj, 0);
        lv_obj_delete(child);
        lv_obj_update_layout(obj);
        LV_LOG_USER("Deleted top track ID: %" PRId32, top_num);
    }

    update_scroll_running = false;
}


FASTRUN static void scroll_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target_obj(e);
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
    }

    sqlite3_finalize(stmt);
    return count;
}
