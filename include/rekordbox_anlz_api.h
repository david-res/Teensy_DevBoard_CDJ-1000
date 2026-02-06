#ifndef REKORDBOX_ANLZ_API_H
#define REKORDBOX_ANLZ_API_H

#include <stdint.h>
#include "rekordbox_anlz_parser.h"

// High-level API for extracting Rekordbox ANLZ data
// Simple functions to extract specific data without manual file handling

/**
 * Extract preview waveform from .2EX file (CORRECTED)
 * 
 * @param filepath Path to the .2EX file (e.g., "ANLZ0000.2EX")
 * @param preview_waveform Output buffer [3][800] for preview waveform
 *                         [0][] = LOW band (bass), [1][] = MID band, [2][] = HIGH band (treble)
 *                         Values scaled to 0-PREVIEW_WAVEFORM_MAX_VALUE (64)
 * @return Error code (0 = success)
 * 
 * Example:
 *   uint8_t waveform[3][800];
 *   uint16_t err = extractPreviewWaveform("0:/PIONEER/ANLZ0000.2EX", waveform);
 */
uint16_t extractPreviewWaveform(const char* filepath, uint8_t preview_waveform[3][PREVIEW_WAVEFORM_WIDTH]);

/**
 * Extract dynamic waveform from .2EX file (CORRECTED - was incorrectly listed as .EXT)
 * 
 * @param filepath Path to the .2EX file (e.g., "ANLZ0000.2EX")
 * @param dynamic_waveform Pointer to store allocated waveform data
 *                         Format: [entry0_low, entry0_mid, entry0_high, entry1_low, ...]
 *                         Values scaled to 0-DYNAMIC_WAVEFORM_MAX_VALUE (164)
 *                         MUST be freed by caller using free()
 * @param num_entries Output: number of waveform entries (multiply by 3 for byte count)
 * @return Error code (0 = success)
 * 
 * Example:
 *   uint8_t* waveform = NULL;
 *   uint32_t entries;
 *   uint16_t err = extractDynamicWaveform("0:/PIONEER/ANLZ0000.2EX", &waveform, &entries);
 *   if (err == 0) {
 *       // Use waveform data: waveform[i*3+0]=low, waveform[i*3+1]=mid, waveform[i*3+2]=high
 *       free(waveform);  // Free when done
 *   }
 */
uint16_t extractDynamicWaveform(const char* filepath, uint8_t** dynamic_waveform, uint32_t* num_entries);

/**
 * Extract beat grid from .DAT file
 * 
 * @param filepath Path to the .DAT file (e.g., "ANLZ0000.DAT")
 * @param beat_grid Pointer to store allocated beat grid data
 *                  MUST be freed by caller using free()
 * @param num_entries Output: number of beat grid entries
 * @param original_bpm Output: original BPM of track (multiply by 100, e.g., 8752 = 87.52 BPM)
 * @param grid_offset Output: first beat position (1-4)
 * @return Error code (0 = success)
 * 
 * Example:
 *   BeatGridEntry* grid = NULL;
 *   uint32_t count;
 *   uint16_t bpm;
 *   uint8_t offset;
 *   uint16_t err = extractBeatGrid("0:/PIONEER/ANLZ0000.DAT", &grid, &count, &bpm, &offset);
 *   if (err == 0) {
 *       float actual_bpm = bpm / 100.0;
 *       // Use grid[i].bpm and grid[i].position
 *       free(grid);  // Free when done
 *   }
 */
uint16_t extractBeatGrid(const char* filepath, BeatGridEntry** beat_grid, uint32_t* num_entries, 
                         uint16_t* original_bpm, uint8_t* grid_offset);

/**
 * Extract hot cues from .DAT file
 * 
 * @param filepath Path to the .DAT file (e.g., "ANLZ0000.DAT")
 * @param hot_cues Output buffer [3] for hot cues (A, B, C)
 * @param num_hot_cues Output: number of active hot cues (0-3)
 * @return Error code (0 = success)
 * 
 * Example:
 *   CuePoint cues[3];
 *   uint8_t count;
 *   uint16_t err = extractHotCues("0:/PIONEER/ANLZ0000.DAT", cues, &count);
 *   if (err == 0) {
 *       for (int i = 0; i < count; i++) {
 *           if (cues[i].type & 0x2) {  // Active?
 *               uint32_t pos = cues[i].start_pos;  // Position in frames (1/150s)
 *               bool is_loop = cues[i].type & 0x1;
 *           }
 *       }
 *   }
 */
