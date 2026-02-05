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
    tracks = (Track*)extmem_malloc(sizeof(Track) * MAX_TRACKS);
    keys = (Key*)extmem_malloc(sizeof(Key) * MAX_KEYS);
    artists = (Artist*)extmem_malloc(sizeof(Artist) * MAX_ARTISTS);
    albums = (Album*)extmem_malloc(sizeof(Album) * MAX_ALBUMS);
    genres = (Genre*)extmem_malloc(sizeof(Genre) * MAX_GENRES);
    labels = (Label*)extmem_malloc(sizeof(Label) * MAX_LABELS);
    playlists = (Playlist*)extmem_malloc(sizeof(Playlist) * MAX_PLAYLISTS);
    
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
    if (tracks) extmem_free(tracks);
    if (keys) extmem_free(keys);
    if (artists) extmem_free(artists);
    if (albums) extmem_free(albums);
    if (genres) extmem_free(genres);
    if (labels) extmem_free(labels);
    if (playlists) extmem_free(playlists);
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

inline void RekordboxParser::parseTrackEntry(uint8_t* page_data, uint16_t offset, uint32_t page_offset) {
    // Check for track marker 0x24 0x00
    if (page_data[offset] != 0x24 || page_data[offset+1] != 0x00) {
        return;
    }
    
    // Read track ID at offset +72
    uint16_t track_id = readUInt16(page_data, offset + 72);
    
    if (track_id == 0 || track_id > MAX_TRACKS) {
        return;
    }
    
    // Find existing track or create new slot
    // IMPORTANT: We update existing tracks to get the latest metadata
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
    
    // ALWAYS update these core fields with latest values
    // Key ID at offset +32
    track->key_id = page_data[offset + 32];
    
    // BPM at offset +56 (stored as BPM * 100)
    uint32_t bpm_raw = page_data[offset + 56] | 
                       (page_data[offset + 57] << 8) | 
                       (page_data[offset + 58] << 16);
    track->bpm = bpm_raw / 100.0f;
    
    // Duration at offset +84 (seconds)
    track->duration = readUInt16(page_data, offset + 84);
    
    // Rating at offset +88
    track->rating = readUInt16(page_data, offset + 88);
    
    // File offset for reference
    track->file_offset = page_offset + offset;
    
    // Update reference fields - ALWAYS update with latest IDs
    // Artist ID at offset +68 (0x44) - 4 bytes as per Deep Symmetry docs
    uint32_t artist_id = readUInt32(page_data, offset + 68);
    
    #ifdef DEBUG_PARSER
    Serial.printf("  Track ID %d: artist_id=%d\n", track_id, artist_id);
    #endif
    
    const char* artist_name = getArtistName(artist_id);
    
    #ifdef DEBUG_PARSER
    Serial.printf("    -> artist_name='%s'\n", artist_name ? artist_name : "NULL");
    #endif
    
    if (artist_name && artist_name[0] != '\0') {
        strncpy(track->artist, artist_name, MAX_TITLE_LENGTH - 1);
        track->artist[MAX_TITLE_LENGTH - 1] = '\0';
    }
    
    // Album ID at offset +64 (0x40) - 4 bytes as per Deep Symmetry docs
    uint32_t album_id = readUInt32(page_data, offset + 64);
    const char* album_name = getAlbumName(album_id);
    
    #ifdef DEBUG_PARSER
    Serial.printf("  Track ID %d: album_id=%d\n", track_id, album_id);
    Serial.printf("    -> album_name='%s'\n", album_name ? album_name : "NULL");
    #endif
    
    if (album_name && album_name[0] != '\0') {
        strncpy(track->album, album_name, MAX_TITLE_LENGTH - 1);
        track->album[MAX_TITLE_LENGTH - 1] = '\0';
    }
    
    // Genre ID at offset +60 (0x3C) - 4 bytes
    uint32_t genre_id = readUInt32(page_data, offset + 60);
    const char* genre_name = getGenreName(genre_id);
    if (genre_name && genre_name[0] != '\0') {
        strncpy(track->genre, genre_name, MAX_TITLE_LENGTH - 1);
        track->genre[MAX_TITLE_LENGTH - 1] = '\0';
    }
    
    // Label ID at offset +40 (0x28) - 4 bytes
    uint32_t label_id = readUInt32(page_data, offset + 40);
    const char* label_name = getLabelName(label_id);
    if (label_name && label_name[0] != '\0') {
        strncpy(track->label, label_name, MAX_TITLE_LENGTH - 1);
        track->label[MAX_TITLE_LENGTH - 1] = '\0';
    }
    
    // Find ANLZ and audio file paths
    // ANLZ path starts before .DAT, audio path starts after title
    for (uint16_t i = offset + 92; i < PAGE_SIZE - 4 && i < offset + 400; i++) {
        if (page_data[i] == '.' && 
            page_data[i+1] == 'D' && 
            page_data[i+2] == 'A' && 
            page_data[i+3] == 'T') {
            
            // ANLZ path: look backwards from .DAT to find start (typically 'Y/')
            uint16_t anlz_start = i;
            for (uint16_t j = i; j > offset + 92 && j > i - 80; j--) {
                if (page_data[j] == 'Y' && page_data[j+1] == '/') {
                    anlz_start = j;
                    break;
                }
            }
            
            // Copy ANLZ .DAT path
            uint16_t anlz_len = 0;
            for (uint16_t j = 0; j < 127 && (anlz_start + j) <= i + 3; j++) {
                track->anlz_path[j] = page_data[anlz_start + j];
                anlz_len++;
            }
            track->anlz_path[anlz_len] = '\0';
            
            // Generate .EXT path (replace .DAT with .EXT)
            strncpy(track->anlz_ext_path, track->anlz_path, 127);
            track->anlz_ext_path[127] = '\0';
            if (anlz_len >= 4) {
                track->anlz_ext_path[anlz_len - 3] = 'E';
                track->anlz_ext_path[anlz_len - 2] = 'X';
                track->anlz_ext_path[anlz_len - 1] = 'T';
            }
            
            // Generate .EX2 path (replace .DAT with .EX2)
            strncpy(track->anlz_ex2_path, track->anlz_path, 127);
            track->anlz_ex2_path[127] = '\0';
            if (anlz_len >= 4) {
                track->anlz_ex2_path[anlz_len - 3] = '2';
                track->anlz_ex2_path[anlz_len - 2] = 'E';
                track->anlz_ex2_path[anlz_len - 1] = 'X';
            }
            
            // Title starts 16 bytes after .DAT
            uint16_t title_start = i + 16;
            uint16_t title_len = 0;
            
            // Read title until 0x03
            for (uint16_t j = 0; j < MAX_TITLE_LENGTH - 1; j++) {
                if (title_start + j >= PAGE_SIZE) break;
                if (page_data[title_start + j] == 0x03) break;
                
                track->title[j] = page_data[title_start + j];
                title_len++;
            }
            track->title[title_len] = '\0';
            
            // Audio path: look for .wav, .mp3, .flac, .aiff after title
            uint16_t search_start = title_start + title_len;
            for (uint16_t j = search_start; j < PAGE_SIZE - 4 && j < search_start + 200; j++) {
                // Check for audio file extensions
                if ((page_data[j] == '.' && page_data[j+1] == 'w' && page_data[j+2] == 'a' && page_data[j+3] == 'v') ||
                    (page_data[j] == '.' && page_data[j+1] == 'm' && page_data[j+2] == 'p' && page_data[j+3] == '3') ||
                    (page_data[j] == '.' && page_data[j+1] == 'f' && page_data[j+2] == 'l' && page_data[j+3] == 'a') ||
                    (page_data[j] == '.' && page_data[j+1] == 'a' && page_data[j+2] == 'i' && page_data[j+3] == 'f')) {
                    
                    // Found extension, look backwards for path start (typically '/' or after 0x03)
                    uint16_t audio_start = j;
                    for (uint16_t k = j; k > search_start && k > j - 120; k--) {
                        if (page_data[k] == '/' && page_data[k-1] != 0x03) {
                            audio_start = k;
                            break;
                        }
                    }
                    
                    // Copy audio path
                    uint16_t audio_len = 0;
                    uint16_t ext_end = j + 4;
                    if (page_data[j+1] == 'f') ext_end = j + 5; // .flac
                    if (page_data[j+1] == 'a') ext_end = j + 5; // .aiff
                    
                    for (uint16_t k = 0; k < 127 && (audio_start + k) < ext_end; k++) {
                        if (page_data[audio_start + k] == 0) break;
                        track->audio_path[k] = page_data[audio_start + k];
                        audio_len++;
                    }
                    track->audio_path[audio_len] = '\0';
                    
                    break;
                }
            }
            
            break;
        }
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
    
    #ifdef DEBUG_PARSER
    Serial.printf("Parsing artists: first_page=%d, last_page=%d\n", first_page, last_page);
    #endif
    
    while (current_page != 0 && current_page <= last_page) {
        if (!readPage(current_page)) break;
        
        uint32_t next_page = readUInt32(buffer, 12);
        uint32_t page_type = readUInt32(buffer, 8);
        uint8_t flags = buffer[27];
        
        #ifdef DEBUG_PARSER
        Serial.printf("  Page %d: type=%d, flags=0x%02x\n", current_page, page_type, flags);
        #endif
        
        // Check page type (artist pages seem to be type 1)
        if (page_type != 1) {
            #ifdef DEBUG_PARSER
            Serial.printf("  -> Skipping (wrong type)\n");
            #endif
            current_page = next_page;
            continue;
        }
        
        // NOTE: Temporarily not filtering strange pages to see what happens
        // if ((flags & 0x40) != 0) {
        //     Serial.printf("  -> Skipping (strange page)\n");
        //     current_page = next_page;
        //     continue;
        // }
        
        // Get row presence flags
        uint16_t num_rows_small = ((buffer[8] << 8 | buffer[9]) & 0x1FFF);
        uint16_t num_rows_present = ((buffer[10] << 8 | buffer[11]) >> 5);
        
        #ifdef DEBUG_PARSER
        Serial.printf("  -> num_rows_small=%d, num_rows_present=%d\n", num_rows_small, num_rows_present);
        #endif
        
        // Parse rows using row index at end of page
        for (uint16_t row_idx = 0; row_idx < num_rows_small && artist_count < MAX_ARTISTS; row_idx++) {
            // Calculate row group (16 rows per group)
            uint16_t group = row_idx / 16;
            uint16_t group_offset = PAGE_SIZE - (group * 36) - 36;
            
            // Check row presence bit
            uint16_t presence_word = readUInt16(buffer, group_offset);
            if ((presence_word & (1 << (row_idx % 16))) == 0) {
                continue; // Row not present
            }
            
            // Get row offset
            uint16_t row_offset_pos = group_offset + 2 + ((row_idx % 16) * 2);
            uint16_t row_offset = readUInt16(buffer, row_offset_pos);
            uint16_t row_start = 0x28 + row_offset; // 0x28 = page header size
            
            if (row_start + 10 >= PAGE_SIZE) continue;
            
            // Parse artist row structure according to Deep Symmetry docs
            uint16_t subtype = readUInt16(buffer, row_start);
            
            #ifdef DEBUG_PARSER
            Serial.printf("    Row %d: subtype=0x%04x, row_start=%d\n", row_idx, subtype, row_start);
            #endif
            
            // Subtype 0x60 = short name, 0x64 = far name
            if (subtype != 0x0060 && subtype != 0x0064) {
                #ifdef DEBUG_PARSER
                Serial.printf("    -> Wrong subtype, skipping\n");
                #endif
                continue;
            }
            
            // Bytes 4-7: artist ID (4 bytes!)
            uint32_t artist_id = readUInt32(buffer, row_start + 4);
            if (artist_id == 0 || artist_id > 10000) {
                #ifdef DEBUG_PARSER
                Serial.printf("    -> Invalid ID=%d, skipping\n", artist_id);
                #endif
                continue;
            }
            
            #ifdef DEBUG_PARSER
            Serial.printf("    -> Artist ID=%d\n", artist_id);
            #endif
            
            // Get name offset
            uint16_t name_offset;
            if (subtype == 0x0060) {
                // Short form: 1-byte offset at position 9
                name_offset = buffer[row_start + 9];
            } else {
                // Long form: 2-byte offset at position 10-11
                name_offset = readUInt16(buffer, row_start + 10);
            }
            
            // Name is relative to row start
            uint16_t name_pos = row_start + name_offset;
            if (name_pos >= PAGE_SIZE) continue;
            
            // Parse DeviceSQL string
            uint8_t len_kind = buffer[name_pos];
            
            #ifdef DEBUG_PARSER
            Serial.printf("    -> name_offset=%d, name_pos=%d, len_kind=0x%02x\n", name_offset, name_pos, len_kind);
            #endif
            
            if (len_kind & 0x01) {
                // Short ASCII string
                uint8_t str_len = (len_kind >> 1) - 1; // Exclude len_kind byte itself
                if (str_len > 0 && str_len < MAX_TITLE_LENGTH && name_pos + 1 + str_len <= PAGE_SIZE) {
                    Artist* artist = &artists[artist_count];
                    artist->id = artist_id;
                    memcpy(artist->name, &buffer[name_pos + 1], str_len);
                    artist->name[str_len] = '\0';
                    #ifdef DEBUG_PARSER
                    Serial.printf("    -> Added artist: '%s'\n", artist->name);
                    #endif
                    artist_count++;
                }
            } else if (len_kind == 0x90) {
                // UTF-16LE string
                uint16_t str_field_len = readUInt16(buffer, name_pos + 1);
                uint16_t str_data_len = (str_field_len - 4) / 2; // UTF-16 = 2 bytes per char
                
                if (str_data_len > 0 && str_data_len < MAX_TITLE_LENGTH && name_pos + 4 + (str_data_len * 2) <= PAGE_SIZE) {
                    Artist* artist = &artists[artist_count];
                    artist->id = artist_id;
                    
                    // Convert UTF-16LE to ASCII (simple conversion, drops high bytes)
                    for (uint16_t i = 0; i < str_data_len && i < MAX_TITLE_LENGTH - 1; i++) {
                        artist->name[i] = buffer[name_pos + 4 + (i * 2)];
                    }
                    artist->name[str_data_len < MAX_TITLE_LENGTH ? str_data_len : MAX_TITLE_LENGTH - 1] = '\0';
                    #ifdef DEBUG_PARSER
                    Serial.printf("    -> Added artist: '%s'\n", artist->name);
                    #endif
                    artist_count++;
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
        
        if (page_type != 2 || (flags & 0x40) != 0) {
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
        
        if (page_type != 4 || (flags & 0x40) != 0) {
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
        
        if (page_type != 3 || (flags & 0x40) != 0) {
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
            case 1:  // Artists
                artists_first = first_page;
                artists_last = last_page;
                break;
            case 2:  // Albums
                albums_first = first_page;
                albums_last = last_page;
                break;
            case 3:  // Labels
                labels_first = first_page;
                labels_last = last_page;
                break;
            case 4:  // Genres
                genres_first = first_page;
                genres_last = last_page;
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
            uint32_t page_type = readUInt32(buffer, 8);
            uint8_t flags = buffer[27];
            
            if (page_type == 0 && (flags & 0x40) == 0) {
                // Scan page for tracks
                for (uint16_t offset = 0; offset < PAGE_SIZE - 100; offset++) {
                    parseTrackEntry(buffer, offset, current_page * PAGE_SIZE);
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
        parseHistory(history_first, history_last);
    }
    
    file.close();
    
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