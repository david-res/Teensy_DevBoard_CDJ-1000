#include "lvgl.h"
#include "file_viewer.h"
#include "SD.h"
#include "dj_screen.h"
#include "globals.h"
#include "lv_utils.h"
#include "utils/jpeg_to_lvgl.h"
//#include "RekordboxParser.h"

LV_FONT_DECLARE(exo2_16)
LV_FONT_DECLARE(exo2_18)
LV_FONT_DECLARE(exo2_20)
LV_FONT_DECLARE(exo2_24)
LV_FONT_DECLARE(exo2_28)
LV_FONT_DECLARE(exo2_32)

// ========== Color Definitions ==========
#define COLOR_BG_MAIN       lv_color_hex(0x1a1a1a)
#define COLOR_BG_SIDEBAR    lv_color_hex(0x0d0d0d)
#define COLOR_BG_TRACK      lv_color_hex(0x2a2a2a)
#define COLOR_TRACK_HOVER   lv_color_hex(0x3a3a3a)
#define COLOR_TRACK_SEL     lv_color_hex(0x4a4a4a)
#define COLOR_BORDER        lv_color_hex(0x404040)
#define COLOR_WHITE         lv_color_hex(0xFFFFFF)
#define COLOR_GRAY          lv_color_hex(0x9a9a9a)
#define COLOR_GRAY_DARK     lv_color_hex(0x606060)
#define COLOR_ACCENT        lv_color_hex(0x00d9ff)

// ========== Layout Dimensions ==========
#define SIDEBAR_WIDTH       80
#define PLAYLIST_WIDTH      150
#define TRACK_LIST_WIDTH    800-(SIDEBAR_WIDTH + PLAYLIST_WIDTH)   // 800 - 80 - 200 = 520px available
#define HEADER_HEIGHT       50
#define TRACK_ITEM_HEIGHT   82


// ========== Forward Declarations ==========
static void create_sidebar(lv_obj_t *parent);
static void create_playlist_panel(lv_obj_t *parent);
static void create_track_list_panel(lv_obj_t *parent);
static lv_obj_t* add_track_item(lv_obj_t *parent, Track *track);
static void sidebar_btn_cb(lv_event_t *e);
static void playlist_item_cb(lv_event_t *e);
static void track_item_cb(lv_event_t *e);
static const char* format_duration(uint32_t seconds);
static lv_color_t get_key_color(uint8_t key_id);
static const char* get_key_name(uint8_t key_id);
void clear_track_list();


// ========== Global References ==========
lv_obj_t *filesScreen;
static lv_obj_t *p_panel;
static lv_obj_t *t_panel;
static lv_obj_t *g_track_list;
static lv_obj_t *g_playlist_list;
lv_obj_t * usb_status_label;

EXTMEM static Track       g_loaded_tracks[20];
EXTMEM static Track*      g_track_ptrs[20];   // pointer array for populate_track_list
EXTMEM static PlaylistInfo g_playlist_info[10];
static uint16_t           g_playlist_count = 0;
static int16_t g_active_playlist_id = -1;
static int16_t g_active_track_id    = -1;
static void restore_selection(int16_t playlist_id, int16_t track_id);

USBFilesystem rekordboxDrive(myusb);  // drive with the PDB file
RekordboxParser* rbParser = nullptr;       // Create it here
bool usb_was_connected = false;

lv_timer_t* usb_poll_timer;
static void usb_poll_cb(lv_timer_t* timer) {
    if (rekordboxDrive && !usb_was_connected) {
        usb_was_connected = true;
        Serial.println("Rekordbox drive connected");
        //on_usb_connected();
        if(lv_obj_is_valid(usb_status_label)){
            lv_obj_delete(usb_status_label);  // Remove "No USB" label
        }
        rbParser->begin("/PIONEER/rekordbox/export.pdb", (uint8_t*)PCM, sizeof(PCM));
        create_playlist_panel(filesScreen);
        create_track_list_panel(filesScreen);

    } else if (!rekordboxDrive && usb_was_connected) {
        usb_was_connected = false;
        Serial.println("Rekordbox drive disconnected");
        rbParser->close();
        lv_obj_delete(p_panel);
        lv_obj_delete(t_panel);
    }
}