uint16_t extractHotCues(const char* filepath, CuePoint hot_cues[3], uint8_t* num_hot_cues);

/**
 * Extract memory cues from .DAT file
 * 
 * @param filepath Path to the .DAT file (e.g., "ANLZ0000.DAT")
 * @param memory_cues Output buffer [8] for memory cues
 * @param num_memory_cues Output: number of active memory cues (0-8)
 * @return Error code (0 = success)
 * 
 * Example:
 *   CuePoint cues[8];
 *   uint8_t count;
 *   uint16_t err = extractMemoryCues("0:/PIONEER/ANLZ0000.DAT", cues, &count);
 */
uint16_t extractMemoryCues(const char* filepath, CuePoint memory_cues[8], uint8_t* num_memory_cues);

/**
 * Extract audio file path from .DAT file
 * 
 * @param filepath Path to the .DAT file (e.g., "ANLZ0000.DAT")
 * @param audio_path Output buffer for audio file path
 * @param max_len Maximum length of output buffer (including null terminator)
 * @return Error code (0 = success)
 * 
 * Example:
 *   char path[256];
 *   uint16_t err = extractAudioPath("0:/PIONEER/ANLZ0000.DAT", path, sizeof(path));
 */
uint16_t extractAudioPath(const char* filepath, char* audio_path, uint16_t max_len);

/**
 * Extract all track information from .DAT file
 * 
 * @param filepath Path to the .DAT file (e.g., "ANLZ0000.DAT")
 * @param data Pointer to AnlzData structure to populate
 *             Beat grid will be dynamically allocated - caller must free with anlz_free()
 * @return Error code (0 = success)
 * 
 * Example:
 *   AnlzData track;
 *   anlz_init(&track);
 *   uint16_t err = extractTrackInfo("0:/PIONEER/ANLZ0000.DAT", &track);
 *   if (err == 0) {
 *       // Use track.original_bpm, track.beat_grid, track.hot_cues, etc.
 *       anlz_free(&track);  // Free when done
 *   }
 */
uint16_t extractTrackInfo(const char* filepath, AnlzData* data);

// ============================================================================
// Implementation
// ============================================================================

#ifdef REKORDBOX_API_IMPLEMENTATION

// Include file system headers based on platform
#ifdef ANLZ_EMBEDDED
    // For Teensy/Arduino
    #include <SD.h>
    typedef File FileHandle;
    #define ANLZ_FILE_OPEN(path) SD.open(path, FILE_READ)
    #define ANLZ_FILE_CLOSE(f) f.close()
    #define ANLZ_FILE_SIZE(f) f.size()
    #define ANLZ_FILE_READ_DATA(f, buf, len) f.read(buf, len)
    #define ANLZ_FILE_VALID(f) f
#else
    // For desktop/testing
    #include <stdio.h>
    typedef FILE* FileHandle;
    #define ANLZ_FILE_OPEN(path) fopen(path, "rb")
    #define ANLZ_FILE_CLOSE(f) fclose(f)
    #define ANLZ_FILE_SIZE(f) ({ fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET); sz; })
    #define ANLZ_FILE_READ_DATA(f, buf, len) fread(buf, 1, len, f)
    #define ANLZ_FILE_VALID(f) (f != NULL)
#endif

uint16_t extractPreviewWaveform(const char* filepath, uint8_t preview_waveform[3][PREVIEW_WAVEFORM_WIDTH]) {
    // Open file
    FileHandle file = ANLZ_FILE_OPEN(filepath);
    if (!ANLZ_FILE_VALID(file)) {
        return ANLZ_ERR_CANNOT_OPEN_EXT;
    }
    
    // Get file size
    uint32_t file_size = ANLZ_FILE_SIZE(file);
    
    // Allocate buffer for file
    uint8_t* buffer = (uint8_t*)malloc(file_size);
    if (!buffer) {
        ANLZ_FILE_CLOSE(file);
        return ANLZ_ERR_CANNOT_READ_EXT;
    }
    
    // Read file
    uint32_t bytes_read = ANLZ_FILE_READ_DATA(file, buffer, file_size);
    ANLZ_FILE_CLOSE(file);
    
    if (bytes_read != file_size) {
        free(buffer);
        return ANLZ_ERR_CANNOT_READ_EXT;
    }
    
    // Parse preview waveform
    AnlzData temp_data;
    anlz_init(&temp_data);
    
    uint16_t error = anlz_parse_preview(buffer, file_size, &temp_data);
    free(buffer);
    
    if (error == ANLZ_OK) {
        // Copy preview waveform to output
        memcpy(preview_waveform, temp_data.preview_waveform, sizeof(temp_data.preview_waveform));
    }
    
    return error;
}

