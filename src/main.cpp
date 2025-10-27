#include <Arduino.h>
#include <lvgl.h>
#include "teensy41SQLite.hpp"
#include <SD.h>
#include "globals.h"
#include "stats/app_stats.h"
#include "file_viewer.h"
#include "dj_screen.h"
#include <SDRAM_t4.h>
#include "inflate.h"
#include "utils/changeSDSpeed.h"
#include "lv_utils.h"

#if defined(USE_LCD_DISP)
#include "eLCDIF_t4.h"
#include <Adafruit_FT6206.h>
#include "T4_PXP.h"
#endif

#if defined(USE_REM_DISP)
#include <RemoteDisplay.h>
#endif

#if defined(RDI_DEVELOPMENTS_REV3)
#include "battery/battery.h"
#include <SdFat.h>
#define BACKLIGHT_PIN 24
SdFs sd_io2;
#else
#define BACKLIGHT_PIN A0
#endif


#include "i2s_sync.h"
#if defined(USE_BEAT_NUMBERS)
#include "utils/digit_renderer.h"
#endif

// Forward declarations
extern "C" void startup_middle_hook(void);
void SAI_IRQHandler(void);
void copyWaveformsToLCD();
void advancePosition_rezo();
void advancePosition_claude();
void advancePosition_claude_optimized();

#if defined(USE_REM_DISP)
RemoteDisplay remoteDisplay;
#endif  

//#define USE_PXP

//For stats
AppStats appStats = AppStats();

//SdFat sd;
FsFile perfDB;
FsFile metaDB;

SDRAM_t4 sdram;
eLCDIF_t4 lcd;
i2s_sync audio;

uint32_t play_count = 0;


// Used in the I2S ISR
volatile uint16_t pitch = 10000;                          // 10000 = 100% step 0,01%            
volatile uint32_t position = 0;
volatile uint8_t reverse = 0;
volatile uint8_t end_of_track = 0;                        //end track flag
volatile uint32_t step_position = 0;
volatile uint32_t sdram_adr = 0;
volatile uint8_t offset_adress = 0;                       //address offset for calling CUE audio data (for work)
volatile int16_t LR[2][4] __attribute__((aligned(32)));
volatile uint16_t PCM_2[2] __attribute__((aligned(32)));
volatile uint8_t SAMPLE[4] __attribute__((aligned(4))) = {0,0,0,0};

// Used in I2S ISR and loop 
volatile uint32_t play_adr = 0;                           //Playing adress in samples (44100 per second)
volatile uint32_t baseSampPerWavePoint = 420;             //Number of samples per wavepoint in dynamic waveform. Updated from the database later
volatile uint32_t all_long = 0;                           //all long of Track in 0.5*frames   150 on 1 sec

// I dont think we mark big ass arrays as volatile?
// TODO why is this 205 and not 128?
EXTMEM_NOCACHE_PCM uint16_t PCM[205][8192][2] __attribute__((aligned(32)));

// Make volatile as critical to buffer management
volatile uint16_t start_adr_valid_data = 0;               //filling adress in memory
volatile uint16_t end_adr_valid_data = 0;                 //filling adress in memory ()
volatile uint8_t filling_step = 0;

// Others
bool is_playing = false;
uint32_t slip_play_adr = 0;                      //Playing adress for SLIP MODE in samples (44100 per second)
uint8_t mem_offset_adress = 0;                   //address offset for calling CUE audio data (for memory)
int32_t even1, even2, odd1, odd2;
float COEF[8] = {            //////optimal 2x
  0.45868970870461956,
  0.04131401926395584,
  0.48068024766578432,
  0.17577925564495955,
  -0.246185007019907091, 
  0.24614027139700284,
  -0.36030925263849456,
  0.10174985775982505
};

 uint8_t play_enable = 0;
 uint8_t slip_play_enable = 0;
 uint32_t slip_position = 0;
 uint16_t pitch_for_slip = 10000;         // 10000 = 100% step 0,01%    
 float SAMPLE_BUFFER;
 float T;
 uint8_t QUANTIZE = 1;                    //QUANTIZE ENABLE
 uint8_t loop_active = 0;                 //loop flag
 uint32_t LOOP_OUT = 0;                   //adr LOOP OUT in frames 150
 uint8_t lock_control = 1;            


 // For static buffer indicator
 bool staticBufferReady = false;
 uint16_t oldStaticBufferX = 0;
 uint16_t newStaticBufferX = 0;
 uint16_t staticIndicatorBuffer[2 * overviewChartHeight];
 uint16_t * staticDestPtr = NULL;



/*
uint32_t height;
  uint32_t vfp; // vertical front porch
  uint32_t vsw; // vertical sync width
  uint32_t vbp; // vertical back porch
  uint32_t width;
  uint32_t hfp; // horizontal front porch
  uint32_t hsw; // horizontal sync width
  uint32_t hbp; // horizontal back porch
  // clk_num * 24MHz / clk_den = pixel clock
  uint32_t clk_num; // pix_clk numerator
  uint32_t clk_den; // pix_clk denominator
  uint32_t vpolarity; // 0 (active low vsync/negative) or LCDIF_VDCTRL0_VSYNC_POL (active high/positive)
  uint32_t hpolarity; // 0 (active low hsync/negative) or LCDIF_VDCTRL0_HSYNC_POL (active high/positive)
  uint32_t pclkpolarity; // 0 (data valid on falling edge/negative) or LCDIF_VDCTRL0_DOTCLK_POL (data valid on rising edge/positive)ss
*/
#if defined(USE_LCD_DISP)
#if defined(RDI_DEVELOPMENTS_REV3)
eLCDIF_t4_config lcd_config = {480, 8, 4, 4, 800, 8, 4, 4, 25, 24, 0, 0};
#else
eLCDIF_t4_config lcd_config = {480, 16, 4, 16, 800, 8, 4, 8, 30, 24, 1, 1};
#endif // RDI_DEVELOPMENTS_REV3
#endif // USE_LCD_DISP

//const char* dbName = "Engine Library/Database2/p.db";

EXTMEM_NOCACHE uint16_t lcdBuffer[LCD_BUFFER_COUNT][SCREEN_WIDTH * SCREEN_HEIGHT] __attribute__((aligned(64)));
//EXTMEM uint16_t tempDisplayBuf[SCREEN_WIDTH * SCREEN_HEIGHT] __attribute__((aligned(64)));
//lv_display_t * disp;

#ifdef USE_PXP

EXTMEM_NOCACHE uint16_t lvglBuffer1[SCREEN_WIDTH * (SCREEN_HEIGHT)] __attribute__((aligned(64)));
EXTMEM_NOCACHE uint16_t lvglBuffer2[SCREEN_WIDTH * (SCREEN_HEIGHT)] __attribute__((aligned(64)));

#endif

