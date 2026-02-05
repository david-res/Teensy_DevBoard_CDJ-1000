#ifndef REKORDBOX_ANLZ_PARSER_H
#define REKORDBOX_ANLZ_PARSER_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Embedded system configuration
// Define ANLZ_EMBEDDED before including this header to enable embedded-friendly features
#ifdef ANLZ_EMBEDDED
    // For Teensy/Arduino environments
    #ifndef ANLZ_NO_MALLOC
        #define ANLZ_NO_MALLOC 0  // Set to 1 to disable dynamic allocation
    #endif
    // Reduce path buffer size for embedded
    #define ANLZ_PATH_SIZE 128
    
    // Memory usage estimate:
    // AnlzData struct base: ~2.5KB (mostly preview_waveform[3][800])
    // + Beat grid: ~16KB max (4096 entries × 4 bytes)
    // + Dynamic waveform: variable (num_entries × 3 bytes)
    //   Example: 3min track = ~33,500 entries × 3 = 100KB
    //
    // For Teensy 4.1 (1MB RAM): Should handle full tracks fine
    // For Teensy 4.0 (512KB RAM): May need to limit dynamic waveform size
    // For Teensy 3.6 (256KB RAM): Consider streaming or partial loading
#else
    #define ANLZ_PATH_SIZE 256
#endif

// ANLZ File Tags (4-byte identifiers)
#define TAG_PMAI 0x504D4149  // File header
#define TAG_PPTH 0x50505448  // Path
#define TAG_PVBR 0x50564252  // VBR
#define TAG_PQTZ 0x5051545A  // Beat grid
#define TAG_PWAV 0x50574156  // Waveform preview (blue)
#define TAG_PWV2 0x50575632  // Waveform preview (colored)
#define TAG_PWV3 0x50575633  // Color waveform high-res (from .EXT file)
#define TAG_PWV4 0x50575634  // Color waveform overview (from .EXT file)
#define TAG_PWV5 0x50575635  // Color waveform extended (from .EXT file)
#define TAG_PWV6 0x50575636  // 3-band waveform overview (from .2EX file, 1200 entries)
#define TAG_PWV7 0x50575637  // 3-band waveform high-res (from .2EX file, variable entries)
#define TAG_PCOB 0x50434F42  // Cue points bank
#define TAG_PCPT 0x50435054  // Cue point entry

// Maximum sizes
#define MAX_FILE_SIZE 135000
#define MAX_CUE_POINTS 8
#define MAX_BEAT_GRID_ENTRIES 4096

// Waveform parameters
#define PREVIEW_WAVEFORM_WIDTH 800       // Output width for preview waveform
#define PREVIEW_WAVEFORM_MAX_VALUE 64    // Max TOTAL height for stacked display (all bands combined)
#define PREVIEW_WAVEFORM_MAX_INDIVIDUAL 21  // Max height for individual band
#define DYNAMIC_WAVEFORM_MAX_VALUE 164   // Max height value for dynamic waveform
#define REKORDBOX_WAVEFORM_MAX 255       // Original Rekordbox max value

// Rekordbox original sizes - PWV6 format
#define REKORDBOX_PREVIEW_SAMPLES 1200   // Each band has 1200 samples
#define REKORDBOX_PREVIEW_BANDS 3        // Three bands: mid, high, low
#define REKORDBOX_INDIVIDUAL_MAX 50      // Max value per band in source data
#define REKORDBOX_TOTAL_MAX 150          // Max total when all bands stacked (3 × 50)

// Error codes
typedef enum {
    ANLZ_OK = 0,
    ANLZ_ERR_INVALID_TRACK = 1,
    ANLZ_ERR_CANNOT_OPEN_DB = 2,
    ANLZ_ERR_DATA_NOT_READ = 3,
    ANLZ_ERR_CANNOT_OPEN_DAT = 4,
    ANLZ_ERR_CANNOT_READ_DAT = 5,
    ANLZ_ERR_DAT_CORRUPTED = 6,
    ANLZ_ERR_CANNOT_OPEN_EXT = 7,
    ANLZ_ERR_CANNOT_READ_EXT = 8,
    ANLZ_ERR_EXT_SIZE_MISMATCH = 9,
    ANLZ_ERR_EXT_PPTH_INVALID = 10,
    ANLZ_ERR_EXT_PWV3_INVALID = 11,
    ANLZ_ERR_CANNOT_OPEN_AUDIO = 13,
    ANLZ_ERR_CANNOT_READ_AUDIO = 14,
    ANLZ_ERR_UNSUPPORTED_FORMAT = 15
} AnlzErrorCode;