uint16_t extractDynamicWaveform(const char* filepath, uint8_t** dynamic_waveform, uint32_t* num_entries) {
    FileHandle file = ANLZ_FILE_OPEN(filepath);
    if (!ANLZ_FILE_VALID(file)) {
        return ANLZ_ERR_CANNOT_OPEN_EXT;
    }
    
    uint32_t file_size = ANLZ_FILE_SIZE(file);
    uint8_t* buffer = (uint8_t*)malloc(file_size);
    if (!buffer) {
        ANLZ_FILE_CLOSE(file);
        return ANLZ_ERR_CANNOT_READ_EXT;
    }
    
    uint32_t bytes_read = ANLZ_FILE_READ_DATA(file, buffer, file_size);
    ANLZ_FILE_CLOSE(file);
    
    if (bytes_read != file_size) {
        free(buffer);
        return ANLZ_ERR_CANNOT_READ_EXT;
    }
    
    // Parse dynamic waveform (PWV7 from .2EX file)
    AnlzData temp_data;
    anlz_init(&temp_data);
    
    uint16_t error = anlz_parse_dynamic(buffer, file_size, &temp_data);
    free(buffer);
    
    if (error == ANLZ_OK) {
        *dynamic_waveform = temp_data.dynamic_waveform;
        *num_entries = temp_data.dynamic_waveform_entries;
        // Don't free temp_data.dynamic_waveform - we're returning it to caller
    }
    
    return error;
}

uint16_t extractBeatGrid(const char* filepath, BeatGridEntry** beat_grid, uint32_t* num_entries,
                         uint16_t* original_bpm, uint8_t* grid_offset) {
    FileHandle file = ANLZ_FILE_OPEN(filepath);
    if (!ANLZ_FILE_VALID(file)) {
        return ANLZ_ERR_CANNOT_OPEN_DAT;
    }
    
    uint32_t file_size = ANLZ_FILE_SIZE(file);
    uint8_t* buffer = (uint8_t*)malloc(file_size);
    if (!buffer) {
        ANLZ_FILE_CLOSE(file);
        return ANLZ_ERR_DATA_NOT_READ;
    }
    
    uint32_t bytes_read = ANLZ_FILE_READ_DATA(file, buffer, file_size);
    ANLZ_FILE_CLOSE(file);
    
    if (bytes_read != file_size) {
        free(buffer);
        return ANLZ_ERR_DATA_NOT_READ;
    }
    
    // Parse DAT file
    AnlzData temp_data;
    anlz_init(&temp_data);
    
    uint16_t error = anlz_parse_dat(buffer, file_size, &temp_data);
    free(buffer);
    
    if (error == ANLZ_OK) {
        *beat_grid = temp_data.beat_grid;
        *num_entries = temp_data.beat_grid_count;
        *original_bpm = temp_data.original_bpm;
        *grid_offset = temp_data.grid_offset;
        // Don't free temp_data.beat_grid - we're returning it to caller
    }
    
    return error;
}

uint16_t extractHotCues(const char* filepath, CuePoint hot_cues[3], uint8_t* num_hot_cues) {
    FileHandle file = ANLZ_FILE_OPEN(filepath);
    if (!ANLZ_FILE_VALID(file)) {
        return ANLZ_ERR_CANNOT_OPEN_DAT;
    }
    
    uint32_t file_size = ANLZ_FILE_SIZE(file);
    uint8_t* buffer = (uint8_t*)malloc(file_size);
    if (!buffer) {
        ANLZ_FILE_CLOSE(file);
        return ANLZ_ERR_DATA_NOT_READ;
    }
    
    uint32_t bytes_read = ANLZ_FILE_READ_DATA(file, buffer, file_size);
    ANLZ_FILE_CLOSE(file);
    
    if (bytes_read != file_size) {
        free(buffer);
        return ANLZ_ERR_DATA_NOT_READ;
    }
    
    // Parse DAT file
    AnlzData temp_data;
    anlz_init(&temp_data);
    
    uint16_t error = anlz_parse_dat(buffer, file_size, &temp_data);
    free(buffer);
    
    if (error == ANLZ_OK) {
        memcpy(hot_cues, temp_data.hot_cues, sizeof(temp_data.hot_cues));
        *num_hot_cues = temp_data.hot_cue_count;
    }
    
    anlz_free(&temp_data);
    return error;
}