void startup_middle_hook(void)
{
#if defined(USE_EXTMEM_NOCACHE)  
  //Disable caching for first 4M of SDRAM
	SCB_MPU_RBAR = 0x80000000 | (SCB_MPU_RBAR_REGION(11) | SCB_MPU_RBAR_VALID); // 0x80000000 | REGION(11);
	SCB_MPU_RASR = SCB_MPU_RASR_TEX(1) | SCB_MPU_RASR_AP(3) | SCB_MPU_RASR_XN | (SCB_MPU_RASR_SIZE(21) | SCB_MPU_RASR_ENABLE); //MEM_NOCACHE | READWRITE | NOEXEC | SIZE_4M;

  // Region 12: Next 8MB nocache (0x80400000 - 0x80BFFFFF) for PCM array
  SCB_MPU_RBAR = 0x80400000 | (SCB_MPU_RBAR_REGION(12) | SCB_MPU_RBAR_VALID);
  SCB_MPU_RASR = SCB_MPU_RASR_TEX(1) | SCB_MPU_RASR_AP(3) | SCB_MPU_RASR_XN | (SCB_MPU_RASR_SIZE(22) | SCB_MPU_RASR_ENABLE); //MEM_NOCACHE | READWRITE | NOEXEC | SIZE_8M;

#endif

  // Start SDRAM, 166/198 MHz for pussies, 221 Mhz for real men
  if (!sdram.begin(32, SDRAM_SPEED, 1)){
    Serial.printf("SDRAM init at %ldMHz failed\n", SDRAM_SPEED);
  }
}

#if defined(USE_REM_DISP)
void refreshDisplayCallback()
{
  lv_area_t area;
  area.x1 = 0; area.y1 = 0; area.x2 = SCREEN_WIDTH; area.y2 = SCREEN_HEIGHT;
  lv_obj_invalidate_area(lv_scr_act(), &area);
}

#if (LVGL_VERSION_MAJOR == 8)
FASTRUN void my_disp_flush(lv_disp_drv_t *display, const lv_area_t *area, lv_color_t * px_map)
{
  if (remoteDisplay.sendRemoteScreen == true ) {
    //remoteDisplay.sendData(area->x1, area->y1, area->x2, area->y2, (uint8_t *)px_map);
  }
  lv_disp_flush_ready(display);
}
#endif
#if (LVGL_VERSION_MAJOR == 9)
FASTRUN void my_disp_flush(lv_display_t *display, const lv_area_t *area, uint8_t * px_map)
{
  if (remoteDisplay.sendRemoteScreen == true ) {
    remoteDisplay.sendData(area->x1, area->y1, area->x2, area->y2, (uint8_t *)px_map);
  }
  lv_disp_flush_ready(display);
}
#endif

#endif // USE_REM_DISP

#if defined(USE_LCD_DISP) || defined(USE_REM_DISP)
//Display driver
#if (LVGL_VERSION_MAJOR == 8)
static lv_disp_draw_buf_t disp_buf;
static lv_disp_drv_t disp_drv;          /*A variable to hold the drivers. Must be static or global.*/
lv_color_t * next_px_map;
#endif
#if (LVGL_VERSION_MAJOR == 9)
lv_display_t * disp_drv;
uint8_t * next_px_map;
#endif
volatile bool ps_framePending = false;
volatile bool lvgl_framePending = false;
#ifdef USE_PXP
FASTRUN void my_disp_flush(lv_disp_drv_t *display, const lv_area_t *area, lv_color_t * px_map){
  if (lv_disp_flush_is_last(&disp_drv)){
    
        ps_framePending = true;
        // Set up PXP input: the small LVGL buffer portion
        PXP_input_buffer((uint8_t*)px_map, 2, SCREEN_WIDTH, SCREEN_HEIGHT);
        //PXP_input_position(0, 0, SCREEN_WIDTH-1, SCREEN_HEIGHT-1);

      // Start the transfer
        PXP_process();
    }
    else{
        lv_disp_flush_ready(&disp_drv);
    }
}


FASTRUN void pxpCallback(){
  if(ps_framePending){
        // Calculate updated area size for cache flush
        lv_disp_flush_ready(&disp_drv);
        ps_framePending = false;
    }
}

#else
#if (LVGL_VERSION_MAJOR == 8) && !defined(USE_REM_DISP)
FASTRUN void my_disp_flush(lv_disp_drv_t *display, const lv_area_t *area, lv_color_t * px_map)
{
  if (lv_disp_flush_is_last(&disp_drv)){
#if !defined(USE_EXTMEM_NOCACHE)    
    arm_dcache_flush_delete((uint16_t*)px_map, 800*480*2);
#endif    
    ps_framePending = true;
    next_px_map = px_map;
  }
  else{
    lv_disp_flush_ready(&disp_drv);
  }
}
#endif
#if (LVGL_VERSION_MAJOR == 9) && !defined(USE_REM_DISP)
FASTRUN void my_disp_flush(lv_display_t *display, const lv_area_t *area, uint8_t * px_map)
{
  if (lv_disp_flush_is_last(disp_drv)){
#if !defined(USE_EXTMEM_NOCACHE)    
    arm_dcache_flush_delete((uint16_t*)px_map, 800*480*2);
#endif    
    ps_framePending = true;
    next_px_map = px_map;
  }
  else{
    lv_disp_flush_ready(disp_drv);
  }
}
#endif

FASTRUN void lcdCallback() {
  CrashReport.breadcrumb(3, 1);

  appStats.start(ISR_LCD);
  
  lvgl_framePending = true;
  if(ps_framePending == true) {
    lcd.setNextBufferAddress((uint16_t*)next_px_map);
#if (LVGL_VERSION_MAJOR == 8)    
    lv_disp_flush_ready(&disp_drv);
#endif
#if (LVGL_VERSION_MAJOR == 9)
    lv_disp_flush_ready(disp_drv);
#endif    
    ps_framePending = false;
  }
  appStats.end(ISR_LCD);
  CrashReport.breadcrumb(3, 0);
}
#endif

#if LV_USE_LOG != 0
#if (LVGL_VERSION_MAJOR == 9)
void my_print( lv_log_level_t level, const char * buf )
{
    LV_UNUSED(level);
    Serial.println(buf);
    Serial.flush();
}
#else
void my_print(const char * buf)
{
    Serial.println(buf);
    Serial.flush();
}
#endif
#endif


lv_indev_t * ts_indev;

// Touch controller instance
Adafruit_FT6206 ctp = Adafruit_FT6206();

#if (LVGL_VERSION_MAJOR == 8)
void touch_read_cb(lv_indev_drv_t * drv, lv_indev_data_t*data)
#endif
#if (LVGL_VERSION_MAJOR == 9)
void touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
#endif
{
    // Check if there's a new touch event from interrupt
    TS_Point p = ctp.getPoint();
    if (ctp.touched()) {
        // Touch detected - map coordinates to 800x480 screen
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = p.x;
        data->point.y = p.y;
    } else {
        // Touch released
        data->state = LV_INDEV_STATE_RELEASED;
    }
#if defined(USE_REM_DISP)    
    if (remoteDisplay.sendRemoteScreen == true) {
        //Handle touch from remote (overrides)
        data->point.x = remoteDisplay.lastRemoteTouchX;
        data->point.y = remoteDisplay.lastRemoteTouchY;
        data->state = remoteDisplay.lastRemoteTouchState == RemoteDisplay::PRESSED ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }
#endif // USE_REM_DISP   
}
#endif

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

