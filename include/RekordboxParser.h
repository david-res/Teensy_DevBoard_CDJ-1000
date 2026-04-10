/*
 * Rekordbox PDB Parser for Teensy 4.x
 * Compatible with SD and SdFat libraries
 * 
 * This parser reads export.pdb files from Pioneer Rekordbox
 * and extracts track metadata including title, BPM, key, duration, etc.
 * 
 * MEMORY USAGE:
 * - Uses EXTMEM (external PSRAM) for large arrays
 * - Requires Teensy 4.1 with PSRAM installed
 * - Current config: ~250KB in EXTMEM for 512 tracks
 * 
 * REDUCE MEMORY:
 * - Change MAX_TRACKS below (e.g., 256 = ~122KB, 128 = ~61KB)
 * 
 * Key improvement: Uses LAST occurrence of each track ID (most recent metadata)
 */

#ifndef REKORDBOX_PARSER_H
#define REKORDBOX_PARSER_H

#include <Arduino.h>
#include <SD.h>  // Or #include <SdFat.h> if using SdFat library
#define DEBUG_PARSER

// ============================================================================
// MEMORY CONFIGURATION
// ============================================================================
// Adjust MAX_TRACKS to control memory usage:
//   512 tracks = ~250KB EXTMEM (default)
//   256 tracks = ~122KB EXTMEM
//   128 tracks = ~61KB EXTMEM
// ============================================================================

#define MAX_TRACKS 512          // Maximum tracks to store
#define MAX_PLAYLISTS 20        // Maximum playlists
#define MAX_KEYS 24             // Musical keys (Camelot wheel)
#define MAX_ARTISTS 256         // Maximum artists
#define MAX_ALBUMS 256          // Maximum albums
#define MAX_GENRES 64           // Maximum genres
#define MAX_LABELS 64           // Maximum labels
#define MAX_TITLE_LENGTH 55     // Max string length
#define MAX_PLAYLIST_NAME 48    // Max playlist name length
#define RB_PAGE_SIZE 4096          // PDB page size
#define RB_BUFFER_SIZE 4096        // Read buffer size

// Track structure
struct Track {
    uint16_t id;
    char title[MAX_TITLE_LENGTH];
    char artist[MAX_TITLE_LENGTH];
    char album[MAX_TITLE_LENGTH];
    char genre[MAX_TITLE_LENGTH];
    char label[MAX_TITLE_LENGTH];
    char anlz_path[128];        // Path to ANLZ .DAT file
    char anlz_ext_path[128];    // Path to ANLZ .EXT file
    char anlz_ex2_path[128];    // Path to ANLZ .EX2 file
    char audio_path[128];       // Path to audio file
    char thumbnail_path[128];   // Path to 80x80 JPEG artwork (empty if none)
    uint32_t artwork_id;        // Rekordbox artwork ID (0 = no artwork)
    float bpm;
    uint16_t duration;  // seconds
    uint8_t key_id;
    uint16_t rating;
    uint32_t file_offset;  // Position in PDB file
    bool valid;
};

// Key structure (musical keys like Am, Bm, Dbm, etc.)
struct Key {
    uint8_t id;
    char name[5];  // 4 chars + null terminator
};

// Artist structure
struct Artist {
    uint16_t id;
    char name[MAX_TITLE_LENGTH];
};

// Album structure
struct Album {
    uint16_t id;
    char name[MAX_TITLE_LENGTH];
};

// Genre structure
struct Genre {
    uint16_t id;
    char name[MAX_TITLE_LENGTH];
};

// Label structure
struct Label {
    uint16_t id;
    char name[MAX_TITLE_LENGTH];
};

// Lightweight playlist descriptor for listing playlists
struct PlaylistInfo {
    uint32_t id;
    char name[MAX_PLAYLIST_NAME];
};

// Artwork structure — maps artwork_id to file path
#define MAX_ARTWORK 256
struct Artwork {
    uint32_t id;
    char path[128];   // path to 80×80 JPEG
};

// Playlist structure — no embedded arrays.
// Track IDs are stored in a shared flat pool (playlist_track_pool[]),
// indexed by track_offset..track_offset+track_count-1.
// Changing MAX_TRACKS or MAX_PLAYLISTS only affects the pool size, not this struct.
struct Playlist {
    uint32_t id;                     // Full 32-bit Rekordbox playlist ID
    char     name[MAX_PLAYLIST_NAME];
    uint16_t track_count;            // number of tracks in this playlist
    uint16_t track_offset;           // start index into playlist_track_pool[]
    uint16_t source_row_off;         // heap offset of the name row (for upsert)
};

// Maximum total track-slots across all playlists combined.
// A track can appear in multiple playlists so this can exceed MAX_TRACKS.
// Tune to fit your library: 20 playlists × avg 50 tracks = 1000 is typical.
#define MAX_PLAYLIST_TRACK_POOL (MAX_PLAYLISTS * MAX_TRACKS)

class RekordboxParser {
private:
    File file;
    uint8_t buffer[RB_BUFFER_SIZE];
    
    // Parsed data - Pointers to arrays allocated in EXTMEM
    Track* tracks;
    uint16_t track_count;
    
    Key* keys;
    uint8_t key_count;
    
    Artist* artists;
    uint16_t artist_count;
    
    Album* albums;
    uint16_t album_count;
    
    Genre* genres;
    uint16_t genre_count;
    
    Label* labels;
    uint16_t label_count;
    
    Playlist* playlists;
    uint16_t  playlist_count;
    uint16_t* playlist_track_pool;  // flat array of track IDs for all playlists
    uint16_t  pool_used;            // how many slots have been filled

    Artwork*  artworks;
    uint16_t  artwork_count;
    
    // Arena allocator — points into caller-supplied buffer (e.g. PCM audio buffer)
    // when not null, all allocations come from here instead of extmem_malloc
    uint8_t*  _arena      = nullptr;
    uint32_t  _arena_size = 0;
    uint32_t  _arena_used = 0;

    void* arenaAlloc(uint32_t size) {
        uint32_t aligned = (size + 3) & ~3u;
        if (_arena) {
            // Arena mode — never fall back to extmem_malloc
            if ((_arena_used + aligned) <= _arena_size) {
                void* ptr = _arena + _arena_used;
                _arena_used += aligned;
                return ptr;
            }
            // Arena exhausted — return null, caller must check
            Serial.printf("ERROR: arena exhausted. Used=%u Size=%u Needed=%u\n",
                          _arena_used, _arena_size, aligned);
            return nullptr;
        }
        // No arena — use EXTMEM heap
        return extmem_malloc(size);
    }

    // Metadata
    char sd_date[14];
    char sd_name[19];
    
    // Helper functions
    uint32_t readUInt32(uint8_t* data, uint32_t offset);
    uint16_t readUInt16(uint8_t* data, uint32_t offset);
    bool readPage(uint32_t page_num);
    void parseTrackEntry(uint8_t* page_data, uint16_t offset, uint32_t page_offset);
    void parseKeysTable(uint32_t first_page, uint32_t last_page);
    void parseArtistsTable(uint32_t first_page, uint32_t last_page);
    void parseAlbumsTable(uint32_t first_page, uint32_t last_page);
    void parseGenresTable(uint32_t first_page, uint32_t last_page);
    void parseLabelsTable(uint32_t first_page, uint32_t last_page);
    void parsePlaylistTree(uint32_t first_page, uint32_t last_page);
    void parsePlaylistEntries(uint32_t first_page, uint32_t last_page);
    void parseHistory(uint32_t first_page, uint32_t last_page);
    void parseArtworkTable(uint32_t first_page, uint32_t last_page);
    void deduplicateTracks();
    void deduplicatePlaylists();
    const char* getArtistName(uint16_t artist_id) const;
    const char* getAlbumName(uint16_t album_id) const;
    const char* getGenreName(uint16_t genre_id) const;
    const char* getLabelName(uint16_t label_id) const;
    
public:
    RekordboxParser();
    ~RekordboxParser();  // Destructor to free memory
    
    // Set an external memory arena to use instead of extmem_malloc.
    // Call this BEFORE parse(). buf must remain valid for the lifetime of the parser.
    // Typical use: pass your audio PCM buffer when it's not needed for playback.
    //   parser.setArena((uint8_t*)PCM, sizeof(PCM));
    void setArena(uint8_t* buf, uint32_t size) {
        _arena      = buf;
        _arena_size = size;
        _arena_used = 0;
    }

    // Call after setArena() to allocate all internal arrays.
    // If not using an arena, this is called automatically by the constructor.
    void allocate();

    uint32_t getArenaUsed()      const { return _arena_used; }
    uint32_t getArenaAvailable() const { return _arena ? (_arena_size - _arena_used) : 0; }

    // Main parsing function
    bool parse(const char* filename);
    
    // Getters
    uint16_t getTrackCount() const { return track_count; }
    const Track* getTracks() const { return tracks; }
    const Track* getTrack(uint16_t index) const;
    
    uint8_t getKeyCount() const { return key_count; }
    const Key* getKeys() const { return keys; }
    const char* getKeyName(uint8_t key_id) const;
    
