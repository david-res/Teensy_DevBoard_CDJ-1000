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
#define MAX_PLAYLIST_NAME 21    // Max playlist name length
#define PAGE_SIZE 4096          // PDB page size
#define BUFFER_SIZE 4096        // Read buffer size

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

// Playlist structure
struct Playlist {
    uint8_t id;
    char name[MAX_PLAYLIST_NAME];
    uint16_t track_ids[MAX_TRACKS];
    uint16_t track_count;
};

class RekordboxParser {
private:
    File file;
    uint8_t buffer[BUFFER_SIZE];
    
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
    uint8_t playlist_count;
    
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
    void deduplicateTracks();
    void deduplicatePlaylists();
    const char* getArtistName(uint16_t artist_id) const;
    const char* getAlbumName(uint16_t album_id) const;
    const char* getGenreName(uint16_t genre_id) const;
    const char* getLabelName(uint16_t label_id) const;
    
public:
    RekordboxParser();
    ~RekordboxParser();  // Destructor to free memory
    
    // Main parsing function
    bool parse(const char* filename);
    
    // Getters
    uint16_t getTrackCount() const { return track_count; }
    const Track* getTracks() const { return tracks; }
    const Track* getTrack(uint16_t index) const;
    
    uint8_t getKeyCount() const { return key_count; }
    const Key* getKeys() const { return keys; }
    const char* getKeyName(uint8_t key_id) const;
    
    uint8_t getPlaylistCount() const { return playlist_count; }
    const Playlist* getPlaylists() const { return playlists; }
    const Playlist* getPlaylist(uint8_t index) const;
    
    const char* getSDDate() const { return sd_date; }
    const char* getSDName() const { return sd_name; }
    
    // Utility functions
    void printSummary();
    void printTrack(uint16_t index);
    void printPlaylist(uint8_t index);
    
    // Get derived ANLZ paths (EXT and EX2 files)
    bool getAnlzExtPath(uint16_t track_index, char* output, size_t output_size) const;
    bool getAnlzEx2Path(uint16_t track_index, char* output, size_t output_size) const;
};

// Implementation

inline RekordboxParser::RekordboxParser() {
    // Allocate arrays in EXTMEM (external RAM)
    tracks = (Track*)malloc(sizeof(Track) * MAX_TRACKS);
    keys = (Key*)malloc(sizeof(Key) * MAX_KEYS);
    artists = (Artist*)malloc(sizeof(Artist) * MAX_ARTISTS);
    albums = (Album*)malloc(sizeof(Album) * MAX_ALBUMS);
    genres = (Genre*)malloc(sizeof(Genre) * MAX_GENRES);
    labels = (Label*)malloc(sizeof(Label) * MAX_LABELS);
    playlists = (Playlist*)malloc(sizeof(Playlist) * MAX_PLAYLISTS);
    
    // Initialize counts
    track_count = 0;
    key_count = 0;
    artist_count = 0;
    album_count = 0;
    genre_count = 0;
    label_count = 0;
    playlist_count = 0;
    
    // Clear memory
    if (tracks) memset(tracks, 0, sizeof(Track) * MAX_TRACKS);
    if (keys) memset(keys, 0, sizeof(Key) * MAX_KEYS);
    if (artists) memset(artists, 0, sizeof(Artist) * MAX_ARTISTS);
    if (albums) memset(albums, 0, sizeof(Album) * MAX_ALBUMS);
    if (genres) memset(genres, 0, sizeof(Genre) * MAX_GENRES);
    if (labels) memset(labels, 0, sizeof(Label) * MAX_LABELS);
    if (playlists) memset(playlists, 0, sizeof(Playlist) * MAX_PLAYLISTS);
    
    memset(sd_date, 0, sizeof(sd_date));
    memset(sd_name, 0, sizeof(sd_name));
}

inline RekordboxParser::~RekordboxParser() {
    // Free EXTMEM allocations
    if (tracks) free(tracks);
    if (keys) free(keys);
    if (artists) free(artists);
    if (albums) free(albums);
    if (genres) free(genres);
    if (labels) free(labels);
    if (playlists) free(playlists);
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
    uint32_t offset = page_num * PAGE_SIZE;
    
    if (!file.seek(offset)) {
        return false;
    }
    
    size_t bytes_read = file.read(buffer, PAGE_SIZE);
    return (bytes_read == PAGE_SIZE);
}