LV_FONT_DECLARE(exo2_16)
LV_FONT_DECLARE(exo2_18)

FLASHMEM void reportAppConfig() {
  Serial.println("\n======================== App Settings ==========================");
  Serial.printf("COMPILED: " SER_CYAN "%s %s" SER_RESET " with GCC " SER_CYAN "%d.%d.%d" SER_RESET ", C++ vers: " SER_CYAN "%ld" SER_RESET "\n", __DATE__, __TIME__, __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__, __cplusplus);
  Serial.printf("F_BUS_ACTUAL: %s%ld" SER_RESET "MHz   VOLTAGE: " SER_CYAN "%ld" SER_RESET "mV   SDRAM_SPEED: " SER_CYAN "%d" SER_RESET "MHz\n", F_CPU_ACTUAL == 528'000'000 ? SER_CYAN : SER_RED,F_CPU_ACTUAL / 1000000, get_voltage_mv(), SDRAM_SPEED);  
  Serial.printf("SD_CARD_SPEED: " SER_CYAN "%ld" SER_RESET "KHz\n", SD_CARD_SPEED);
  Serial.printf("USE_EXTMEM_NOCACHE: %s%s" SER_RESET "\n", MACRO_EXISTS(USE_EXTMEM_NOCACHE) ? SER_CYAN : SER_RED, MACRO_EXISTS(USE_EXTMEM_NOCACHE) ? "TRUE" : "FALSE");
  Serial.printf("LCD_BUFFER_COUNT: " SER_CYAN "%d" SER_RESET "\n", LCD_BUFFER_COUNT);
  Serial.printf("LVGL: " SER_CYAN "%d.%d.%d" SER_RESET "\n", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
  Serial.printf("DISPLAY: USE_LCD_DISP: %s%s" SER_RESET "  USE_REM_DISP: %s%s" SER_RESET "\n", MACRO_EXISTS(USE_LCD_DISP) ? SER_CYAN : SER_RED, MACRO_EXISTS(USE_LCD_DISP) ? "TRUE" : "FALSE",
      MACRO_EXISTS(USE_REM_DISP) ? SER_RED : SER_CYAN, MACRO_EXISTS(USE_REM_DISP) ? "TRUE" : "FALSE");
  Serial.printf("USE_STATS: %s%s" SER_RESET "\n",  MACRO_EXISTS(USE_STATS) ? SER_YELLOW : SER_GREEN, MACRO_EXISTS(USE_STATS) ? "TRUE" : "FALSE");
  Serial.printf("IRQ_GEN: %s%s" SER_RESET "\n", MACRO_EXISTS(IRQ_FROM_INT_TIMER) ? SER_RED : SER_CYAN, MACRO_EXISTS(IRQ_FROM_INT_TIMER) ? "IntervalTimer" : "I2S");
  // buffer count, LVGL version
  Serial.println("======================== App Settings ==========================\n");
}

FLASHMEM void errorHalt(const char* message)
{
  Serial.printf("CRITICAL ERROR: %s - halting execution\n", message);
  while (true) {
    delay(1000);
  }
}

void setup()
{
#if defined(USE_LCD_DISP)
  // Turn off backlight
  pinMode(BACKLIGHT_PIN, OUTPUT);
  analogWriteFrequency(BACKLIGHT_PIN, 200);
  analogWrite(BACKLIGHT_PIN, 0);
#endif  

#if defined(RDI_DEVELOPMENTS_REV3)
  Battery.init();
#endif

  // Initialize Serial
  Serial.begin (115200);
  while (!Serial) {};
  delay(1000);
  if(CrashReport){
    Serial.print(CrashReport);
  }

  // Report on configuration
  reportAppConfig();

  // Start SDcard
#if defined(RDI_DEVELOPMENTS_REV3)
  bool sdCardResult = sd_io2.begin(SdioConfig(FIFO_SDIO | USE_SDIO2));
#else
  bool sdCardResult = SD.begin(BUILTIN_SDCARD);
#endif

  if (sdCardResult == true) {
    setSDCardClock(SD_CARD_SPEED, MACRO_EXISTS(RDI_DEVELOPMENTS_REV3));
  } else {
    errorHalt("sd.begin() failed");
  }

  // Start SQLite
  T41SQLite::getInstance().setLogCallback(errorLogCallback);

#if defined(RDI_DEVELOPMENTS_REV3)
  int resultBegin = T41SQLite::getInstance().begin(&sd_io2);
#else
  int resultBegin = T41SQLite::getInstance().begin(&SD);
#endif

  if (resultBegin == SQLITE_OK) {
    Serial.println("T41SQLite::getInstance().begin() succeded!");
  } else {
    errorHalt("T41SQLite::getInstance().begin failed");
  }

  // Init buffers
  memset(PCM, 0, sizeof(PCM));
  memset(lcdBuffer, 3333, SCREEN_WIDTH * SCREEN_HEIGHT * LCD_BUFFER_COUNT * 2);
  memset(staticIndicatorBuffer, 0xFF, overviewChartHeight * 2 * 2); // White is easy - if marker color hi/li bytes differ, use a loop to fill color

#ifdef USE_REM_DISP
  remoteDisplay.init(SCREEN_WIDTH , SCREEN_HEIGHT);
  remoteDisplay.registerRefreshCallback(refreshDisplayCallback);
#endif

#if defined(USE_LCD_DISP)

  // Init touch screen
  if (ctp.begin(20)) {
    Serial.println("FT5316 touch controller initialized");
  }

  // Init LCD, PXP
#if defined(RDI_DEVELOPMENTS_REV3)
  lcd.begin(lcd_config, BUS_16BIT, WORD_16BIT, PIXEL_16BIT);
#else
  lcd.begin(BUS_16BIT, WORD_16BIT, lcd_config);
#endif
  #ifndef USE_PXP
  lcd.onCompleteCallback(lcdCallback);
  #endif
  lcd.setCurrentBufferAddress(lcdBuffer[LCD_BUFFER_COUNT - 1]);
  lcd.setNextBufferAddress(lcdBuffer[0]);

  #ifdef USE_PXP
  PXP_init();
  PXP_input_format(PXP_RGB565, 0, 0, 0);
  PXP_overlay_format(PXP_RGB565, 0, 0, 0);
  PXP_output_format(PXP_RGB565, 0, 0, 0);
  PXP_output_buffer((uint16_t*)lcdBuffer1, 2, SCREEN_WIDTH, SCREEN_HEIGHT);
  PXP_callback(pxpCallback);
  //PXP_enable_repeat(true);
  #endif

#if !defined(RDI_DEVELOPMENTS_REV3)
  IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_05 = 5; // Set mux to GPIO mode
  GPIO6_GDIR |= (1 << 21); // Set as output
  GPIO6_DR_SET = (1 << 21);
  Serial.println("LCD ON");
#endif

#endif // USE_LCD_DISP

  lv_init();
  #if (LVGL_VERSION_MAJOR == 8)
  lv_disp_drv_init(&disp_drv);            /*Basic initialization*/
  disp_drv.draw_buf = &disp_buf;          /*Set an initialized buffer*/
  disp_drv.flush_cb = my_disp_flush;      /*Set a flush callback to draw to the display*/
  disp_drv.hor_res = SCREEN_WIDTH;        /*Set the horizontal resolution in pixels*/
  disp_drv.ver_res = SCREEN_HEIGHT;       /*Set the vertical resolution in pixels*/
  disp_drv.direct_mode = 1;
  disp_drv.full_refresh = 0;
  

    
  #ifdef USE_PXP
  lv_disp_draw_buf_init(&disp_buf, lvglBuffer1, lvglBuffer2, 800*480);  
  #else
  lv_disp_draw_buf_init(&disp_buf, (void *)lcdBuffer[0], LCD_BUFFER_COUNT == 1 ? NULL : (void *)lcdBuffer[1], SCREEN_WIDTH * SCREEN_HEIGHT);    
  #endif
  lv_disp_drv_register(&disp_drv); /*Register the driver and save the created display objects*/

  static lv_indev_drv_t indev_drv;
    lv_indev_drv_init( &indev_drv );
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    //indev_drv.read_cb = touch_glass_input;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register( &indev_drv );
#endif    
#if (LVGL_VERSION_MAJOR == 9)
  disp_drv = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_display_set_color_format(disp_drv, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp_drv, lcdBuffer[0], LCD_BUFFER_COUNT == 2 ? lcdBuffer[1] : NULL, SCREEN_WIDTH * SCREEN_HEIGHT * 2, LV_DISPLAY_RENDER_MODE_DIRECT);

  //Tick callback
  lv_tick_set_cb(millis);

  //Flush callback
  lv_display_set_flush_cb(disp_drv, my_disp_flush);

  ts_indev = lv_indev_create();
  lv_indev_set_type(ts_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(ts_indev, touch_read_cb);
#endif

#if LV_USE_LOG != 0
  lv_log_register_print_cb(my_print);
#endif

#if defined(USE_BEAT_NUMBERS)
  // Create in memory rendered
  prerender_digit_buffers(&exo2_16, lv_color_white(), lv_color_black());
#endif  

  /*
  lv_obj_t * btn = lv_btn_create(lv_scr_act());
  lv_obj_set_size(btn, 120, 50);
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t * label = lv_label_create(btn);
  lv_label_set_text(label, "Play");
  lv_obj_set_style_text_font(label, &exo2_18, 0);
  lv_obj_center(label);
  */

  //readWaveFormBlob();
  createListScreen();
  lv_scr_load_anim(filesScreen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
  //dj_ui_init();
    
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
  
  audio.begin(&SAI_IRQHandler);

#if defined(USE_LCD_DISP)
  // Setup complete, turn on LCD
  analogWrite(BACKLIGHT_PIN, 200);
  lcd.runLCD(); // Turn on the LCDIF when the 1st frame is ready to be displayed
#endif  // USE_LCD_DISP
}

uint32_t bytes_read = 0;

FASTRUN void playFileSeek(uint64_t pos) 
{
  appStats.start(PLAYFILE_SEEK);

  CrashReport.breadcrumb(1, 1);
  playFile.seek(pos);
  CrashReport.breadcrumb(1, 0);

  appStats.end(PLAYFILE_SEEK);
}

FASTRUN int playFileRead(void *buf, size_t count)
{
  appStats.start(PLAYFILE_READ);

    uint32_t bufAddr = (uint32_t)buf;
  if (bufAddr < 0x80000000 || bufAddr > 0x81400000) {
    Serial.printf("INVALID BUFFER ADDRESS: 0x%08X\n", bufAddr);
    Serial.flush();
  }

  int32_t bytes_read = 0;
  CrashReport.breadcrumb(1, 2);
  //noInterrupts();
  bytes_read = playFile.read(buf, count);
  if (bytes_read != count) {
    Serial.printf(SER_RED "File read mismatch: count: %ld, bytes_read: %ld" SER_RESET "\n", count, bytes_read);
  }
  //interrupts();
  CrashReport.breadcrumb(1, 0);  

  appStats.end(PLAYFILE_READ);
  appStats.addByteCount(PLAYFILE_READ, bytes_read);
  return bytes_read;
}

FASTRUN void drawVerticalStrip(uint16_t* srcBuffer, uint16_t xPos, uint16_t height, uint16_t* destBuffer, uint16_t srcWidth, uint16_t destWidth)
{
  for (uint16_t y = 0; y < height; y++) {
    uint16_t destOffset = (y * destWidth) + xPos;
    uint16_t srcOffset = y * srcWidth;  
    destBuffer[destOffset] = srcBuffer[srcOffset];
    destBuffer[destOffset + 1] = srcBuffer[srcOffset + 1];
  }
}

FASTRUN void copyWaveformsToLCD()
{
  CrashReport.breadcrumb(4, 1);
  
  if (dynamicBufferReady == true) {

    CrashReport.breadcrumb(4, 2);
    
    appStats.start(DYNAMIC_MEMCPY);
        
    // Calculate destination pointer
    uint32_t offset = (SCREEN_WIDTH * middleContainerPos * 2);
    uint32_t baseAddr = LCDIF_NEXT_BUF;
    uint32_t destAddr = baseAddr + offset;
    uint8_t *destPtr = (uint8_t *)destAddr;
    
    // Validate the destination address before using it
    bool isValid = true;
    
    // Check if address is in valid EXTMEM range (your lcdBuffer array)
    if (destAddr < 0x80000000 || destAddr > 0x81400000) {
      Serial.printf(SER_RED "ERROR: Invalid dynamic destPtr=0x%08X, base=0x%08X, offset=%lu, middlePos=%d" SER_RESET "\n", 
                    destAddr, baseAddr, offset, middleContainerPos);
      isValid = false;
    }
    
    // Check if it would overflow the LCD buffer
    uint32_t copySize = (chartWidth * chartHeight * 2);
    if (destAddr + copySize > 0x81400000) {
      Serial.printf(SER_RED "ERROR: Dynamic copy would overflow: dest=0x%08X, size=%lu" SER_RESET "\n", 
                    destAddr, copySize);
      isValid = false;
    }
    
    // Check for the specific crash address
    if (destAddr == 0x20300000) {
      Serial.printf(SER_RED "CRITICAL: Calculated exact crash address 0x20300000!" SER_RESET "\n");
      Serial.printf("  LCDIF_NEXT_BUF=0x%08X, middleContainerPos=%d, SCREEN_WIDTH=%d\n", 
                    baseAddr, middleContainerPos, SCREEN_WIDTH);
      isValid = false;
    }
    
    // Only perform copy if address is valid
    if (isValid) {
      CrashReport.breadcrumb(4, 4); // Mark actual memcpy
      memcpy(destPtr, dynamicCanvasBuffer, copySize);
      CrashReport.breadcrumb(4, 2); // Back to dynamic section
      
      // As this isn't updated per frame, it needs to be done in all LCD buffers in use
      if (LCD_BUFFER_COUNT == 2) {
        destPtr = (uint8_t *)(LCDIF_CUR_BUF + offset);
        destAddr = (uint32_t)destPtr;
        
        // Validate second buffer too
        if (destAddr >= 0x80000000 && destAddr <= 0x81400000 && 
            destAddr + copySize <= 0x81400000) {
          memcpy(destPtr, dynamicCanvasBuffer, copySize);
        } else {
          Serial.printf(SER_RED "ERROR: Invalid dynamic destPtr (buf2)=0x%08X" SER_RESET "\n", destAddr);
        }
      }
      
      dynamicBufferReady = false;

    } else {
      // Skip copy, clear flag to prevent repeated errors
      dynamicBufferReady = false;
      Serial.println(SER_RED "Skipped dynamic waveform copy due to invalid address" SER_RESET);
    }

    // Finish stats
    appStats.end(DYNAMIC_MEMCPY);
    appStats.addByteCount(DYNAMIC_MEMCPY, copySize); 
    
    CrashReport.breadcrumb(4, 1); // Back to main function
  }

  if (staticBufferReady == true) {
    CrashReport.breadcrumb(4, 3);
    
    // Start time for stats
    appStats.start(OVERVIEW_COPY);
    
    // Calculate destination pointer for static waveform
    uint32_t offset = (SCREEN_WIDTH * (bottomContainerPos + 1));
    uint32_t destAddr = (uint32_t)(lcdBuffer[0] + offset);
    uint16_t *staticDestPtr = (uint16_t *)destAddr;
    
    // Validate the destination address
    bool isValid = true;
    
    // Check if address is in valid EXTMEM range
    if (destAddr < 0x80000000 || destAddr > 0x81400000) {
      Serial.printf(SER_RED "ERROR: Invalid static destPtr=0x%08X, offset=%lu, bottomPos=%d" SER_RESET "\n", 
                    destAddr, offset, bottomContainerPos);
      isValid = false;
    }
    
    // Check for the specific crash address
    if (destAddr == 0x20300000 || (destAddr >= 0x20200000 && destAddr <= 0x20400000)) {
      Serial.printf(SER_RED "CRITICAL: Static pointer in crash range 0x%08X!" SER_RESET "\n", destAddr);
      Serial.printf("  lcdBuffer[0]=0x%08X, bottomContainerPos=%d, SCREEN_WIDTH=%d\n", 
                    (uint32_t)lcdBuffer[0], bottomContainerPos, SCREEN_WIDTH);
      isValid = false;
    }
    
    // Validate array indices
    if (oldStaticBufferX >= chartWidth || newStaticBufferX >= chartWidth) {
      Serial.printf(SER_RED "ERROR: Invalid static buffer X positions: old=%d, new=%d, chartWidth=%d" SER_RESET "\n",
                    oldStaticBufferX, newStaticBufferX, chartWidth);
      isValid = false;
    }
    
    if (isValid) {
      CrashReport.breadcrumb(4, 5); // Mark actual drawing
      
      // Erase old marker by copying from pristine canvas buffer into eLCDIF buffer
      drawVerticalStrip(overviewCanvasBuffer + oldStaticBufferX, oldStaticBufferX, 
                       overviewChartHeight - 1, staticDestPtr, chartWidth, chartWidth);
      
      // Draw new marker
      drawVerticalStrip(staticIndicatorBuffer, newStaticBufferX, 
                       overviewChartHeight - 1, staticDestPtr, 2, chartWidth);
      
      CrashReport.breadcrumb(4, 3); // Back to static section
      
      // As this isn't updated per frame, it needs to be done in all LCD buffers in use
      if (LCD_BUFFER_COUNT == 2) {
        staticDestPtr = (uint16_t *)(lcdBuffer[1] + offset);
        destAddr = (uint32_t)staticDestPtr;
        
        // Validate second buffer too
        if (destAddr >= 0x80000000 && destAddr <= 0x81400000) {
          drawVerticalStrip(overviewCanvasBuffer + oldStaticBufferX, oldStaticBufferX, 
                           overviewChartHeight - 1, staticDestPtr, chartWidth, chartWidth);
          drawVerticalStrip(staticIndicatorBuffer, newStaticBufferX, 
                           overviewChartHeight - 1, staticDestPtr, 2, chartWidth);
        } else {
          Serial.printf(SER_RED "ERROR: Invalid static destPtr (buf2)=0x%08X" SER_RESET "\n", destAddr);
        }
      }

      staticBufferReady = false;
    
      // Finish stats
      appStats.end(OVERVIEW_COPY);
    } else {
      // Skip copy, clear flag
      staticBufferReady = false;
      Serial.println(SER_RED "Skipped static waveform copy due to invalid address" SER_RESET);
    }
    
    CrashReport.breadcrumb(4, 1); // Back to main function
  }
  
  CrashReport.breadcrumb(4, 0); // Clear breadcrumb
}

FASTRUN void loop()
{
  // Take snapshot of play_adr, so we dont have issues as the ISR updates it. Intent is to use it atomicly anyway
  uint32_t snapshot_play_adr = play_adr;
  
  // Stats
  if (appStats.readyToReport() == true) {
    appStats.report();
  }

  appStats.start(MAIN_LOOP);

  if (lvgl_framePending == true) {
      appStats.start(LV_TIMER_HANDLER);
      lv_timer_handler(); 
      appStats.end(LV_TIMER_HANDLER);

#ifdef USE_REM_DISP
      remoteDisplay.pollRemoteCommand();
#endif

      if (is_playing == true) {
        copyWaveformsToLCD();
      }
      
      lvgl_framePending = false;
  }

  if (is_playing == true) {
    if (end_of_track == 0) {
      static uint32_t play_adr_temp = 0;

      if ((play_adr_temp / baseSampPerWavePoint) != (snapshot_play_adr / baseSampPerWavePoint)) {
        //Serial.printf("Play adr: %lu\n", snapshot_play_adr);
        updateDynamicWaveform(snapshot_play_adr);
        updatePlaybackPosition_new((snapshot_play_adr / baseSampPerWavePoint) * (chartWidth - 1)/all_long);
        play_adr_temp = snapshot_play_adr; 
      }
      
      if(end_adr_valid_data<128) {
        bytes_read = playFileRead(PCM[end_adr_valid_data][0], 32768);
        //Serial.printf("Start filling buffers: end_adr_valid_data: %d wav file bytes read: %d \n",end_adr_valid_data, bytes_read);
        end_adr_valid_data++;
          
      } else if((end_adr_valid_data<((snapshot_play_adr>>13)+42)) && (filling_step==0 || filling_step==6)) {
        
        //filling the buffer forward
        if(filling_step==6) {
          playFileSeek((32768*end_adr_valid_data)+44);
          filling_step = 0;	
        }

        bytes_read = playFileRead(PCM[end_adr_valid_data&0x7F][0], 32768);
        //Serial.printf("Filling buffer forward: end_adr_valid_data: %d wav file bytes read: %d \n",end_adr_valid_data, bytes_read);	
        //Serial.printf("all_long %d snapshot_play_adr %d \n", all_long, snapshot_play_adr);		
        //DrawCueMarker(1+((end_adr_valid_data*11145)/all_long));
        end_adr_valid_data++;
        if ((end_adr_valid_data-start_adr_valid_data)>128) {
          start_adr_valid_data = end_adr_valid_data-128;	
        }
      } else if(((end_adr_valid_data>((snapshot_play_adr>>13)+86) || ((end_adr_valid_data-start_adr_valid_data)<124)) && start_adr_valid_data>3) || (filling_step!=0 && filling_step!=6)) {					//filling the buffer back
        Serial.println("filling buffers backwards");		
        if(filling_step == 0 || filling_step == 6) {
          if((end_adr_valid_data - start_adr_valid_data) > 127) {
            end_adr_valid_data = start_adr_valid_data + 124;	
          }	
          start_adr_valid_data -= 4;	
          playFileSeek((32768 * start_adr_valid_data) + 44);
          filling_step = 1;	
        } else if (filling_step >= 1 && filling_step <= 4) {
          playFileRead(PCM[(start_adr_valid_data + filling_step - 1) & 0x7F][0], 32768);
          filling_step++;
        } else if(filling_step == 5) {
          //DrawCueMarker(1+((start_adr_valid_data*11145)/all_long));	
          filling_step = 6;		
        }
      }
    } else {
      audio.stopI2SInterrupt();
      play_count += 1;
      Serial.printf("END OF TRACK, plays: %ld\n", play_count);
      // Restart
      play_adr = 0;
      sdram_adr = 0;
      position = 0;
      reverse = 0;
      end_of_track = 0;
      step_position = 0;
      start_adr_valid_data = 0;
      end_adr_valid_data = 0;
      filling_step = 0;
      playFile.seek(44);
      audio.startI2SInterrupt();
    } 
  }
  appStats.end(MAIN_LOOP);
}

FASTRUN void SAI_IRQHandler(void)
{
  if (play_adr > (baseSampPerWavePoint * all_long)) {
    Serial.printf("ERROR: play_adr %lu exceeds track length %lu\n", 
                  play_adr, baseSampPerWavePoint * all_long);
}
  CrashReport.breadcrumb(2, 1);
  appStats.start(ISR_I2S);

#if !defined(IRQ_FROM_INT_TIMER)
  //I2S_TCSR_REG &= ~I2S_TCSR_FRIE;  // Disable interrupt temporarily

  uint16_t left = (SAMPLE[1] << 8) | SAMPLE[0];
  uint16_t right = (SAMPLE[3] << 8) | SAMPLE[2];

  __DSB();  // completes when all explicit memory accesses before this instruction complete
  
  I2S_TDR0_REG = (uint32_t)left << 16;
  I2S_TDR0_REG = (uint32_t)right << 16;
#endif
  
  advancePosition_claude_optimized();
  
  I2S_TCSR_REG |= 0x00040000;     // Clear error flag
  //I2S_TCSR_REG |= I2S_TCSR_FRIE;  // Re-enable interrupt
  appStats.end(ISR_I2S);
  CrashReport.breadcrumb(2, 0);
}

FASTRUN void advancePosition_rezo() {
  float c0, c1, c2, c3, r0, r1, r2, r3;
  uint8_t step_position;
  uint32_t sdram_adr;
  
  position+= pitch;
	
	if (position>9999) {
    step_position = position/10000;				
    if (reverse==0 && end_of_track==0) {			
      play_adr+= step_position;	
      if (step_position==1) {
        LR[0][0] = LR[0][1];
        LR[1][0] = LR[1][1];
        LR[0][1] = LR[0][2];
        LR[1][1] = LR[1][2];
        LR[0][2] = LR[0][3];
        LR[1][2] = LR[1][3];					
      }	else {
        sdram_adr = play_adr&0xFFFFF;						
        LR[0][0] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][0];							
        LR[1][0] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][1];
        sdram_adr = (play_adr+1)&0xFFFFF;
        LR[0][1] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][0];								
        LR[1][1] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][1];		
        sdram_adr = (play_adr+2)&0xFFFFF;
        LR[0][2] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][0];									
        LR[1][2] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][1];
      }
      sdram_adr = (play_adr+3)&0xFFFFF;	
      LR[0][3] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][0];							
      LR[1][3] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][1];		
		}	else if (reverse==1 && play_adr>=step_position)	{
		  play_adr-= step_position;
      if (step_position==1) {
        LR[0][0] = LR[0][1];
        LR[1][0] = LR[1][1];
        LR[0][1] = LR[0][2];
        LR[1][1] = LR[1][2];
        LR[0][2] = LR[0][3];
        LR[1][2] = LR[1][3];
        sdram_adr = (play_adr)&0xFFFFF;	
        LR[0][3] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][0];							
        LR[1][3] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][1];		
      } else {
        sdram_adr = play_adr&0xFFFFF;						
        LR[0][3] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][0];							
        LR[1][3] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][1];
        sdram_adr = (play_adr+1)&0xFFFFF;
        LR[0][2] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][0];								
        LR[1][2] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][1];		
        sdram_adr = (play_adr+2)&0xFFFFF;
        LR[0][1] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][0];									
        LR[1][1] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][1];
        sdram_adr = (play_adr+3)&0xFFFFF;	
        LR[0][0] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][0];							
        LR[1][0] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][1];			
      }	
		}	
	  position = position%10000;	
	}	

	T = position;
	T = T/10000;
	T = T - 1/2.0F;
	
	even1 = LR[0][2];
	even1 = even1 + LR[0][1];
	odd1 = LR[0][2];
	odd1 = odd1 - LR[0][1];
	even2 = LR[0][3];
	even2 = even2 + LR[0][0]; 
	odd2 = LR[0][3];
	odd2 = odd2 - LR[0][0];
	c0 = (float)even1*COEF[0];
	r0 = (float)even2*COEF[1];
	c0 = c0 + r0;
	c1 = (float)odd1*COEF[2];
	r1 = (float)odd2*COEF[3];
	c1 = c1 + r1;
	c2 = (float)even1*COEF[4]; 
	r2 = (float)even2*COEF[5];
	c2 = c2 + r2;
	c3 = (float)odd1*COEF[6];
	r3 = (float)odd2*COEF[7];
	c3 = c3 + r3;

	SAMPLE_BUFFER = c0+T*(c1+T*(c2+T*c3));
	SAMPLE_BUFFER = SAMPLE_BUFFER*0.90F;
	PCM_2[0] = (int)SAMPLE_BUFFER;

	even1 = LR[1][2];
	even1 = even1 + LR[1][1];
	odd1 = LR[1][2];
	odd1 = odd1 - LR[1][1];
	even2 = LR[1][3];
	even2 = even2 + LR[1][0]; 
	odd2 = LR[1][3];
	odd2 = odd2 - LR[1][0];
	c0 = (float)even1*COEF[0];
	r0 = (float)even2*COEF[1];
	c0 = c0 + r0;
	c1 = (float)odd1*COEF[2];
	r1 = (float)odd2*COEF[3];
	c1 = c1 + r1;
	c2 = (float)even1*COEF[4]; 
	r2 = (float)even2*COEF[5];
	c2 = c2 + r2;
	c3 = (float)odd1*COEF[6];
	r3 = (float)odd2*COEF[7];
	c3 = c3 + r3;

	SAMPLE_BUFFER = c0+T*(c1+T*(c2+T*c3));
	SAMPLE_BUFFER = SAMPLE_BUFFER*0.90F;
	PCM_2[1] = (int)SAMPLE_BUFFER;
	
	SAMPLE[3] = PCM_2[0]/256;
	SAMPLE[2] = PCM_2[0]%256;
	SAMPLE[1] = PCM_2[1]/256;
	SAMPLE[0] = PCM_2[1]%256;
	//HAL_GPIO_WritePin(GPIOB, LED_TAG_LIST_Pin, GPIO_PIN_RESET);
}