    uint16_t getPlaylistCount() const { return playlist_count; }
    const Playlist* getPlaylists() const { return playlists; }
    const Playlist* getPlaylist(uint16_t index) const;
    
    // ── Playlist list API ──────────────────────────────────────────────────
    // Fill 'out' with up to max_count PlaylistInfo entries in display order.
    // Returns number of playlists written.
    uint16_t getPlaylistInfoList(PlaylistInfo* out, uint16_t max_count) const;

    // ── Playlist tracks API ─────────────────────────────────────────────────
    // Fill 'out' with Track objects for every track in the playlist with the
    // given Rekordbox ID, in playlist order.
    // Returns number of tracks written, or 0 if playlist not found.
    uint16_t getTracksForPlaylist(uint32_t playlist_id,
                                  Track* out,
                                  uint16_t max_count) const;

    // ── Thumbnail API ───────────────────────────────────────────────────────
    // Load the 80x80 JPEG artwork for 'track' into 'buf_rgb565' as RGB565
    // pixels (little-endian, row-major). 'buf_rgb565' must be at least
    // 80*80*2 = 12800 bytes.
    // Returns true on success, false if no artwork or load failed.
    // Requires JPEG decode library (TJpgDec or similar) linked separately.
    bool loadThumbnail(const Track& track, uint16_t* buf_rgb565) const;

    const char* getSDDate() const { return sd_date; }
    const char* getSDName() const { return sd_name; }
    
    // Utility functions
    void printSummary();
    void printTrack(uint16_t index);
    void printPlaylist(uint16_t index);
    
    // Get derived ANLZ paths (EXT and EX2 files)
    bool getAnlzExtPath(uint16_t track_index, char* output, size_t output_size) const;
    bool getAnlzEx2Path(uint16_t track_index, char* output, size_t output_size) const;
};

// Implementation

inline RekordboxParser::RekordboxParser() {
    // Zero all pointers and counts — no allocation here.
    // If using an arena: call setArena() then allocate() before parse().
    // If using extmem_malloc: call allocate() directly before parse().
    tracks              = nullptr;
    keys                = nullptr;
    artists             = nullptr;
    albums              = nullptr;
    genres              = nullptr;
    labels              = nullptr;
    playlists           = nullptr;
    playlist_track_pool = nullptr;
    artworks            = nullptr;
    _arena              = nullptr;
    _arena_size         = 0;
    _arena_used         = 0;
    track_count         = 0;
    key_count           = 0;
    artist_count        = 0;
    album_count         = 0;
    genre_count         = 0;
    label_count         = 0;
    playlist_count      = 0;
    artwork_count       = 0;
    pool_used           = 0;
    memset(sd_date, 0, sizeof(sd_date));
    memset(sd_name, 0, sizeof(sd_name));
}

inline void RekordboxParser::allocate() {
    // Allocate all arrays from arena (if set) or extmem_malloc.
    tracks              = (Track*)   arenaAlloc(sizeof(Track)    * MAX_TRACKS);
    keys                = (Key*)     arenaAlloc(sizeof(Key)      * MAX_KEYS);
    artists             = (Artist*)  arenaAlloc(sizeof(Artist)   * MAX_ARTISTS);
    albums              = (Album*)   arenaAlloc(sizeof(Album)    * MAX_ALBUMS);
    genres              = (Genre*)   arenaAlloc(sizeof(Genre)    * MAX_GENRES);
    labels              = (Label*)   arenaAlloc(sizeof(Label)    * MAX_LABELS);
    playlists           = (Playlist*)arenaAlloc(sizeof(Playlist) * MAX_PLAYLISTS);
    playlist_track_pool = (uint16_t*)arenaAlloc(sizeof(uint16_t) * MAX_PLAYLIST_TRACK_POOL);
    artworks            = (Artwork*) arenaAlloc(sizeof(Artwork)  * MAX_ARTWORK);

    if (_arena) {
        uint32_t needed = sizeof(Track)*MAX_TRACKS + sizeof(Key)*MAX_KEYS
                        + sizeof(Artist)*MAX_ARTISTS + sizeof(Album)*MAX_ALBUMS
                        + sizeof(Genre)*MAX_GENRES   + sizeof(Label)*MAX_LABELS
                        + sizeof(Playlist)*MAX_PLAYLISTS
                        + sizeof(uint16_t)*MAX_PLAYLIST_TRACK_POOL
                        + sizeof(Artwork)*MAX_ARTWORK;
        Serial.printf("Arena: %lu / %lu used  (need %lu)  artworks=%s\n",
                      _arena_used, _arena_size, needed,
                      artworks ? "OK" : "NULL-increase arena size");
    }

    // Zero-initialise all successfully allocated arrays
    if (tracks)              memset(tracks,              0, sizeof(Track)    * MAX_TRACKS);
    if (keys)                memset(keys,                0, sizeof(Key)      * MAX_KEYS);
    if (artists)             memset(artists,             0, sizeof(Artist)   * MAX_ARTISTS);
    if (albums)              memset(albums,              0, sizeof(Album)    * MAX_ALBUMS);
    if (genres)              memset(genres,              0, sizeof(Genre)    * MAX_GENRES);
    if (labels)              memset(labels,              0, sizeof(Label)    * MAX_LABELS);
    if (playlists)           memset(playlists,           0, sizeof(Playlist) * MAX_PLAYLISTS);
    if (playlist_track_pool) memset(playlist_track_pool, 0, sizeof(uint16_t) * MAX_PLAYLIST_TRACK_POOL);
    if (artworks)            memset(artworks,            0, sizeof(Artwork)  * MAX_ARTWORK);
}

inline RekordboxParser::~RekordboxParser() {
    // Only free if we used extmem_malloc (not arena mode)
    if (_arena) return;  // arena memory is owned by caller, don't free it
    if (tracks)    extmem_free(tracks);
    if (keys)      extmem_free(keys);
    if (artists)   extmem_free(artists);
    if (albums)    extmem_free(albums);
    if (genres)    extmem_free(genres);
    if (labels)    extmem_free(labels);
    if (playlists)           extmem_free(playlists);
    if (playlist_track_pool) extmem_free(playlist_track_pool);
    if (artworks)            extmem_free(artworks);
}

inline uint32_t RekordboxParser::readUInt32(uint8_t* data, uint32_t offset) {
    return data[offset] | 
           (data[offset+1] << 8) | 
           (data[offset+2] << 16) | 
           (data[offset+3] << 24);
}

inline uint16_t RekordboxParser::readUInt16(uint8_t* data, uint32_t offset) {
    return data[offset] | (data[offset+1] << 8);
}

inline bool RekordboxParser::readPage(uint32_t page_num) {
    uint32_t offset = page_num * RB_PAGE_SIZE;
    
    if (!file.seek(offset)) {
        return false;
    }
    
    size_t bytes_read = file.read(buffer, RB_PAGE_SIZE);
    return (bytes_read == RB_PAGE_SIZE);
}

// Helper: Read a DeviceSQL string from buffer at given position into output char array
// Returns the number of characters read, or 0 on failure
inline uint16_t readDeviceSQLString(uint8_t* page_data, uint16_t str_pos, char* output, uint16_t max_len) {
    if (str_pos >= RB_PAGE_SIZE) return 0;
    
    uint8_t len_kind = page_data[str_pos];
    
    if (len_kind & 0x01) {
        // Short ASCII string: length is (len_kind >> 1) - 1
        uint8_t str_len = (len_kind >> 1) - 1;
        if (str_len == 0 || str_len >= max_len) return 0;
        if (str_pos + 1 + str_len > RB_PAGE_SIZE) return 0;
        
        memcpy(output, &page_data[str_pos + 1], str_len);
        output[str_len] = '\0';
        return str_len;
    } else if (len_kind == 0x40) {
        // Long ASCII string: next 2 bytes are total field length
        if (str_pos + 3 >= RB_PAGE_SIZE) return 0;
        uint16_t field_len = page_data[str_pos + 1] | (page_data[str_pos + 2] << 8);
        uint16_t str_len = field_len - 4;  // subtract header overhead
        if (str_len == 0 || str_len >= max_len) return 0;
        if (str_pos + 4 + str_len > RB_PAGE_SIZE) return 0;
        
        memcpy(output, &page_data[str_pos + 4], str_len);
        output[str_len] = '\0';
        return str_len;
    } else if (len_kind == 0x90) {
        // UTF-16LE string: next 2 bytes are total field length
        if (str_pos + 3 >= RB_PAGE_SIZE) return 0;
        uint16_t field_len = page_data[str_pos + 1] | (page_data[str_pos + 2] << 8);
        uint16_t str_data_len = (field_len - 4) / 2;  // number of UTF-16 chars
        if (str_data_len == 0 || str_data_len >= max_len) return 0;
        if (str_pos + 4 + (str_data_len * 2) > RB_PAGE_SIZE) return 0;
        
        // Convert UTF-16LE to ASCII (take low byte of each char)
        for (uint16_t i = 0; i < str_data_len && i < max_len - 1; i++) {
            output[i] = page_data[str_pos + 4 + (i * 2)];
        }
        uint16_t copy_len = (str_data_len < max_len - 1) ? str_data_len : max_len - 1;
        output[copy_len] = '\0';
        return copy_len;
    }
    
    return 0;  // Unknown string format
}

