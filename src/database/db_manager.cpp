#include "db_manager.h"
#include "inflate.h"
#include "Arduino.h"
#include "lv_utils.h"
#include <stdlib.h>
#include <string.h>

// Database handles
sqlite3 * mdb = nullptr;
sqlite3 * pdb = nullptr;

// Helper function for error logging
FLASHMEM static void log_sqlite_error(const char* context, sqlite3* db)
{
    if (db) {
        int ext_rc = sqlite3_extended_errcode(db);
        Serial.printf("SQL error in %s: %s (Code: %d, Extended: %d)\n", context, sqlite3_errmsg(db), sqlite3_errcode(db), ext_rc);
    } else {
        Serial.printf("SQL error in %s: database handle is null\n", context);
    }
}

FLASHMEM bool db_open()
{
    Serial.println("Initializing database connections...");
    
    // Open metadata database
    if (sqlite3_open("databases/m.db", &mdb) != SQLITE_OK) {
        log_sqlite_error("db_open (m.db)", mdb);
        return false;
    }
    Serial.println("m.db opened successfully");
    
    // Note: p.db is opened on-demand in load functions
    Serial.println("Database initialization complete");
    return true;
}

FLASHMEM void db_close()
{
    if (mdb) {
        sqlite3_close(mdb);
        mdb = nullptr;
    }
    if (pdb) {
        sqlite3_close(pdb);
        pdb = nullptr;
    }
}

FLASHMEM int16_t db_get_track_count()
{
    if (!mdb) {
        Serial.println("Error: mdb not initialized");
        return -1;
    }
    
    sqlite3_stmt *stmt;
    int16_t count = 0;
    
    if (sqlite3_prepare_v2(mdb, "SELECT COUNT(*) FROM Track;", -1, &stmt, NULL) != SQLITE_OK) {
        log_sqlite_error("db_get_track_count", mdb);
        return -1;
    }
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
        Serial.printf("Total tracks: %d\n", count);
    }
    
    sqlite3_finalize(stmt);
    return count;
}

FLASHMEM int16_t db_get_first_track_id()
{
    if (!mdb) {
        Serial.println("Error: mdb not initialized");
        return -1;
    }
    
    sqlite3_stmt *stmt;
    int16_t first_id = -1;
    
    const char *sql = "SELECT id FROM Track ORDER BY id ASC LIMIT 1";
    
    if (sqlite3_prepare_v2(mdb, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            first_id = sqlite3_column_int(stmt, 0);
        }
    } else {
        log_sqlite_error("db_get_first_track_id", mdb);
    }
    
    sqlite3_finalize(stmt);
    
    return first_id;
}

FLASHMEM  Track* db_get_track_by_id(uint16_t track_id)
{
    if (!mdb) {
        Serial.println("Error: mdb not initialized");
        return nullptr;
    }
    
    Track *track = (Track *)calloc(1, sizeof(Track));
    if (!track) {
        Serial.println("Failed to allocate track memory");
        return nullptr;
    }
    
    sqlite3_stmt *stmt;
    const char *sqlTrack = "SELECT length, bpmAnalyzed, filename, path, title, artist, key, rating FROM Track WHERE id = ?";
    
    if (sqlite3_prepare_v2(mdb, sqlTrack, -1, &stmt, NULL) != SQLITE_OK) {
        log_sqlite_error("db_get_track_by_id", mdb);
        free(track);
        return nullptr;
    }
    
    sqlite3_bind_int(stmt, 1, track_id);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        // Length
        int lenVal = sqlite3_column_int(stmt, 0);
        char buf[164];
        snprintf(buf, sizeof(buf), "%d", lenVal);
        track->trackLength = strdup(buf);
        
        // BPM
        track->bpmAnalyzed = (float)sqlite3_column_double(stmt, 1);
        
        // Text fields
        const char *filename = (const char *)sqlite3_column_text(stmt, 2);
        const char *path     = (const char *)sqlite3_column_text(stmt, 3);
        const char *title    = (const char *)sqlite3_column_text(stmt, 4);
        const char *artist   = (const char *)sqlite3_column_text(stmt, 5);
        const char *key      = (const char *)sqlite3_column_text(stmt, 6);
        uint8_t rating       = (uint8_t)sqlite3_column_int(stmt, 7);
        
        if (filename) track->filename = strdup(filename);
        if (path)     track->path     = strdup(path);
        if (title)    track->title    = strdup(title);
        if (artist)   track->artist   = strdup(artist);
        if (key)      track->musical_key = strdup(key);
        if (rating)   track->star_rating = lookupValue(rating);
        
        track->track_id = track_id;
    } else {
        log_sqlite_error("db_get_track_by_id (no data)", mdb);
        sqlite3_finalize(stmt);
        free(track);
        return nullptr;
    }
    
    sqlite3_finalize(stmt);
    return track;
}