// Cue point types
typedef enum {
    CUE_TYPE_SINGLE = 0,
    CUE_TYPE_LOOP = 1
} CueType;

// Cue point structure
typedef struct {
    uint8_t type;           // Bit 0: 0=cue, 1=loop; Bit 1: 0=inactive, 1=active
    uint32_t start_pos;     // Start position in frames (1/150s)
    uint32_t end_pos;       // End position in frames (for loops, 0xFFFF if not a loop)
} CuePoint;

// Beat grid entry
typedef struct {
    uint16_t bpm;           // BPM value (multiplied by 100)
    uint32_t position;      // Position in frames (1/150s)
} BeatGridEntry;

// ANLZ data structure
typedef struct {
    // Track metadata
    uint16_t original_bpm;
    uint8_t grid_offset;    // First beat position (1-4)
    uint32_t track_length_ms;
    
    // Beat grid (dynamically allocated)
    BeatGridEntry* beat_grid;
    uint32_t beat_grid_count;
    
    // Waveforms
    // Preview waveform: 3 bands × 800 samples each
    // Band 0: Mid-range frequencies (vocals, instruments - highest energy)
    // Band 1: High frequencies (treble)
    // Band 2: Low frequencies (bass)
    // Statically allocated, downsampled from 1200 to 800, scaled from 0-255 to 0-64
    uint8_t preview_waveform[3][PREVIEW_WAVEFORM_WIDTH];
    
    // Dynamic waveform: Variable size with 3 bands per entry
    // PWV7 format: each entry has 3 bytes [mid, high, low]
    // 150 entries per second of audio (75 frames/sec × 2 half-frames)
    // Dynamically allocated, scaled from 0-255 to 0-DYNAMIC_WAVEFORM_MAX_VALUE
    uint8_t* dynamic_waveform;
    uint32_t dynamic_waveform_entries;  // Number of entries (size = entries * 3)
    
    // Cue points (fixed arrays for up to 8 cues)
    CuePoint hot_cues[3];           // Hot cues A, B, C
    CuePoint memory_cues[8];        // Memory cue points
    uint8_t hot_cue_count;
    uint8_t memory_cue_count;
    
    // Audio file path
    char audio_path[ANLZ_PATH_SIZE];
    
} AnlzData;

// Function prototypes

/**
 * Initialize ANLZ data structure
 * @param data Pointer to AnlzData structure to initialize
 */
void anlz_init(AnlzData* data);

/**
 * Free dynamically allocated memory in ANLZ data structure
 * @param data Pointer to AnlzData structure to free
 */
void anlz_free(AnlzData* data);

/**
 * Parse ANLZ .DAT file
 * @param file_data Pointer to file data buffer
 * @param file_size Size of the file data
 * @param data Pointer to AnlzData structure to populate
 * @return Error code (0 = success)
 */
uint16_t anlz_parse_dat(const uint8_t* file_data, uint32_t file_size, AnlzData* data);

/**
 * Parse ANLZ .EXT file (color waveform data - PWV3/4/5)
 * Note: This parses COLOR waveforms, not 3-band frequency data
 * @param file_data Pointer to file data buffer
 * @param file_size Size of the file data
 * @param data Pointer to AnlzData structure to update
 * @return Error code (0 = success)
 */
uint16_t anlz_parse_ext(const uint8_t* file_data, uint32_t file_size, AnlzData* data);

/**
 * Parse PWV6 preview waveform from .2EX file (CORRECTED)
 * PWV6 is 3-band frequency overview (1200 entries) found in .2EX file
 * @param file_data Pointer to file data buffer
 * @param file_size Size of the file data
 * @param data Pointer to AnlzData structure to update
 * @return Error code (0 = success)
 */