inline void RekordboxParser::parseTrackEntry(uint8_t* page_data, uint16_t offset, uint32_t page_offset) {
    // Track row structure per Deep Symmetry docs:
    // Bytes 0x00-0x01: subtype (always 0x0024 for tracks)
    // Bytes 0x04-0x07: bitmask
    // Bytes 0x08-0x0b: sample_rate
    // Bytes 0x20-0x23: key_id
    // Bytes 0x28-0x2b: label_id
    // Bytes 0x38-0x3b: tempo (BPM * 100)
    // Bytes 0x3c-0x3f: genre_id
    // Bytes 0x40-0x43: album_id
    // Bytes 0x44-0x47: artist_id
    // Bytes 0x48-0x4b: id (track ID)
    // Bytes 0x54-0x55: duration (seconds)
    // Byte  0x58: color_id
    // Byte  0x59: rating
    // Bytes 0x5a-0x5b: file_type
    // Bytes 0x5c-0x5d: u7 (always 0x0003)
    // Bytes 0x5e onwards: 21 x uint16 string offsets
    //   Index 14 (at 0x5e + 28 = 0x7a): analyze_path
    //   Index 17 (at 0x5e + 34 = 0x80): title
    //   Index 20 (at 0x5e + 40 = 0x86): file_path (audio)
    
    // Verify subtype is 0x0024
    uint16_t subtype = readUInt16(page_data, offset);
    if (subtype != 0x0024) {
        return;
    }
    
    // Need at least 0x88 bytes for the full row header + string offsets
    if (offset + 0x88 > RB_PAGE_SIZE) {
        return;
    }
    
    // Read track ID at offset 0x48
    uint16_t track_id = readUInt16(page_data, offset + 0x48);
    
    if (track_id == 0 || track_id > MAX_TRACKS) {
        return;
    }
    
    // Find existing track or create new slot
    int track_index = -1;
    for (uint16_t i = 0; i < track_count; i++) {
        if (tracks[i].id == track_id) {
            track_index = i;
            break;
        }
    }
    
    if (track_index == -1) {
        if (track_count >= MAX_TRACKS) return;
        track_index = track_count++;
    }
    
    Track* track = &tracks[track_index];
    
    // Set ID and valid flag
    track->id = track_id;
    track->valid = true;
    
    // Key ID at offset 0x20 (4 bytes, use low byte)
    track->key_id = page_data[offset + 0x20];
    
    // BPM (tempo) at offset 0x38 (stored as BPM * 100, 4 bytes)
    uint32_t bpm_raw = readUInt32(page_data, offset + 0x38);
    track->bpm = bpm_raw / 100.0f;
    
    // Duration at offset 0x54 (seconds, 2 bytes)
    track->duration = readUInt16(page_data, offset + 0x54);
    
    // Rating at byte 0x59
    track->rating = page_data[offset + 0x59];
    
    // File offset for reference
    track->file_offset = page_offset + offset;

    // Artwork ID at offset 0x1c (4 bytes) — links to artwork table row
    track->artwork_id = readUInt32(page_data, offset + 0x1c);
    track->thumbnail_path[0] = '\0';  // cleared; filled by parseArtworkTable
    
    // Artist ID at offset 0x44 (4 bytes)
    uint32_t artist_id = readUInt32(page_data, offset + 0x44);
    
    const char* artist_name = getArtistName(artist_id);
    
    #ifdef DEBUG_PARSER
    Serial.printf("  Track %d: artist_id=%d, artist_count=%d, artist_name='%s'\n", 
                  track_id, artist_id, artist_count, artist_name ? artist_name : "NULL");
    #endif
    
    if (artist_name && artist_name[0] != '\0') {
        strncpy(track->artist, artist_name, MAX_TITLE_LENGTH - 1);
        track->artist[MAX_TITLE_LENGTH - 1] = '\0';
    }
    
    // Album ID at offset 0x40 (4 bytes)
    uint32_t album_id = readUInt32(page_data, offset + 0x40);
    const char* album_name = getAlbumName(album_id);
    
    #ifdef DEBUG_PARSER
    Serial.printf("  Track ID %d: album_id=%d\n", track_id, album_id);
    Serial.printf("    -> album_name='%s'\n", album_name ? album_name : "NULL");
    #endif
    
    if (album_name && album_name[0] != '\0') {
        strncpy(track->album, album_name, MAX_TITLE_LENGTH - 1);
        track->album[MAX_TITLE_LENGTH - 1] = '\0';
    }
    
    // Genre ID at offset 0x3C (4 bytes)
    uint32_t genre_id = readUInt32(page_data, offset + 0x3C);
    const char* genre_name = getGenreName(genre_id);
    if (genre_name && genre_name[0] != '\0') {
        strncpy(track->genre, genre_name, MAX_TITLE_LENGTH - 1);
        track->genre[MAX_TITLE_LENGTH - 1] = '\0';
    }
    
    // Label ID at offset 0x28 (4 bytes)
    uint32_t label_id = readUInt32(page_data, offset + 0x28);
    const char* label_name = getLabelName(label_id);
    if (label_name && label_name[0] != '\0') {
        strncpy(track->label, label_name, MAX_TITLE_LENGTH - 1);
        track->label[MAX_TITLE_LENGTH - 1] = '\0';
    }
    
    // ========================================================================
    // Read strings using the 21 x uint16 offset array starting at byte 0x5e
    // Each offset is relative to the start of the track row
    // ========================================================================
    
    // String index 17 = title (offset at 0x5e + 17*2 = 0x80)
    uint16_t title_str_offset = readUInt16(page_data, offset + 0x80);
    if (title_str_offset > 0 && title_str_offset < RB_PAGE_SIZE) {
        uint16_t title_pos = offset + title_str_offset;
        readDeviceSQLString(page_data, title_pos, track->title, MAX_TITLE_LENGTH);
    }
    
    #ifdef DEBUG_PARSER
    Serial.printf("  Track ID %d: title='%s'\n", track_id, track->title);
    #endif
    
    // String index 14 = analyze_path (offset at 0x5e + 14*2 = 0x7a)
    uint16_t anlz_str_offset = readUInt16(page_data, offset + 0x7a);
    if (anlz_str_offset > 0 && anlz_str_offset < RB_PAGE_SIZE) {
        uint16_t anlz_pos = offset + anlz_str_offset;
        char temp_path[128];
        temp_path[0] = '\0';
        readDeviceSQLString(page_data, anlz_pos, temp_path, 128);
        
        // Strip leading "Y/" if present
        const char* anlz_src = temp_path;
        if (anlz_src[0] == 'Y' && anlz_src[1] == '/') {
            anlz_src += 2;
        }
        strncpy(track->anlz_path, anlz_src, 127);
        track->anlz_path[127] = '\0';
        
        size_t path_len = strlen(track->anlz_path);
        
        // Generate .EXT path from .DAT path
        strncpy(track->anlz_ext_path, track->anlz_path, 127);
        track->anlz_ext_path[127] = '\0';
        if (path_len >= 4) {
            track->anlz_ext_path[path_len - 3] = 'E';
            track->anlz_ext_path[path_len - 2] = 'X';
            track->anlz_ext_path[path_len - 1] = 'T';
        }
        
        // Generate .2EX path from .DAT path
        strncpy(track->anlz_ex2_path, track->anlz_path, 127);
        track->anlz_ex2_path[127] = '\0';
        if (path_len >= 4) {
            track->anlz_ex2_path[path_len - 3] = '2';
            track->anlz_ex2_path[path_len - 2] = 'E';
            track->anlz_ex2_path[path_len - 1] = 'X';
        }
    }
    
    // String index 20 = file_path / audio path (offset at 0x5e + 20*2 = 0x86)
    uint16_t audio_str_offset = readUInt16(page_data, offset + 0x86);
    if (audio_str_offset > 0 && audio_str_offset < RB_PAGE_SIZE) {
        uint16_t audio_pos = offset + audio_str_offset;
        char temp_audio[128];
        temp_audio[0] = '\0';
        readDeviceSQLString(page_data, audio_pos, temp_audio, 128);
        
        // Strip leading "Y/" if present
        const char* audio_src = temp_audio;
        if (audio_src[0] == 'Y' && audio_src[1] == '/') {
            audio_src += 2;
        }
        strncpy(track->audio_path, audio_src, 127);
        track->audio_path[127] = '\0';
    }
}