FLASHMEM void db_free_track(Track *track)
{
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

FLASHMEM bool db_load_dynamic_waveform_data(uint16_t track_id, uint8_t** dynamicWaveSampleData, uint64_t* dynamicWaveformSampleCount, uint32_t* baseSampPerWavePoint)
{  
    Serial.printf("Loading dynamic waveform for track_id %d\n", track_id);
    
    // Open performance database
    if (sqlite3_open("databases/p.db", &pdb) != SQLITE_OK) {
        log_sqlite_error("db_load_dynamic_waveform_data (open)", pdb);
        return false;
    }
    
    sqlite3_stmt *stmt = nullptr;
    const char *sqlPerfData = "SELECT highResolutionWaveFormData FROM PerformanceData WHERE trackId = ?";
    
    if (sqlite3_prepare_v2(pdb, sqlPerfData, -1, &stmt, NULL) != SQLITE_OK) {
        log_sqlite_error("db_load_dynamic_waveform_data (prepare)", pdb);
        sqlite3_close(pdb);
        return false;
    }
    
    sqlite3_bind_int(stmt, 1, track_id);
    
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        Serial.println("No dynamic waveform data found for track_id");
        sqlite3_finalize(stmt);
        sqlite3_close(pdb);
        return false;
    }
    
    const void *highResblobData = sqlite3_column_blob(stmt, 0);
    int32_t fileSize = sqlite3_column_bytes(stmt, 0);
    
    uint8_t *highResBuffer = (uint8_t *)malloc(fileSize);
    if (highResBuffer == NULL) {
        Serial.println("Failed to allocate highResBuffer");
        sqlite3_finalize(stmt);
        sqlite3_close(pdb);
        return false;
    }
    
    memcpy(highResBuffer, highResblobData, fileSize);
    
    // Done with SQLite - clean up immediately
    sqlite3_finalize(stmt);
    sqlite3_close(pdb);
    
    // Read uncompressed size
    uint32_t uncompressedSize = 0;
    memcpy(&uncompressedSize, highResBuffer, 4);
    uncompressedSize = __builtin_bswap32(uncompressedSize);
    Serial.printf("File size: %d, Uncompressed size: %u\n", fileSize, uncompressedSize);
    
    // Allocate output buffer
    uint8_t *uncompressedBuffer = (uint8_t *)malloc(uncompressedSize);
    if (uncompressedBuffer == NULL) {
        Serial.println("ERROR: uncompressedBuffer allocation failed!");
        free(highResBuffer);
        return false;
    }
    
    // Decompress
    int64_t zlib_rc = inflate_zlib(
        (const unsigned char *)(highResBuffer + 4), 
        (uint64_t)(fileSize - 4),
        (unsigned char *)uncompressedBuffer, 
        (uint64_t)uncompressedSize
    );
    
    if (zlib_rc < 0) {
        Serial.printf("ERROR: Decompression failed with code %lld\n", zlib_rc);
        free(uncompressedBuffer);
        free(highResBuffer);
        return false;
    }
    
    Serial.printf("Decompressed to %lld bytes\n", zlib_rc);

    /* Waveform spec:
    int64 big-endian - number of samples
    int64 big-endian - number of samples again (don't know why)
    double big-endian - number of samples per waveform point
    BEGIN repeated section *  number of samples
    uint8 - low-frequency waveform height, 0 means silence, 255 means max volume
    uint8 - medium-frequency waveform height, 0 means silence, 255 means max volume
    uint8 - high-frequency waveform height, 0 means silence, 255 means max volume
    uint8 - low-frequency waveform opacity, 0 means invisible, 255 means opaque
    uint8 - medium-frequency waveform opacity, 0 means invisible, 255 means opaque
    uint8 - high-frequency waveform opacity, 0 means invisible, 255 means opaque
    END repeated section
    uint8 - low-frequency waveform height from repeated section
    uint8 - medium-frequency waveform height from repeated section
    uint8 - high-frequency waveform height from repeated section
    uint8 - low-frequency waveform opacity from repeated section
    uint8 - medium-frequency waveform opacity from repeated section
    uint8 - high-frequency waveform opacity from repeated section
    There may be extra junk data after this point - it can be ignored
    */
    
    // Extract sample count
    memcpy(dynamicWaveformSampleCount, uncompressedBuffer, 8);
    *dynamicWaveformSampleCount = __builtin_bswap64(*dynamicWaveformSampleCount);
    Serial.printf("sampleCount: %" PRId64 "\n", *dynamicWaveformSampleCount);
    
    // Extract samplesPerWaveformPoint
    union { char b[8]; double numSamplesPerWaveformPoint; };
    for (uint8_t i = 16; i < 24; i++) {
        b[23 - i] = uncompressedBuffer[i];
    }
    *baseSampPerWavePoint = (uint32_t)numSamplesPerWaveformPoint;
    Serial.printf("numSamplesPerWaveformPoint: %lf (converted to %u)\n", numSamplesPerWaveformPoint, *baseSampPerWavePoint);
    
    
    // Create data arrays
    for (int8_t i = 0; i < 6; i++) {
        dynamicWaveSampleData[i] = (uint8_t *)malloc(*dynamicWaveformSampleCount);
        if (!dynamicWaveSampleData[i]) {
            Serial.printf("ERROR: Failed to allocate dynamicWaveSampleData[%d]\n", i);
            for (int8_t j = 0; j < i; j++) {
                free(dynamicWaveSampleData[j]);
            }
            free(uncompressedBuffer);
            free(highResBuffer);
            return false;
        }
    }
    
    // Fill data arrays
    uint32_t index = 0;
    for (uint32_t i = 24; i < (*dynamicWaveformSampleCount * 6) + 24; i += 6) {
        for (uint8_t j = 0; j < 6; j++) {
            dynamicWaveSampleData[j][index] = uncompressedBuffer[i + j];
        }
        index++;
    }
    Serial.printf("Filled %u samples into dynamicWaveSampleData arrays\n", index);
    
    // Free temporary buffers
    free(uncompressedBuffer);
    free(highResBuffer);
    
    Serial.println("Dynamic waveform data loaded");
    return true;
}

