#include <Arduino.h>
#include <lvgl.h>
#include "teensy41SQLite.hpp"
#include <SD.h>
#include "globals.h"
#include "file_viewer.h"
#include "dj_screen.h"
#include <SDRAM_t4.h>
#include "eLCDIF_t4.h"
#include <Adafruit_FT6206.h>
#include "inflate.h"
#include "T4_PXP.h"

#if defined(RDI_DEVELOPMENTS_REV3)
#include <SdFat.h>
#define BACKLIGHT_PIN 24
SdFs sd_io2;
#else
#define BACKLIGHT_PIN A0
SdExFat SD;
#endif


#include "i2s_sync.h"

extern "C" void startup_middle_hook(void);
void SAI_IRQHandler(void);
void advancePosition_rezo();
void advancePosition_claude();

#define LCDDISP
//#define REMDISP

//#define USE_PXP

//SdFat sd;
FsFile perfDB;
FsFile metaDB;

SDRAM_t4 sdram;
eLCDIF_t4 lcd;
i2s_sync audio;

bool is_playing = false;
uint32_t all_long = 0;                     //all long of Track in 0.5*frames   150 on 1 sec
volatile uint32_t play_adr = 0;            //Playing adress in samples (44100 per second)
uint32_t slip_play_adr = 0;                //Playing adress for SLIP MODE in samples (44100 per second)
uint16_t start_adr_valid_data = 0;         //filling adress in memory
uint16_t end_adr_valid_data = 0;           //filling adress in memory ()
uint8_t filling_step = 0;
uint8_t offset_adress = 0;                 //address offset for calling CUE audio data (for work)
uint8_t mem_offset_adress = 0;             //address offset for calling CUE audio data (for memory)
EXTMEM_NOCACHE_PCM uint16_t PCM[206][8192][2] __attribute__((aligned(32)));
uint16_t PCM_2[2] __attribute__((aligned(32)));
int16_t LR[2][4] __attribute__((aligned(32)));
volatile uint8_t SAMPLE[4] __attribute__((aligned(4))) = {0,0,0,0};
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
 volatile uint8_t reverse = 0;
 volatile uint16_t pitch = 10000;         // 10000 = 100% step 0,01%            
 volatile uint32_t position = 0;
 uint32_t slip_position = 0;
 uint16_t pitch_for_slip = 10000;         // 10000 = 100% step 0,01%    
 float SAMPLE_BUFFER;
 float T;
 uint8_t QUANTIZE = 1;                    //QUANTIZE ENABLE
 volatile uint8_t end_of_track = 0;       //end track flag
 uint8_t loop_active = 0;                 //loop flag
 uint32_t LOOP_OUT = 0;                   //adr LOOP OUT in frames 150
 uint8_t lock_control = 1;            

volatile uint32_t interrupt_counter = 0;

uint32_t last_check_time = 0;

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
#if defined(RDI_DEVELOPMENTS_REV3)
eLCDIF_t4_config lcd_config = {480, 8, 4, 4, 800, 8, 4, 4, 25, 24, 0, 0};
#else
eLCDIF_t4_config lcd_config = {480, 16, 4, 16, 800, 8, 4, 8, 30, 24, 1, 1};
#endif

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
  if (!sdram.begin(32, 198, 1)){
    Serial.println("SDRAM init fail :( ...");
  }
}