inline void RekordboxParser::parseKeysTable(uint32_t first_page, uint32_t last_page) {
    uint32_t current_page = first_page;
    
    while (current_page != 0 && current_page <= last_page) {
        if (!readPage(current_page)) break;
        
        // Get next page
        uint32_t next_page = readUInt32(buffer, 12);
        
        // Verify page type
        uint32_t page_type = readUInt32(buffer, 8);
        uint8_t flags = buffer[27];
        uint8_t num_rows = buffer[24];
        
        if (page_type != 5 || (flags & 0x40) != 0 || num_rows == 0) {
            current_page = next_page;
            continue;
        }
        
        // Parse key entries
        uint16_t cursor = 40;
        
        for (uint8_t row = 0; row < num_rows && key_count < MAX_KEYS; row++) {
            uint8_t key_id = buffer[cursor];
            
            if (key_id >= 1 && key_id <= 24) {
                Key* key = &keys[key_count++];
                key->id = key_id;
                
                // Read 4-character key name
                key->name[0] = buffer[cursor + 9];
                key->name[1] = buffer[cursor + 10];
                key->name[2] = buffer[cursor + 11];
                key->name[3] = buffer[cursor + 12];
                key->name[4] = '\0';
                
                // Validate characters
                if (key->name[3] < 33 || key->name[3] > 125) {
                    key->name[3] = '\0';
                }
                
                // Handle different name lengths
                uint8_t name_len = buffer[cursor + 8];
                if (name_len == 5) {
                    key->name[1] = '\0';
                    cursor += 12;
                } else if (name_len == 7) {
                    key->name[2] = '\0';
                    cursor += 12;
                } else if (name_len == 9) {
                    key->name[3] = '\0';
                    cursor += 12;
                } else {
                    cursor += 12 + 4 * ((((name_len - 1) / 2) - 1) / 4);
                }
            } else {
                break;
            }
        }
        
        current_page = next_page;
    }
}