uint16_t anlz_parse_preview(const uint8_t* file_data, uint32_t file_size, AnlzData* data);

/**
 * Parse PWV7 high-resolution waveform from .2EX file (NEW)
 * PWV7 is 3-band frequency high-res (variable entries) found in .2EX file
 * @param file_data Pointer to file data buffer
 * @param file_size Size of the file data
 * @param data Pointer to AnlzData structure to update
 * @return Error code (0 = success)
 */
uint16_t anlz_parse_dynamic(const uint8_t* file_data, uint32_t file_size, AnlzData* data);

/**
 * Read 32-bit big-endian value
 * @param data Pointer to 4-byte buffer
 * @return 32-bit value
 */
static inline uint32_t read_be32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           ((uint32_t)data[3]);
}

/**
 * Read 16-bit big-endian value
 * @param data Pointer to 2-byte buffer
 * @return 16-bit value
 */
static inline uint16_t read_be16(const uint8_t* data) {
    return ((uint16_t)data[0] << 8) | ((uint16_t)data[1]);
}

/**
 * Convert milliseconds to frames (1/150s units)
 * @param ms Milliseconds value
 * @return Frames value
 */
static inline uint32_t ms_to_frames(uint32_t ms) {
    return (ms * 3) / 20;
}

/**
 * Convert frames to milliseconds
 * @param frames Frames value (1/150s units)
 * @return Milliseconds value
 */
static inline uint32_t frames_to_ms(uint32_t frames) {
    return (frames * 20) / 3;
}

/**
 * Scale waveform value from Rekordbox range to target range
 * @param value Original value (0-50 for individual band, 0-150 for total)
 * @param max_value Target maximum value (21 for individual, 64 for total)
 * @param source_max Source maximum value (50 for individual, 150 for total)
 * @return Scaled value
 */
static inline uint8_t scale_waveform_value(uint8_t value, uint8_t max_value, uint8_t source_max) {
    // Scale: (value * max_value) / source_max
    // For individual: (value * 21) / 50
    // For total: (value * 64) / 150
    
    if (value == 0) return 0;
    
    // Use 16-bit intermediate to prevent overflow
    uint16_t scaled = ((uint16_t)value * max_value) / source_max;
    
    // Clamp to max_value
    return (scaled > max_value) ? max_value : (uint8_t)scaled;
}

// ============================================================================
// Implementation
// ============================================================================

#ifdef REKORDBOX_PARSER_IMPLEMENTATION

void anlz_init(AnlzData* data) {
    memset(data, 0, sizeof(AnlzData));
    data->beat_grid = NULL;
    data->dynamic_waveform = NULL;
}

void anlz_free(AnlzData* data) {
    if (data->beat_grid) {
        free(data->beat_grid);
        data->beat_grid = NULL;
    }
    if (data->dynamic_waveform) {
        free(data->dynamic_waveform);
        data->dynamic_waveform = NULL;
    }
}

static inline int verify_tag(const uint8_t* data, uint32_t pos, uint32_t expected_tag) {
    uint32_t tag = read_be32(&data[pos]);
    return (tag == expected_tag);
}

/**
 * Process preview waveform data
 * Input: 1200 samples × 3 bytes [mid, high, low]
 * Output: 800 samples × 3 bands, scaled properly
 * 
 * Scaling strategy:
 * - Individual values: 0-50 → 0-21 (so max stacked = 63, fits in 64)
 * - This prevents overflow when stacking all three bands
 */