FLASHMEM bool db_load_overview_waveform_data(uint16_t track_id, uint8_t** overViewWaveSampleData, uint64_t* overviewSampleCount, double* samplesPerOverviewPoint, uint16_t overviewChartHeight)
{
    Serial.printf("Loading overview waveform for track_id %d\n", track_id);
    
    if (!mdb) {
        Serial.println("Failed to open database - mdb not initialized");
        return false;
    }
    
    sqlite3_stmt *stmt = nullptr;
    const char *sqlOverviewData = "SELECT overviewWaveFormData FROM PerformanceData WHERE trackId = ?";
    
    if (sqlite3_prepare_v2(mdb, sqlOverviewData, -1, &stmt, NULL) != SQLITE_OK) {
        log_sqlite_error("db_load_overview_waveform_data (prepare)", mdb);
        return false;
    }
    
    sqlite3_bind_int(stmt, 1, track_id);
    
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        Serial.println("No overview data found for track_id");
        sqlite3_finalize(stmt);
        return false;
    }
    
    const void *OverViewBlobData = sqlite3_column_blob(stmt, 0);
    uint64_t fileSize = sqlite3_column_bytes(stmt, 0);
    
    uint8_t *overviewBuffer = (uint8_t *)malloc(fileSize);
    if (!overviewBuffer) {
        Serial.println("Failed to allocate overviewBuffer");
        sqlite3_finalize(stmt);
        return false;
    }
    
    memcpy(overviewBuffer, OverViewBlobData, fileSize);
    
    // Done with SQLite - clean up
    sqlite3_finalize(stmt);
    
    uint32_t uncompressedSize = 0;
    memcpy(&uncompressedSize, overviewBuffer, 4);
    uncompressedSize = __builtin_bswap32(uncompressedSize);
    
    uint8_t *uncompressedBuffer = (uint8_t *)malloc(uncompressedSize);
    if (!uncompressedBuffer) {
        Serial.println("Failed to allocate uncompressedBuffer");
        free(overviewBuffer);
        return false;
    }
    
    // Decompress
    int64_t zlib_rc = inflate_zlib(
        (const unsigned char *)(overviewBuffer + 4), 
        (uint64_t)(fileSize - 4),
        (unsigned char *)uncompressedBuffer, 
        (uint64_t)uncompressedSize
    );
    
    if (zlib_rc < 0) {
        Serial.printf("ERROR: Decompression failed with code %lld\n", zlib_rc);
        free(uncompressedBuffer);
        free(overviewBuffer);
        return false;
    }

    /* OverviewWaveform spec:
    int64 big-endian - number of samples in overview waveform, always 1024
    int64 big-endian - number of samples in overview waveform again (don't know why), always 1024
    double big-endian - number of samples per waveform point
    BEGIN repeated section * number of samples
    uint8 - low-frequency waveform height, 0 means silence, 255 means max volume
    uint8 - medium-frequency waveform height, 0 means silence, 255 means max volume
    uint8 - high-frequency waveform height, 0 means silence, 255 means max volume
    END repeated section
    uint8 - maximum low-frequency waveform height from repeated section
    uint8 - maximum medium-frequency waveform height from repeated section
    uint8 - maximum high-frequency waveform height from repeated section
    There may be extra junk data after this point - it can be ignored 
    */
    
    // Extract sample count
    memcpy(overviewSampleCount, uncompressedBuffer, 8);
    *overviewSampleCount = __builtin_bswap64(*overviewSampleCount);
    Serial.printf("Overview sample count: %" PRId64 "\n", *overviewSampleCount);
    
    // Extract samplesPerWaveformPoint
    union { char b[8]; double numSamplesPerWaveformPoint; };
    for (uint8_t i = 16; i < 24; i++) {
        b[23 - i] = uncompressedBuffer[i];
    }
    *samplesPerOverviewPoint = numSamplesPerWaveformPoint;
    
    // Create data arrays
    for (int8_t i = 0; i < 3; i++) {
        overViewWaveSampleData[i] = (uint8_t *)malloc(800);
        if (!overViewWaveSampleData[i]) {
            Serial.printf("ERROR: Failed to allocate overViewWaveSampleData[%d]\n", i);
            for (int8_t j = 0; j < i; j++) {
                free(overViewWaveSampleData[j]);
            }
            free(uncompressedBuffer);
            free(overviewBuffer);
            return false;
        }
    }
    
    // Fill data arrays with interpolation
    float heightRatio = (float)overviewChartHeight / 255.0;
    float scaleFactor = (float)1024 / 800;
    const float waveformUserGain[3] = {1.0, 0.66, 0.33};
    
    for (uint32_t i = 0; i < 800; i++) {
        float srcIndex = i * scaleFactor;
        uint32_t idx = (uint32_t)srcIndex;
        float frac = srcIndex - idx;
        
        uint32_t bufferOffset = 24 + (idx * 3);
        
        for (uint8_t j = 0; j < 3; j++) {
            uint8_t v1 = uncompressedBuffer[bufferOffset + j];
            uint8_t v2;
            
            if (idx + 1 < 1024) {
                v2 = uncompressedBuffer[bufferOffset + 3 + j];
            } else {
                v2 = v1;
            }
            
            float interpolatedValue = v1 + (frac * (v2 - v1));
            float finalValue = interpolatedValue * heightRatio * waveformUserGain[j];
            overViewWaveSampleData[j][i] = (uint8_t)(finalValue + 0.5f);
        }
    }
    
    // Free temporary buffers
    free(uncompressedBuffer);
    free(overviewBuffer);
    
    Serial.println("Overview waveform data loaded");
    return true;
}

