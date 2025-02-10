#include <Arduino.h>
#include <lvgl.h>
#include "teensy41SQLite.hpp"
#include <SD.h>
#include "globals.h"
#include "include/file_viewer.h"
#include "RemoteDisplay.h"
#include <SDRAM_t4.h>
#include "inflate.h"

RemoteDisplay remoteDisplay;

//SdFat sd;
FsFile perfDB;
FsFile metaDB;

SDRAM_t4 sdram;

//const char* dbName = "Engine Library/Database2/p.db";
#define SCREENWIDTH 800
#define SCREENHEIGHT 480

EXTMEM uint16_t lcdBuffer1[SCREEN_WIDTH * 480] __attribute__((aligned(64)));
EXTMEM uint16_t lcdBuffer2[SCREEN_WIDTH * 480] __attribute__((aligned(64)));
//lv_display_t * disp;


void my_disp_flush(lv_display_t *display, const lv_area_t *area, uint8_t * px_map);
void touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data);


void refreshDisplayCallback()
{
  lv_area_t area;
  area.x1 = 0; area.y1 = 0; area.x2 = SCREENWIDTH; area.y2 = SCREENHEIGHT;
  lv_obj_invalidate_area(lv_scr_act(), &area);
}


FASTRUN void my_disp_flush(lv_display_t *display, const lv_area_t *area, uint8_t * px_map)
{
  if (remoteDisplay.sendRemoteScreen == true ) {
    remoteDisplay.sendData(area->x1, area->y1, area->x2, area->y2, (uint8_t *)px_map);
  }
    lv_display_flush_ready(display);
    
}

FASTRUN void touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
  //Handle touch from remote (overrides)
  if (remoteDisplay.sendRemoteScreen == true) {
      data->point.x = remoteDisplay.lastRemoteTouchX;
      data->point.y = remoteDisplay.lastRemoteTouchY;
      data->state = remoteDisplay.lastRemoteTouchState == RemoteDisplay::PRESSED ? LV_INDEV_STATE_PRESSED: LV_INDEV_STATE_RELEASED;
  }
}



void errorLogCallback(void *pArg, int iErrCode, const char *zMsg)
{
  Serial.printf("(%d) %s\n", iErrCode, zMsg);
}

void checkSQLiteError(sqlite3* in_db, int in_rc)
{
  if (in_rc == SQLITE_OK)
  {
    Serial.println(">>>> testSQLite - operation - success <<<<");
  }
  else
  {
    int ext_rc = sqlite3_extended_errcode(in_db);
    Serial.print(ext_rc);
    Serial.print(": ");
    Serial.println(sqlite3_errstr(ext_rc));
  }
}

void readWaveFormBlob(){
  sqlite3* db;
  Serial.println("---- testSQLite - sqlite3_open - begin ----");
  int rc = sqlite3_open("Engine Library/Database2/p.db", &db);
  checkSQLiteError(db, rc);
  Serial.println("---- testSQLite - sqlite3_open - end ----");
  
  if (rc == SQLITE_OK)
  {
    
    Serial.println("---- testSQLite - sqlite3_prepare_v2 - begin ----");
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT highResolutionWaveFormData FROM PerformanceData WHERE id = 1", -1, &stmt, 0);
    checkSQLiteError(db, rc);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      const void *blobData = sqlite3_column_blob(stmt, 0);
      int blobSize = sqlite3_column_bytes(stmt, 0);

      if (blobData && blobSize > 0) {
          // Allocate memory to hold the BLOB data
          uint8_t *buffer = (uint8_t *)malloc(blobSize);
          if (buffer) {
              memcpy(buffer, blobData, blobSize);
              Serial.printf("BLOB data: %d Bytes copied to buffer.\n ", blobSize);

            // START ZLIB
              uint32_t uncompressedSize = 0;
              uint8_t * uncompressedBuffer;
              //Read uncompressed size (account for endian)
              memcpy(&uncompressedSize, buffer, 4);
              uncompressedSize = __builtin_bswap32(uncompressedSize);
              Serial.printf("File size: %ld, Uncompressed size: %ld\n", blobSize, uncompressedSize);

              //Allocate output buffers
              uncompressedBuffer = (uint8_t *)malloc(uncompressedSize); // Add 8 bytes due to waveform_turbo requirements

          
              //Do it
              Serial.printf("Starting to Inflate.\n");
              uint64_t startMicros = micros();

              uint64_t zlib_rc = inflate_zlib((const unsigned char *)buffer+4, (uint64_t)blobSize, (unsigned char *)uncompressedBuffer, (uint64_t)uncompressedSize);
              
              Serial.printf("Time: %lduS\n", micros()-startMicros);
              Serial.printf("Return code from inflate: %ld\n", zlib_rc);

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
              int64_t sampleCount=0;
              memcpy(&sampleCount, uncompressedBuffer, 8);
              sampleCount = __builtin_bswap64(sampleCount);
              Serial.printf("sampleCount: %" PRId64 "\n", sampleCount);
              free(buffer);

            // END ZLIB


      
      } else {
              Serial.println("Error: Memory allocation failed.");
          }
      } else {
          Serial.println("No data or empty BLOB.");
      }

      sqlite3_finalize(stmt);
    } else {
      Serial.println("Query failed or no row found.");
      checkSQLiteError(db, rc);
    }

  }

}



void setup()
{
  //setupSerial(115200);
  //delaySetup(3);

  Serial.begin (115200);
  delay(1000);
  if(CrashReport){
    Serial.print(CrashReport);
  }

  if (!sdram.begin(32, 132,0)){
    Serial.println("SDRAM init fail :( ...");
  }

  if (!SD.begin(BUILTIN_SDCARD))
  {
    Serial.println("SD.begin() failed! - Halting!");
    while (true) { delay(1000); }
  }

  //REMDISP_init();
  //REMDISP_register_callbacks();
  memset(lcdBuffer1, 3333, sizeof(lcdBuffer1));
  remoteDisplay.init(800 , 480);
  remoteDisplay.registerRefreshCallback(refreshDisplayCallback);
  
  lv_init();
  lv_display_t * disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, lcdBuffer1, NULL, SCREEN_WIDTH * 480 * 2, LV_DISPLAY_RENDER_MODE_DIRECT);

  lv_indev_t * ts_indev;
  ts_indev = lv_indev_create();
  lv_indev_set_type(ts_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(ts_indev, touch_read_cb);

  lv_tick_set_cb(millis);

  T41SQLite::getInstance().setLogCallback(errorLogCallback);
  int resultBegin = T41SQLite::getInstance().begin(&SD);

  if (resultBegin == SQLITE_OK)
  {
    Serial.println("T41SQLite::getInstance().begin() succeded!");

    //readWaveFormBlob();
      createListScreen();
    /*
    int resultEnd = T41SQLite::getInstance().end();

    if (resultEnd == SQLITE_OK)
    {
      Serial.println("T41SQLite::getInstance().end() succeded!");
    }
    else
    {
      Serial.print("T41SQLite::getInstance().end() failed! result code: ");
      Serial.println(resultEnd);
    }
      */
  }
  else
  {
    Serial.println("T41SQLite::getInstance().begin() failed!");
  }
}

unsigned long previousMillis0 = 0; 
void loop()
{
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis0 >= 3) {
    // Save the last time lv_timer_handler was called
    previousMillis0 = currentMillis;

    // Call lv_timer_handler
    lv_timer_handler();
    remoteDisplay.pollRemoteCommand();  
  }
  
}
