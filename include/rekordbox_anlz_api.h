#ifndef REKORDBOX_ANLZ_API_H
#define REKORDBOX_ANLZ_API_H

#include <stdint.h>
#include <FS.h>
#include "rekordbox_anlz_parser.h"

// ── Filesystem ───────────────────────────────────────────────────────────────
// Call anlz_setFS() once before any extract functions, e.g.:
//   anlz_setFS(rekordboxDrive);
extern FS* _anlz_fs;
void anlz_setFS(FS& fs);

// ── API declarations ─────────────────────────────────────────────────────────

uint16_t extractPreviewWaveform(const char* filepath, uint8_t preview_waveform[3][PREVIEW_WAVEFORM_WIDTH]);
uint16_t extractDynamicWaveform(const char* filepath, uint8_t** dynamic_waveform, uint32_t* num_entries);
uint16_t extractBeatGrid(const char* filepath, BeatGridEntry** beat_grid, uint32_t* num_entries,
                         uint16_t* original_bpm, uint8_t* grid_offset);
uint16_t extractHotCues(const char* filepath, CuePoint hot_cues[3], uint8_t* num_hot_cues);
uint16_t extractMemoryCues(const char* filepath, CuePoint memory_cues[8], uint8_t* num_memory_cues);
uint16_t extractAudioPath(const char* filepath, char* audio_path, uint16_t max_len);
uint16_t extractTrackInfo(const char* filepath, AnlzData* data);

#endif // REKORDBOX_ANLZ_API_H