FLASHMEM bool db_load_beatgrid_data(uint16_t track_id, Beatgrid* beatgrid, uint32_t baseSampPerWavePoint, uint32_t* all_long)
{
    // Clear the beatgrid structure
    memset(beatgrid, 0, sizeof(Beatgrid));
    
    if (!mdb) {
        Serial.println("Failed to open beatgrid database - mdb not initialized");
        return false;
    }
    
    sqlite3_stmt *stmt = nullptr;
    const char *sqlBeatData = "SELECT beatData FROM PerformanceData WHERE trackId = ?";
    
    if (sqlite3_prepare_v2(mdb, sqlBeatData, -1, &stmt, NULL) != SQLITE_OK) {
        log_sqlite_error("db_load_beatgrid_data (prepare)", mdb);
        return false;
    }
    
    sqlite3_bind_int(stmt, 1, track_id);
    
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        Serial.println("No beatgrid data found");
        sqlite3_finalize(stmt);
        return false;
    }
    
    const void *beatBlobData = sqlite3_column_blob(stmt, 0);
    int beatFileSize = sqlite3_column_bytes(stmt, 0);
    
    if (beatFileSize == 0) {
        Serial.println("Empty beatgrid data");
        sqlite3_finalize(stmt);
        return false;
    }
    
    Serial.printf("Beatgrid blob size: %d bytes\n", beatFileSize);
    
    uint8_t *beatBuffer = (uint8_t *)malloc(beatFileSize);
    if (!beatBuffer) {
        Serial.println("Failed to allocate beatgrid buffer");
        sqlite3_finalize(stmt);
        return false;
    }
    
    memcpy(beatBuffer, beatBlobData, beatFileSize);
    
    // Done with SQLite - clean up
    sqlite3_finalize(stmt);
    
    // Extract uncompressed size
    uint32_t beatUncompressedSize;
    memcpy(&beatUncompressedSize, beatBuffer, 4);
    beatUncompressedSize = __builtin_bswap32(beatUncompressedSize);
    Serial.printf("Beatgrid uncompressed size: %u\n", beatUncompressedSize);
    
    uint8_t *uncompressedBuffer = (uint8_t *)malloc(beatUncompressedSize);
    if (!uncompressedBuffer) {
        Serial.println("Failed to allocate uncompressed beatgrid buffer");
        free(beatBuffer);
        return false;
    }
    
    // Decompress
    int64_t inflate_rc = inflate_zlib(
        (const unsigned char *)(beatBuffer + 4), 
        (uint64_t)(beatFileSize - 4), 
        (unsigned char *)uncompressedBuffer, 
        (uint64_t)beatUncompressedSize
    );
    
    free(beatBuffer);
    
    if (inflate_rc < 0) {
        Serial.printf("Beatgrid decompression failed with error code: %lld\n", inflate_rc);
        free(uncompressedBuffer);
        return false;
    }
    
    /* Beat Grid data spec:
    Sample Rate (in Hertz):
    Track length (in samples):
    Is beat data set (always 1):
    Default beatgrid:
    Number N of "markers" in this beatgrid	uint64	2 or 3, usually 2
    Beat grid marker (repeated N times)	marker*N	see below..
    Sample offset	double (little-endian!)	-ve or +ve number!
    Beat number/index	int64 (little-endian!)	-ve or +ve number!
    Number of beats until next marker (0 if done)	uint32 (little-endian!)	+ve, or 0 if done
    Unknown field?!?	uint32 (little-endian!)	???
    Adjusted beatgrid: (same as default if unadjusted)

    The first beat marker in any beat grid is always "beat -4", i.e. four beats before the first usable beat
    in the track. Hence, its sample offset in the file is negative (before the start of the track!)
    */
    
    // Parse beatgrid data
    uint32_t offset = 0;
    
    auto checkBounds = [&](uint32_t bytesNeeded) -> bool {
        if (offset + bytesNeeded > beatUncompressedSize) {
            Serial.printf("ERROR: Buffer overrun at offset %u\n", offset);
            return false;
        }
        return true;
    };
    
    // Extract sample rate
    if (!checkBounds(8)) {
        free(uncompressedBuffer);
        return false;
    }
    union { char b[8]; double sampleRate; } srUnion;
    for (uint8_t i = 0; i < 8; i++) {
        srUnion.b[7 - i] = uncompressedBuffer[offset + i];
    }
    beatgrid->sampleRate = srUnion.sampleRate;
    offset += 8;
    
    // Extract number of samples
    if (!checkBounds(8)) {
        free(uncompressedBuffer);
        return false;
    }
    union { char b[8]; double numSamples; } nsUnion;
    for (uint8_t i = 0; i < 8; i++) {
        nsUnion.b[7 - i] = uncompressedBuffer[offset + i];
    }
    beatgrid->numSamples = nsUnion.numSamples;
    offset += 8;
    Serial.printf("Number of samples: %lf\n", beatgrid->numSamples);
    *all_long = beatgrid->numSamples / baseSampPerWavePoint;
    Serial.printf("Number of all_long samples: %d\n", *all_long);
    
    // Extract beatgrid exists flag
    if (!checkBounds(1)) {
        free(uncompressedBuffer);
        return false;
    }
    beatgrid->hasGrid = uncompressedBuffer[offset];
    offset += 1;
    
    if (!beatgrid->hasGrid) {
        Serial.println("Track has no beatgrid");
        free(uncompressedBuffer);
        return true;
    }
    
    // Skip default beatgrid
    if (!checkBounds(8)) {
        free(uncompressedBuffer);
        return false;
    }
    int64_t defaultMarkerCount;
    memcpy(&defaultMarkerCount, uncompressedBuffer + offset, 8);
    defaultMarkerCount = __builtin_bswap64(defaultMarkerCount);
    offset += 8;
    
    uint32_t defaultMarkersSize = defaultMarkerCount * (8 + 8 + 4 + 4);
    if (!checkBounds(defaultMarkersSize)) {
        free(uncompressedBuffer);
        return false;
    }
    offset += defaultMarkersSize;
    
    // Parse adjusted beatgrid
    if (!checkBounds(8)) {
        free(uncompressedBuffer);
        return false;
    }
    int64_t adjustedMarkerCount;
    memcpy(&adjustedMarkerCount, uncompressedBuffer + offset, 8);
    adjustedMarkerCount = __builtin_bswap64(adjustedMarkerCount);
    offset += 8;
    Serial.printf("Adjusted markers: %lld\n", adjustedMarkerCount);
    
    beatgrid->markerCount = (int)adjustedMarkerCount;
    
    if (beatgrid->markerCount <= 0 || beatgrid->markerCount > 10000) {
        Serial.printf("ERROR: Invalid marker count: %d\n", beatgrid->markerCount);
        free(uncompressedBuffer);
        return false;
    }
    
    uint32_t markersSize = beatgrid->markerCount * (8 + 8 + 4 + 4);
    if (!checkBounds(markersSize)) {
        free(uncompressedBuffer);
        return false;
    }
    
    // Allocate memory for markers
    beatgrid->markers = (BeatMarker *)malloc(beatgrid->markerCount * sizeof(BeatMarker));
    if (!beatgrid->markers) {
        Serial.println("Failed to allocate memory for beat markers");
        free(uncompressedBuffer);
        return false;
    }
    
    // Extract markers
    for (int i = 0; i < beatgrid->markerCount; i++) {
        union { char b[8]; double sampleOffset; } soUnion;
        memcpy(soUnion.b, uncompressedBuffer + offset, 8);
        beatgrid->markers[i].sampleOffset = soUnion.sampleOffset;
        offset += 8;
        
        memcpy(&beatgrid->markers[i].beatIndex, uncompressedBuffer + offset, 8);
        offset += 8;
        
        memcpy(&beatgrid->markers[i].beatsUntilNext, uncompressedBuffer + offset, 4);
        offset += 4;
        
        memcpy(&beatgrid->markers[i].unknown, uncompressedBuffer + offset, 4);
        offset += 4;
    }
    
    beatgrid->samplesPerWaveformPoint = (double)baseSampPerWavePoint;
    
    free(uncompressedBuffer);
    
    Serial.println("Beatgrid extraction completed successfully");
    return true;
}