
#include "dj_screen.h"
#include "globals.h"
#include "Arduino.h"
#include "lv_utils.h"
#include "sqlite3.h"
#include "SD.h"
#include "inflate.h"
#include "file_viewer.h"
#include "T4_PXP.h"
#include <SDRAM_t4.h>
#include "DMAChannel.h"


File playFile;



LV_FONT_DECLARE(exo2_16)
LV_FONT_DECLARE(exo2_18)
LV_FONT_DECLARE(exo2_20)
LV_FONT_DECLARE(exo2_24)
LV_FONT_DECLARE(exo2_28)
LV_FONT_DECLARE(exo2_32)

// Display dimensions
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 480

// Color definitions
#define COLOR_BG LV_COLOR_MAKE(0x1a, 0x1a, 0x1a)
#define COLOR_TEXT_PRIMARY LV_COLOR_MAKE(0xff, 0xff, 0xff)
#define COLOR_TEXT_SECONDARY LV_COLOR_MAKE(0xa0, 0xa0, 0xa0)
#define COLOR_WAVEFORM_HIGH LV_COLOR_MAKE(0x40, 0x80, 0xff)
#define COLOR_WAVEFORM_LOW LV_COLOR_MAKE(0x80, 0xff, 0x40)
#define COLOR_PROGRESS LV_COLOR_MAKE(0x80, 0xff, 0x40)

#define COLOR_GRAY          lv_color_hex(0xB0B0B0)

// Cue button colors (exact DJ controller colors)
static const lv_color_t cue_colors[8] = {
    LV_COLOR_MAKE(0xEA, 0xC5, 0x32), // 1 - EAC532 (Yellow)
    LV_COLOR_MAKE(0xEA, 0x8F, 0x32), // 2 - EA8F32 (Orange)
    LV_COLOR_MAKE(0xB8, 0x55, 0xBF), // 3 - B855BF (Purple)
    LV_COLOR_MAKE(0xBA, 0x2A, 0x41), // 4 - BA2A41 (Red)
    LV_COLOR_MAKE(0x86, 0xC6, 0x4B), // 5 - 86C64B (Light Green)
    LV_COLOR_MAKE(0x20, 0xC6, 0x7C), // 6 - 20C67C (Green)
    LV_COLOR_MAKE(0x00, 0xA8, 0xB1), // 7 - 00A8B1 (Teal)
    LV_COLOR_MAKE(0x15, 0x8E, 0xE2)  // 8 - 158EE2 (Blue)
};

// Main containers
lv_obj_t * main_screen;
static lv_obj_t *top_container;
static lv_obj_t *middle_container;
static lv_obj_t *bottom_container;

// UI elements
static lv_obj_t *title_label;
static lv_obj_t *artist_label;
static lv_obj_t *bpm_label;
static lv_obj_t *key_label;
static lv_obj_t *bar_count_label;
static lv_obj_t *time_label;
static lv_obj_t *current_bpm_label;
static lv_obj_t *tempo_range_label;
static lv_obj_t *adjusted_tempo_label;
static lv_obj_t *progress_bars[4];
static lv_obj_t *daynamic_waveform_canvas;
static lv_obj_t *static_waveform_canvas;
static lv_obj_t *cue_buttons[8];


bool loadDynamicWaveformData(uint16_t track_id);
bool loadOverviewWaveformData(uint16_t track_id);
bool loadBeatgridData(uint16_t track_id);
bool loadCuesData(uint16_t track_id);

void drawFastVLine16Bit(uint16_t x, uint16_t y, uint16_t h, uint16_t color, uint16_t * buffer, uint16_t stride);
void drawFastVLine16BitOverview(uint16_t x, uint16_t y, uint16_t h, uint16_t color, uint16_t * buffer, uint16_t stride);
void drawSlope16Bit(uint16_t * buf, uint8_t p1, uint8_t p2, uint16_t x, uint16_t color, uint8_t opa);
void updateDynamicWaveform(uint32_t waveformOffset);
void updateOverviewWaveform(uint32_t waveformOffset);
bool useOpa = false;


uint64_t overviewSampleCount = 0;
uint64_t highResSampleCount = 0;


const uint16_t col_blue = 0x135D; //From Rezo, was 0x001F;
const uint16_t col_green = 0x15EA; //From Rezo, was 0x07E0;
const uint16_t col_white = 0xF7DE; //From Rezo, was 0xFFFF;

const uint16_t waveformColors[3] = {col_blue, col_green, col_white};
const float waveformUserGain[3] = {1.0, 0.66, 0.33};

EXTMEM uint8_t * dynamicWaveSampleData[6]; // 0=lo samples, 1=med samples, 2=hi samples
DMAMEM uint16_t dynamicCanvasBuffer[800 * 164];
uint64_t dynamicWaveformSampleCount = 0;
double samplesPerDaynamicPoint = 0;

//To store repeating group data of samples for the overview waveform
EXTMEM uint8_t * overViewWaveSampleData[3];
DMAMEM uint16_t overviewCanvasBuffer[800 * 64];
uint64_t overviewWaveformSampleCount = 0;
double samplesPerOverviewPoint = 0;

EXTMEM uint8_t * uncompressedBuffer;
EXTMEM uint8_t * highResBuffer;
EXTMEM uint8_t * overviewBuffer;