inline void RekordboxParser::parseArtistsTable(uint32_t first_page, uint32_t last_page) {
    uint32_t current_page = first_page;
    uint8_t page_count = 0;
    const uint8_t MAX_PAGES = 100;
    
    #ifdef DEBUG_PARSER
    Serial.printf("Parsing artists: first_page=%d, last_page=%d\n", first_page, last_page);
    #endif
    
    // Check if first_page is valid (within reasonable bounds)
    if (first_page == 0 || first_page > 100) {
        // Invalid pointer - scan all pages instead
        #ifdef DEBUG_PARSER
        Serial.println("Invalid artists pointer - scanning all pages");
        #endif
        
        // Get total pages from header
        if (!readPage(0)) return;
        uint32_t next_unused = readUInt32(buffer, 20);
        if (next_unused == 0 || next_unused > 1000) next_unused = 100;
        
        // Scan all pages looking for artist entries
        for (uint32_t page_num = 1; page_num < next_unused && page_num < 100; page_num++) {
            if (!readPage(page_num)) continue;
            
            uint8_t flags = buffer[0x1b];
            if ((flags & 0x40) != 0) continue;  // Skip strange pages
            
            // Row count is at byte 0x18
            uint8_t num_rows_page = buffer[0x18];
            if (num_rows_page == 0 || num_rows_page > 16) continue;
            
            // Row offsets are in group 0, slots (15-N) through 14
            uint16_t group_offset = RB_PAGE_SIZE - 36;
            
            for (uint8_t slot = (15 - num_rows_page); slot < 15 && artist_count < MAX_ARTISTS; slot++) {
                uint16_t row_offset = readUInt16(buffer, group_offset + 2 + slot * 2);
                uint16_t row_start = 0x28 + row_offset;
                
                if (row_start + 20 >= RB_PAGE_SIZE) continue;
                
                // Check for artist subtype
                uint16_t subtype = readUInt16(buffer, row_start);
                if (subtype != 0x0060 && subtype != 0x0064) continue;
                
                // Get artist ID
                uint32_t artist_id = readUInt32(buffer, row_start + 4);
                if (artist_id == 0 || artist_id > 10000) continue;
                
                // Check if already exists
                bool exists = false;
                for (uint16_t i = 0; i < artist_count; i++) {
                    if (artists[i].id == artist_id) {
                        exists = true;
                        break;
                    }
                }
                if (exists) continue;
                
                // Get name offset
                uint16_t name_offset;
                if (subtype == 0x0060) {
                    name_offset = buffer[row_start + 9];
                } else {
                    name_offset = readUInt16(buffer, row_start + 10);
                }
                
                uint16_t name_pos = row_start + name_offset;
                if (name_pos >= RB_PAGE_SIZE) continue;
                
                // Parse DeviceSQL string
                uint8_t len_kind = buffer[name_pos];
                
                if (len_kind & 0x01) {
                    // Short ASCII string
                    uint8_t str_len = (len_kind >> 1) - 1;
                    if (str_len > 0 && str_len < MAX_TITLE_LENGTH && name_pos + 1 + str_len <= RB_PAGE_SIZE) {
                        Artist* artist = &artists[artist_count];
                        artist->id = artist_id;
                        memcpy(artist->name, &buffer[name_pos + 1], str_len);
                        artist->name[str_len] = '\0';
                        
                        #ifdef DEBUG_PARSER
                        Serial.printf("Found artist ID=%d: '%s'\n", artist_id, artist->name);
                        #endif
                        
                        artist_count++;
                    }
                } else if (len_kind == 0x90) {
                    // UTF-16LE string
                    uint16_t str_field_len = readUInt16(buffer, name_pos + 1);
                    uint16_t str_data_len = (str_field_len - 4) / 2;
                    
                    if (str_data_len > 0 && str_data_len < MAX_TITLE_LENGTH && name_pos + 4 + (str_data_len * 2) <= RB_PAGE_SIZE) {
                        Artist* artist = &artists[artist_count];
                        artist->id = artist_id;
                        
                        for (uint16_t i = 0; i < str_data_len && i < MAX_TITLE_LENGTH - 1; i++) {
                            artist->name[i] = buffer[name_pos + 4 + (i * 2)];
                        }
                        artist->name[str_data_len < MAX_TITLE_LENGTH ? str_data_len : MAX_TITLE_LENGTH - 1] = '\0';
                        
                        #ifdef DEBUG_PARSER
                        Serial.printf("Found artist ID=%d: '%s'\n", artist_id, artist->name);
                        #endif
                        
                        artist_count++;
                    }
                }
            }
        }
        
        #ifdef DEBUG_PARSER
        Serial.printf("Scanned all pages - found %d artists\n", artist_count);
        #endif
        
        return;  // Done with scan
    }
    
    // Original code for valid pointers - follow next_page links
    while (current_page != 0 && page_count++ < MAX_PAGES) {
        if (!readPage(current_page)) break;
        
        uint32_t next_page = readUInt32(buffer, 12);
        uint32_t page_type = readUInt32(buffer, 8);
        uint8_t flags = buffer[27];
        
        #ifdef DEBUG_PARSER
        Serial.printf("  Artist page %d: type=%d, flags=0x%02x, next=%d\n", current_page, page_type, flags, next_page);
        #endif
        
        // Don't filter by page_type
        if ((flags & 0x40) != 0) {
            current_page = next_page;
            continue;
        }
        
        // Row count is at byte 0x18
        uint8_t num_rows_page = buffer[0x18];
        
        #ifdef DEBUG_PARSER
        Serial.printf("    num_rows=%d\n", num_rows_page);
        #endif
        
        if (num_rows_page > 0 && num_rows_page <= 16) {
            uint16_t group_offset = RB_PAGE_SIZE - 36;
            
            for (uint8_t slot = (15 - num_rows_page); slot < 15 && artist_count < MAX_ARTISTS; slot++) {
                uint16_t row_offset = readUInt16(buffer, group_offset + 2 + slot * 2);
                uint16_t row_start = 0x28 + row_offset;
            
                if (row_start + 10 >= RB_PAGE_SIZE) continue;
            
            uint16_t subtype = readUInt16(buffer, row_start);
            
            if (subtype != 0x0060 && subtype != 0x0064) {
                continue;
            }
            
            uint32_t artist_id = readUInt32(buffer, row_start + 4);
            if (artist_id == 0 || artist_id > 10000) {
                continue;
            }
            
            uint16_t name_offset;
            if (subtype == 0x0060) {
                name_offset = buffer[row_start + 9];
            } else {
                name_offset = readUInt16(buffer, row_start + 10);
            }
            
            uint16_t name_pos = row_start + name_offset;
            if (name_pos >= RB_PAGE_SIZE) continue;
            
            uint8_t len_kind = buffer[name_pos];
            
            if (len_kind & 0x01) {
                uint8_t str_len = (len_kind >> 1) - 1;
                if (str_len > 0 && str_len < MAX_TITLE_LENGTH && name_pos + 1 + str_len <= RB_PAGE_SIZE) {
                    Artist* artist = &artists[artist_count];
                    artist->id = artist_id;
                    memcpy(artist->name, &buffer[name_pos + 1], str_len);
                    artist->name[str_len] = '\0';
                    artist_count++;
                }
            } else if (len_kind == 0x90) {
                uint16_t str_field_len = readUInt16(buffer, name_pos + 1);
                uint16_t str_data_len = (str_field_len - 4) / 2;
                
                if (str_data_len > 0 && str_data_len < MAX_TITLE_LENGTH && name_pos + 4 + (str_data_len * 2) <= RB_PAGE_SIZE) {
                    Artist* artist = &artists[artist_count];
                    artist->id = artist_id;
                    
                    for (uint16_t i = 0; i < str_data_len && i < MAX_TITLE_LENGTH - 1; i++) {
                        artist->name[i] = buffer[name_pos + 4 + (i * 2)];
                    }
                    artist->name[str_data_len < MAX_TITLE_LENGTH ? str_data_len : MAX_TITLE_LENGTH - 1] = '\0';
                    artist_count++;
                }
            }
            }
        }
        
        current_page = next_page;
    }
    
    #ifdef DEBUG_PARSER
    Serial.printf("Total artists parsed: %d\n", artist_count);
    #endif
}
inline void RekordboxParser::parseAlbumsTable(uint32_t first_page, uint32_t last_page) {
    uint32_t current_page = first_page;
    
    while (current_page != 0 && current_page <= last_page) {
        if (!readPage(current_page)) break;
        
        uint32_t next_page = readUInt32(buffer, 12);
        uint32_t page_type = readUInt32(buffer, 8);
        uint8_t flags = buffer[27];
        
        if (page_type != 3 || (flags & 0x40) != 0) {
            current_page = next_page;
            continue;
        }
        
        // Parse album entries
        for (uint16_t offset = 40; offset < RB_PAGE_SIZE - 20 && album_count < MAX_ALBUMS; offset++) {
            uint16_t album_id = readUInt16(buffer, offset);
            
            if (album_id > 0 && album_id < 10000) {
                for (uint16_t str_off = 8; str_off < 32; str_off += 2) {
                    if (offset + str_off + 2 >= RB_PAGE_SIZE) break;
                    
                    uint8_t len_byte = buffer[offset + str_off];
                    if (len_byte > 4 && len_byte < 200 && buffer[offset + str_off + 1] == 0) {
                        uint8_t name_len = ((len_byte - 1) / 2) - 1;
                        
                        if (name_len > 2 && name_len < MAX_TITLE_LENGTH) {
                            Album* album = &albums[album_count];
                            album->id = album_id;
                            
                            for (uint8_t i = 0; i < name_len && i < MAX_TITLE_LENGTH - 1; i++) {
                                album->name[i] = buffer[offset + str_off + 2 + i];
                            }
                            album->name[name_len < MAX_TITLE_LENGTH ? name_len : MAX_TITLE_LENGTH - 1] = '\0';
                            
                            if (album->name[0] >= 32) {
                                album_count++;
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        current_page = next_page;
    }
}

inline void RekordboxParser::parseGenresTable(uint32_t first_page, uint32_t last_page) {
    uint32_t current_page = first_page;
    
    while (current_page != 0 && current_page <= last_page) {
        if (!readPage(current_page)) break;
        
        uint32_t next_page = readUInt32(buffer, 12);
        uint32_t page_type = readUInt32(buffer, 8);
        uint8_t flags = buffer[27];
        
        if (page_type != 1 || (flags & 0x40) != 0) {
            current_page = next_page;
            continue;
        }
        
        // Parse genre entries
        for (uint16_t offset = 40; offset < RB_PAGE_SIZE - 20 && genre_count < MAX_GENRES; offset++) {
            uint16_t genre_id = readUInt16(buffer, offset);
            
            if (genre_id > 0 && genre_id < 10000) {
                for (uint16_t str_off = 8; str_off < 32; str_off += 2) {
                    if (offset + str_off + 2 >= RB_PAGE_SIZE) break;
                    
                    uint8_t len_byte = buffer[offset + str_off];
                    if (len_byte > 4 && len_byte < 200 && buffer[offset + str_off + 1] == 0) {
                        uint8_t name_len = ((len_byte - 1) / 2) - 1;
                        
                        if (name_len > 2 && name_len < MAX_TITLE_LENGTH) {
                            Genre* genre = &genres[genre_count];
                            genre->id = genre_id;
                            
                            for (uint8_t i = 0; i < name_len && i < MAX_TITLE_LENGTH - 1; i++) {
                                genre->name[i] = buffer[offset + str_off + 2 + i];
                            }
                            genre->name[name_len < MAX_TITLE_LENGTH ? name_len : MAX_TITLE_LENGTH - 1] = '\0';
                            
                            if (genre->name[0] >= 32) {
                                genre_count++;
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        current_page = next_page;
    }
}

inline void RekordboxParser::parseLabelsTable(uint32_t first_page, uint32_t last_page) {
    uint32_t current_page = first_page;
    
    while (current_page != 0 && current_page <= last_page) {
        if (!readPage(current_page)) break;
        
        uint32_t next_page = readUInt32(buffer, 12);
        uint32_t page_type = readUInt32(buffer, 8);
        uint8_t flags = buffer[27];
        
        if (page_type != 4 || (flags & 0x40) != 0) {
            current_page = next_page;
            continue;
        }
        
        // Parse label entries
        for (uint16_t offset = 40; offset < RB_PAGE_SIZE - 20 && label_count < MAX_LABELS; offset++) {
            uint16_t label_id = readUInt16(buffer, offset);
            
            if (label_id > 0 && label_id < 10000) {
                for (uint16_t str_off = 8; str_off < 32; str_off += 2) {
                    if (offset + str_off + 2 >= RB_PAGE_SIZE) break;
                    
                    uint8_t len_byte = buffer[offset + str_off];
                    if (len_byte > 4 && len_byte < 200 && buffer[offset + str_off + 1] == 0) {
                        uint8_t name_len = ((len_byte - 1) / 2) - 1;
                        
                        if (name_len > 2 && name_len < MAX_TITLE_LENGTH) {
                            Label* label = &labels[label_count];
                            label->id = label_id;
                            
                            for (uint8_t i = 0; i < name_len && i < MAX_TITLE_LENGTH - 1; i++) {
                                label->name[i] = buffer[offset + str_off + 2 + i];
                            }
                            label->name[name_len < MAX_TITLE_LENGTH ? name_len : MAX_TITLE_LENGTH - 1] = '\0';
                            
                            if (label->name[0] >= 32) {
                                label_count++;
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        current_page = next_page;
    }
}

inline void RekordboxParser::parsePlaylistTree(uint32_t first_page, uint32_t last_page) {
    // Linear heap walk — matches reference implementation exactly.
    // Row layout at cursor:
    //   +0x00 u32 parent_id  (ignored)
    //   +0x04 u32 unknown
    //   +0x08 u32 sort_order
    //   +0x0c u8  pl_id      (low byte only — single byte in reference)
    //   +0x10 u32 raw_is_folder (0 = playlist)
    //   +0x14 u8  name_len_byte (lk: short ASCII, (lk-1)/2 - 1 = char count)
    //   +0x15 ... name chars
    // Row stride: 24 + 4*(name_len/4)
    // Last-write-wins per pl_id (overwrites same slot each time).

    uint32_t current_page = first_page;

    while (current_page != 0 && current_page <= last_page) {
        if (!readPage(current_page)) break;

        uint32_t next_page = readUInt32(buffer, 12);

        // Reference checks: type==7, not index page, bytes 36-37==0, buffer[24]!=0
        if (buffer[8]!=7 || (buffer[27]&0x40)!=0 || buffer[36]!=0 || buffer[37]!=0 || buffer[24]==0) {
            current_page = next_page;
            continue;
        }

        uint8_t  row_count = buffer[24];   // plain byte, same as reference
        uint16_t cursor    = 40;

        for (uint8_t row = 0; row < row_count + 1 && cursor + 24 < RB_PAGE_SIZE; row++) {
            uint8_t lk       = buffer[cursor + 0x14];
            uint8_t name_len = (lk >= 3) ? ((lk - 1) / 2) - 1 : 0;

            bool is_playlist = (buffer[cursor+16]==0 && buffer[cursor+17]==0 &&
                                buffer[cursor+18]==0 && buffer[cursor+19]==0);
            uint8_t pl_id    = buffer[cursor + 12];   // single byte, matches reference

            if (is_playlist && pl_id > 0 && pl_id <= MAX_PLAYLISTS) {
                // Find existing slot or create new one
                int16_t slot = -1;
                for (uint16_t x = 0; x < playlist_count; x++) {
                    if ((uint8_t)playlists[x].id == pl_id) { slot = x; break; }
                }
                if (slot < 0) {
                    if (playlist_count >= MAX_PLAYLISTS) {
                        cursor += 24 + 4*(name_len/4);
                        continue;
                    }
                    slot = playlist_count++;
                    playlists[slot].id           = pl_id;
                    playlists[slot].track_count  = 0;
                    // Reserve MAX_TRACKS contiguous slots in the pool for this playlist
                    playlists[slot].track_offset = (uint16_t)(slot * MAX_TRACKS);
                }

                // Overwrite name — last write wins (highest heap addr = most recent)
                uint8_t copy = (name_len < MAX_PLAYLIST_NAME - 1) ? name_len : MAX_PLAYLIST_NAME - 1;
                for (uint8_t i = 0; i < copy; i++) {
                    playlists[slot].name[i] = (cursor + 0x15 + i < RB_PAGE_SIZE)
                                              ? buffer[cursor + 0x15 + i] : 0;
                }
                playlists[slot].name[copy] = '\0';

                #ifdef DEBUG_PARSER
                Serial.printf("Playlist #%u: id=%u name='%s'\n", slot, pl_id, playlists[slot].name);
                #endif
            }

            cursor += 24 + 4*(name_len/4);
        }

        current_page = next_page;
    }
}

inline void RekordboxParser::parsePlaylistEntries(uint32_t first_page, uint32_t last_page) {
    // Linear heap walk — matches reference implementation exactly.
    // Row layout (12 bytes each):
    //   +0x00 u16 entry_index  (1-based position in playlist)
    //   +0x04 u16 track_id
    //   +0x08 u8  playlist_id  (single byte in reference)
    // Row count from buffer[34]+256*buffer[35] (+1).
    // Last-write-wins per (playlist, entry_index) slot.

    uint32_t current_page = first_page;

    while (current_page != 0 && current_page <= last_page) {
        if (!readPage(current_page)) break;

        uint32_t next_page = readUInt32(buffer, 12);

        // Reference checks: type==8, not index, bytes 36-37==0, buf[24] or buf[25] nonzero
        if (buffer[8]!=8 || (buffer[27]&0x40)!=0 || buffer[36]!=0 || buffer[37]!=0 ||
            (buffer[24]==0 && buffer[25]==0)) {
            current_page = next_page;
            continue;
        }

        uint16_t row_count = buffer[34] + 256*buffer[35];  // matches reference exactly
        uint16_t cursor    = 40;

        for (uint16_t row = 0; row < row_count + 1 && cursor + 12 <= RB_PAGE_SIZE; row++) {
            uint16_t entry_index = buffer[cursor]   + 256*buffer[cursor+1];
            uint16_t track_id   = buffer[cursor+4]  + 256*buffer[cursor+5];
            uint8_t  playlist_id = buffer[cursor+8];   // single byte

            if (entry_index == 0 || track_id == 0 || playlist_id == 0) {
                cursor += 12; continue;
            }

            // Find playlist slot
            for (uint16_t i = 0; i < playlist_count; i++) {
                if ((uint8_t)playlists[i].id == playlist_id) {
                    uint16_t ei_slot = entry_index - 1;  // 0-based
                    // Write into pool at playlist's reserved offset + ei_slot.
                    // Each playlist reserves MAX_TRACKS slots during parsing.
                    // deduplicatePlaylists() will compact these afterwards.
                    if (ei_slot < MAX_TRACKS) {
                        uint16_t pool_idx = playlists[i].track_offset + ei_slot;
                        if (pool_idx < MAX_PLAYLIST_TRACK_POOL) {
                            playlist_track_pool[pool_idx] = track_id;
                            if (ei_slot >= playlists[i].track_count) {
                                playlists[i].track_count = ei_slot + 1;
                            }
                        }
                    }
                    break;
                }
            }

            cursor += 12;
        }

        current_page = next_page;
    }
}

inline void RekordboxParser::parseHistory(uint32_t first_page, uint32_t last_page) {
    uint32_t current_page = first_page;
    
    while (current_page != 0 && current_page <= last_page) {
        if (!readPage(current_page)) break;
        
        uint32_t next_page = readUInt32(buffer, 12);
        uint32_t page_type = readUInt32(buffer, 8);
        uint8_t flags = buffer[27];
        uint8_t num_rows = buffer[24];
        
        if (page_type != 19 || (flags & 0x40) != 0 || num_rows == 0) {
            current_page = next_page;
            continue;
        }
        
        uint16_t cursor = 40;
        
        for (uint8_t row = 0; row < num_rows && cursor < RB_PAGE_SIZE - 20; row++) {
            if (buffer[cursor] == 0x80 && buffer[cursor + 1] == 0x02) {
                cursor += 12;
                
                // Date string
                uint8_t date_len = ((buffer[cursor] - 1) / 2) - 1;
                if (date_len < sizeof(sd_date) - 1) {
                    for (uint8_t i = 0; i < date_len; i++) {
                        sd_date[i] = buffer[cursor + 1 + i];
                    }
                    sd_date[date_len] = '\0';
                }
                cursor += date_len + 3;
                
                // Skip next string
                uint8_t skip_len = ((buffer[cursor] - 1) / 2) - 1;
                cursor += skip_len + 1;
                
                // SD card name
                uint8_t name_len = ((buffer[cursor] - 1) / 2) - 1;
                if (name_len < sizeof(sd_name) - 1) {
                    for (uint8_t i = 0; i < name_len; i++) {
                        sd_name[i] = buffer[cursor + 1 + i];
                    }
                    sd_name[name_len] = '\0';
                }
                
                return;  // Found what we need
            }
        }
        
        current_page = next_page;
    }
}

inline void RekordboxParser::deduplicateTracks() {
    // Tracks are already deduplicated by the parsing logic
    // which overwrites earlier entries with later ones
    // Just compact the array to remove gaps
    
    uint16_t write_pos = 0;
    for (uint16_t i = 0; i < track_count; i++) {
        if (tracks[i].valid) {
            if (i != write_pos) {
                tracks[write_pos] = tracks[i];
            }
            write_pos++;
        }
    }
    track_count = write_pos;
}

inline void RekordboxParser::deduplicatePlaylists() {
    // Each playlist's slots in playlist_track_pool are indexed by entry_index-1
    // and may have gaps (0 = slot never written). Compact each playlist's
    // region of the pool in-place, then update track_offset/track_count so
    // all playlists are tightly packed at the front of the pool.
    uint16_t write_pool = 0;
    for (uint16_t i = 0; i < playlist_count; i++) {
        uint16_t new_offset = write_pool;
        uint16_t src_base   = playlists[i].track_offset;
        uint16_t new_count  = 0;
        for (uint16_t j = 0; j < playlists[i].track_count; j++) {
            uint16_t tid = playlist_track_pool[src_base + j];
            if (tid != 0) {
                playlist_track_pool[write_pool++] = tid;
                new_count++;
            }
        }
        playlists[i].track_offset = new_offset;
        playlists[i].track_count  = new_count;
    }
    pool_used = write_pool;
}

inline bool RekordboxParser::parse(const char* filename) {
    // Auto-allocate if not done yet (covers the no-arena extmem_malloc path)
    if (!tracks) allocate();

    // Critical arrays must be valid — artworks is optional
    if (!tracks || !keys || !artists || !albums || !genres || !labels
        || !playlists || !playlist_track_pool) {
        Serial.println("ERROR: allocation failed. Check arena size or EXTMEM availability.");
        return false;
    }
    
    // Open file
    file = SD.open(filename, FILE_READ);
    if (!file) {
        Serial.println("Failed to open file");
        return false;
    }
    
    // Read page 0 (header)
    if (!readPage(0)) {
        Serial.println("Failed to read header");
        file.close();
        return false;
    }
    
    uint32_t page_size = readUInt32(buffer, 4);
    uint32_t num_tables = readUInt32(buffer, 8);
    
    if (page_size != RB_PAGE_SIZE) {
        Serial.println("Unexpected page size");
        file.close();
        return false;
    }
    
    Serial.print("Tables: ");
    Serial.println(num_tables);
    
    // Parse table directory
    uint32_t tracks_first = 0, tracks_last = 0;
    uint32_t artists_first = 0, artists_last = 0;
    uint32_t albums_first = 0, albums_last = 0;
    uint32_t genres_first = 0, genres_last = 0;
    uint32_t labels_first = 0, labels_last = 0;
    uint32_t keys_first = 0, keys_last = 0;
    uint32_t playlists_tree_first = 0, playlists_tree_last = 0;
    uint32_t playlists_entries_first = 0, playlists_entries_last = 0;
    uint32_t history_first = 0; (void)history_first;  // reserved for future use
    uint32_t artwork_first = 0, artwork_last = 0;
    
    uint16_t cursor = 28;
    for (uint32_t i = 0; i < num_tables; i++) {
        uint32_t table_type = readUInt32(buffer, cursor);
        uint32_t first_page = readUInt32(buffer, cursor + 8);
        uint32_t last_page = readUInt32(buffer, cursor + 12);
        
        switch (table_type) {
            case 0:  // Tracks
                tracks_first = first_page;
                tracks_last = last_page;
                break;
            case 1:  // Genres
                genres_first = first_page;
                genres_last = last_page;
                break;
            case 2:  // Artists
                artists_first = first_page;
                artists_last = last_page;
                break;
            case 3:  // Albums
                albums_first = first_page;
                albums_last = last_page;
                break;
            case 4:  // Labels
                labels_first = first_page;
                labels_last = last_page;
                break;
            case 5:  // Keys
                keys_first = first_page;
                keys_last = last_page;
                break;
            case 7:  // Playlist tree
                playlists_tree_first = first_page;
                playlists_tree_last = last_page;
                break;
            case 8:  // Playlist entries
                playlists_entries_first = first_page;
                playlists_entries_last = last_page;
                break;
            case 13:  // Artwork
                artwork_first = first_page;
                artwork_last  = last_page;
                break;
            case 19:  // History (parsing disabled)
                (void)first_page; (void)last_page;
                break;
        }
        
        cursor += 16;
    }
    
    // Parse reference tables FIRST (so tracks can look up names)
    Serial.println("Parsing artists...");
    if (artists_first != 0) {
        parseArtistsTable(artists_first, artists_last);
    }
    
    Serial.println("Parsing albums...");
    if (albums_first != 0) {
        parseAlbumsTable(albums_first, albums_last);
    }
    
    Serial.println("Parsing genres...");
    if (genres_first != 0) {
        parseGenresTable(genres_first, genres_last);
    }
    
    Serial.println("Parsing labels...");
    if (labels_first != 0) {
        parseLabelsTable(labels_first, labels_last);
    }
    
    Serial.println("Parsing keys...");
    if (keys_first != 0) {
        parseKeysTable(keys_first, keys_last);
    }
    
    Serial.println("Parsing tracks...");
    if (tracks_first != 0) {
        uint32_t current_page = tracks_first;

        while (current_page != 0 && current_page <= tracks_last) {
            if (!readPage(current_page)) break;

            uint32_t next_page = readUInt32(buffer, 12);
            uint8_t  flags     = buffer[0x1b];

            if ((flags & 0x40) == 0) {
                // num_row_offsets: top 13 bits of the packed 24-bit field at 0x18
                uint32_t packed = buffer[0x18] | (buffer[0x19] << 8) | (buffer[0x1a] << 16);
                uint16_t nro    = packed >> 11;

                #ifdef DEBUG_PARSER
                Serial.printf("Track page %d: nro=%d, flags=0x%02x\n",
                              current_page, nro, flags);
                #endif

                if (nro > 0) {
                    uint16_t num_groups = (nro + 15) / 16;
                    // Deduplicate slots — rowpf bitmask unreliable in some exports
                    uint16_t seen[512]; uint16_t seen_n = 0;

                    for (uint16_t g = 0; g < num_groups; g++) {
                        uint16_t group_base   = RB_PAGE_SIZE - 36 * (g + 1);
                        uint16_t slots_in_grp = (nro > g*16) ? min((uint16_t)(nro-g*16),(uint16_t)16) : 0;

                        for (uint16_t r = 0; r < slots_in_grp; r++) {
                            uint16_t row_off = readUInt16(buffer, group_base + r * 2);

                            bool dup = false;
                            for (uint16_t s = 0; s < seen_n; s++) if (seen[s]==row_off){dup=true;break;}
                            if (dup) continue;
                            if (seen_n < 512) seen[seen_n++] = row_off;

                            uint16_t row_start = 0x28 + row_off;
                            if (row_start + 0x88 >= RB_PAGE_SIZE) continue;
                            // Validate subtype before parsing
                            if (readUInt16(buffer, row_start) != 0x0024) continue;

                            parseTrackEntry(buffer, row_start, current_page * RB_PAGE_SIZE);
                        }
                    }
                }
            }

            current_page = next_page;
        }
    }
    
        deduplicateTracks();

    if (_arena) {
        uint32_t needed = sizeof(Track)*MAX_TRACKS + sizeof(Key)*MAX_KEYS
                        + sizeof(Artist)*MAX_ARTISTS + sizeof(Album)*MAX_ALBUMS
                        + sizeof(Genre)*MAX_GENRES   + sizeof(Label)*MAX_LABELS
                        + sizeof(Playlist)*MAX_PLAYLISTS
                        + sizeof(uint16_t)*MAX_PLAYLIST_TRACK_POOL
                        + sizeof(Artwork)*MAX_ARTWORK;
        Serial.printf("Arena: %u used / %u available  (need %u total)  artworks=%s\n",
                      _arena_used, _arena_size, needed,
                      artworks ? "OK" : "NULL - increase arena size");
    }

    if (artwork_first != 0 && artworks != nullptr) {
        parseArtworkTable(artwork_first, artwork_last);
    } else if (artworks == nullptr) {
        Serial.println("WARNING: artworks buffer is null — skipping artwork (arena too small?)");
    }
    
    Serial.println("Parsing playlists...");
    if (playlists_tree_first != 0) {
       parsePlaylistTree(playlists_tree_first, playlists_tree_last);
    }
    
    if (playlists_entries_first != 0) {
        parsePlaylistEntries(playlists_entries_first, playlists_entries_last);
    }
    
    deduplicatePlaylists();
    
    Serial.println("Parsing history...");
    if (history_first != 0) {
        //parseHistory(history_first, history_last);
    }
    
    //file.close();
    
    Serial.print("Parsed: ");
    Serial.print(track_count);
    Serial.println(" tracks");
    
    return true;
}

// ============================================================================
// Artwork table parser
// Artwork row: id (u32) at +0x00, then DeviceSQL path string at +0x04
// The path points to an 80x80 JPEG. A 240x240 version exists at path_m.jpg.
// ============================================================================
inline void RekordboxParser::parseArtworkTable(uint32_t first_page, uint32_t last_page) {
    if (!artworks) return;  // allocation failed — skip silently
    uint32_t current_page = first_page;

    while (current_page != 0 && current_page <= last_page) {
        if (!readPage(current_page)) break;

        uint32_t next_page = readUInt32(buffer, 12);
        uint8_t  flags     = buffer[0x1b];

        // Skip index pages and non-artwork pages
        uint32_t page_type = readUInt32(buffer, 8);
        if ((flags & 0x40) != 0 || page_type != 13) {
            current_page = next_page;
            continue;
        }

        uint32_t packed = buffer[0x18]|(buffer[0x19]<<8)|(buffer[0x1a]<<16);
        uint16_t nro    = packed >> 11;

        if (nro == 0 || nro > 256) {  // sanity check — artwork tables are small
            current_page = next_page;
            continue;
        }

        uint16_t seen[64]; uint8_t seen_n = 0;
        uint16_t num_groups = (nro + 15) / 16;
        for (uint16_t g = 0; g < num_groups; g++) {
            uint16_t gb = RB_PAGE_SIZE - 36*(g+1);
            if (gb < RB_PAGE_SIZE/2) continue;  // group base out of range
            uint16_t slots = min((uint16_t)(nro - g*16), (uint16_t)16);
            for (uint16_t r = 0; r < slots; r++) {
                uint16_t row_off = readUInt16(buffer, gb + r*2);
                if (row_off == 0) continue;
                bool dup = false;
                for (uint8_t s = 0; s < seen_n; s++) if (seen[s]==row_off){dup=true;break;}
                if (dup) continue;
                if (seen_n < 64) seen[seen_n++] = row_off;

                uint16_t rs = 0x28 + row_off;
                if (rs + 8 > RB_PAGE_SIZE) continue;

                uint32_t art_id = readUInt32(buffer, rs);
                if (art_id == 0) continue;

                // Read the path string at rs+4
                char path[128]; path[0] = '\0';
                readDeviceSQLString(buffer, rs + 4, path, 128);
                if (path[0] == '\0') continue;

                // Store in artworks table
                if (artwork_count < MAX_ARTWORK) {
                    artworks[artwork_count].id = art_id;
                    snprintf(artworks[artwork_count].path, 128, "%s", path);
                    artwork_count++;
                }
                // Resolve into tracks that reference this artwork
                for (uint16_t i = 0; i < track_count; i++) {
                    if (tracks[i].artwork_id == art_id) {
                        snprintf(tracks[i].thumbnail_path, 128, "%s", path);
                    }
                }
            }
        }
        current_page = next_page;
    }
}

// ============================================================================
// API: fill PlaylistInfo list in display order
// ============================================================================
inline uint16_t RekordboxParser::getPlaylistInfoList(PlaylistInfo* out,
                                                     uint16_t max_count) const {
    if (!out || max_count == 0) return 0;
    uint16_t n = (playlist_count < max_count) ? playlist_count : max_count;
    for (uint16_t i = 0; i < n; i++) {
        out[i].id = playlists[i].id;
        strncpy(out[i].name, playlists[i].name, MAX_PLAYLIST_NAME - 1);
        out[i].name[MAX_PLAYLIST_NAME - 1] = '\0';
    }
    return n;
}

// ============================================================================
// API: fill Track array for a given playlist ID, in playlist order
// ============================================================================
inline uint16_t RekordboxParser::getTracksForPlaylist(uint32_t playlist_id,
                                                      Track*   out,
                                                      uint16_t max_count) const {
    if (!out || max_count == 0) return 0;

    // Find playlist
    const Playlist* pl = nullptr;
    for (uint16_t i = 0; i < playlist_count; i++) {
        if (playlists[i].id == playlist_id) { pl = &playlists[i]; break; }
    }
    if (!pl) return 0;

    uint16_t written = 0;
    for (uint16_t ti = 0; ti < pl->track_count && written < max_count; ti++) {
        uint16_t pool_idx = pl->track_offset + ti;
        if (pool_idx >= MAX_PLAYLIST_TRACK_POOL) break;
        uint16_t tid = playlist_track_pool[pool_idx];
        if (tid == 0) continue;
        // Find track by ID
        for (uint16_t j = 0; j < track_count; j++) {
            if (tracks[j].id == tid) {
                out[written++] = tracks[j];
                break;
            }
        }
    }
    return written;
}

// ============================================================================
// API: load 80x80 thumbnail into RGB565 buffer
// Requires a JPEG decoder. We use TJpgDec (Teensy-compatible, header-only).
// Include TJpgDec before this header to enable:
//   #include "TJpgDec.h"
//   #define REKORDBOX_HAS_TJPGDEC
// Without it, this function always returns false.
// ============================================================================
inline bool RekordboxParser::loadThumbnail(const Track& track,
                                           uint16_t*    buf_rgb565) const {
    if (track.thumbnail_path[0] == '\0' || !buf_rgb565) return false;

#ifdef REKORDBOX_HAS_TJPGDEC
    // TJpgDec output callback — called per MCU block by the decoder
    static uint16_t* _thumb_buf = nullptr;
    static bool _thumb_ok       = false;

    _thumb_buf = buf_rgb565;
    _thumb_ok  = true;

    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);   // RGB565 little-endian for most TFTs/Teensy

    // Output callback: write decoded pixels into buf_rgb565
    TJpgDec.setCallback([](int16_t x, int16_t y,
                           uint16_t w, uint16_t h,
                           uint16_t* bitmap) -> bool {
        if (!_thumb_buf) return false;
        for (int16_t row = 0; row < h; row++) {
            if (y + row >= 80) break;
            uint16_t* dst = _thumb_buf + (y + row) * 80 + x;
            uint16_t* src = bitmap + row * w;
            uint16_t  cols = (x + w <= 80) ? w : (80 - x);
            memcpy(dst, src, cols * 2);
        }
        return true;
    });

    uint16_t w = 0, h = 0;
    bool ok = (TJpgDec.drawFsJpg(0, 0, track.thumbnail_path) == JDR_OK);
    return ok && _thumb_ok;
#else
    (void)track; (void)buf_rgb565;
    return false;  // No JPEG decoder linked
#endif
}

inline const Track* RekordboxParser::getTrack(uint16_t index) const {
    if (index >= track_count) return nullptr;
    return &tracks[index];
}

inline const char* RekordboxParser::getKeyName(uint8_t key_id) const {
    for (uint8_t i = 0; i < key_count; i++) {
        if (keys[i].id == key_id) {
            return keys[i].name;
        }
    }
    return "";
}

inline const char* RekordboxParser::getArtistName(uint16_t artist_id) const {
    for (uint16_t i = 0; i < artist_count; i++) {
        if (artists[i].id == artist_id) {
            return artists[i].name;
        }
    }
    return "";
}

inline const char* RekordboxParser::getAlbumName(uint16_t album_id) const {
    for (uint16_t i = 0; i < album_count; i++) {
        if (albums[i].id == album_id) {
            return albums[i].name;
        }
    }
    return "";
}

inline const char* RekordboxParser::getGenreName(uint16_t genre_id) const {
    for (uint16_t i = 0; i < genre_count; i++) {
        if (genres[i].id == genre_id) {
            return genres[i].name;
        }
    }
    return "";
}

inline const char* RekordboxParser::getLabelName(uint16_t label_id) const {
    for (uint16_t i = 0; i < label_count; i++) {
        if (labels[i].id == label_id) {
            return labels[i].name;
        }
    }
    return "";
}

inline const Playlist* RekordboxParser::getPlaylist(uint16_t index) const {
    if (index >= playlist_count) return nullptr;
    return &playlists[index];
}

inline void RekordboxParser::printSummary() {
    Serial.println("=== REKORDBOX DATABASE SUMMARY ===");
    Serial.print("Tracks: ");
    Serial.println(track_count);
    Serial.print("Keys: ");
    Serial.println(key_count);
    Serial.print("Playlists: ");
    Serial.println(playlist_count);
    
    if (sd_date[0] != '\0') {
        Serial.print("Export Date: ");
        Serial.println(sd_date);
    }
    
    if (sd_name[0] != '\0') {
        Serial.print("SD Card: ");
        Serial.println(sd_name);
    }
}

inline void RekordboxParser::printTrack(uint16_t index) {
    const Track* track = getTrack(index);
    if (!track) return;
    
    Serial.print("Track ");
    Serial.print(track->id);
    Serial.print(": ");
    Serial.println(track->title);
    
    if (track->artist[0] != '\0') {
        Serial.print("  Artist: ");
        Serial.println(track->artist);
    }
    
    if (track->album[0] != '\0') {
        Serial.print("  Album: ");
        Serial.println(track->album);
    }
    
    Serial.print("  BPM: ");
    Serial.println(track->bpm, 1);
    
    Serial.print("  Key: ");
    Serial.println(getKeyName(track->key_id));
    
    Serial.print("  Duration: ");
    Serial.print(track->duration / 60);
    Serial.print(":");
    if (track->duration % 60 < 10) Serial.print("0");
    Serial.println(track->duration % 60);
    
    if (track->genre[0] != '\0') {
        Serial.print("  Genre: ");
        Serial.println(track->genre);
    }
    
    if (track->label[0] != '\0') {
        Serial.print("  Label: ");
        Serial.println(track->label);
    }
    
    if (track->anlz_path[0] != '\0') {
        Serial.print("  ANLZ DAT: ");
        Serial.println(track->anlz_path);
    }
    
    if (track->anlz_ext_path[0] != '\0') {
        Serial.print("  ANLZ EXT: ");
        Serial.println(track->anlz_ext_path);
    }
    
    if (track->anlz_ex2_path[0] != '\0') {
        Serial.print("  ANLZ EX2: ");
        Serial.println(track->anlz_ex2_path);
    }
    
    if (track->audio_path[0] != '\0') {
        Serial.print("  Audio: ");
        Serial.println(track->audio_path);
    }
    
    if (track->rating > 0) {
        Serial.print("  Rating: ");
        Serial.println(track->rating);
    }
}

inline void RekordboxParser::printPlaylist(uint16_t index) {
    const Playlist* pl = getPlaylist(index);
    if (!pl) return;
    
    Serial.print("Playlist: ");
    Serial.println(pl->name);
    Serial.print("  Tracks: ");
    Serial.println(pl->track_count);
    
    for (uint16_t i = 0; i < pl->track_count && i < 10; i++) {
        Serial.print("    [");
        Serial.print(playlist_track_pool[pl->track_offset + i]);
        Serial.print("] ");
        
        // Find track and show details
        uint16_t _tid = playlist_track_pool[pl->track_offset + i];
        for (uint16_t j = 0; j < track_count; j++) {
            if (tracks[j].id == _tid) {
                Serial.print(tracks[j].title);
                if (tracks[j].artist[0] != '\0') {
                    Serial.print(" - ");
                    Serial.print(tracks[j].artist);
                }
                Serial.print(" (");
                Serial.print(tracks[j].bpm, 1);
                Serial.print(" BPM, ");
                Serial.print(getKeyName(tracks[j].key_id));
                Serial.println(")");
                break;
            }
        }
    }
    
    if (pl->track_count > 10) {
        Serial.print("    ... and ");
        Serial.print(pl->track_count - 10);
        Serial.println(" more");
    }
}

inline bool RekordboxParser::getAnlzExtPath(uint16_t track_index, char* output, size_t output_size) const {
    if (track_index >= track_count || !output || output_size < 128) {
        return false;
    }
    
    const Track* track = &tracks[track_index];
    if (track->anlz_path[0] == '\0') {
        return false;
    }
    
    // Find the .DAT extension and replace with .EXT
    const char* dat_pos = nullptr;
    for (size_t i = 0; track->anlz_path[i] != '\0'; i++) {
        if (track->anlz_path[i] == '.' && 
            track->anlz_path[i+1] == 'D' &&
            track->anlz_path[i+2] == 'A' &&
            track->anlz_path[i+3] == 'T') {
            dat_pos = &track->anlz_path[i];
            break;
        }
    }
    
    if (!dat_pos) {
        return false;
    }
    
    // Copy everything before .DAT
    size_t prefix_len = dat_pos - track->anlz_path;
    if (prefix_len + 4 >= output_size) {
        return false;
    }
    
    strncpy(output, track->anlz_path, prefix_len);
    output[prefix_len] = '\0';
    strcat(output, ".EXT");
    
    return true;
}

inline bool RekordboxParser::getAnlzEx2Path(uint16_t track_index, char* output, size_t output_size) const {
    if (track_index >= track_count || !output || output_size < 128) {
        return false;
    }
    
    const Track* track = &tracks[track_index];
    if (track->anlz_path[0] == '\0') {
        return false;
    }
    
    // Find the .DAT extension and replace with .EX2
    const char* dat_pos = nullptr;
    for (size_t i = 0; track->anlz_path[i] != '\0'; i++) {
        if (track->anlz_path[i] == '.' && 
            track->anlz_path[i+1] == 'D' &&
            track->anlz_path[i+2] == 'A' &&
            track->anlz_path[i+3] == 'T') {
            dat_pos = &track->anlz_path[i];
            break;
        }
    }
    
    if (!dat_pos) {
        return false;
    }
    
    // Copy everything before .DAT
    size_t prefix_len = dat_pos - track->anlz_path;
    if (prefix_len + 4 >= output_size) {
        return false;
    }
    
    strncpy(output, track->anlz_path, prefix_len);
    output[prefix_len] = '\0';
    strcat(output, ".EX2");
    
    return true;
}

#endif // REKORDBOX_PARSER_H