// Helper: Read a DeviceSQL string from buffer at given position into output char array
// Returns the number of characters read, or 0 on failure
inline uint16_t readDeviceSQLString(uint8_t* page_data, uint16_t str_pos, char* output, uint16_t max_len) {
    if (str_pos >= PAGE_SIZE) return 0;
    
    uint8_t len_kind = page_data[str_pos];
    
    if (len_kind & 0x01) {
        // Short ASCII string: length is (len_kind >> 1) - 1
        uint8_t str_len = (len_kind >> 1) - 1;
        if (str_len == 0 || str_len >= max_len) return 0;
        if (str_pos + 1 + str_len > PAGE_SIZE) return 0;
        
        memcpy(output, &page_data[str_pos + 1], str_len);
        output[str_len] = '\0';
        return str_len;
    } else if (len_kind == 0x40) {
        // Long ASCII string: next 2 bytes are total field length
        if (str_pos + 3 >= PAGE_SIZE) return 0;
        uint16_t field_len = page_data[str_pos + 1] | (page_data[str_pos + 2] << 8);
        uint16_t str_len = field_len - 4;  // subtract header overhead
        if (str_len == 0 || str_len >= max_len) return 0;
        if (str_pos + 4 + str_len > PAGE_SIZE) return 0;
        
        memcpy(output, &page_data[str_pos + 4], str_len);
        output[str_len] = '\0';
        return str_len;
    } else if (len_kind == 0x90) {
        // UTF-16LE string: next 2 bytes are total field length
        if (str_pos + 3 >= PAGE_SIZE) return 0;
        uint16_t field_len = page_data[str_pos + 1] | (page_data[str_pos + 2] << 8);
        uint16_t str_data_len = (field_len - 4) / 2;  // number of UTF-16 chars
        if (str_data_len == 0 || str_data_len >= max_len) return 0;
        if (str_pos + 4 + (str_data_len * 2) > PAGE_SIZE) return 0;
        
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
    if (offset + 0x88 > PAGE_SIZE) {
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
    if (title_str_offset > 0 && title_str_offset < PAGE_SIZE) {
        uint16_t title_pos = offset + title_str_offset;
        readDeviceSQLString(page_data, title_pos, track->title, MAX_TITLE_LENGTH);
    }
    
    #ifdef DEBUG_PARSER
    Serial.printf("  Track ID %d: title='%s'\n", track_id, track->title);
    #endif
    
    // String index 14 = analyze_path (offset at 0x5e + 14*2 = 0x7a)
    uint16_t anlz_str_offset = readUInt16(page_data, offset + 0x7a);
    if (anlz_str_offset > 0 && anlz_str_offset < PAGE_SIZE) {
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
    if (audio_str_offset > 0 && audio_str_offset < PAGE_SIZE) {
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
            uint16_t group_offset = PAGE_SIZE - 36;
            
            for (uint8_t slot = (15 - num_rows_page); slot < 15 && artist_count < MAX_ARTISTS; slot++) {
                uint16_t row_offset = readUInt16(buffer, group_offset + 2 + slot * 2);
                uint16_t row_start = 0x28 + row_offset;
                
                if (row_start + 20 >= PAGE_SIZE) continue;
                
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
                if (name_pos >= PAGE_SIZE) continue;
                
                // Parse DeviceSQL string
                uint8_t len_kind = buffer[name_pos];
                
                if (len_kind & 0x01) {
                    // Short ASCII string
                    uint8_t str_len = (len_kind >> 1) - 1;
                    if (str_len > 0 && str_len < MAX_TITLE_LENGTH && name_pos + 1 + str_len <= PAGE_SIZE) {
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
                    
                    if (str_data_len > 0 && str_data_len < MAX_TITLE_LENGTH && name_pos + 4 + (str_data_len * 2) <= PAGE_SIZE) {
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
            uint16_t group_offset = PAGE_SIZE - 36;
            
            for (uint8_t slot = (15 - num_rows_page); slot < 15 && artist_count < MAX_ARTISTS; slot++) {
                uint16_t row_offset = readUInt16(buffer, group_offset + 2 + slot * 2);
                uint16_t row_start = 0x28 + row_offset;
            
                if (row_start + 10 >= PAGE_SIZE) continue;
            
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
            if (name_pos >= PAGE_SIZE) continue;
            
            uint8_t len_kind = buffer[name_pos];
            
            if (len_kind & 0x01) {
                uint8_t str_len = (len_kind >> 1) - 1;
                if (str_len > 0 && str_len < MAX_TITLE_LENGTH && name_pos + 1 + str_len <= PAGE_SIZE) {
                    Artist* artist = &artists[artist_count];
                    artist->id = artist_id;
                    memcpy(artist->name, &buffer[name_pos + 1], str_len);
                    artist->name[str_len] = '\0';
                    artist_count++;
                }
            } else if (len_kind == 0x90) {
                uint16_t str_field_len = readUInt16(buffer, name_pos + 1);
                uint16_t str_data_len = (str_field_len - 4) / 2;
                
                if (str_data_len > 0 && str_data_len < MAX_TITLE_LENGTH && name_pos + 4 + (str_data_len * 2) <= PAGE_SIZE) {
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
        for (uint16_t offset = 40; offset < PAGE_SIZE - 20 && album_count < MAX_ALBUMS; offset++) {
            uint16_t album_id = readUInt16(buffer, offset);
            
            if (album_id > 0 && album_id < 10000) {
                for (uint16_t str_off = 8; str_off < 32; str_off += 2) {
                    if (offset + str_off + 2 >= PAGE_SIZE) break;
                    
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
        for (uint16_t offset = 40; offset < PAGE_SIZE - 20 && genre_count < MAX_GENRES; offset++) {
            uint16_t genre_id = readUInt16(buffer, offset);
            
            if (genre_id > 0 && genre_id < 10000) {
                for (uint16_t str_off = 8; str_off < 32; str_off += 2) {
                    if (offset + str_off + 2 >= PAGE_SIZE) break;
                    
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
        for (uint16_t offset = 40; offset < PAGE_SIZE - 20 && label_count < MAX_LABELS; offset++) {
            uint16_t label_id = readUInt16(buffer, offset);
            
            if (label_id > 0 && label_id < 10000) {
                for (uint16_t str_off = 8; str_off < 32; str_off += 2) {
                    if (offset + str_off + 2 >= PAGE_SIZE) break;
                    
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
    uint32_t current_page = first_page;
    
    while (current_page != 0 && current_page <= last_page) {
        if (!readPage(current_page)) break;
        
        uint32_t next_page = readUInt32(buffer, 12);
        uint32_t page_type = readUInt32(buffer, 8);
        uint8_t flags = buffer[27];
        uint8_t num_rows = buffer[24];
        
        if (page_type != 7 || (flags & 0x40) != 0 || num_rows == 0) {
            current_page = next_page;
            continue;
        }
        
        uint16_t cursor = 40;
        
        for (uint8_t row = 0; row <= num_rows && playlist_count < MAX_PLAYLISTS; row++) {
            // Check if this is a playlist (not a folder)
            uint32_t parent = readUInt32(buffer, cursor + 16);
            
            if (parent == 0) {
                Playlist* pl = &playlists[playlist_count];
                pl->id = buffer[cursor + 12];
                pl->track_count = 0;
                
                uint8_t name_length = ((buffer[cursor + 20] - 1) / 2) - 1;
                
                // Copy playlist name
                for (uint8_t i = 0; i < name_length && i < MAX_PLAYLIST_NAME - 1; i++) {
                    pl->name[i] = buffer[cursor + 21 + i];
                }
                pl->name[name_length < MAX_PLAYLIST_NAME ? name_length : MAX_PLAYLIST_NAME - 1] = '\0';
                
                playlist_count++;
            }
            
            uint8_t name_length = ((buffer[cursor + 20] - 1) / 2) - 1;
            cursor += 24 + 4 * (name_length / 4);
        }
        
        current_page = next_page;
    }
}

inline void RekordboxParser::parsePlaylistEntries(uint32_t first_page, uint32_t last_page) {
    uint32_t current_page = first_page;
    
    while (current_page != 0 && current_page <= last_page) {
        if (!readPage(current_page)) break;
        
        uint32_t next_page = readUInt32(buffer, 12);
        uint32_t page_type = readUInt32(buffer, 8);
        uint8_t flags = buffer[27];
        uint16_t num_rows = readUInt16(buffer, 34);
        
        if (page_type != 8 || (flags & 0x40) != 0) {
            current_page = next_page;
            continue;
        }
        
        uint16_t cursor = 40;
        
        for (uint16_t row = 0; row <= num_rows && cursor + 12 < PAGE_SIZE; row++) {
            uint16_t entry_id = readUInt16(buffer, cursor);
            uint16_t track_id = readUInt16(buffer, cursor + 4);
            uint8_t playlist_id = buffer[cursor + 8];
            
            // Find the playlist
            for (uint8_t i = 0; i < playlist_count; i++) {
                if (playlists[i].id == playlist_id) {
                    if (playlists[i].track_count < MAX_TRACKS) {
                        playlists[i].track_ids[playlists[i].track_count++] = track_id;
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
        
        for (uint8_t row = 0; row < num_rows && cursor < PAGE_SIZE - 20; row++) {
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
    // Remove duplicate playlists (same ID)
    uint16_t write_pos = 0;
    
    for (uint8_t i = 0; i < playlist_count; i++) {
        bool is_duplicate = false;
        
        // Check if we've already seen this playlist ID
        for (uint16_t j = 0; j < write_pos; j++) {
            if (playlists[j].id == playlists[i].id) {
                is_duplicate = true;
                break;
            }
        }
        
        if (!is_duplicate && playlists[i].name[0] != '\0') {
            if (i != write_pos) {
                playlists[write_pos] = playlists[i];
            }
            write_pos++;
        }
    }
    
    playlist_count = write_pos;
    
    // Deduplicate tracks within each playlist
    for (uint8_t i = 0; i < playlist_count; i++) {
        uint16_t write_track = 0;
        
        for (uint16_t j = 0; j < playlists[i].track_count; j++) {
            bool is_dup = false;
            
            // Check if this track ID already exists in the cleaned list
            for (uint16_t k = 0; k < write_track; k++) {
                if (playlists[i].track_ids[k] == playlists[i].track_ids[j]) {
                    is_dup = true;
                    break;
                }
            }
            
            if (!is_dup) {
                playlists[i].track_ids[write_track] = playlists[i].track_ids[j];
                write_track++;
            }
        }
        
        playlists[i].track_count = write_track;
    }
}

inline bool RekordboxParser::parse(const char* filename) {
    // Check if memory was allocated successfully
    if (!tracks || !keys || !artists || !albums || !genres || !labels || !playlists) {
        Serial.println("ERROR: Failed to allocate EXTMEM!");
        Serial.println("Make sure you have Teensy 4.1 with PSRAM installed.");
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
    
    if (page_size != PAGE_SIZE) {
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
    uint32_t history_first = 0, history_last = 0;
    
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
            case 19:  // History
                history_first = first_page;
                history_last = last_page;
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
            uint8_t flags = buffer[0x1b];
            
            // Only process data pages (page_flags & 0x40 == 0)
            if ((flags & 0x40) == 0) {
                // Row count is at byte 0x18
                uint8_t num_rows = buffer[0x18];
                
                #ifdef DEBUG_PARSER
                Serial.printf("Track page %d: num_rows=%d, flags=0x%02x\n", 
                              current_page, num_rows, flags);
                #endif
                
                if (num_rows > 0 && num_rows <= 16) {
                    // Row offsets are in group 0, slots (15-num_rows) through 14
                    // Group 0 starts at PAGE_SIZE - 36
                    uint16_t group_offset = PAGE_SIZE - 36;
                    
                    for (uint8_t slot = (15 - num_rows); slot < 15; slot++) {
                        uint16_t row_offset = readUInt16(buffer, group_offset + 2 + slot * 2);
                        uint16_t row_start = 0x28 + row_offset;
                        
                        if (row_start + 0x88 >= PAGE_SIZE) continue;
                        
                        parseTrackEntry(buffer, row_start, current_page * PAGE_SIZE);
                    }
                }
            }
            
            current_page = next_page;
        }
    }
    
        deduplicateTracks();
    
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

inline const Playlist* RekordboxParser::getPlaylist(uint8_t index) const {
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

inline void RekordboxParser::printPlaylist(uint8_t index) {
    const Playlist* pl = getPlaylist(index);
    if (!pl) return;
    
    Serial.print("Playlist: ");
    Serial.println(pl->name);
    Serial.print("  Tracks: ");
    Serial.println(pl->track_count);
    
    for (uint16_t i = 0; i < pl->track_count && i < 10; i++) {
        Serial.print("    [");
        Serial.print(pl->track_ids[i]);
        Serial.print("] ");
        
        // Find track and show details
        for (uint16_t j = 0; j < track_count; j++) {
            if (tracks[j].id == pl->track_ids[i]) {
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