FASTRUN void advancePosition_claude()
{
  position += pitch;

  if (position > 9999) {
      step_position = position / 10000;
      
      if (reverse == 0 && end_of_track == 0) {
          play_adr += step_position;
          
          if (step_position == 1) {
              LR[0][0] = LR[0][1];
              LR[0][1] = LR[0][2];
              LR[0][2] = LR[0][3];
              LR[1][0] = LR[1][1];
              LR[1][1] = LR[1][2];
              LR[1][2] = LR[1][3];
          } else {
              uint32_t base_adr = play_adr & 0xFFFFF;
              uint32_t bank_offset = (offset_adress & 0x7F) + (base_adr >> 13);  // MASK HERE
              
              for (int i = 0; i < 3; i++) {
                  uint32_t addr = (base_adr + i) & 0xFFFFF;
                  uint32_t bank = (bank_offset + ((addr >> 13) - (base_adr >> 13))) & 0x7F;
                  uint32_t idx = addr & 0x1FFF;
                  LR[0][i] = PCM[bank][idx][0];
                  LR[1][i] = PCM[bank][idx][1];
              }
          }
          
          // Always load last sample
          sdram_adr = (play_adr + 3) & 0xFFFFF;
          uint32_t bank_index = (((sdram_adr >> 13) + offset_adress) & 0x7F);  // MASK HERE
          LR[0][3] = PCM[bank_index][sdram_adr & 0x1FFF][0];
          LR[1][3] = PCM[bank_index][sdram_adr & 0x1FFF][1];
      }
      else if (reverse == 1 && play_adr >= step_position) {
          play_adr -= step_position;
          
          if (step_position == 1) {
              LR[0][0] = LR[0][1];
              LR[0][1] = LR[0][2];
              LR[0][2] = LR[0][3];
              LR[1][0] = LR[1][1];
              LR[1][1] = LR[1][2];
              LR[1][2] = LR[1][3];
              
              sdram_adr = play_adr & 0xFFFFF;
              uint32_t bank_index = (((sdram_adr >> 13) + offset_adress) & 0x7F);  // MASK HERE
              LR[0][3] = PCM[bank_index][sdram_adr & 0x1FFF][0];
              LR[1][3] = PCM[bank_index][sdram_adr & 0x1FFF][1];
          } else {
              uint32_t base_adr = play_adr & 0xFFFFF;
              
              for (int i = 0; i < 4; i++) {
                  uint32_t addr = (base_adr + i) & 0xFFFFF;
                  uint32_t bank = (((addr >> 13) + offset_adress) & 0x7F);  // MASK HERE
                  uint32_t idx = addr & 0x1FFF;
                  LR[0][3 - i] = PCM[bank][idx][0];
                  LR[1][3 - i] = PCM[bank][idx][1];
              }
          }
      }
      
      position %= 10000;
  }

  // Rest of interpolation code unchanged...
  float T = (position * 0.0001f) - 0.5f;

  for (int ch = 0; ch < 2; ch++) {
      int32_t even1 = (int32_t)LR[ch][2] + (int32_t)LR[ch][1];
      int32_t odd1 = (int32_t)LR[ch][2] - (int32_t)LR[ch][1];
      int32_t even2 = (int32_t)LR[ch][3] + (int32_t)LR[ch][0];
      int32_t odd2 = (int32_t)LR[ch][3] - (int32_t)LR[ch][0];
      
      float c0 = (float)even1 * COEF[0] + (float)even2 * COEF[1];
      float c1 = (float)odd1 * COEF[2] + (float)odd2 * COEF[3];
      float c2 = (float)even1 * COEF[4] + (float)even2 * COEF[5];
      float c3 = (float)odd1 * COEF[6] + (float)odd2 * COEF[7];
      
      float result = c0 + T * (c1 + T * (c2 + T * c3));
      result *= 0.90f;
      
      if (result > 2147483647.0f) result = 2147483647.0f;
      if (result < -2147483648.0f) result = -2147483648.0f;
      
      PCM_2[ch] = (int32_t)result;
  }

  SAMPLE[3] = (uint8_t)((uint32_t)PCM_2[0] >> 8);
  SAMPLE[2] = (uint8_t)(PCM_2[0] & 0xFF);
  SAMPLE[1] = (uint8_t)((uint32_t)PCM_2[1] >> 8);
  SAMPLE[0] = (uint8_t)(PCM_2[1] & 0xFF);

#if defined(RDI_DEVELOPMENTS_REV3)
  // Lower volume
  #define VOLUME_FACTOR 655 // 98%, 3277 is 90% reduced volume 
  int16_t raw_left = (int16_t)((SAMPLE[1] << 8) | SAMPLE[0]);
  int32_t scaled_left_32 = (int32_t)raw_left * VOLUME_FACTOR;
  int16_t scaled_left = (int16_t)(scaled_left_32 >> 15); 
  int16_t raw_right = (int16_t)((SAMPLE[3] << 8) | SAMPLE[2]);
  int32_t scaled_right_32 = (int32_t)raw_right * VOLUME_FACTOR;
  int16_t scaled_right = (int16_t)(scaled_right_32 >> 15);
  SAMPLE[2] = (uint8_t)(scaled_right & 0xFF);    
  SAMPLE[3] = (uint8_t)((scaled_right >> 8) & 0xFF); 
  SAMPLE[0] = (uint8_t)(scaled_left & 0xFF);       
  SAMPLE[1] = (uint8_t)((scaled_left >> 8) & 0xFF); 
#endif  
}