static void process_preview_waveform(const uint8_t* raw_data, uint8_t output[3][PREVIEW_WAVEFORM_WIDTH]) {
    // Downsample from 1200 to 800 samples
    // Each output sample represents 1.5 input samples
    
    for (uint16_t i = 0; i < PREVIEW_WAVEFORM_WIDTH; i++) {
        // Calculate source position (1.5 samples per output)
        uint16_t src_pos = (i * 3) / 2;  // i * 1.5
        
        if (src_pos >= REKORDBOX_PREVIEW_SAMPLES) {
            src_pos = REKORDBOX_PREVIEW_SAMPLES - 1;
        }
        
        // Each entry is 3 bytes: [mid, high, low]
        uint32_t offset = src_pos * 3;
        uint8_t mid = raw_data[offset];
        uint8_t high = raw_data[offset + 1];
        uint8_t low = raw_data[offset + 2];
        
        // Scale each band individually from 0-50 to 0-21
        // This ensures that when stacked, max total is 63 (fits in 64)
        output[0][i] = scale_waveform_value(mid, PREVIEW_WAVEFORM_MAX_INDIVIDUAL, REKORDBOX_INDIVIDUAL_MAX);
        output[1][i] = scale_waveform_value(high, PREVIEW_WAVEFORM_MAX_INDIVIDUAL, REKORDBOX_INDIVIDUAL_MAX);
        output[2][i] = scale_waveform_value(low, PREVIEW_WAVEFORM_MAX_INDIVIDUAL, REKORDBOX_INDIVIDUAL_MAX);
    }
}

/**
 * Process dynamic waveform data
 * Scale individual values from 0-50 to 0-DYNAMIC_WAVEFORM_MAX_VALUE
 * Each entry is 3 bytes: [mid, high, low]
 */
static void process_dynamic_waveform(uint8_t* waveform, uint32_t num_entries) {
    for (uint32_t i = 0; i < num_entries * 3; i++) {
        waveform[i] = scale_waveform_value(waveform[i], DYNAMIC_WAVEFORM_MAX_VALUE, REKORDBOX_INDIVIDUAL_MAX);
    }
}