uint16_t extractMemoryCues(const char* filepath, CuePoint memory_cues[8], uint8_t* num_memory_cues) {
    FileHandle file = ANLZ_FILE_OPEN(filepath);
    if (!ANLZ_FILE_VALID(file)) {
        return ANLZ_ERR_CANNOT_OPEN_DAT;
    }
    
    uint32_t file_size = ANLZ_FILE_SIZE(file);
    uint8_t* buffer = (uint8_t*)malloc(file_size);
    if (!buffer) {
        ANLZ_FILE_CLOSE(file);
        return ANLZ_ERR_DATA_NOT_READ;
    }
    
    uint32_t bytes_read = ANLZ_FILE_READ_DATA(file, buffer, file_size);
    ANLZ_FILE_CLOSE(file);
    
    if (bytes_read != file_size) {
        free(buffer);
        return ANLZ_ERR_DATA_NOT_READ;
    }
    
    // Parse DAT file
    AnlzData temp_data;
    anlz_init(&temp_data);
    
    uint16_t error = anlz_parse_dat(buffer, file_size, &temp_data);
    free(buffer);
    
    if (error == ANLZ_OK) {
        memcpy(memory_cues, temp_data.memory_cues, sizeof(temp_data.memory_cues));
        *num_memory_cues = temp_data.memory_cue_count;
    }
    
    anlz_free(&temp_data);
    return error;
}

uint16_t extractAudioPath(const char* filepath, char* audio_path, uint16_t max_len) {
    FileHandle file = ANLZ_FILE_OPEN(filepath);
    if (!ANLZ_FILE_VALID(file)) {
        return ANLZ_ERR_CANNOT_OPEN_DAT;
    }
    
    uint32_t file_size = ANLZ_FILE_SIZE(file);
    uint8_t* buffer = (uint8_t*)malloc(file_size);
    if (!buffer) {
        ANLZ_FILE_CLOSE(file);
        return ANLZ_ERR_DATA_NOT_READ;
    }
    
    uint32_t bytes_read = ANLZ_FILE_READ_DATA(file, buffer, file_size);
    ANLZ_FILE_CLOSE(file);
    
    if (bytes_read != file_size) {
        free(buffer);
        return ANLZ_ERR_DATA_NOT_READ;
    }
    
    // Parse DAT file
    AnlzData temp_data;
    anlz_init(&temp_data);
    
    uint16_t error = anlz_parse_dat(buffer, file_size, &temp_data);
    free(buffer);
    
    if (error == ANLZ_OK) {
        strncpy(audio_path, temp_data.audio_path, max_len - 1);
        audio_path[max_len - 1] = '\0';
    }
    
    anlz_free(&temp_data);
    return error;
}

uint16_t extractTrackInfo(const char* filepath, AnlzData* data) {
    FileHandle file = ANLZ_FILE_OPEN(filepath);
    if (!ANLZ_FILE_VALID(file)) {
        return ANLZ_ERR_CANNOT_OPEN_DAT;
    }
    
    uint32_t file_size = ANLZ_FILE_SIZE(file);
    uint8_t* buffer = (uint8_t*)malloc(file_size);
    if (!buffer) {
        ANLZ_FILE_CLOSE(file);
        return ANLZ_ERR_DATA_NOT_READ;
    }
    
    uint32_t bytes_read = ANLZ_FILE_READ_DATA(file, buffer, file_size);
    ANLZ_FILE_CLOSE(file);
    
    if (bytes_read != file_size) {
        free(buffer);
        return ANLZ_ERR_DATA_NOT_READ;
    }
    
    // Parse DAT file directly into provided structure
    uint16_t error = anlz_parse_dat(buffer, file_size, data);
    free(buffer);
    
    return error;
}

#endif // REKORDBOX_API_IMPLEMENTATION

#endif // REKORDBOX_ANLZ_API_H