// Precompute these as constants (outside ISR, during initialization)
// Original COEF values converted to Q15.16 fixed point (multiply by 65536)
// Example values - replace with your actual COEF[] converted to fixed point
static const int32_t COEF_FIXED[8] = {
    (int32_t)(COEF[0] * 65536.0f),
    (int32_t)(COEF[1] * 65536.0f),
    (int32_t)(COEF[2] * 65536.0f),
    (int32_t)(COEF[3] * 65536.0f),
    (int32_t)(COEF[4] * 65536.0f),
    (int32_t)(COEF[5] * 65536.0f),
    (int32_t)(COEF[6] * 65536.0f),
    (int32_t)(COEF[7] * 65536.0f)
};

// 0.90 as Q15.16 fixed point
static const int32_t SCALE_090 = 59000; // 0.90 * 65536

FASTRUN void advancePosition_claude_optimized()
{
  if(((play_adr+step_position+3) <= (baseSampPerWavePoint * all_long))) {						//change all_long extract!
		end_of_track = 0;	
	}	else {
		end_of_track = 1;	
    return;
	}
  
  position += pitch;

    if (position > 9999) {
        step_position = position / 10000;
        
        if (reverse == 0 && end_of_track == 0) {
            play_adr += step_position;
            
            if (step_position == 1) {
                // Shift samples using 32-bit copies (faster than individual assignments)
                LR[0][0] = LR[0][1];
                LR[0][1] = LR[0][2];
                LR[0][2] = LR[0][3];
                LR[1][0] = LR[1][1];
                LR[1][1] = LR[1][2];
                LR[1][2] = LR[1][3];
                
                // Load only the new sample
                sdram_adr = (play_adr + 3) & 0xFFFFF;
                uint32_t bank_index = ((sdram_adr >> 13) + offset_adress) & 0x7F;
                uint32_t idx = sdram_adr & 0x1FFF;
                LR[0][3] = PCM[bank_index][idx][0];
                LR[1][3] = PCM[bank_index][idx][1];
            } else {
                // Multi-step advance
                uint32_t base_adr = play_adr & 0xFFFFF;
                uint32_t bank_offset = (offset_adress & 0x7F) + (base_adr >> 13);
                uint32_t base_bank_shift = base_adr >> 13;
                
                for (int i = 0; i < 4; i++) {
                    uint32_t addr = (base_adr + i) & 0xFFFFF;
                    uint32_t bank = (bank_offset + ((addr >> 13) - base_bank_shift)) & 0x7F;
                    uint32_t idx = addr & 0x1FFF;
                    LR[0][i] = PCM[bank][idx][0];
                    LR[1][i] = PCM[bank][idx][1];
                }
            }
        }
        else if (reverse == 1 && play_adr >= step_position) {
            play_adr -= step_position;
            
            if (step_position == 1) {
                // Shift samples
                LR[0][0] = LR[0][1];
                LR[0][1] = LR[0][2];
                LR[0][2] = LR[0][3];
                LR[1][0] = LR[1][1];
                LR[1][1] = LR[1][2];
                LR[1][2] = LR[1][3];
                
                sdram_adr = play_adr & 0xFFFFF;
                uint32_t bank_index = ((sdram_adr >> 13) + offset_adress) & 0x7F;
                LR[0][3] = PCM[bank_index][sdram_adr & 0x1FFF][0];
                LR[1][3] = PCM[bank_index][sdram_adr & 0x1FFF][1];
            } else {
                uint32_t base_adr = play_adr & 0xFFFFF;
                
                for (int i = 0; i < 4; i++) {
                    uint32_t addr = (base_adr + i) & 0xFFFFF;
                    uint32_t bank = ((addr >> 13) + offset_adress) & 0x7F;
                    uint32_t idx = addr & 0x1FFF;
                    LR[0][3 - i] = PCM[bank][idx][0];
                    LR[1][3 - i] = PCM[bank][idx][1];
                }
            }
        }
        
        position %= 10000;
    }

    // ===== OPTIMIZED INTERPOLATION USING FIXED-POINT MATH =====
    // T ranges from -0.5 to 0.5, convert to Q15.16 fixed point
    // T_fixed = (position - 5000) * 65536 / 10000 = (position - 5000) * 6.5536
    // Approximate as (position - 5000) * 13107 >> 14 (13107/16384 ≈ 0.8, close enough)
    int32_t T_fixed = ((int32_t)position - 5000) * 13107 >> 14;
    
    int32_t PCM_2_temp[2];
    
    for (int ch = 0; ch < 2; ch++) {
        // Load samples once
        int32_t s0 = LR[ch][0];
        int32_t s1 = LR[ch][1];
        int32_t s2 = LR[ch][2];
        int32_t s3 = LR[ch][3];
        
        int32_t even1 = s2 + s1;
        int32_t odd1 = s2 - s1;
        int32_t even2 = s3 + s0;
        int32_t odd2 = s3 - s0;
        
        // Fixed-point multiplication: (a * b) >> 16
        int64_t c0_64 = ((int64_t)even1 * COEF_FIXED[0] + (int64_t)even2 * COEF_FIXED[1]) >> 16;
        int64_t c1_64 = ((int64_t)odd1 * COEF_FIXED[2] + (int64_t)odd2 * COEF_FIXED[3]) >> 16;
        int64_t c2_64 = ((int64_t)even1 * COEF_FIXED[4] + (int64_t)even2 * COEF_FIXED[5]) >> 16;
        int64_t c3_64 = ((int64_t)odd1 * COEF_FIXED[6] + (int64_t)odd2 * COEF_FIXED[7]) >> 16;
        
        // Polynomial evaluation: c0 + T*(c1 + T*(c2 + T*c3))
        int64_t temp3 = (c3_64 * T_fixed) >> 16;
        int64_t temp2 = ((c2_64 + temp3) * T_fixed) >> 16;
        int64_t temp1 = ((c1_64 + temp2) * T_fixed) >> 16;
        int64_t result = c0_64 + temp1;
        
        // Apply 0.90 scaling
        result = (result * SCALE_090) >> 16;
        
        // Clamp to int32 range
        if (result > 2147483647LL) result = 2147483647LL;
        if (result < -2147483648LL) result = -2147483648LL;
        
        PCM_2_temp[ch] = (int32_t)result;
    }
    
    PCM_2[0] = PCM_2_temp[0];
    PCM_2[1] = PCM_2_temp[1];

    // Pack samples
    SAMPLE[3] = (uint8_t)((uint32_t)PCM_2[0] >> 8);
    SAMPLE[2] = (uint8_t)(PCM_2[0] & 0xFF);
    SAMPLE[1] = (uint8_t)((uint32_t)PCM_2[1] >> 8);
    SAMPLE[0] = (uint8_t)(PCM_2[1] & 0xFF);

#if defined(RDI_DEVELOPMENTS_REV3)
    #define VOLUME_FACTOR 655 // 98%, 3277 is 90% reduced volume 
    // Optimized volume scaling using int16_t directly
    int16_t raw_left = (int16_t)((SAMPLE[1] << 8) | SAMPLE[0]);
    int16_t raw_right = (int16_t)((SAMPLE[3] << 8) | SAMPLE[2]);
    
    // Single multiply-shift operation
    int16_t scaled_left = (int16_t)(((int32_t)raw_left * VOLUME_FACTOR) >> 15);
    int16_t scaled_right = (int16_t)(((int32_t)raw_right * VOLUME_FACTOR) >> 15);
    
    SAMPLE[2] = (uint8_t)(scaled_right & 0xFF);    
    SAMPLE[3] = (uint8_t)((scaled_right >> 8) & 0xFF); 
    SAMPLE[0] = (uint8_t)(scaled_left & 0xFF);       
    SAMPLE[1] = (uint8_t)((scaled_left >> 8) & 0xFF); 
#endif  
}