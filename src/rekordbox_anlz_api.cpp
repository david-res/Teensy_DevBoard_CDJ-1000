// rekordbox_anlz_api.cpp
// All implementations live here — never in the header.

#include <FS.h>
#include "rekordbox_anlz_parser.h"
#include "rekordbox_anlz_api.h"

// ── Filesystem ───────────────────────────────────────────────────────────────
FS* _anlz_fs = nullptr;

void anlz_setFS(FS& fs) {
    _anlz_fs = &fs;
}

// ── Internal file macros (used only in this .cpp) ────────────────────────────
#define ANLZ_FILE_OPEN(path)            _anlz_fs->open(path, FILE_READ)
#define ANLZ_FILE_CLOSE(f)              f.close()
#define ANLZ_FILE_SIZE(f)               f.size()
#define ANLZ_FILE_READ_DATA(f, buf, len) f.read(buf, len)
#define ANLZ_FILE_VALID(f)              ((bool)(f))

// ── Guard against null FS ────────────────────────────────────────────────────
#define ANLZ_CHECK_FS(errcode) \
    if (!_anlz_fs) { Serial.println("ANLZ: call anlz_setFS() first"); return errcode; }

// ── Implementations ──────────────────────────────────────────────────────────

uint16_t extractPreviewWaveform(const char* filepath, uint8_t preview_waveform[3][PREVIEW_WAVEFORM_WIDTH]) {
    ANLZ_CHECK_FS(ANLZ_ERR_CANNOT_OPEN_EXT);
    File file = ANLZ_FILE_OPEN(filepath);
    if (!ANLZ_FILE_VALID(file)) return ANLZ_ERR_CANNOT_OPEN_EXT;

    uint32_t file_size = ANLZ_FILE_SIZE(file);
    uint8_t* buffer = (uint8_t*)malloc(file_size);
    if (!buffer) { ANLZ_FILE_CLOSE(file); return ANLZ_ERR_CANNOT_READ_EXT; }

    uint32_t bytes_read = ANLZ_FILE_READ_DATA(file, buffer, file_size);
    ANLZ_FILE_CLOSE(file);
    if (bytes_read != file_size) { free(buffer); return ANLZ_ERR_CANNOT_READ_EXT; }

    AnlzData temp_data;
    anlz_init(&temp_data);
    uint16_t error = anlz_parse_preview(buffer, file_size, &temp_data);
    free(buffer);

    if (error == ANLZ_OK)
        memcpy(preview_waveform, temp_data.preview_waveform, sizeof(temp_data.preview_waveform));

    return error;
}

uint16_t extractDynamicWaveform(const char* filepath, uint8_t** dynamic_waveform, uint32_t* num_entries) {
    ANLZ_CHECK_FS(ANLZ_ERR_CANNOT_OPEN_EXT);
    File file = ANLZ_FILE_OPEN(filepath);
    if (!ANLZ_FILE_VALID(file)) return ANLZ_ERR_CANNOT_OPEN_EXT;
    Serial.printf("ANLZ: opened %s\n", filepath);

    uint32_t file_size = ANLZ_FILE_SIZE(file);
    uint8_t* buffer = (uint8_t*)malloc(file_size);
    if (!buffer) { ANLZ_FILE_CLOSE(file); return ANLZ_ERR_CANNOT_READ_EXT; }

    uint32_t bytes_read = ANLZ_FILE_READ_DATA(file, buffer, file_size);
    ANLZ_FILE_CLOSE(file);
    if (bytes_read != file_size) { free(buffer); return ANLZ_ERR_CANNOT_READ_EXT; }

    if (*dynamic_waveform) { free(*dynamic_waveform); *dynamic_waveform = nullptr; }

    AnlzData temp_data;
    anlz_init(&temp_data);
    uint16_t error = anlz_parse_dynamic(buffer, file_size, &temp_data);
    free(buffer);

    if (error == ANLZ_OK) {
        *dynamic_waveform = temp_data.dynamic_waveform;
        *num_entries = temp_data.dynamic_waveform_entries;
    }

    return error;
}

uint16_t extractBeatGrid(const char* filepath, BeatGridEntry** beat_grid, uint32_t* num_entries,
                         uint16_t* original_bpm, uint8_t* grid_offset) {
    ANLZ_CHECK_FS(ANLZ_ERR_CANNOT_OPEN_DAT);
    File file = ANLZ_FILE_OPEN(filepath);
    if (!ANLZ_FILE_VALID(file)) return ANLZ_ERR_CANNOT_OPEN_DAT;

    uint32_t file_size = ANLZ_FILE_SIZE(file);
    uint8_t* buffer = (uint8_t*)malloc(file_size);
    if (!buffer) { ANLZ_FILE_CLOSE(file); return ANLZ_ERR_DATA_NOT_READ; }

    uint32_t bytes_read = ANLZ_FILE_READ_DATA(file, buffer, file_size);
    ANLZ_FILE_CLOSE(file);
    if (bytes_read != file_size) { free(buffer); return ANLZ_ERR_DATA_NOT_READ; }

    AnlzData temp_data;
    anlz_init(&temp_data);
    uint16_t error = anlz_parse_dat(buffer, file_size, &temp_data);
    free(buffer);

    if (error == ANLZ_OK) {
        *beat_grid    = temp_data.beat_grid;
        *num_entries  = temp_data.beat_grid_count;
        *original_bpm = temp_data.original_bpm;
        *grid_offset  = temp_data.grid_offset;
    }

    return error;
}