uint16_t anlz_parse_dat(const uint8_t* file_data, uint32_t file_size, AnlzData* data) {
    if (!file_data || !data || file_size < 12) {
        return ANLZ_ERR_DAT_CORRUPTED;
    }
    
    // Verify file size
    uint32_t header_file_size = read_be32(&file_data[8]);
    if (header_file_size != file_size) {
        return ANLZ_ERR_DAT_CORRUPTED;
    }
    
    // Get first tag position
    uint32_t pos = read_be32(&file_data[4]);
    
    // Verify PPTH tag
    if (!verify_tag(file_data, pos, TAG_PPTH)) {
        return ANLZ_ERR_DAT_CORRUPTED;
    }
    
    uint32_t ppth_header_size = read_be32(&file_data[pos + 4]);
    uint32_t ppth_tag_size = read_be32(&file_data[pos + 8]);
    uint32_t ppth_string_len = read_be32(&file_data[pos + 12]);
    
    // Extract path (UTF-16 to ASCII)
    uint32_t path_start = pos + ppth_header_size;
    uint32_t path_chars = (ppth_string_len < ANLZ_PATH_SIZE) ? ppth_string_len : (ANLZ_PATH_SIZE - 1);
    
    for (uint32_t i = 0; i < path_chars; i++) {
        data->audio_path[i] = file_data[path_start + i * 2 + 1];
    }
    data->audio_path[path_chars] = '\0';
    
    // Move to PVBR tag
    pos += ppth_tag_size;
    if (!verify_tag(file_data, pos, TAG_PVBR)) {
        return ANLZ_ERR_DAT_CORRUPTED;
    }
    
    uint32_t pvbr_header_size = read_be32(&file_data[pos + 4]);
    uint32_t pvbr_tag_size = read_be32(&file_data[pos + 8]);
    
    // Extract BPM info
    uint32_t pvbr_data = pos + pvbr_header_size;
    data->original_bpm = read_be16(&file_data[pvbr_data + 8]);
    data->grid_offset = file_data[pvbr_data + 14];
    
    // Move to PQTZ tag (beat grid)
    pos += pvbr_tag_size;
    if (!verify_tag(file_data, pos, TAG_PQTZ)) {
        return ANLZ_ERR_DAT_CORRUPTED;
    }
    
    uint32_t pqtz_header_size = read_be32(&file_data[pos + 4]);
    uint32_t pqtz_tag_size = read_be32(&file_data[pos + 8]);
    uint32_t pqtz_entry_bytes = read_be32(&file_data[pos + 12]);
    uint32_t pqtz_entry_count = read_be32(&file_data[pos + 16]);
    
    // Allocate beat grid
    if (pqtz_entry_count > MAX_BEAT_GRID_ENTRIES) {
        pqtz_entry_count = MAX_BEAT_GRID_ENTRIES;
    }
    
    data->beat_grid = (BeatGridEntry*)malloc(pqtz_entry_count * sizeof(BeatGridEntry));
    if (!data->beat_grid) {
        return ANLZ_ERR_DATA_NOT_READ;
    }
    
    data->beat_grid_count = pqtz_entry_count;
    
    // Parse beat grid entries
    uint32_t beat_data_start = pos + pqtz_header_size;
    for (uint32_t i = 0; i < pqtz_entry_count; i++) {
        uint32_t entry_pos = beat_data_start + (i * pqtz_entry_bytes);
        data->beat_grid[i].bpm = read_be16(&file_data[entry_pos]);
        data->beat_grid[i].position = read_be32(&file_data[entry_pos + 2]);
    }
    
    // Move to PWV2 tag
    pos += pqtz_tag_size;
    if (!verify_tag(file_data, pos, TAG_PWV2)) {
        return ANLZ_ERR_DAT_CORRUPTED;
    }
    
    uint32_t pwv2_tag_size = read_be32(&file_data[pos + 8]);
    
    // Move to PCOB tag (cue points)
    pos += pwv2_tag_size;
    if (!verify_tag(file_data, pos, TAG_PCOB)) {
        return ANLZ_ERR_DAT_CORRUPTED;
    }
    
    uint8_t pcob_type = file_data[pos + 15];
    uint32_t pcob_header_size = read_be32(&file_data[pos + 4]);
    uint32_t pcob_tag_size = read_be32(&file_data[pos + 8]);
    uint8_t num_cue_points = file_data[pos + 19] & 0x0F;
    
    // Parse Hot Cues (PCOB type 1)
    if (pcob_type == 1 && num_cue_points > 0) {
        data->hot_cue_count = (num_cue_points > 3) ? 3 : num_cue_points;
        uint32_t cue_pos = pos + pcob_header_size;
        
        for (uint8_t i = 0; i < num_cue_points; i++) {
            if (!verify_tag(file_data, cue_pos, TAG_PCPT)) {
                break;
            }
            
            uint32_t pcpt_header_size = read_be32(&file_data[cue_pos + 4]);
            uint32_t pcpt_tag_size = read_be32(&file_data[cue_pos + 8]);
            uint8_t cue_id = file_data[cue_pos + 15];
            uint8_t cue_active = file_data[cue_pos + 19];
            
            if (cue_id > 0 && cue_id <= 3) {
                uint8_t idx = cue_id - 1;
                uint32_t data_pos = cue_pos + pcpt_header_size;
                
                if (cue_active) {
                    data->hot_cues[idx].type = 2; // Active
                }
                
                uint32_t start_ms = read_be32(&file_data[data_pos + 4]);
                data->hot_cues[idx].start_pos = ms_to_frames(start_ms);
                
                if (file_data[data_pos] == 2) { // Loop type
                    data->hot_cues[idx].type |= 0x1;
                    uint32_t end_ms = read_be32(&file_data[data_pos + 8]);
                    data->hot_cues[idx].end_pos = ms_to_frames(end_ms);
                }
            }
            
            cue_pos += pcpt_tag_size;
        }
    }
    
    // Move to second PCOB (Memory Cues)
    uint32_t pcob2_pos = pos + pcob_tag_size;
    if (verify_tag(file_data, pcob2_pos, TAG_PCOB)) {
        pcob_type = file_data[pcob2_pos + 15];
        pcob_header_size = read_be32(&file_data[pcob2_pos + 4]);
        num_cue_points = file_data[pcob2_pos + 19] & 0x0F;
        
        // Parse Memory Cues (PCOB type 0)
        if (pcob_type == 0 && num_cue_points > 0) {
            data->memory_cue_count = (num_cue_points > 8) ? 8 : num_cue_points;
            uint32_t cue_pos = pcob2_pos + pcob_header_size;
            
            for (uint8_t i = 0; i < data->memory_cue_count; i++) {
                if (!verify_tag(file_data, cue_pos, TAG_PCPT)) {
                    break;
                }
                
                uint32_t pcpt_header_size = read_be32(&file_data[cue_pos + 4]);
                uint32_t pcpt_tag_size = read_be32(&file_data[cue_pos + 8]);
                uint8_t cue_active = file_data[cue_pos + 19];
                
                if (cue_active) {
                    data->memory_cues[i].type = 2; // Active
                }
                
                uint32_t data_pos = cue_pos + pcpt_header_size;
                uint32_t start_ms = read_be32(&file_data[data_pos + 4]);
                data->memory_cues[i].start_pos = ms_to_frames(start_ms);
                
                if (file_data[data_pos] == 2) { // Loop type
                    data->memory_cues[i].type |= 0x1;
                    uint32_t end_ms = read_be32(&file_data[data_pos + 8]);
                    data->memory_cues[i].end_pos = ms_to_frames(end_ms);
                }
                
                cue_pos += pcpt_tag_size;
            }
        }
    }
    
    return ANLZ_OK;
}