// ---- Call this once after parser.parse() ----
void init_playlists() {
    // Same call as before — unchanged
    g_playlist_count = rbParser->getPlaylistInfoList(g_playlist_info, RB_MAX_PLAYLISTS);
    Serial.printf("Found %d playlists\n", g_playlist_count);
}

// ---- Playlist click callback ----
static void playlist_item_cb(lv_event_t* e) {
    uint32_t pl_id = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    g_active_playlist_id = (int16_t)pl_id;  // <-- save it
    g_active_track_id    = -1;              // reset track on playlist change

    uint16_t count = rbParser->getTracksForPlaylist(pl_id);
    for (uint16_t i = 0; i < count; i++)
        g_track_ptrs[i] = (Track*)rbParser->getTrack(i);
    populate_track_list(g_track_ptrs, count);
}

// ========== Main UI Creation ==========
void create_dj_browser_ui(int16_t playlist_id = -1, int16_t track_id = -1)
{
    // Create main container with flex row layout

    
    Serial.println("Creating main container...");
    static lv_style_t fileScreen_style;
    filesScreen = lv_obj_create(NULL);
    lv_obj_set_size(filesScreen, 800, 480);
    lv_obj_align(filesScreen, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_flex_flow(filesScreen, LV_FLEX_FLOW_ROW);  // <- ROW not COLUMN!
    lv_obj_set_style_opa(filesScreen, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    lv_obj_set_scroll_dir(filesScreen, LV_DIR_HOR);
    lv_style_set_border_width(&fileScreen_style, 0);
    lv_style_set_border_color(&fileScreen_style, lv_color_hex(0xFFFFFF));
    //lv_style_set_bg_color(&fileScreen_style, COLOR_BG_MAIN);
    lv_obj_add_style(filesScreen, &fileScreen_style, 0);
    // Remove scrolling entirely for the main screen
    lv_obj_remove_flag(filesScreen, LV_OBJ_FLAG_SCROLLABLE);


    // Create three columns
    Serial.println("Creating sidebar...");
    create_sidebar(filesScreen);
    //create_playlist_panel(filesScreen);
    rbParser = new RekordboxParser();
	rbParser->setFS(rekordboxDrive);
  	//rbParser->begin("/PIONEER/rekordbox/export.pdb", (uint8_t*)PCM, sizeof(PCM));
    lv_timer_create(usb_poll_cb, 500, NULL);  // Poll USB every 500ms

    if(rekordboxDrive) {
        usb_was_connected = true;
        rbParser->begin("/PIONEER/rekordbox/export.pdb", (uint8_t*)PCM, sizeof(PCM));
        create_playlist_panel(filesScreen);
        create_track_list_panel(filesScreen);
        if (g_active_playlist_id != -1 && g_active_track_id != -1){
        restore_selection(g_active_playlist_id, g_active_track_id);  // <-- add this
        }
    }
    else{
        usb_status_label = lv_label_create(filesScreen); // Placeholder label while waiting for USB connection   
        lv_label_set_text(usb_status_label, "No USB device detected..");
        lv_obj_set_style_text_font(usb_status_label, &exo2_32, 0);
        lv_obj_set_flex_align(usb_status_label, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }

    //create_track_list_panel(filesScreen);
}

// ========== Sidebar Creation ==========
static void create_sidebar(lv_obj_t *parent)
{
    lv_obj_t *sidebar = lv_obj_create(parent);
    lv_obj_set_size(sidebar, SIDEBAR_WIDTH, LV_PCT(100));
    lv_obj_set_flex_flow(sidebar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sidebar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(sidebar, 8, 0);
    lv_obj_set_style_pad_gap(sidebar, 8, 0);
    lv_obj_set_style_bg_color(sidebar, COLOR_BG_SIDEBAR, 0);
    lv_obj_set_style_border_width(sidebar, 0, 0);
    lv_obj_set_style_radius(sidebar, 0, 0);
    lv_obj_remove_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);

    // Helper function to create sidebar button
    const char *icons[] = {LV_SYMBOL_LIST, LV_SYMBOL_SETTINGS};
    const char *labels[] = {"Playlists", "Settings"};
    
    for (int i = 0; i < 2; i++) {
        lv_obj_t *btn = lv_button_create(sidebar);
        lv_obj_set_size(btn, 64, 64);
        lv_obj_set_style_bg_color(btn, COLOR_BG_TRACK, LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(btn, COLOR_TRACK_HOVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, COLOR_BORDER, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_add_event_cb(btn, sidebar_btn_cb, LV_EVENT_CLICKED, (void*)labels[i]);

        lv_obj_t *label = lv_label_create(btn);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
        lv_label_set_text(label, icons[i]);
        lv_obj_center(label);
        lv_obj_set_style_text_color(label, COLOR_WHITE, 0);
    }
}


static JpegImageHandle g_album_art = {0};
// ========== Playlist Panel ==========
static void create_playlist_panel(lv_obj_t *parent)
{
    p_panel = lv_obj_create(parent);
    lv_obj_set_size(p_panel, PLAYLIST_WIDTH, LV_PCT(100));
    lv_obj_set_flex_flow(p_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(p_panel, 0, 0);
    lv_obj_set_style_bg_color(p_panel, COLOR_BG_SIDEBAR, 0);
    lv_obj_set_style_border_width(p_panel, 0, 0);
    lv_obj_set_style_border_side(p_panel, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(p_panel, COLOR_BORDER, 0);
    lv_obj_set_style_radius(p_panel, 0, 0);

    // Header
    lv_obj_t *header = lv_obj_create(p_panel);
    lv_obj_set_size(header, LV_PCT(100), HEADER_HEIGHT);
    lv_obj_set_style_bg_color(header, COLOR_BG_SIDEBAR, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, COLOR_BORDER, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_left(header, 12, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header_label = lv_label_create(header);
    lv_label_set_text(header_label, "PLAYLISTS");
    lv_obj_align(header_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_color(header_label, COLOR_GRAY, 0);
    lv_obj_set_style_text_font(header_label, &exo2_16, 0);

    // Playlist list (simple obj with flex, not lv_list in v9)
    g_playlist_list = lv_obj_create(p_panel);
    lv_obj_set_size(g_playlist_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(g_playlist_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(g_playlist_list, COLOR_BG_SIDEBAR, 0);
    lv_obj_set_style_border_width(g_playlist_list, 0, 0);
    lv_obj_set_style_radius(g_playlist_list, 0, 0);
    lv_obj_set_style_pad_all(g_playlist_list, 0, 0);
    lv_obj_set_style_pad_gap(g_playlist_list, 0, 0);
    lv_obj_add_flag(g_playlist_list, LV_OBJ_FLAG_SCROLLABLE);

    init_playlists();
    lv_obj_clean(g_playlist_list);

    for (uint16_t i = 0; i < g_playlist_count; i++) {
        lv_obj_t* btn = lv_button_create(g_playlist_list);
        lv_obj_set_width(btn, LV_PCT(80));
        lv_obj_set_height(btn, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(btn, COLOR_BG_SIDEBAR, LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(btn, COLOR_TRACK_HOVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_pad_ver(btn, 12, 0);
        lv_obj_set_style_pad_hor(btn, 12, 0);

        // Pass the playlist ID as user data (not the name pointer —
        // the name lives in EXTMEM and stays valid, but the ID is safer
        // as it uniquely identifies the playlist across rebuilds)
        lv_obj_add_event_cb(btn, playlist_item_cb, LV_EVENT_CLICKED,
                            (void*)(uintptr_t)g_playlist_info[i].id);

        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, g_playlist_info[i].name);
    
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_color(label, COLOR_WHITE, 0);
        lv_obj_set_style_text_font(label, &exo2_18, 0);
        
    }
    g_playlist_count=0;
}

// ========== Track List Panel ==========
static void create_track_list_panel(lv_obj_t *parent)
{
    t_panel = lv_obj_create(parent);
    lv_obj_set_size(t_panel, TRACK_LIST_WIDTH, LV_PCT(100));
    lv_obj_set_flex_flow(t_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(t_panel, 0, 0);
    lv_obj_set_style_bg_color(t_panel, COLOR_BG_MAIN, 0);
    lv_obj_set_style_border_width(t_panel, 0, 0);
    lv_obj_set_style_radius(t_panel, 0, 0);

    // Header with column titles
    lv_obj_t *header = lv_obj_create(t_panel);
    lv_obj_set_size(header, LV_PCT(100), HEADER_HEIGHT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(header, COLOR_BG_MAIN, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, COLOR_BORDER, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_hor(header, 16, 0);
    lv_obj_set_style_pad_gap(header, 8, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    

    lv_obj_t *col_label = lv_label_create(header);
    lv_label_set_text(col_label, "Tracks");
    lv_obj_set_width(col_label, 170);  // Fixed width for "Tracks" column
    lv_obj_set_style_text_align(col_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(col_label, COLOR_GRAY_DARK, 0);
    lv_obj_set_style_text_font(col_label, &exo2_24, 0);

    // Track list (scrollable)
    g_track_list = lv_obj_create(t_panel);
    lv_obj_set_size(g_track_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(g_track_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_track_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_color(g_track_list, COLOR_BG_MAIN, 0);
    lv_obj_set_style_border_width(g_track_list, 0, 0);
    lv_obj_set_style_radius(g_track_list, 0, 0);
    lv_obj_set_style_pad_all(g_track_list, 0, 0);
    lv_obj_set_style_pad_gap(g_track_list, 1, 0);  // 1px gap between tracks
    
    // Enable vertical scrolling ONLY
    lv_obj_set_scroll_dir(g_track_list, LV_DIR_VER);
    lv_obj_add_flag(g_track_list, LV_OBJ_FLAG_SCROLLABLE);
    
    // Add sample tracks (call this with your actual track data)
    // populate_track_list(g_track_list);
}
LV_IMG_DECLARE(no_artwork_80px)
// ========== Add Track Item ==========
static lv_obj_t* add_track_item(lv_obj_t *parent, Track *track)
{
    if (!parent || !track || !track->title || !track->artist) return NULL;

    // ── Outer container ──────────────────────────────────────────────────────
    lv_obj_t *track_cont = lv_obj_create(parent);
    if (!track_cont) return NULL;

    lv_obj_set_width(track_cont, LV_PCT(95));
    lv_obj_set_height(track_cont, TRACK_ITEM_HEIGHT);
    lv_obj_set_flex_flow(track_cont, LV_FLEX_FLOW_ROW);          // ← ROW now
    lv_obj_set_flex_align(track_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(track_cont, COLOR_BG_TRACK, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(track_cont, COLOR_TRACK_HOVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(track_cont, 0, 0);
    lv_obj_set_style_radius(track_cont, 0, 0);
    lv_obj_set_style_pad_hor(track_cont, 16, 0);
    lv_obj_set_style_pad_ver(track_cont, 8, 0);
    lv_obj_set_style_pad_gap(track_cont, 12, 0);                 // gap between art & text
    lv_obj_add_flag(track_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(track_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(track_cont, track_item_cb, LV_EVENT_CLICKED, track);
    lv_obj_set_user_data(track_cont, track);

    // ── Album art (left) ─────────────────────────────────────────────────────
    lv_obj_t *album_art = lv_img_create(track_cont);
    lv_obj_set_size(album_art, 80, 80);
    lv_obj_set_style_border_width(album_art, 0, 0);

   
    if (track->thumbnail_path[0] == '/' && !track->art_buf) {
        JpegImageHandle tmp;
        if (jpeg_load(rekordboxDrive, track->thumbnail_path, tmp)) {
            track->art_buf = (uint16_t *)malloc(80 * 80 * 2);
            track->art_dsc = (lv_img_dsc_t *)malloc(sizeof(lv_img_dsc_t));
            if (track->art_buf && track->art_dsc) {
                memcpy(track->art_buf, tmp.buf, 80 * 80 * 2);
                track->art_dsc->header.magic  = LV_IMAGE_HEADER_MAGIC;
                track->art_dsc->header.cf     = LV_COLOR_FORMAT_RGB565;
                track->art_dsc->header.w      = 80;
                track->art_dsc->header.h      = 80;
                track->art_dsc->header.stride = 80 * 2;
                track->art_dsc->data_size     = 80 * 80 * 2;
                track->art_dsc->data          = (uint8_t *)track->art_buf;
            }
            jpeg_image_free(tmp);
        }
    }

    lv_img_set_src(album_art, track->art_dsc ? track->art_dsc : &no_artwork_80px);
    

    // ── Text column (right of image) ─────────────────────────────────────────
    lv_obj_t *text_col = lv_obj_create(track_cont);
    lv_obj_set_flex_grow(text_col, 1);                           // fills remaining width
    lv_obj_set_height(text_col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(text_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(text_col, 0, 0);
    lv_obj_set_style_pad_all(text_col, 0, 0);
    lv_obj_set_style_pad_gap(text_col, 4, 0);
    lv_obj_remove_flag(text_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(text_col, LV_OBJ_FLAG_CLICKABLE);

    // ── Top row: Title | BPM | Key | Duration ────────────────────────────────
    lv_obj_t *top_row = lv_obj_create(text_col);
    if (!top_row) { lv_obj_delete(track_cont); return NULL; }

    lv_obj_set_size(top_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_row, 0, 0);
    lv_obj_set_style_pad_all(top_row, 0, 0);
    lv_obj_set_style_pad_gap(top_row, 8, 0);
    lv_obj_remove_flag(top_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(top_row, LV_OBJ_FLAG_CLICKABLE);

    // Title (grows to fill space)
    lv_obj_t *lbl_title = lv_label_create(top_row);
    if (!lbl_title) { lv_obj_delete(track_cont); return NULL; }
    lv_label_set_text(lbl_title, track->title);
    lv_obj_set_flex_grow(lbl_title, 1);                          // pushes BPM/Key/Dur to the right
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_font(lbl_title, &exo2_18, 0);
    lv_obj_set_style_text_color(lbl_title, COLOR_WHITE, 0);
    lv_obj_remove_flag(lbl_title, LV_OBJ_FLAG_CLICKABLE);

    // Key
    lv_obj_t *lbl_key = lv_label_create(top_row);
    const char *keyName  = rbParser->getKeyName(track->key_id);
    const char *keyColor = getKeyColor(keyName);
    lv_label_set_text(lbl_key, keyName);
    lv_obj_set_width(lbl_key, 40);
    lv_obj_set_style_text_align(lbl_key, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl_key, &exo2_16, 0);
    lv_obj_set_style_text_color(lbl_key, lv_color_hex(strtol(keyColor + 1, nullptr, 16)), 0);
    lv_obj_remove_flag(lbl_key, LV_OBJ_FLAG_CLICKABLE);

    // BPM
    lv_obj_t *lbl_bpm = lv_label_create(top_row);
    lv_label_set_text_fmt(lbl_bpm, "%.1f", track->bpm);
    lv_obj_set_width(lbl_bpm, 50);
    lv_obj_set_style_text_align(lbl_bpm, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl_bpm, &exo2_16, 0);
    lv_obj_set_style_text_color(lbl_bpm, COLOR_WHITE, 0);
    lv_obj_remove_flag(lbl_bpm, LV_OBJ_FLAG_CLICKABLE);

    // Duration
    lv_obj_t *lbl_duration = lv_label_create(top_row);
    lv_label_set_text(lbl_duration, format_duration(track->duration));
    lv_obj_set_width(lbl_duration, 55);
    lv_obj_set_style_text_align(lbl_duration, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(lbl_duration, &exo2_16, 0);
    lv_obj_set_style_text_color(lbl_duration, COLOR_GRAY, 0);
    lv_obj_remove_flag(lbl_duration, LV_OBJ_FLAG_CLICKABLE);

    // ── Bottom row: Artist | Format ───────────────────────────────────────────
    lv_obj_t *bottom_row = lv_obj_create(text_col);
    if (!bottom_row) { lv_obj_delete(track_cont); return NULL; }

    lv_obj_set_size(bottom_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bottom_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(bottom_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottom_row, 0, 0);
    lv_obj_set_style_pad_all(bottom_row, 0, 0);
    lv_obj_set_style_pad_gap(bottom_row, 8, 0);
    lv_obj_remove_flag(bottom_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(bottom_row, LV_OBJ_FLAG_CLICKABLE);

    // Artist (grows to fill space)
    lv_obj_t *lbl_artist = lv_label_create(bottom_row);
    if (!lbl_artist) { lv_obj_delete(track_cont); return NULL; }
    lv_label_set_text(lbl_artist, track->artist);
    lv_obj_set_flex_grow(lbl_artist, 1);                         // pushes format to the right
    lv_label_set_long_mode(lbl_artist, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(lbl_artist, &exo2_16, 0);
    lv_obj_set_style_text_color(lbl_artist, COLOR_GRAY, 0);
    lv_obj_remove_flag(lbl_artist, LV_OBJ_FLAG_CLICKABLE);

    // Format
    lv_obj_t *lbl_format = lv_label_create(bottom_row);
    lv_label_set_text(lbl_format, fileTypeString(track->file_type));
    lv_obj_set_width(lbl_format, 50);
    lv_obj_set_style_text_align(lbl_format, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(lbl_format, &exo2_16, 0);
    lv_obj_set_style_text_color(lbl_format, COLOR_GRAY, 0);
    lv_obj_remove_flag(lbl_format, LV_OBJ_FLAG_CLICKABLE);

    return track_cont;
}
// ========== Helper Functions ==========
static const char* format_duration(uint32_t seconds)
{    static char buf[16];
    uint32_t mins = seconds / 60;
    uint32_t secs = seconds % 60;
    snprintf(buf, sizeof(buf), "%02u:%02u", mins, secs);
    return buf;
}

static lv_color_t get_key_color(uint8_t key_id)
{
    // Camelot color coding: warm (red/orange) vs cool (blue/green)
    // This is a simplified version - adjust based on your key system
    if (key_id >= 1 && key_id <= 6) {
        return lv_color_hex(0xFF6B6B);  // Warm keys (red)
    } else if (key_id >= 7 && key_id <= 12) {
        return lv_color_hex(0x4ECDC4);  // Cool keys (cyan)
    }
    return COLOR_WHITE;
}

static const char* get_key_name(uint8_t key_id)
{
    // Implement your key naming logic
    static char key_buf[8];
    
    // Example Camelot notation
    const char *camelot[] = {
        "1A", "1B", "2A", "2B", "3A", "3B", "4A", "4B",
        "5A", "5B", "6A", "6B", "7A", "7B", "8A", "8B",
        "9A", "9B", "10A", "10B", "11A", "11B", "12A", "12B"
    };
    
    if (key_id < 24) {
        return camelot[key_id];
    }
    
    snprintf(key_buf, sizeof(key_buf), "%dA", key_id);
    return key_buf;
}

// ========== Event Callbacks ==========
static void sidebar_btn_cb(lv_event_t *e)
{
    const char *label = (const char*)lv_event_get_user_data(e);
    // Handle sidebar button click
    // Example: Load tracks from selected source
    Serial.printf("Sidebar clicked: %s\n", label);
}



static void track_item_cb(lv_event_t *e) {
    Track *track = (Track*)lv_event_get_user_data(e);
    g_active_track_id = (int16_t)track->id;  // <-- save it (assuming Track has an id field)
    dj_ui_init(track);
    lv_scr_load_anim(main_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}

// ========== Public API to Populate Tracks ==========
void populate_track_list(Track **tracks, size_t count)
{
    Serial.println("=== populate_track_list START ===");
    
    if (!g_track_list) {
        Serial.println("ERROR: g_track_list is NULL");
        return;
    }
    Serial.printf("g_track_list = %p\n", g_track_list);
    
    if (!tracks) {
        Serial.println("ERROR: tracks array is NULL");
        return;
    }
    Serial.printf("tracks = %p, count = %d\n", tracks, count);

    Serial.println("About to call lv_obj_clean...");
    // Clear existing tracks - in v9 use lv_obj_delete_children
    lv_obj_clean(g_track_list);
    Serial.println("lv_obj_clean completed");

    Serial.printf("Starting to add %d tracks...\n", count);

    // Add all tracks
    size_t added = 0;
    for (size_t i = 0; i < count; i++) {
        Serial.printf("\n--- Processing track %d ---\n", i);
        
        if (!tracks[i]) {
            Serial.printf("WARNING: Track %d is NULL, skipping\n", i);
            continue;
        }
        Serial.printf("Track %d pointer = %p\n", i, tracks[i]);
        
        if (!tracks[i]->title) {
            Serial.printf("WARNING: Track %d has NULL title, skipping\n", i);
            continue;
        }
        Serial.printf("Track %d title = '%s'\n", i, tracks[i]->title);
        
        if (!tracks[i]->artist) {
            Serial.printf("WARNING: Track %d has NULL artist, skipping\n", i);
            continue;
        }
        Serial.printf("Track %d artist = '%s'\n", i, tracks[i]->artist);
        
        Serial.printf("Calling add_track_item for track %d...\n", i);
        lv_obj_t *item = add_track_item(g_track_list, tracks[i]);
        
        if (item) {
            Serial.printf("Track %d added successfully\n", i);
            added++;
        } else {
            Serial.printf("WARNING: Failed to create track item %d\n", i);
        }
    }
    
    Serial.printf("=== populate_track_list END: %d/%d tracks added ===\n", added, count);
}

void clear_track_list(void)
{
    if (g_track_list) {
        lv_obj_clean(g_track_list);
    }
}


static void restore_selection(int16_t playlist_id, int16_t track_id) {
    if (playlist_id < 0) return;

    // Re-load the playlist's tracks
    uint16_t count = rbParser->getTracksForPlaylist((uint32_t)playlist_id);
    for (uint16_t i = 0; i < count; i++)
        g_track_ptrs[i] = (Track*)rbParser->getTrack(i);
    populate_track_list(g_track_ptrs, count);

    g_active_playlist_id = playlist_id;

    // Highlight the matching playlist button in the sidebar
    uint32_t child_count = lv_obj_get_child_count(g_playlist_list);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *btn = lv_obj_get_child(g_playlist_list, i);
        uint32_t btn_pl_id = (uint32_t)(uintptr_t)lv_obj_get_user_data(btn);
        if ((int16_t)btn_pl_id == playlist_id) {
            lv_obj_set_style_bg_color(btn, COLOR_TRACK_SEL, LV_STATE_DEFAULT);
            // Scroll the playlist panel to show it
            lv_obj_scroll_to_view(btn, LV_ANIM_OFF);
            break;
        }
    }

    // Scroll track list to the previously selected track
    if (track_id >= 0) {
        uint32_t track_count = lv_obj_get_child_count(g_track_list);
        for (uint32_t i = 0; i < track_count; i++) {
            lv_obj_t *item = lv_obj_get_child(g_track_list, i);
            Track *t = (Track*)lv_obj_get_user_data(item);  // needs user_data set (see note)
            if (t && (int16_t)t->id == track_id) {
                lv_obj_set_style_bg_color(item, COLOR_TRACK_SEL, LV_STATE_DEFAULT);
                lv_obj_scroll_to_view(item, LV_ANIM_OFF);
                break;
            }
        }
    }
}