uint16_t extractExtCues(const char* filepath, AnlzData* data) {
    ANLZ_CHECK_FS(ANLZ_ERR_CANNOT_OPEN_EXT);
    File file = ANLZ_FILE_OPEN(filepath);
    if (!ANLZ_FILE_VALID(file)) return ANLZ_ERR_CANNOT_OPEN_EXT;

    uint32_t file_size = ANLZ_FILE_SIZE(file);
    uint8_t* buffer = (uint8_t*)malloc(file_size);
    if (!buffer) { ANLZ_FILE_CLOSE(file); return ANLZ_ERR_CANNOT_READ_EXT; }

    uint32_t bytes_read = ANLZ_FILE_READ_DATA(file, buffer, file_size);
    ANLZ_FILE_CLOSE(file);
    if (bytes_read != file_size) { free(buffer); return ANLZ_ERR_CANNOT_READ_EXT; }

    uint16_t error = anlz_parse_ext_cues(buffer, file_size, data);
    free(buffer);
    return error;
}

uint16_t extractHotCues(const char* filepath, CuePoint hot_cues[8], uint8_t* num_hot_cues) {
    ANLZ_CHECK_FS(ANLZ_ERR_CANNOT_OPEN_EXT);
    File file = ANLZ_FILE_OPEN(filepath);
    if (!ANLZ_FILE_VALID(file)) return ANLZ_ERR_CANNOT_OPEN_EXT;

    uint32_t file_size = ANLZ_FILE_SIZE(file);
    uint8_t* buffer = (uint8_t*)malloc(file_size);
    if (!buffer) { ANLZ_FILE_CLOSE(file); return ANLZ_ERR_CANNOT_READ_EXT; }

    uint32_t bytes_read = ANLZ_FILE_READ_DATA(file, buffer, file_size);
    ANLZ_FILE_CLOSE(file);
    if (bytes_read != file_size) { free(buffer); return ANLZ_ERR_CANNOT_READ_EXT; }

    AnlzData temp_data;
    anlz_init(&temp_data);
    uint16_t error = anlz_parse_ext_cues(buffer, file_size, &temp_data);
    free(buffer);

    if (error == ANLZ_OK) {
        memcpy(hot_cues, temp_data.hot_cues, sizeof(temp_data.hot_cues));
        *num_hot_cues = temp_data.hot_cue_count;
    }
    return error;
}

uint16_t extractMemoryCues(const char* filepath, CuePoint memory_cues[8], uint8_t* num_memory_cues) {
    ANLZ_CHECK_FS(ANLZ_ERR_CANNOT_OPEN_DAT);
    File file = ANLZ_FILE_OPEN(filepath);
    if (!ANLZ_FILE_VALID(file)) return ANLZ_ERR_CANNOT_OPEN_DAT;

    uint32_t file_size = ANLZ_FILE_SIZE(file);
    uint8_t* buffer = (uint8_t*)malloc(file_size);
    if (!buffer) { ANLZ_FILE_CLOSE(file); return ANLZ_ERR_DATA_NOT_READ; }

    uint32_t bytes_read = ANLZ_FILE_READ_DATA(file, buffer, file_size);
    ANLZ_FILE_CLOSE(file);
    if (bytes_read != file_size) { free(buffer); return ANLZ_ERR_DATA_NOT_READ; }

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
    ANLZ_CHECK_FS(ANLZ_ERR_CANNOT_OPEN_DAT);
    File file = ANLZ_FILE_OPEN(filepath);
    if (!ANLZ_FILE_VALID(file)) return ANLZ_ERR_CANNOT_OPEN_DAT;

    uint32_t file_size = ANLZ_FILE_SIZE(file);
    uint8_t* buffer = (uint8_t*)malloc(file_size);
    if (!buffer) { ANLZ_FILE_CLOSE(file); return ANLZ_ERR_DATA_NOT_READ; }

    uint32_t bytes_read = ANLZ_FILE_READ_DATA(file, buffer, file_size);
    ANLZ_FILE_CLOSE(file);
    if (bytes_read != file_size) { free(buffer); return ANLZ_ERR_DATA_NOT_READ; }

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
    ANLZ_CHECK_FS(ANLZ_ERR_CANNOT_OPEN_DAT);
    File file = ANLZ_FILE_OPEN(filepath);
    if (!ANLZ_FILE_VALID(file)) return ANLZ_ERR_CANNOT_OPEN_DAT;

    uint32_t file_size = ANLZ_FILE_SIZE(file);
    uint8_t* buffer = (uint8_t*)malloc(file_size);
    if (!buffer) { ANLZ_FILE_CLOSE(file); return ANLZ_ERR_DATA_NOT_READ; }

    uint32_t bytes_read = ANLZ_FILE_READ_DATA(file, buffer, file_size);
    ANLZ_FILE_CLOSE(file);
    if (bytes_read != file_size) { free(buffer); return ANLZ_ERR_DATA_NOT_READ; }

    uint16_t error = anlz_parse_dat(buffer, file_size, data);
    free(buffer);
    return error;
}