uint16_t anlz_parse_ext(const uint8_t* file_data, uint32_t file_size, AnlzData* data) {
    // NOTE: .EXT file contains COLOR waveforms (PWV3/4/5), NOT 3-band frequency data
    // This is kept for legacy compatibility but most users will want PWV6/7 from .2EX
    
    if (!file_data || !data || file_size < 12) {
        return ANLZ_ERR_EXT_PWV3_INVALID;
    }
    
    // Verify file size (big-endian)
    uint32_t header_file_size = read_be32(&file_data[8]);
    if (header_file_size != file_size) {
        return ANLZ_ERR_EXT_SIZE_MISMATCH;
    }
    
    // Get first tag position
    uint32_t pos = read_be32(&file_data[4]);
    
    // Verify PPTH tag
    if (!verify_tag(file_data, pos, TAG_PPTH)) {
        return ANLZ_ERR_EXT_PPTH_INVALID;
    }
    
    uint32_t ppth_tag_size = read_be32(&file_data[pos + 8]);
    
    // Move to PWV3 section (color waveform, not frequency bands)
    pos += ppth_tag_size;
    
    if (!verify_tag(file_data, pos, TAG_PWV3)) {
        return ANLZ_ERR_EXT_PWV3_INVALID;
    }
    
    // PWV3 is single-byte color waveform data, not 3-band frequency data
    // If you need 3-band frequency data, use anlz_parse_preview() or anlz_parse_dynamic() on .2EX file
    
    return ANLZ_OK;
}

uint16_t anlz_parse_preview(const uint8_t* file_data, uint32_t file_size, AnlzData* data) {
    // Parse PWV6 from .2EX file
    // PWV6 has 1200 entries × 3 bytes [mid, high, low]
    // Header is 20 bytes (not 14 as documented)
    
    if (!file_data || !data || file_size < 12) {
        return ANLZ_ERR_EXT_PWV3_INVALID;
    }
    
    // Verify file size
    uint32_t header_file_size = read_be32(&file_data[8]);
    if (header_file_size != file_size) {
        return ANLZ_ERR_EXT_SIZE_MISMATCH;
    }
    
    // Scan through file looking for PWV6 tag (0x50575636)
    uint32_t pos = read_be32(&file_data[4]); // Start at first tag
    
    // Scan up to 20 tags
    for (int i = 0; i < 20 && pos < file_size - 12; i++) {
        // Check if this is PWV6
        if (verify_tag(file_data, pos, TAG_PWV6)) {
            // Found PWV6!
            uint32_t pwv6_header_size = read_be32(&file_data[pos + 4]);
            uint32_t pwv6_tag_size = read_be32(&file_data[pos + 8]);
            uint32_t pwv6_entry_bytes = read_be32(&file_data[pos + 12]);
            uint32_t pwv6_num_entries = read_be32(&file_data[pos + 16]);
            
            // Verify structure: should be 3 bytes per entry, 1200 entries
            if (pwv6_entry_bytes != 3 || pwv6_num_entries != 1200) {
                return ANLZ_ERR_CANNOT_READ_EXT;
            }
            
            // PWV6 data starts after header (header is 20 bytes, not 14!)
            uint32_t pwv6_data_start = pos + pwv6_header_size;
            uint32_t pwv6_data_bytes = 3600; // 1200 entries × 3 bytes
            
            // Verify we have enough data in the file
            if (pwv6_data_start + pwv6_data_bytes > file_size) {
                return ANLZ_ERR_CANNOT_READ_EXT;
            }
            
            // Process the preview waveform
            process_preview_waveform(&file_data[pwv6_data_start], data->preview_waveform);
            
            return ANLZ_OK;
        }
        
        // Move to next tag
        uint32_t tag_size = read_be32(&file_data[pos + 8]);
        if (tag_size == 0 || tag_size > file_size) {
            break;
        }
        pos += tag_size;
    }
    
    // PWV6 not found
    return ANLZ_ERR_EXT_PWV3_INVALID;
}