#ifdef REMDISP
void refreshDisplayCallback()
{
  lv_area_t area;
  area.x1 = 0; area.y1 = 0; area.x2 = SCREEN_WIDTH; area.y2 = SCREEN_HEIGHT;
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

#endif

#ifdef LCDDISP
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
#if (LVGL_VERSION_MAJOR == 8)
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
#if (LVGL_VERSION_MAJOR == 9)
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
  if (LCD_BUFFER_COUNT == 2 && dynamicBufferReady == true) {
    uint8_t *destPtr = (uint8_t *)LCDIF_NEXT_BUF + (SCREEN_WIDTH * middleContainerPos * 2);
    memcpy(destPtr, dynamicCanvasBuffer, (SCREEN_WIDTH * chartHeight * 2));
    dynamicBufferReady = false;
  }
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

LV_FONT_DECLARE(exo2_18)

FLASHMEM void reportAppConfig() {
  Serial.println("\n========= App settings ===========");
  Serial.printf("USE_EXTMEM_NOCACHE: %s%s" SER_RESET "\n", MACRO_EXISTS(USE_EXTMEM_NOCACHE) ? SER_GREEN : SER_RED, MACRO_EXISTS(USE_EXTMEM_NOCACHE) ? "TRUE" : "FALSE");
  Serial.printf("LCD_BUFFER_COUNT: " SER_YELLOW "%d" SER_RESET "\n", LCD_BUFFER_COUNT);
  Serial.printf("LVGL: " SER_YELLOW "%d.%d.%d" SER_RESET "\n", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
 // buffer count, LVGL version
  Serial.println("========= App settings ===========\n");
}

void setup()
{
  Serial.begin (115200);
  while (!Serial) {};
  delay(1000);
  if(CrashReport){
    Serial.print(CrashReport);
  }

  reportAppConfig();

  int resultBegin = -1;
  T41SQLite::getInstance().setLogCallback(errorLogCallback);

#if defined(RDI_DEVELOPMENTS_REV3)
  if (sd_io2.begin(SdioConfig(FIFO_SDIO | USE_SDIO2))) {
    resultBegin = T41SQLite::getInstance().begin(&sd_io2);
#else
  if (SD.begin(BUILTIN_SDCARD)) {
    resultBegin = T41SQLite::getInstance().begin(&SD);
#endif
  } else {
    Serial.println("sd.begin() failed! - Halting!");
    while (true) { delay(1000); }
  }

  Serial.println("Initializing PCM buffer...");
  memset(PCM, 0, sizeof(PCM));
  Serial.println("PCM buffer initialized");

  //REMDISP_init();
  //REMDISP_register_callbacks();
  memset(lcdBuffer, 3333, SCREEN_WIDTH * SCREEN_HEIGHT * LCD_BUFFER_COUNT * 2);
  #ifdef REMDISP
  remoteDisplay.init(SCREEN_WIDTH , SCREEN_HEIGHT);
  remoteDisplay.registerRefreshCallback(refreshDisplayCallback);
  #endif

  #ifdef LCDDISP

  pinMode(BACKLIGHT_PIN, OUTPUT);
  analogWriteFrequency(BACKLIGHT_PIN, 200);
  analogWrite(BACKLIGHT_PIN, 0);
  if (ctp.begin(20)) {
    Serial.println("FT5316 touch controller initialized");
  }
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

  #endif
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

  analogWrite(BACKLIGHT_PIN, 220);
 
  lcd.runLCD(); // Turn on the LCDIF when the 1st frame is ready to be displayed
  audio.begin(&SAI_IRQHandler);


  if (resultBegin == SQLITE_OK)
  {
    Serial.println("T41SQLite::getInstance().begin() succeded!");
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
  }
  else
  {
    Serial.println("T41SQLite::getInstance().begin() failed!");
  }
}

uint32_t bytes_read = 0;

FASTRUN void loop()
{
  // Debug code for interrupt counts
  uint32_t now = millis();
  if (now - last_check_time >= 5000) {  // Every 5 seconds
    Serial.printf("Interrupts per second: %ld\n", interrupt_counter / 5);
    interrupt_counter = 0;
    last_check_time = now;
  }

  if (lvgl_framePending == true) {
      lvgl_framePending = false;
      lv_timer_handler(); 
  }

  if(is_playing){
    static uint32_t play_adr_temp =0;
    if ((play_adr_temp/420) != (play_adr/420)) {
      //Serial.printf("Play adr: %lu\n", play_adr);
      updateDynamicWaveform(play_adr);
      updatePlaybackPosition((play_adr/420)*799/all_long);
      play_adr_temp = play_adr; 
    }
    
    
      if(end_adr_valid_data<128){
        bytes_read = playFile.read(PCM[end_adr_valid_data][0], 32768);
        Serial.printf("Start filling buffers: end_adr_valid_data: %d wav file bytes read: %d \n",end_adr_valid_data, bytes_read);
        end_adr_valid_data++;
          
        }

    else if((end_adr_valid_data<((play_adr>>13)+42)) && (filling_step==0 || filling_step==6)){
      						//filling the buffer forward
      if(filling_step==6){
        playFile.seek((32768*end_adr_valid_data)+44);
        filling_step = 0;	
        }

      bytes_read = playFile.read(PCM[end_adr_valid_data&0x7F][0], 32768);
      //Serial.printf("Filling buffer forward: end_adr_valid_data: %d wav file bytes read: %d \n",end_adr_valid_data, bytes_read);	
      //Serial.printf("all_long %d play_adr %d \n", all_long, play_adr);		
      //DrawCueMarker(1+((end_adr_valid_data*11145)/all_long));
      end_adr_valid_data++;
      if((end_adr_valid_data-start_adr_valid_data)>128){
        start_adr_valid_data = end_adr_valid_data-128;	
        }
      }
    else if(((end_adr_valid_data>((play_adr>>13)+86) || ((end_adr_valid_data-start_adr_valid_data)<124)) && start_adr_valid_data>3) || (filling_step!=0 && filling_step!=6)){					//filling the buffer back
    Serial.println("filling buffers backwards");		
      if(filling_step==0 || filling_step==6){
        if((end_adr_valid_data-start_adr_valid_data)>127)	{
          end_adr_valid_data = start_adr_valid_data+124;	
          }	
        start_adr_valid_data-= 4;	
        playFile.seek((32768*(start_adr_valid_data))+44);
        filling_step = 1;	
        }

      else if(filling_step==1){
        playFile.read(PCM[start_adr_valid_data&0x7F][0], 32768);
        filling_step = 2;	
        }

      else if(filling_step==2){
        playFile.read(PCM[(start_adr_valid_data+1)&0x7F][0], 32768);
        filling_step = 3;	
        }

      else if(filling_step==3){
        playFile.read(PCM[(start_adr_valid_data+2)&0x7F][0], 32768);
        filling_step = 4;	
        }

      else if(filling_step==4){
        playFile.read(PCM[(start_adr_valid_data+3)&0x7F][0], 32768);
        filling_step = 5;	
        }

      else if(filling_step==5){
        //DrawCueMarker(1+((start_adr_valid_data*11145)/all_long));	
        filling_step = 6;		
        }
      }
    } 
}

FASTRUN void SAI_IRQHandler(void)
{
  interrupt_counter++;

  //I2S_TCSR_REG &= ~I2S_TCSR_FRIE;  // Disable interrupt temporarily

  uint16_t left = (SAMPLE[1] << 8) | SAMPLE[0];
  uint16_t right = (SAMPLE[3] << 8) | SAMPLE[2];

  __DMB();  // Data Memory Barrier - force ordering
  
  I2S3_TDR0 = (uint32_t)left << 16;
  I2S3_TDR0 = (uint32_t)right << 16;
  
  advancePosition_claude();
  
  I2S_TCSR_REG |= 0x00040000;     // Clear error flag
  //I2S_TCSR_REG |= I2S_TCSR_FRIE;  // Re-enable interrupt
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
  int32_t PCM_2[2];
  uint32_t step_position;
  uint32_t sdram_adr;
  
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
  #define VOLUME_FACTOR 3277 // 90% reduced volume 
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