FLASHMEM bool loadDynamicWaveformData(uint16_t track_id){
  int32_t fileSize = 0;
  Serial.printf("Starting to open track_id %d \n", track_id);
  
  //Open the p.db file that contains performance Data
  if (sqlite3_open("databases/p.db", &pdb) != SQLITE_OK) {
    Serial.println("Failed to open database");
    return false;
  }

  sqlite3_stmt *stmt = nullptr;
  const char *sqlPerfData = "SELECT highResolutionWaveFormData FROM PerformanceData WHERE trackId = ?";

  if (sqlite3_prepare_v2(pdb, sqlPerfData, -1, &stmt, NULL) != SQLITE_OK) {
    Serial.println("Failed to prepare statement");
    sqlite3_close(pdb);
    return false;
  }

  sqlite3_bind_int(stmt, 1, track_id);
  
  if (sqlite3_step(stmt) != SQLITE_ROW) {
    Serial.println("No data found for track_id");
    sqlite3_finalize(stmt);
    sqlite3_close(pdb);
    return false;
  }

  const void *highResblobData = sqlite3_column_blob(stmt, 0);
  fileSize = sqlite3_column_bytes(stmt, 0);
  
  Serial.printf("Starting inflate %d bytes\n", fileSize);

  highResBuffer = (uint8_t *)malloc(fileSize);
  if (highResBuffer == NULL) {
    Serial.println("Failed to allocate highResBuffer");
    sqlite3_finalize(stmt);
    sqlite3_close(pdb);
    return false;
  }
  
  Serial.println("we have a buffer for dynamic waveform");
  memcpy(highResBuffer, highResblobData, fileSize);
  
  // We're done with SQLite now - clean up immediately
  sqlite3_finalize(stmt);
  sqlite3_close(pdb);
  
  uint32_t uncompressedSize = 0;
  
  // Read uncompressed size (account for endian)
  memcpy(&uncompressedSize, highResBuffer, 4);
  uncompressedSize = __builtin_bswap32(uncompressedSize);
  Serial.printf("File size: %d, Uncompressed size: %u\n", fileSize, uncompressedSize);
  
  //Allocate output buffers
  uncompressedBuffer = (uint8_t *)malloc(uncompressedSize);
  if (uncompressedBuffer == NULL) {
    Serial.println("ERROR: uncompressedBuffer allocation failed!");
    free(highResBuffer);
    return false;
  }

  // Debug: Check all pointers and values before decompression
  Serial.println("=== Pre-decompression Debug ===");
  Serial.printf("highResBuffer address: 0x%08X\n", (uint32_t)highResBuffer);
  Serial.printf("uncompressedBuffer address: 0x%08X\n", (uint32_t)uncompressedBuffer);
  Serial.printf("Input pointer (highResBuffer+4): 0x%08X\n", (uint32_t)(highResBuffer + 4));
  Serial.printf("Input size (fileSize-4): %d\n", fileSize - 4);
  Serial.printf("Output size (uncompressedSize): %u\n", uncompressedSize);

  int64_t zlib_rc = inflate_zlib(
    (const unsigned char *)(highResBuffer + 4), 
    (uint64_t)(fileSize - 4),  // Fixed: subtract 4 bytes for header
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

  //Extract sampleCount
  memcpy(&dynamicWaveformSampleCount, uncompressedBuffer, 8);
  dynamicWaveformSampleCount = __builtin_bswap64(dynamicWaveformSampleCount);
  Serial.printf("sampleCount: %" PRId64 "\n", dynamicWaveformSampleCount);

  //Extract samplesPerWaveformPoint
  union { char b[8]; double numSamplesPerWaveformPoint; };

  Serial.println();
  //Copy bytes for double into union
  for (uint8_t i = 16; i < 24; i++) {
    b[23 - i] = uncompressedBuffer[i];
  }

  //Create data arrays - lo med high samples
  for (int8_t i = 0; i < 6; i++) {
    dynamicWaveSampleData[i] = (uint8_t *)malloc(dynamicWaveformSampleCount);
    if (!dynamicWaveSampleData[i]) {
      Serial.printf("ERROR: Failed to allocate dynamicWaveSampleData[%d]\n", i);
      // Clean up previously allocated arrays
      for (int8_t j = 0; j < i; j++) {
        free(dynamicWaveSampleData[j]);
      }
      free(uncompressedBuffer);
      free(highResBuffer);
      return false;
    }
    Serial.printf("highResBuffer[%d] address: 0x%08X\n", i, (uint32_t)dynamicWaveSampleData[i]);
  }
  
  //Fill 'em up!

  uint32_t index = 0;
  for (uint32_t i = 24; i < (dynamicWaveformSampleCount * 6) + 24; i+= 6) {
      for (uint8_t j = 0; j < 6; j++) {
        if ( j < 3) {
          //Sample data
          dynamicWaveSampleData[j][index] = uncompressedBuffer[i + j]; //Scale the waveform height
          
        } else {
          //Opacity data
          dynamicWaveSampleData[j][index] = uncompressedBuffer[i + j];
        }
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

FLASHMEM bool loadOverviewWaveformData(uint16_t track_id)
{
  uint64_t fileSize = 0;

  Serial.printf("Starting to open track_id %d \n", track_id);
  Serial.println("Squirk§§");
  
  // Open the p.db file that contains performance Data
  if (sqlite3_open("databases/m.db", &mdb) != SQLITE_OK) {
    Serial.println("Failed to open database");
    return false;
  }
  sqlite3_stmt *stmt = nullptr;  // Initialize to nullptr

  const char *sqlOverviewData = "SELECT overviewWaveFormData FROM PerformanceData WHERE trackId = ?";

  if (sqlite3_prepare_v2(mdb, sqlOverviewData, -1, &stmt, NULL) != SQLITE_OK) {
    Serial.println("Failed to prepare statement");
    sqlite3_close(mdb);
    
    return false;
  }
  

  sqlite3_bind_int(stmt, 1, track_id);
  
  
  if (sqlite3_step(stmt) != SQLITE_ROW) {
    Serial.println("No data found for track_id");
    sqlite3_finalize(stmt);
    sqlite3_close(mdb);
    return false;
  }

  const void *OverViewBlobData = sqlite3_column_blob(stmt, 0);
  fileSize = sqlite3_column_bytes(stmt, 0);
  
  // Allocate and copy blob data
  overviewBuffer = (uint8_t *)malloc(fileSize);
  if (!overviewBuffer) {
    Serial.println("Failed to allocate overviewBuffer");
    sqlite3_finalize(stmt);
    sqlite3_close(mdb);
    return false;
  }
  
  memcpy(overviewBuffer, OverViewBlobData, fileSize);
  
  // Done with SQLite - clean up immediately
  sqlite3_finalize(stmt);
  sqlite3_close(mdb);
  
  uint32_t uncompressedSize = 0;
  // Read uncompressed size (account for endian)
  memcpy(&uncompressedSize, overviewBuffer, 4);
  uncompressedSize = __builtin_bswap32(uncompressedSize);

  // Allocate output buffer
  uncompressedBuffer = (uint8_t *)malloc(uncompressedSize);
  if (!uncompressedBuffer) {
    Serial.println("Failed to allocate uncompressedBuffer");
    free(overviewBuffer);
    return false;
  }

  // Decompress
  int64_t zlib_rc = inflate_zlib(
    (const unsigned char *)(overviewBuffer + 4), 
    (uint64_t)(fileSize - 4),  // Fixed: subtract 4 bytes for header
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

  // Extract sampleCount
  memcpy(&overviewSampleCount, uncompressedBuffer, 8);
  overviewSampleCount = __builtin_bswap64(overviewSampleCount);
  Serial.printf("Overview sample count: %" PRId64 "\n", overviewSampleCount);

  // Extract samplesPerWaveformPoint
  union { char b[8]; double numSamplesPerWaveformPoint; };

  // Copy bytes for double into union
  for (uint8_t i = 16; i < 24; i++) {
    b[23 - i] = uncompressedBuffer[i];
  }
  samplesPerOverviewPoint = numSamplesPerWaveformPoint;
  
  // Create data arrays - lo med high samples
  for (int8_t i = 0; i < 3; i++) {
    overViewWaveSampleData[i] = (uint8_t *)malloc(800);  // Fixed: allocate for 800 output samples
    if (!overViewWaveSampleData[i]) {
      Serial.printf("ERROR: Failed to allocate overViewWaveSampleData[%d]\n", i);
      // Clean up previously allocated arrays
      for (int8_t j = 0; j < i; j++) {
        free(overViewWaveSampleData[j]);
      }
      free(uncompressedBuffer);
      free(overviewBuffer);
      return false;
    }
  }

  // Fill 'em up!
  // Scale waveform height from 255 to overviewChartHeight
  float heightRatio = (float)overviewChartHeight / 255.0;
  float scaleFactor = (float)1024 / 800; // 1.28

  uint32_t index = 0;
  for (uint32_t i = 0; i < 800; i++) {
    float srcIndex = i * scaleFactor;
    uint32_t idx = (uint32_t)srcIndex;
    float frac = srcIndex - idx;

    // Calculate offset in uncompressed buffer (skip 24 byte header)
    uint32_t bufferOffset = 24 + (idx * 3);

    for (uint8_t j = 0; j < 3; j++) {
      uint8_t v1 = uncompressedBuffer[bufferOffset + j];
      
      // Bounds checking for interpolation
      uint8_t v2;
      if (idx + 1 < 1024) {  // Source has 1024 samples
        v2 = uncompressedBuffer[bufferOffset + 3 + j];  // Next sample
      } else {
        v2 = v1; // Use same value if at boundary
      }

      float interpolatedValue = v1 + (frac * (v2 - v1));
      
      // Add proper rounding for integer conversion
      float finalValue = interpolatedValue * heightRatio * waveformUserGain[j];
      overViewWaveSampleData[j][i] = (uint8_t)(finalValue + 0.5f); // Round to nearest
      Serial.printf("ov[%d][%d] = %d\n", j, i, overViewWaveSampleData[j][i]);
    }
  }

  // Free temporary buffers
  free(uncompressedBuffer);
  free(overviewBuffer);
  
  Serial.println("Overview waveform data loaded");
  return true;
}


GlobalBeatLUT globalBeats = {0};

bool loadBeatgridData(int track_id, Beatgrid *beatgrid) {
    // Clear the beatgrid structure
    memset(beatgrid, 0, sizeof(Beatgrid));
    
    // Open database
    if (sqlite3_open("databases/m.db", &mdb) != SQLITE_OK) {
        Serial.println("Failed to open beatgrid database");
        return false;
    }
    
    sqlite3_stmt *stmt = nullptr;
    const char *sqlBeatData = "SELECT beatData FROM PerformanceData WHERE trackId = ?";
    
    if (sqlite3_prepare_v2(mdb, sqlBeatData, -1, &stmt, NULL) != SQLITE_OK) {
        Serial.println("Failed to prepare beatgrid query");
        sqlite3_close(mdb);
        return false;
    }
    
    sqlite3_bind_int(stmt, 1, track_id);
    
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        Serial.println("No beatgrid data found");
        sqlite3_finalize(stmt);
        sqlite3_close(mdb);
        return false;
    }

    const void *beatBlobData = sqlite3_column_blob(stmt, 0);
    int beatFileSize = sqlite3_column_bytes(stmt, 0);
    
    if (beatFileSize == 0) {
        Serial.println("Empty beatgrid data");
        sqlite3_finalize(stmt);
        sqlite3_close(mdb);
        return false;
    }
    
    Serial.printf("Beatgrid blob size: %d bytes\n", beatFileSize);

    // Copy blob data to buffer
    uint8_t *beatBuffer = (uint8_t *)malloc(beatFileSize);
    if (!beatBuffer) {
        Serial.println("Failed to allocate beatgrid buffer");
        sqlite3_finalize(stmt);
        sqlite3_close(mdb);
        return false;
    }
    
    memcpy(beatBuffer, beatBlobData, beatFileSize);
    
    // Done with SQLite - clean up immediately
    sqlite3_finalize(stmt);
    sqlite3_close(mdb);

    // Extract uncompressed size (first 4 bytes, big-endian)
    uint32_t beatUncompressedSize;
    memcpy(&beatUncompressedSize, beatBuffer, 4);
    beatUncompressedSize = __builtin_bswap32(beatUncompressedSize);
    Serial.printf("Beatgrid uncompressed size: %u\n", beatUncompressedSize);

    // Allocate buffer for uncompressed data
    uint8_t *uncompressedBuffer = (uint8_t *)malloc(beatUncompressedSize);
    if (!uncompressedBuffer) {
        Serial.println("Failed to allocate uncompressed beatgrid buffer");
        free(beatBuffer);
        return false;
    }

    // Decompress beatgrid data
    Serial.println("Decompressing beatgrid data...");
    uint64_t startMicros = micros();
    
    int64_t inflate_rc = inflate_zlib(
        (const unsigned char *)(beatBuffer + 4), 
        (uint64_t)(beatFileSize - 4), 
        (unsigned char *)uncompressedBuffer, 
        (uint64_t)beatUncompressedSize
    );
    
    Serial.printf("Beatgrid decompression time: %lluuS\n", micros() - startMicros);
    Serial.printf("Beatgrid inflate return code: %lld\n", inflate_rc);
    
    // Free compressed buffer - no longer needed
    free(beatBuffer);
    
    if (inflate_rc < 0) {
        Serial.printf("Beatgrid decompression failed with error code: %lld\n", inflate_rc);
        free(uncompressedBuffer);
        return false;
    }

    // Parse beatgrid data according to spec
    uint32_t offset = 0;
 
    // Bounds check helper
    auto checkBounds = [&](uint32_t bytesNeeded) -> bool {
        if (offset + bytesNeeded > beatUncompressedSize) {
            Serial.printf("ERROR: Buffer overrun at offset %u (need %u bytes, have %u)\n",
                         offset, bytesNeeded, beatUncompressedSize);
            return false;
        }
        return true;
    };

    // Extract sample rate (double big-endian)
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
    Serial.printf("Sample rate: %lf\n", beatgrid->sampleRate);

    // Extract number of samples (double big-endian)
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
    all_long = beatgrid->numSamples/420;
    Serial.printf("Number of all_long samples: %d\n", all_long);

    // Extract beatgrid exists flag
    if (!checkBounds(1)) {
        free(uncompressedBuffer);
        return false;
    }
    beatgrid->hasGrid = uncompressedBuffer[offset];
    offset += 1;
    Serial.printf("Has beatgrid: %d\n", beatgrid->hasGrid);

    if (!beatgrid->hasGrid) {
        Serial.println("Track has no beatgrid");
        free(uncompressedBuffer);
        return true; // Not an error, just no beatgrid
    }

    // Parse default beatgrid first
    Serial.println("Parsing default beatgrid...");
    
    // Extract number of markers (int64 big-endian)
    if (!checkBounds(8)) {
        free(uncompressedBuffer);
        return false;
    }
    int64_t defaultMarkerCount;
    memcpy(&defaultMarkerCount, uncompressedBuffer + offset, 8);
    defaultMarkerCount = __builtin_bswap64(defaultMarkerCount);
    offset += 8;
    Serial.printf("Default markers: %lld\n", defaultMarkerCount);

    // Skip default beatgrid markers (we'll use adjusted beatgrid)
    uint32_t defaultMarkersSize = defaultMarkerCount * (8 + 8 + 4 + 4); // double + int64 + uint32 + uint32
    if (!checkBounds(defaultMarkersSize)) {
        free(uncompressedBuffer);
        return false;
    }
    offset += defaultMarkersSize;

    // Parse adjusted beatgrid
    Serial.println("Parsing adjusted beatgrid...");
    
    // Extract number of markers (int64 big-endian)
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
    
    if (beatgrid->markerCount <= 0) {
        Serial.println("No valid beatgrid markers found");
        free(uncompressedBuffer);
        return false;
    }

    // Sanity check on marker count
    if (beatgrid->markerCount > 10000) {
        Serial.printf("ERROR: Unrealistic marker count: %d\n", beatgrid->markerCount);
        free(uncompressedBuffer);
        return false;
    }

    // Check if we have enough data for all markers
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

    // Extract markers (all little-endian)
    for (int i = 0; i < beatgrid->markerCount; i++) {
        // Sample offset (double little-endian)
        union { char b[8]; double sampleOffset; } soUnion;
        memcpy(soUnion.b, uncompressedBuffer + offset, 8);
        beatgrid->markers[i].sampleOffset = soUnion.sampleOffset;
        offset += 8;

        // Beat index (int64 little-endian) 
        memcpy(&beatgrid->markers[i].beatIndex, uncompressedBuffer + offset, 8);
        offset += 8;

        // Beats until next marker (uint32 little-endian)
        memcpy(&beatgrid->markers[i].beatsUntilNext, uncompressedBuffer + offset, 4);
        offset += 4;

        // Unknown constant (uint32 little-endian)
        memcpy(&beatgrid->markers[i].unknown, uncompressedBuffer + offset, 4);
        offset += 4;

        if (i < 5) {  // Only print first 5 markers to avoid spam
            Serial.printf("Marker %d: Sample=%lf, Beat=%lld, BeatsUntil=%u\n", 
                         i, beatgrid->markers[i].sampleOffset, 
                         beatgrid->markers[i].beatIndex, 
                         beatgrid->markers[i].beatsUntilNext);
        }
    }

    // Calculate BPM
    if (beatgrid->markerCount >= 2) {
        double firstSample = beatgrid->markers[0].sampleOffset;
        double lastSample = beatgrid->markers[beatgrid->markerCount - 1].sampleOffset;
        int64_t firstBeat = beatgrid->markers[0].beatIndex;
        int64_t lastBeat = beatgrid->markers[beatgrid->markerCount - 1].beatIndex;
        
        if (lastSample != firstSample) {  // Avoid division by zero
            double bpm = beatgrid->sampleRate * 60.0 * (double)(lastBeat - firstBeat) / (lastSample - firstSample);
            Serial.printf("Calculated BPM: %lf\n", bpm);
        }
    }

    // Set samples per waveform point (use the same value from waveform data)
    beatgrid->samplesPerWaveformPoint = 420; // From your waveform extraction

    free(uncompressedBuffer);
    
    Serial.println("Beatgrid extraction completed successfully");
    return true;
}

FLASHMEM void drawOverviewCanvas()
{
  //Clear overview canvas
  memset(overviewCanvasBuffer, 0, 800*64*2);

  for (uint16_t x = 0; x < chartWidth; x++) {
    for (uint8_t i = 0; i < 3; i++) {
      drawFastVLine16BitOverview(x, overviewChartHeight - (overViewWaveSampleData[i][x]), (overViewWaveSampleData[i][x]), waveformColors[i], overviewCanvasBuffer, chartWidth);
      Serial.printf("drawing x=%d, height=%d\n", x, (overViewWaveSampleData[i][x]));
    }
  }
  //Invalidate canvas, as this is updated done rarely
  lv_obj_invalidate(static_waveform_canvas);
 
}

void create_top_container(Track * track) {
    // Main top container
    top_container = lv_obj_create(main_screen);
    lv_obj_set_size(top_container, SCREEN_WIDTH, 160);
    lv_obj_set_pos(top_container, 0, 0);
    lv_obj_set_style_bg_color(top_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(top_container, 0, 0);
    lv_obj_set_style_pad_all(top_container, 0, 0);
    lv_obj_set_style_radius(top_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(top_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title and BPM/Key container
    //LV_COLOR_MAKE(0x53, 0x53, 0x53)
    lv_obj_t *title_bpm_container = lv_obj_create(top_container);
    lv_obj_set_size(title_bpm_container, SCREEN_WIDTH, 70);
    lv_obj_set_pos(title_bpm_container, 0, 0);
    //lv_obj_set_style_bg_opa(title_bpm_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(title_bpm_container, LV_COLOR_MAKE(0x53, 0x53, 0x53), 0);
    lv_obj_set_style_border_width(title_bpm_container, 0, 0);
    lv_obj_set_style_pad_all(title_bpm_container, 0, 0);
     lv_obj_set_style_radius(title_bpm_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(title_bpm_container, LV_OBJ_FLAG_SCROLLABLE);

    // Title label (left side)
    title_label = lv_label_create(title_bpm_container);
    lv_label_set_text(title_label, track->title);
    lv_obj_set_style_text_color(title_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title_label, &exo2_20, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 5);
    lv_obj_clear_flag(title_label, LV_OBJ_FLAG_SCROLLABLE);
    

    // BPM label (right side)
    bpm_label = lv_label_create(title_bpm_container);
    lv_label_set_text_fmt(bpm_label,"%.1f", track->bpmAnalyzed);
    lv_obj_set_style_text_color(bpm_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(bpm_label, &exo2_32, 0);
    lv_obj_align(bpm_label, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_clear_flag(bpm_label, LV_OBJ_FLAG_SCROLLABLE);
   

    // Key label (below BPM)
    key_label = lv_label_create(title_bpm_container);
    lv_label_set_text(key_label, (char*)getKey(atoi(track->musical_key)));
    lv_obj_set_style_text_font(key_label, &exo2_20, 0);
    lv_obj_align_to(key_label, bpm_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    int key_numeric = atoi(track->musical_key); 
    lv_obj_set_style_text_color(key_label, getKeyColor(key_numeric), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(key_label, LV_OBJ_FLAG_SCROLLABLE);


    lv_obj_t *progress_line = lv_obj_create(title_bpm_container);
    lv_obj_set_size(progress_line, 700, 3);
    lv_obj_set_pos(progress_line, 0, 67);
    lv_obj_set_style_bg_color(progress_line, lv_color_hex(0x158EE2), 0);
    lv_obj_set_style_border_width(progress_line, 0, 0);
    lv_obj_clear_flag(progress_line, LV_OBJ_FLAG_SCROLLABLE);
    
    // Artist label (below title)
    artist_label = lv_label_create(top_container);
    lv_label_set_text(artist_label, track->artist);
    lv_obj_set_style_text_color(artist_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(artist_label, &exo2_20, 0);
    lv_obj_set_pos(artist_label, 0, 40);
    lv_obj_clear_flag(artist_label, LV_OBJ_FLAG_SCROLLABLE);


    // Bottom info container (bars, time, BPM info)
    lv_obj_t *info_container = lv_obj_create(top_container);
    lv_obj_set_size(info_container, LV_PCT(100), 60);
    lv_obj_set_pos(info_container, 0, 80);
    lv_obj_set_style_bg_opa(info_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(info_container, 0, 0);
    lv_obj_set_style_pad_all(info_container, 0, 0);
    lv_obj_clear_flag(info_container, LV_OBJ_FLAG_SCROLLABLE);


    // Progress bars (left section)
    
    for (int i = 0; i < 4; i++) {
        progress_bars[i] = lv_obj_create(info_container);
        lv_obj_set_size(progress_bars[i], 60, 16);
        lv_obj_set_pos(progress_bars[i], i * 65, 15);
        lv_obj_set_style_radius(progress_bars[i], 0, LV_PART_MAIN);
        lv_obj_clear_flag(progress_bars[i], LV_OBJ_FLAG_SCROLLABLE);
        
        if (i < 3) {
            lv_obj_set_style_bg_color(progress_bars[i], COLOR_TEXT_SECONDARY, 0);
        } else {
            lv_obj_set_style_bg_color(progress_bars[i], COLOR_PROGRESS, 0);
        }
        lv_obj_set_style_border_width(progress_bars[i], 0, 0);
    }

    // Bar count label
    bar_count_label = lv_label_create(info_container);
    lv_label_set_text(bar_count_label, "94 || 0.34");
    lv_obj_set_style_text_color(bar_count_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(bar_count_label, &exo2_18, 0);
    lv_obj_set_pos(bar_count_label, 0, 35);
    lv_obj_clear_flag(bar_count_label, LV_OBJ_FLAG_SCROLLABLE);

    // Time label (center)
    time_label = lv_label_create(info_container);
    lv_label_set_text(time_label, "03:00.4");
    lv_obj_set_style_text_color(time_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(time_label, &exo2_28, 0);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(time_label, LV_OBJ_FLAG_SCROLLABLE);
   

    // BPM info (right section)
    current_bpm_label = lv_label_create(info_container);
    lv_label_set_text(current_bpm_label, "125");
    lv_obj_set_style_text_color(current_bpm_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(current_bpm_label, &exo2_24, 0);
    lv_obj_align(current_bpm_label, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_clear_flag(current_bpm_label, LV_OBJ_FLAG_SCROLLABLE);

    // Additional tempo info labels can be added here
}

void create_middle_container(void) {
    middle_container = lv_obj_create(main_screen);
    lv_obj_set_size(middle_container, SCREEN_WIDTH, 164);
    lv_obj_set_pos(middle_container, 0, 164);
    lv_obj_set_style_bg_color(middle_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(middle_container, 0, 0);
    //lv_obj_set_style_pad_all(middle_container, 10, 0);
    lv_obj_set_style_border_color(middle_container, lv_color_white(), 0);
    lv_obj_set_style_radius(middle_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(middle_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(middle_container, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
    

    // Dynamic waveform canvas
    daynamic_waveform_canvas = lv_canvas_create(middle_container);
    lv_obj_set_size(daynamic_waveform_canvas, SCREEN_WIDTH, 164);
    lv_obj_center(daynamic_waveform_canvas);
    lv_obj_clear_flag(daynamic_waveform_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(daynamic_waveform_canvas, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);

    // Create canvas buffer (16-bit RGB565)
    lv_canvas_set_buffer(daynamic_waveform_canvas, dynamicCanvasBuffer, 800, 164, LV_IMG_CF_TRUE_COLOR);
}

void create_bottom_container(void) {
    bottom_container = lv_obj_create(main_screen);
    lv_obj_set_size(bottom_container, SCREEN_WIDTH, 158);
    lv_obj_set_pos(bottom_container, 0, 322);
    lv_obj_set_style_bg_color(bottom_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(bottom_container, 0, 0);
    lv_obj_set_style_pad_all(bottom_container, 0, 0);
    lv_obj_set_style_border_width(bottom_container, 0, 0);
    lv_obj_set_style_pad_all(bottom_container, 0, 0);
    lv_obj_set_style_pad_row(bottom_container, 0, 0);
    lv_obj_set_style_pad_column(bottom_container, 0, 0);
    lv_obj_set_style_pad_gap(bottom_container, 0, 0);
    lv_obj_clear_flag(bottom_container, LV_OBJ_FLAG_SCROLLABLE);

    // Static waveform container
    lv_obj_t *static_wave_container = lv_obj_create(bottom_container);
    lv_obj_set_size(static_wave_container, SCREEN_WIDTH, 64);
    lv_obj_set_pos(static_wave_container, 0, 0);
    lv_obj_set_style_bg_color(static_wave_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(static_wave_container, 1, 0);
    lv_obj_set_style_border_color(static_wave_container, lv_color_white(), 0);
    lv_obj_set_style_radius(static_wave_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(static_wave_container, LV_OBJ_FLAG_SCROLLABLE);

    // Static waveform canvas
    static_waveform_canvas = lv_canvas_create(static_wave_container);
    lv_canvas_set_buffer(static_waveform_canvas, (lv_color_t *)overviewCanvasBuffer, 800, 64, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(static_waveform_canvas, SCREEN_WIDTH, 64);
    lv_obj_center(static_waveform_canvas);
    lv_obj_clear_flag(static_waveform_canvas, LV_OBJ_FLAG_SCROLLABLE);
    //lv_obj_clear_flag(static_waveform_canvas, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
    
    
    //lv_canvas_fill_bg(static_waveform_canvas, COLOR_BG, LV_OPA_COVER);
    drawOverviewCanvas();
    //lv_obj_invalidate(static_waveform_canvas);
    
   


    // Cue buttons container
    /*
    lv_obj_t *cue_container = lv_obj_create(bottom_container);
    lv_obj_set_size(cue_container, SCREEN_WIDTH, 94);
    lv_obj_set_pos(cue_container, 0, 90);
    lv_obj_set_style_bg_opa(cue_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cue_container, 0, 0);
    lv_obj_set_style_pad_all(cue_container, 0, 0);
    lv_obj_clear_flag(cue_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create 8 cue buttons (A-H)
    for (int i = 0; i < 8; i++) {
        cue_buttons[i] = lv_btn_create(cue_container);
        lv_obj_set_size(cue_buttons[i], 80, 50);
        lv_obj_set_pos(cue_buttons[i], i * 100 + 10, 5);
        lv_obj_set_style_bg_color(cue_buttons[i], cue_colors[i], 0);
        lv_obj_set_style_border_width(cue_buttons[i], 0, 0);
        lv_obj_set_style_radius(cue_buttons[i], 8, 0);
        lv_obj_clear_flag(cue_buttons[i], LV_OBJ_FLAG_SCROLLABLE);

        // Button label
        lv_obj_t *btn_label = lv_label_create(cue_buttons[i]);
        char btn_text[2] = {'A' + i, '\0'};
        lv_label_set_text(btn_label, btn_text);
        lv_obj_set_style_text_color(btn_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(btn_label, &exo2_20, 0);
        lv_obj_center(btn_label);
    }
        */
}


void dj_ui_init(Track * track) {
    // Create main screen
    main_screen = lv_obj_create(NULL);
    lv_obj_set_size(main_screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(main_screen, COLOR_BG, 0);
    lv_obj_set_style_border_width(main_screen, 0, 0);
    lv_obj_set_style_pad_all(main_screen, 0, 0);
    lv_obj_set_style_pad_row(main_screen, 0, 0);
    lv_obj_set_style_pad_column(main_screen, 0, 0);
    lv_obj_set_style_pad_gap(main_screen, 0, 0);
    lv_obj_set_style_radius(main_screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    
    //sqlite3_open("databases/p.db", &pdb);
    loadDynamicWaveformData(track->track_id);
    loadOverviewWaveformData(track->track_id);

    Beatgrid * beatgrid = (Beatgrid*)malloc(sizeof(Beatgrid));
    loadBeatgridData(track->track_id, beatgrid);

    // Create all containers
    create_top_container(track);
    create_middle_container();
    create_bottom_container();

    //PXP_overlay_buffer((uint16_t*)dynamicCanvasBuffer, 2, SCREEN_WIDTH, 164);
    //PXP_overlay_position(0, 158, 799, 321);

      

    playFile = SD.open("mixxx-export/86 - raise_your_hands.wav", FILE_READ);
    if (!playFile) {
      Serial.printf("Trying to open: %s\n", track->path);
      Serial.println("Failed to open audio file");
    
    }
    else{
      Serial.println("Audio file opened");
      is_playing = true;
      I2S1_TCSR |= 1<<8;
      updateDynamicWaveform(0); 
    }

    

}

// Update functions for dynamic content

void update_track_info(const char *title, const char *artist, int bpm, const char *key) {
    lv_label_set_text(title_label, title);
    lv_label_set_text(artist_label, artist);
    lv_label_set_text_fmt(bpm_label, "%d", "128");
    lv_label_set_text(key_label, key);
}

void update_time_info(const char *time, int bars, float beat_pos) {
    lv_label_set_text(time_label, time);
    lv_label_set_text_fmt(bar_count_label, "%d || %.2f", bars, beat_pos);
}

void update_progress_bars(int active_bar) {
    for (int i = 0; i < 4; i++) {
        if (i <= active_bar) {
            lv_obj_set_style_bg_color(progress_bars[i], COLOR_PROGRESS, 0);
        } else {
            lv_obj_set_style_bg_color(progress_bars[i], COLOR_TEXT_SECONDARY, 0);
        }
    }
}

FASTRUN void drawFastVLine16Bit(uint16_t x, uint16_t y, uint16_t h, uint16_t color, uint16_t * buffer, uint16_t stride)
{
  if (h <= 0) return;
  if(y+h >= chartHeight ) return;
    uint16_t *p = buffer + y * stride + x;
    for (int i = 0; i < h; ++i)
    {
        *p = color;
        p += stride; // move one row down
    }
}

FASTRUN void drawFastVLine16BitOverview(uint16_t x, uint16_t y, uint16_t h, uint16_t color, uint16_t * buffer, uint16_t stride)
{
  if (h <= 0) return;
  //if(y+h >= 64 ) return;
    uint16_t *p = buffer + y * stride + x;
    for (int i = 0; i < h; ++i)
    {
        *p = color;
        p += stride; // move one row down
    }
}

FASTRUN uint16_t fastBlend( uint32_t fg, uint32_t bg, uint8_t opa)
{
    //Dont blend if canvas background, or opa is max
    if ((bg == 0x0000) || (fg == 0x0000) || (opa == 0xFF)) {
      return fg;
    }

    opa = ( opa + 4 ) >> 3;
    bg = (bg | (bg << 16)) & 0b00000111111000001111100000011111;
    fg = (fg | (fg << 16)) & 0b00000111111000001111100000011111;
    uint32_t result = ((((fg - bg) * opa) >> 5) + bg) & 0b00000111111000001111100000011111;
    return (uint16_t)((result >> 16) | result);
}

FASTRUN void drawSlope16Bit(uint16_t * buf, uint8_t p1, uint8_t p2, uint16_t x, uint16_t color, uint8_t opa)
{
  if (opa > 0) {
    //Calculate increment, simple lerp between two points
    float delta = (p2 - p1) / slopePoints;

    for (uint16_t i = 0; i < slopePoints; i++) {
      uint8_t height = p1 + (i * delta);
      drawFastVLine16Bit((x * slopePoints) + i, (chartHeight - height) / 2, height, color, buf, chartWidth);
    }
  }
}


int DynamicWaveformZOOM =1;
FASTRUN void updateDynamicWaveform(uint32_t waveformOffset)
{
  //Clear canvas
  int offset   = 800 * 82; // start of row 82
  memset(dynamicCanvasBuffer, 0, chartWidth * chartHeight * 2);
  
  uint32_t pos = (waveformOffset/420) / DynamicWaveformZOOM; 
  /*If zoom is bigger than 1, need to find the highest value in between each jump*/
  //Serial.printf("Waveform offset: %d, pos: %d sample count: %d \n", waveformOffset, pos, sampleCount);

  
  //Draw waveforms - expanded, interpolated
  for (uint16_t x = 0; x < 800; x++) {
    int64_t index = DynamicWaveformZOOM * (x + pos- (chartWidth/2));
    if (index < 0 || index >= all_long) continue;
    for (uint8_t i = 0; i < 6; i++) {
        uint8_t sampleValue = (uint8_t)(dynamicWaveSampleData[i][index]);
        if(i < 3) { 
            drawFastVLine16Bit(x, ((164 -sampleValue) /2), sampleValue, waveformColors[i], dynamicCanvasBuffer, chartWidth);
        }
        //if (dynamicWaveSampleData[i][index] > 164) Serial.printf("Sample value too high: %d at index %d\n", dynamicWaveSampleData[i][index], i);
    }
  }
  memset((dynamicCanvasBuffer + offset), 0xFFFF, 1600);
  //Draw mid-canvas line
  drawFastVLine16Bit(chartWidth / 2, 0, chartHeight - 1, col_white, dynamicCanvasBuffer, chartWidth);
  //lv_obj_invalidate(daynamic_waveform_canvas);
  //memcpy(lcdBuffer1+(800*158),dynamicCanvasBuffer,(800*164*2));
  //arm_dcache_flush_delete((uint16_t*)dynamicCanvasBuffer, chartWidth * chartHeight * 2);
  //PXP_process();
  memcpy(lcdBuffer1 + (800 * 76 * 2), dynamicCanvasBuffer, (800 * 164 * 2));
}

// Add these as global/static variables
static uint16_t oldX = 0xFFFF; // Initialize to invalid position
// New function to update position efficiently
FASTRUN void updatePlaybackPosition(uint16_t newX)
{
  if(oldX == newX) return; // No change, skip update
  // Restore the old position by redrawing the waveform data
  if (oldX < chartWidth && oldX + 1 < chartWidth) {
    // Clear the old indicator position (both pixels)
    for (uint8_t px = 0; px < 2; px++) {
      uint16_t xPos = oldX + px;
      drawFastVLine16BitOverview(xPos, 0, overviewChartHeight, 0x0000, 
                                 overviewCanvasBuffer, chartWidth);
      // Redraw the waveform at old position
      for (uint8_t i = 0; i < 3; i++) {
        uint16_t height = overViewWaveSampleData[i][xPos];
        uint16_t startY = overviewChartHeight - height;
        
        drawFastVLine16BitOverview(xPos, startY, height, waveformColors[i], 
                                   overviewCanvasBuffer, chartWidth);
      }
    }
    
    // Invalidate old area
    lv_area_t area;
    area.x1 = newX;
    area.y1 = 0;
    area.x2 = newX + 1;
    area.y2 = overviewChartHeight - 1;
    lv_obj_invalidate_area(static_waveform_canvas, &area);
  }
  
  // Draw the new position indicator (2px wide)
  if (newX < chartWidth && newX + 1 < chartWidth) {
    uint16_t indicatorColor = 0xFFFF; // White, or use your preferred indicator color
    
    for (uint8_t px = 0; px < 2; px++) {
      uint16_t xPos = newX + px;
      
      // Draw full-height indicator line
      drawFastVLine16BitOverview(xPos, 0, overviewChartHeight, indicatorColor, 
                                 overviewCanvasBuffer, chartWidth);
    }
    
    // Invalidate new area
    lv_area_t area;
    area.x1 = oldX;
    area.y1 = 0;
    area.x2 = oldX + 1;
    area.y2 = overviewChartHeight - 1;
    lv_obj_invalidate_area(static_waveform_canvas, &area);
  }
  
  // Update stored position
  oldX = newX;
}