uint16_t anlz_parse_dynamic(const uint8_t* file_data, uint32_t file_size, AnlzData* data) {
    // Parse PWV7 from .2EX file
    // PWV7 has variable entries × 3 bytes [mid, high, low]
    // Header is 24 bytes (not 14 as documented)
    
    if (!file_data || !data || file_size < 12) {
        return ANLZ_ERR_EXT_PWV3_INVALID;
    }
    
    // Verify file size
    uint32_t header_file_size = read_be32(&file_data[8]);
    if (header_file_size != file_size) {
        return ANLZ_ERR_EXT_SIZE_MISMATCH;
    }
    
    // Scan through file looking for PWV7 tag (0x50575637)
    uint32_t pos = read_be32(&file_data[4]); // Start at first tag
    
    // Scan up to 20 tags
    for (int i = 0; i < 20 && pos < file_size - 24; i++) {
        // Check if this is PWV7
        if (verify_tag(file_data, pos, TAG_PWV7)) {
            // Found PWV7!
            uint32_t pwv7_header_size = read_be32(&file_data[pos + 4]);
            uint32_t pwv7_tag_size = read_be32(&file_data[pos + 8]);
            uint32_t pwv7_entry_bytes = read_be32(&file_data[pos + 12]);
            uint32_t pwv7_num_entries = read_be32(&file_data[pos + 16]);
            
            // Verify structure: should be 3 bytes per entry
            if (pwv7_entry_bytes != 3) {
                return ANLZ_ERR_CANNOT_READ_EXT;
            }
            
            // PWV7 data starts after header (header is 24 bytes, not 14!)
            uint32_t pwv7_data_start = pos + pwv7_header_size;
            uint32_t pwv7_data_bytes = pwv7_num_entries * 3;
            
            // Verify we have enough data in the file
            if (pwv7_data_start + pwv7_data_bytes > file_size) {
                return ANLZ_ERR_CANNOT_READ_EXT;
            }
            
            // Allocate dynamic waveform buffer
            data->dynamic_waveform = (uint8_t*)malloc(pwv7_data_bytes);
            if (!data->dynamic_waveform) {
                return ANLZ_ERR_CANNOT_READ_EXT;
            }
            
            data->dynamic_waveform_entries = pwv7_num_entries;
            
            // Copy waveform data
            memcpy(data->dynamic_waveform, &file_data[pwv7_data_start], pwv7_data_bytes);
            
            // Scale waveform values from 0-255 to 0-DYNAMIC_WAVEFORM_MAX_VALUE
            process_dynamic_waveform(data->dynamic_waveform, data->dynamic_waveform_entries);
            
            return ANLZ_OK;
        }
        
        // Move to next tag
        uint32_t tag_size = read_be32(&file_data[pos + 8]);
        if (tag_size == 0 || tag_size > file_size) {
            break;
        }
        pos += tag_size;
    }
    
    // PWV7 not found
    return ANLZ_ERR_EXT_PWV3_INVALID;
}

#endif // REKORDBOX_PARSER_IMPLEMENTATION

#endif // REKORDBOX_ANLZ_PARSER_H