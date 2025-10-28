#ifndef DB_MANAGER_H
#define DB_MANAGER_H

#include "sqlite3.h"
#include <stdint.h>

// Database handles
extern sqlite3 * mdb;
extern sqlite3 * pdb;

// Track structure
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
    double numberOfSamples;
    double sampleRate;
} Track;

// Beatgrid structures
typedef struct {
    double sampleOffset;
    int64_t beatIndex;
    uint32_t beatsUntilNext;
    uint32_t unknown;
} BeatMarker;

typedef struct {
    double sampleRate;
    double numSamples;
    uint8_t hasGrid;
    BeatMarker *markers;
    int markerCount;
    double samplesPerWaveformPoint; // e.g., 420 for 44.1kHz
} Beatgrid;

// Database initialization and cleanup
bool db_open();
void db_close();

// Track operations
int16_t db_get_track_count();
int16_t db_get_first_track_id();
Track* db_get_track_by_id(uint16_t track_id);
void db_free_track(Track* track);

// Waveform data operations
bool db_load_dynamic_waveform_data(uint16_t track_id, uint8_t** dynamicWaveSampleData, uint64_t* dynamicWaveformSampleCount, uint32_t* baseSampPerWavePoint); 
bool db_load_overview_waveform_data(uint16_t track_id, uint8_t** overViewWaveSampleData, uint64_t* overviewSampleCount, double* samplesPerOverviewPoint, uint16_t overviewChartHeight);
bool db_load_beatgrid_data(uint16_t track_id, Beatgrid* beatgrid, uint32_t baseSampPerWavePoint, uint32_t* all_long);
bool db_load_cues_data(uint16_t track_id); // TODO

#endif // DB_MANAGER_H