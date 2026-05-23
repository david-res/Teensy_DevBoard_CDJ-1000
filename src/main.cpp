#include <Arduino.h>
#include <lvgl.h>
#include <SD.h>
#include "USBHost_t36.h"
#include "globals.h"
#include "stats/app_stats.h"
#include "file_viewer.h"
#include "dj_screen.h"
#include "utils/changeSDSpeed.h"
#include "lv_utils.h"
#include "clockspeed/beermat_clockspeed.h"
#include "PacketsSerial.h"
#include "T4_DMA_SPI_SLAVE.h"
#include "rekordbox_anlz_api.h"
#include "utils/letter_renderer.h"


#if defined(USE_LCD_DISP)
#include "eLCDIF_t4.h"
#include <Adafruit_FT6206.h>
#include "T4_PXP.h"
#else
IntervalTimer lcdTimer;
#endif

#if defined(TEENSY41)
#include "teensy_display/display_drv.h"
#include "teensy_display/pin_defines.h"
#include <Adafruit_FT6206.h>
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
void advancePosition_claude_optimized();
void flushtoScreen(uint8_t * destPtr, uint16_t * srcPtr, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

#if defined(USE_REM_DISP)
RemoteDisplay remoteDisplay;
#endif  

PacketsSerial OurSerial1WireT4toT4dataExch;
uint8_t MasterAddress = 0;
uint8_t SlaveAddress = 1;
void g_process_serial_rx (uint16_t x);
//#define USE_PXP

//For stats
AppStats appStats = AppStats();

USBHost myusb;
USBDrive myDrive(myusb);


i2s_sync audio;

#if !defined(TEENSY41)
SDRAM_t4 sdram;
#endif
#if defined(USE_LCD_DISP)
eLCDIF_t4 lcd;
#endif

uint32_t play_count = 0;
uint32_t targetFrequency = CPU_SPEED_MHZ;


int16_t track_count = 0;
Track** all_tracks = nullptr;      // Pointer to array of Track pointers, initialized to nullptr	

triangle_shape_t s_cue_loop_triangle;



void DMA2_Stream5_IRQHandler(void);
void CheckTXCRC();
uint8_t CheckRXCRC(void);



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
float c0, c1, c2, c3, r0, r1, r2, r3;
int32_t even1, even2, odd1, odd2;
static float COEF[8] = {			//////optimal 2x
0.45868970870461956,
0.04131401926395584,
0.48068024766578432,
0.17577925564495955,
-0.246185007019907091, 
0.24614027139700284,
-0.36030925263849456,
0.10174985775982505
};

// Used in I2S ISR and loop 
volatile uint32_t play_adr = 0;                           //Playing adress in samples (44100 per second)
volatile uint32_t baseSampPerWavePoint = 294;             //Number of samples per wavepoint in dynamic waveform. Updated from the database later
volatile uint32_t all_long = 0;                           //all long of Track in 0.5*frames   150 on 1 sec
uint16_t track_play_now = 0;                     //Track ID of the track currently playing
uint8_t	ftp = 0;										/////temp!

// I dont think we mark big ass arrays as volatile?
// TODO why is this 205 and not 128?
EXTMEM_NOCACHE_PCM uint16_t PCM[206][8192][2] __attribute__((aligned(32)));

// Make volatile as critical to buffer management
volatile uint32_t start_adr_valid_data = 0;               //filling adress in memory
volatile uint32_t end_adr_valid_data = 0;                 //filling adress in memory ()
volatile uint8_t filling_step = 0;

uint8_t change_speed = 0;							//flag for RELEASE/START or TOUCH/BREAKE 
#define NO_CHANGE	0
#define NEED_UP		1
#define NEED_DOWN	2
uint32_t CUE_ADR = 0;					//REAL CUE adr in frames 150
uint16_t PreviousPhase = 0xFFFF;
uint16_t bars = 0;
uint16_t originalBPM = 0xFFFF;						//this original BPM*100 of track (pitch = 0.00%) 	

void CALL_CUE(void);

uint16_t acceleration_UP = 0xFFFF;				//acceleration_UP for vinyl RELEASE/START
uint16_t acceleration_DOWN = 0xFFFF;			//acceleration_DOWN for vinyl TOUCH/BREAKE
uint16_t LOG_TABLE[8] = {1200, 128, 72, 40, 18, 15, 9, 3};		

// Others
bool is_playing = false;
uint32_t slip_play_adr = 0;                      //Playing adress for SLIP MODE in samples (44100 per second)
uint8_t mem_offset_adress = 0;                   //address offset for calling CUE audio data (for memory)
uint8_t play_enable = 0;
uint8_t slip_play_enable = 0;
uint32_t slip_position = 0;
uint16_t pitch_for_slip = 10000;         // 10000 = 100% step 0,01%    
float SAMPLE_BUFFER;
float T;
uint8_t QUANTIZE = 1;                    //QUANTIZE ENABLE
uint8_t loop_active = 0;                 //loop flag
uint32_t LOOP_OUT = 0;                   //adr LOOP OUT in frames 150
uint8_t loop_pending = 0;
uint8_t loop_in_active_pressed = 0;
uint8_t loop_out_active_pressed = 0;
uint8_t lock_control = 0;            


// For static buffer indicator
bool staticBufferReady = false;
uint16_t oldStaticBufferX = 0;
uint16_t newStaticBufferX = 0;
uint16_t staticIndicatorBuffer[2 * overviewChartHeight];


uint8_t Tbuffer[32] = {168,  119,  119,  0,  119,  119,  0,  0,  0,  1,  176,  0,  0,  0,  0,  0,  0,  0xC,  0,  0x20,  0,  0,  0,  88,  0,  0,  0};
uint8_t load_animation_enable = 0;
uint8_t Rbuffer[32]={0};
uint16_t zi = 0;
uint8_t a = 0;
uint8_t dma_cnt = 0;
uint32_t DD;
uint16_t potenciometer_tempo;
uint32_t pitch_center;
uint32_t previous_adc_pitch;
uint32_t previous_adc_DD;
uint16_t previous_potenciometer_tempo = 0;
uint8_t tempo_need_update = 0;
uint8_t tempo_range = 1;					//10% default
uint8_t tempo_range_need_update = 0;
uint8_t time_mode_need_update = 0;
uint8_t quantize_mode_need_update = 0;
uint8_t track_need_load = 0;
uint8_t inertial_rotation = 0;							//inertial rotation for jog
uint8_t keep_to_play = 0;
uint8_t PLAY_BUTTON_pressed = 0;
uint8_t CUE_BUTTON_pressed = 0;
uint8_t JOG_MODE_BUTTON_pressed = 0;
uint16_t JOG_MODE_BUTTON_hold_ticks = 0;
uint8_t display_mode =0;
uint8_t TEMPO_RESET_BUTTON_pressed = 0;
uint8_t TEMPO_RANGE_BUTTON_pressed = 0;
uint8_t TRACK_NEXT_BUTTON_pressed = 0;
uint8_t TRACK_PREVIOUS_BUTTON_pressed = 0;
uint8_t WAVE_NEXT_BUTTON_pressed = 0;
uint8_t WAVE_PREVIOUS_BUTTON_pressed = 0;
uint8_t CALL_NEXT_BUTTON_pressed = 0;
uint8_t CALL_PREVIOUS_BUTTON_pressed = 0;
uint8_t SEARCH_FF_BUTTON_pressed = 0;
uint8_t SEARCH_REW_BUTTON_pressed = 0;
uint8_t TIME_MODE_BUTTON_pressed = 0;
uint8_t QUANTIZE_BUTTON_pressed = 0;							//TEXT MODE real button
uint8_t SLIP_MODE_BUTTON_pressed = 0;
uint8_t REVERSE_SWITCH_pressed = 0;
uint8_t REALTIME_CUE_BUTTON_pressed = 0;
uint8_t LOOP_OUT_BUTTON_pressed = 0;	
uint8_t RELOOP_BUTTON_pressed = 0;
uint8_t TIM_PLAY_LED = 0;
uint8_t TIM_CUE_LED = 0;
uint8_t RED_CIRCLE_CUE_ADR = 1;
uint8_t TMPSLP = 0;
uint8_t REALTIME_CUE_LED_BLINK = 16;
uint8_t TIM_REALTIME_CUE_LED = 1;
uint8_t LOOP_LEDS_BLINK = 0;
uint8_t LED_SD_timer = 0;
uint8_t timer_time = 0;
uint8_t JOG_PRESSED = 0;
uint8_t need_call_to_cue = 0;
uint8_t keep_slip = 0;
uint8_t slip_mode = 0;
uint8_t dynamicWaveformZOOM = 1;
uint8_t CUE_OPERATION = 0;


uint8_t dSHOW = TRACK_LIST;
uint8_t REMAIN_ENABLE = 1;
uint8_t waveformType = WAVEFORM_RGB;


void ledTIMER();
IntervalTimer ledTimer;
IntervalTimer serialTimer;

void serialTimerISR();



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

uint8_t displayRefreshRate = 60;


const uint16_t lvglBufferHeight = 120;
uint16_t lcdBuffer[LCD_BUFFER_COUNT][SCREEN_WIDTH * (SCREEN_HEIGHT/10)] __attribute__((aligned(64)));
//EXTMEM uint16_t tempDisplayBuf[SCREEN_WIDTH * SCREEN_HEIGHT] __attribute__((aligned(64)));
//lv_display_t * disp;

#ifdef USE_PXP

EXTMEM_NOCACHE uint16_t lvglBuffer1[SCREEN_WIDTH * (SCREEN_HEIGHT)] __attribute__((aligned(64)));
EXTMEM_NOCACHE uint16_t lvglBuffer2[SCREEN_WIDTH * (SCREEN_HEIGHT)] __attribute__((aligned(64)));

#endif

void startup_middle_hook(void)
{
  //Check reasonable range for safety
  if (targetFrequency < 150 || targetFrequency > 816) {
    targetFrequency = 528;
  }

  uint32_t cpuMilliVolts = CPU_MILLIVOLTS;

  //Check reasonable range for safety
  if (cpuMilliVolts < 800 || cpuMilliVolts > 1575) {
    cpuMilliVolts = 1150;
  }
  beermat_set_arm_clock(targetFrequency * 1'000'000, MACRO_EXISTS(TEENSY41) ? 0 : cpuMilliVolts);

#if defined(USE_EXTMEM_NOCACHE)  
  //Disable caching for first 4M of SDRAM
	SCB_MPU_RBAR = 0x80000000 | (SCB_MPU_RBAR_REGION(11) | SCB_MPU_RBAR_VALID); // 0x80000000 | REGION(11);
	SCB_MPU_RASR = SCB_MPU_RASR_TEX(1) | SCB_MPU_RASR_AP(3) | SCB_MPU_RASR_XN | (SCB_MPU_RASR_SIZE(21) | SCB_MPU_RASR_ENABLE); //MEM_NOCACHE | READWRITE | NOEXEC | SIZE_4M;

  // Region 12: Next 8MB nocache (0x80400000 - 0x80BFFFFF) for PCM array
  SCB_MPU_RBAR = 0x80400000 | (SCB_MPU_RBAR_REGION(12) | SCB_MPU_RBAR_VALID);
  SCB_MPU_RASR = SCB_MPU_RASR_TEX(1) | SCB_MPU_RASR_AP(3) | SCB_MPU_RASR_XN | (SCB_MPU_RASR_SIZE(22) | SCB_MPU_RASR_ENABLE); //MEM_NOCACHE | READWRITE | NOEXEC | SIZE_8M;
#endif

#if defined(TEENSY41)  
  setPSRamSpeed(EXTMEM_SPEED); 
#else
  // Start SDRAM, 166/198 MHz for pussies, 221 Mhz for real men
  if (!sdram.begin(32, EXTMEM_SPEED, 1)){
    Serial.printf("SDRAM init at %ldMHz failed\n", EXTMEM_SPEED);
  }
#endif  
}

#if defined(USE_REM_DISP)
void refreshDisplayCallback()
{
  lv_area_t area;
  area.x1 = 0; area.y1 = 0; area.x2 = SCREEN_WIDTH; area.y2 = SCREEN_HEIGHT;
  lv_obj_invalidate_area(lv_scr_act(), &area);
}
#endif // USE_REM_DISP

#if defined(USE_LCD_DISP) || defined(USE_REM_DISP) || defined(TEENSY41)
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
#if defined(USE_LCD_DISP)
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
#endif  
#if defined(TEENSY41)
  flushtoScreen(NULL, (uint16_t *)px_map, area->x1, area->y1, area->x2, area->y2);
  lv_disp_flush_ready(&disp_drv);
  if (lv_disp_flush_is_last(&disp_drv)) {
    ps_framePending = true;
  }
#endif  
}
#endif
#if (LVGL_VERSION_MAJOR == 9)
FASTRUN void my_disp_flush(lv_display_t *display, const lv_area_t *area, uint8_t * px_map)
{
#if defined(USE_LCD_DISP)  
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
#endif  
#if defined(TEENSY41)
  flushtoScreen(NULL, (uint16_t *)px_map, area->x1, area->y1, area->x2, area->y2);
  lv_disp_flush_ready(disp_drv);
  if (lv_disp_flush_is_last(disp_drv)) {
    ps_framePending = true;
  }
#endif   
}
#endif

FASTRUN void lcdCallback() {

#if defined(SSD1963) && defined(USE_TEAR)
  uint8_t pinState = digitalReadFast(TFT_TEAR);
  if (pinState == 0) {
    //Falling, end of VNDP, start of display period
    return;
  } else {
    //Rising, end of display period, start of VNDP period
  }
#endif  

  CrashReport.breadcrumb(3, 1);

  appStats.start(ISR_LCD);
  
  lvgl_framePending = true;
  if(ps_framePending == true) {
#if defined(USE_LCD_DISP)    
    lcd.setNextBufferAddress((uint16_t*)next_px_map);
#endif    
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

#if defined(USE_LCD_DISP)   || defined(TEENSY41) 
// Touch controller instance
Adafruit_FT6206 cpt = Adafruit_FT6206();
#endif

#if (LVGL_VERSION_MAJOR == 8)
void touch_read_cb(lv_indev_drv_t * drv, lv_indev_data_t*data)
#endif
#if (LVGL_VERSION_MAJOR == 9)
void touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
#endif
{
#if defined(USE_LCD_DISP) || defined(TEENSY41)   
    // Check if there's a new touch event from interrupt
    TS_Point p = cpt.getPoint();
    if (cpt.touched()) {
        // Touch detected - map coordinates to 800x480 screen
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = p.x;
        data->point.y = p.y;
    } else {
        // Touch released
        data->state = LV_INDEV_STATE_RELEASED;
    }
#endif    
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

LV_FONT_DECLARE(exo2_16)
LV_FONT_DECLARE(exo2_18)



FLASHMEM void reportAppConfig() {
  Serial.println("\n======================== App Settings ==========================");
  Serial.printf("COMPILED: " SER_CYAN "%s %s" SER_RESET " with GCC " SER_CYAN "%d.%d.%d" SER_RESET ", C++ vers: " SER_CYAN "%ld" SER_RESET "\n", __DATE__, __TIME__, __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__, __cplusplus);
#if defined(TEENSY41)
  Serial.printf("F_BUS_ACTUAL: %s%ld" SER_RESET "MHz   VOLTAGE: " SER_CYAN "%ld" SER_RESET "mV   PSRAM_SPEED: " SER_CYAN "%0.2f" SER_RESET "MHz\n", F_CPU_ACTUAL == 528'000'000 ? SER_CYAN : SER_RED,F_CPU_ACTUAL / 1000000, get_voltage_mv(), getPSRamSpeed());  
#else  
  Serial.printf("F_BUS_ACTUAL: %s%ld" SER_RESET "MHz   VOLTAGE: " SER_CYAN "%ld" SER_RESET "mV   SDRAM_SPEED: " SER_CYAN "%d" SER_RESET "MHz\n", F_CPU_ACTUAL == 528'000'000 ? SER_CYAN : SER_RED,F_CPU_ACTUAL / 1000000, get_voltage_mv(), EXTMEM_SPEED);  
#endif
  Serial.printf("SD_CARD_SPEED: " SER_CYAN "%ld" SER_RESET "KHz\n", SD_CARD_SPEED);
  Serial.printf("USE_EXTMEM_NOCACHE: %s%s" SER_RESET "\n", MACRO_EXISTS(USE_EXTMEM_NOCACHE) ? SER_CYAN : SER_RED, MACRO_EXISTS(USE_EXTMEM_NOCACHE) ? "TRUE" : "FALSE");
  Serial.printf("LCD_BUFFER_COUNT: " SER_CYAN "%d" SER_RESET "\n", LCD_BUFFER_COUNT);
  Serial.printf("LVGL: " SER_CYAN "%d.%d.%d" SER_RESET "\n", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
  Serial.printf("DISPLAY: USE_LCD_DISP: %s%s" SER_RESET "  USE_REM_DISP: %s%s" SER_RESET "\n", MACRO_EXISTS(USE_LCD_DISP) ? SER_CYAN : SER_RED, MACRO_EXISTS(USE_LCD_DISP) ? "TRUE" : "FALSE",
      MACRO_EXISTS(USE_REM_DISP) ? SER_RED : SER_CYAN, MACRO_EXISTS(USE_REM_DISP) ? "TRUE" : "FALSE");
  Serial.printf("USE_STATS: %s%s" SER_RESET "\n",  MACRO_EXISTS(USE_STATS) ? SER_YELLOW : SER_GREEN, MACRO_EXISTS(USE_STATS) ? "TRUE" : "FALSE");
  Serial.printf("IRQ_GEN: %s%s" SER_RESET "   TEENSY41: %s\n", MACRO_EXISTS(IRQ_FROM_INT_TIMER) ? SER_RED : SER_CYAN, MACRO_EXISTS(IRQ_FROM_INT_TIMER) ? "IntervalTimer" : "I2S", MACRO_EXISTS(TEENSY41) ? "TRUE": "FALSE");
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
  // Initialize Serial
  Serial.begin (115200);
  //while (!Serial) {};
  delay(1000);

  if (CrashReport) {
    Serial.print(CrashReport);
  }
 
  // Report on configuration
  reportAppConfig();

  myusb.begin();

  // Init buffers
  memset(PCM, 0, sizeof(PCM));
  ///memset(lcdBuffer, 3333, SCREEN_WIDTH * SCREEN_HEIGHT * LCD_BUFFER_COUNT * 2);
  memset(staticIndicatorBuffer, 0xFF, overviewChartHeight * 2 * 2); // White is easy - if marker color hi/li bytes differ, use a loop to fill color

#if defined(TEENSY41)
  // Init touch screen
  if (cpt.begin(10)) {
    Serial.println("FT5316 touch controller initialized");
  }
	#ifdef MK1
	OurSerial1WireT4toT4dataExch.begin(MasterAddress, &DMA_Serial7, 2000000UL);
	OurSerial1WireT4toT4dataExch.PacketSerialRole = MASTER;
	OurSerial1WireT4toT4dataExch.OurSerial->set_process_serial_rx_call(g_process_serial_rx);
	//ledTimer.begin(ledTIMER, 1000); //1ms
	#elif defined(MK3)
	SPI_SLAVE.begin(SPI_MODE3, MSBFIRST, MODE_4WIRE);
	SPI_SLAVE.auto_repeat = 0;
	SPI_SLAVE.setOnComplete(DMA2_Stream5_IRQHandler);
	SPI_SLAVE.prepare_for_slave_transfer(Tbuffer, Rbuffer, 27);
	#endif

#endif



  lv_init();
#if (LVGL_VERSION_MAJOR == 8)
#ifdef USE_PXP
  lv_disp_draw_buf_init(&disp_buf, lvglBuffer1, lvglBuffer2, 800*480);  
#else
  lv_disp_draw_buf_init(&disp_buf, (void *)lcdBuffer[0], LCD_BUFFER_COUNT == 1 ? NULL : (void *)lcdBuffer[1], SCREEN_WIDTH * (MACRO_EXISTS(TEENSY41) ? lvglBufferHeight : SCREEN_HEIGHT)); 
#endif
  lv_disp_drv_init(&disp_drv);            /*Basic initialization*/
  disp_drv.draw_buf = &disp_buf;          /*Set an initialized buffer*/
  disp_drv.flush_cb = my_disp_flush;      /*Set a flush callback to draw to the display*/
  disp_drv.hor_res = SCREEN_WIDTH;        /*Set the horizontal resolution in pixels*/
  disp_drv.ver_res = SCREEN_HEIGHT;       /*Set the vertical resolution in pixels*/
  disp_drv.direct_mode = MACRO_EXISTS(TEENSY41) ? 0 : 1;
  disp_drv.full_refresh = 0;
  lv_disp_drv_register(&disp_drv); /*Register the driver and save the created display objects*/
  


  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init( &indev_drv );
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  //indev_drv.read_cb = touch_glass_input;
  indev_drv.read_cb = touch_read_cb;
  lv_indev_drv_register( &indev_drv );
#endif  // LVGL_VERSION_MAJOR == 8 
#if (LVGL_VERSION_MAJOR == 9)
  disp_drv = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_display_set_color_format(disp_drv, LV_COLOR_FORMAT_RGB565);
#if defined(TEENSY41)
  lv_display_set_buffers(disp_drv, lcdBuffer[0], NULL, SCREEN_WIDTH * (SCREEN_HEIGHT/10)*2, LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif
#if defined(USE_LCD_DISP)
  lv_display_set_buffers(disp_drv, lcdBuffer[0], LCD_BUFFER_COUNT == 2 ? lcdBuffer[1] : NULL, SCREEN_WIDTH * SCREEN_HEIGHT * 2, LV_DISPLAY_RENDER_MODE_DIRECT);
#endif  
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

lr_prerender_glyphs(&lv_font_montserrat_10);

lr_build_triangle_shape(&s_cue_loop_triangle,
    0,              0,
    LR_BOX_WIDTH-1, 0,
    LR_BOX_WIDTH/2, LR_BOX_HEIGHT-1,
    LR_BOX_WIDTH,   LR_BOX_HEIGHT);


  Serial.println("Initializing Audio");      // <-- add before startI2SInterrupt
  audio.begin(&SAI_IRQHandler);
  




  if (disp_init(displayRefreshRate) == false) {
    errorHalt("Failed to initialize display");
  }


  attachInterrupt(digitalPinToInterrupt(TFT_TEAR), lcdCallback, CHANGE);
 
  //lcdTimer.priority(128);
  //lcdTimer.begin(lcdCallback, 17 * 1000); 
  //Serial.printf("Enabled LCD Interval Timer\n");
  Serial.println("Finished loading tracks from rekordbox db");
	LV_IMG_DECLARE(teensy_dj_400px)
	lv_obj_t* bootScreen = lv_obj_create(NULL);
	lv_obj_set_size(bootScreen, SCREEN_WIDTH, SCREEN_HEIGHT);
	lv_obj_set_style_bg_color(bootScreen, lv_color_make(0,0,0), 0);
	lv_obj_t* logo = lv_img_create(bootScreen);
	lv_img_set_src(logo, &teensy_dj_400px);
	lv_obj_set_align(logo, LV_ALIGN_CENTER);
	lv_scr_load_anim(bootScreen, LV_SCR_LOAD_ANIM_FADE_IN, 3000, 500, true);
	
	create_dj_browser_ui();
	//populate_track_list(all_tracks, track_count);
	lv_scr_load_anim(filesScreen, LV_SCR_LOAD_ANIM_FADE_IN, 500, 3500, true); 
	disp_setBrightness(255);


#ifdef MK1
serialTimer.begin(serialTimerISR, 1000); //1ms
#endif

ledTimer.begin(ledTIMER, 1000); //1ms
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
#if defined(TEENSY41)    
  if (bufAddr < 0x70000000 || bufAddr > 0x71400000) {
#else
  if (bufAddr < 0x80000000 || bufAddr > 0x81400000) {
#endif    
    Serial.printf("INVALID BUFFER ADDRESS: 0x%08X\n", bufAddr);
    Serial.flush();
  }

  size_t bytes_read = 0;
  CrashReport.breadcrumb(1, 2);
  //noInterrupts();
  bytes_read = playFile.read(buf, count);
  if (bytes_read != count) {
    Serial.printf(SER_YELLOW "File read mismatch: count: %ld, bytes_read: %ld, %s" SER_RESET "\n", count, bytes_read, bytes_read < 0 ? "error" : bytes_read == 0 ? "EOF" : "Last bytes or error?");
  }
  //interrupts();
  CrashReport.breadcrumb(1, 0);  

  appStats.end(PLAYFILE_READ);
  appStats.addByteCount(PLAYFILE_READ, bytes_read);
  return bytes_read;
}

FASTRUN void draw2PxVerticalStrip(uint16_t* destPtr, uint16_t* srcPtr, uint16_t x1, uint16_t y1, uint16_t height, uint16_t srcWidth, uint16_t destWidth)
{
#if defined(USE_LCD_DISP)
  for (uint16_t y = 0; y < height; y++) {
    uint32_t destOffset = ((y1 + y) * destWidth) + x1;
    uint16_t srcOffset = y * srcWidth;  
    destPtr[destOffset] = srcPtr[srcOffset];
    destPtr[destOffset + 1] = srcPtr[srcOffset + 1];
  }
#endif
#if defined(TEENSY41)
  disp_setAddrWindow(x1, y1, x1 + 1, y1 + height - 1);
  for (uint16_t y = 0; y < height; y++) {
    uint16_t srcOffset = y * srcWidth; 
    disp_pushPixels16bit(srcPtr + srcOffset, srcPtr + srcOffset + 1);
  }
#endif  
#if defined(USE_REM_DISP)
  if (remoteDisplay.sendRemoteScreen == true ) {
    if (srcWidth == 2) {
        remoteDisplay.sendData(x1, y1, x1 + 1, y1 + height - 1, (uint8_t *)(srcPtr));
    } else {
      for (uint16_t y = 0; y < height; y++) {
        uint16_t srcOffset = y * srcWidth; 
        remoteDisplay.sendData(x1, y1 + y, x1 + 1, y1 + y + 1, (uint8_t *)(srcPtr + srcOffset));
      }
    }
  }
#endif
}

FASTRUN void flushtoScreen(uint8_t * destPtr, uint16_t * srcPtr, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
  uint16_t width = x2 - x1 + 1;
  uint16_t height = y2 - y1 + 1;
  
  //Serial.printf("flush: x1: %ld, x2: %ld, y1: %ld, y2: %ld, width: %ld, height: %ld\n", x1, x2, y1, y2, width, height);
  #if defined(USE_LCD_DISP)
  memcpy((uint8_t *)(destPtr + SCREEN_WIDTH * y1 * 2), srcPtr, (width * height * 2));

#endif  
#if defined(USE_REM_DISP)
  if (remoteDisplay.sendRemoteScreen == true ) {
    remoteDisplay.sendData(x1, y1, x2, y2, (uint8_t *)srcPtr);
  }
#endif
#if defined(TEENSY41)
  // Note that x2, y2 are width and height, not co-ords
  uint16_t *pBuf = srcPtr;
  uint16_t *pBufEnd = srcPtr + (width * height) - 1;

  disp_setAddrWindow(x1, y1, x2, y2);
  disp_pushPixels16bit(pBuf, pBufEnd);
#endif  
}

FASTRUN void copyWaveformsToLCD()
{
  if (dynamicBufferReady == true) {

    // Start time for stats
    appStats.start(DYNAMIC_MEMCPY);

    uint8_t * destPtr = MACRO_EXISTS(TEENSY41) ? NULL : (uint8_t *)LCDIF_NEXT_BUF;
    flushtoScreen(destPtr, dynamicCanvasBuffer, 0, middleContainerPos, chartWidth - 1, middleContainerPos + chartHeight - 1);

#if defined(USE_LCD_DISP)    
    // As this isn't updated per frame, it needs to be done in all LCD buffers in use
    if (LCD_BUFFER_COUNT == 2) {
      flushtoScreen((uint8_t *)LCDIF_CUR_BUF, dynamicCanvasBuffer, 0, middleContainerPos, chartWidth - 1, middleContainerPos + chartHeight - 1);
    }
#endif // USE_LCD_DISP
    dynamicBufferReady = false;

    // Finish stats
    appStats.end(DYNAMIC_MEMCPY);
    appStats.addByteCount(DYNAMIC_MEMCPY, (chartWidth * chartHeight * 2)); 
  }

  if (staticBufferReady == true) {

    // Start time for stats
    appStats.start(OVERVIEW_COPY);

    // Erase old marker by copying from pristine canvas buffer into eLCDIF buffer (preserve bottomContainer border with 1 pixel offsets)
    // Draw marker by copying pre-made color-filled marker buffer into eLCDIF buffer (preserve bottomContainer border with 1 pixel offsets)
    //draw2PxVerticalStrip(lcdBuffer[0], overviewCanvasBuffer + oldStaticBufferX, oldStaticBufferX, bottomContainerPos + 1, overviewChartHeight - 3, chartWidth, PREVIEW_WAVEFORM_WIDTH);
    //draw2PxVerticalStrip(lcdBuffer[0], staticIndicatorBuffer, newStaticBufferX, bottomContainerPos + 1, overviewChartHeight - 3, 2, PREVIEW_WAVEFORM_WIDTH);

#if defined(USE_LCD_DISP) 
    // As this isn't updated per frame, it needs to be done in all LCD buffers in use. Fast, though, ~10uS per buffer
    if (LCD_BUFFER_COUNT == 2) {
      draw2PxVerticalStrip(lcdBuffer[1], overviewCanvasBuffer + oldStaticBufferX, oldStaticBufferX, bottomContainerPos + 1, overviewChartHeight - 3, chartWidth, PREVIEW_WAVEFORM_WIDTH);
      draw2PxVerticalStrip(lcdBuffer[1], staticIndicatorBuffer, newStaticBufferX, bottomContainerPos + 1, overviewChartHeight - 3, 2, PREVIEW_WAVEFORM_WIDTH);
    }
#endif // USE_LCD_DISP

    staticBufferReady = false;
  
    // Finish stats
    appStats.end(OVERVIEW_COPY);
  }
}

FASTRUN void loop()
{

	myusb.Task();
  // Take snapshot of play_adr, so we dont have issues as the ISR updates it. Intent is to use it atomicly anyway
  uint32_t snapshot_play_adr = play_adr;

  #ifdef MK1

  if (OurSerial1WireT4toT4dataExch.flag_new_packet_received){
    OurSerial1WireT4toT4dataExch.mmemcpydw ((uint32_t*)Rbuffer, OurSerial1WireT4toT4dataExch.PZDreceive);
	//Serial.printf("Received packet: Rbuffer[0]: 0x%08lX Rbuffer[1]: 0x%08lX Rbuffer[2]: 0x%08lX Rbuffer[3]: 0x%08lX\n", Rbuffer[0], Rbuffer[1], Rbuffer[2], Rbuffer[3]);
    DMA2_Stream5_IRQHandler();
    OurSerial1WireT4toT4dataExch.flag_new_packet_received = 0;
    OurSerial1WireT4toT4dataExch.mmemcpydw (OurSerial1WireT4toT4dataExch.PZDsend, (uint32_t*)Tbuffer);
	
 }
 #endif

  
  // Stats
  if (appStats.readyToReport() == true) {
    appStats.report();
}

  appStats.start(MAIN_LOOP);

  if (lvgl_framePending == true) {

      //if (is_playing == true) {
        copyWaveformsToLCD();
      //}
      

      appStats.start(LV_TIMER_HANDLER);
      lv_timer_handler(); 
      appStats.end(LV_TIMER_HANDLER);

#ifdef USE_REM_DISP
      remoteDisplay.pollRemoteCommand();
#endif

      lvgl_framePending = false;
  }

  if (is_playing == true) {
	
    if(end_of_track == 0) {
	  RedrawWaveforms(play_adr/294);
      
      	if (end_adr_valid_data < 128)
		{
			appStats.start(PLAYFILE_READ);
			playFile.read(PCM[end_adr_valid_data][0], 32768);
			end_adr_valid_data++;
			appStats.end(PLAYFILE_READ);
		}
		else if ((end_adr_valid_data < ((play_adr >> 13) + 42)) && (filling_step == 0 || filling_step == 6))  // filling the buffer forward
		{
			if (filling_step == 6)
			{
				playFile.seek((32768UL * end_adr_valid_data) + 44);
				filling_step = 0;
			}
			appStats.start(PLAYFILE_READ);
			playFile.read(PCM[end_adr_valid_data & 0x7F][0], 32768);
			end_adr_valid_data++;
			appStats.end(PLAYFILE_READ);
			if ((end_adr_valid_data - start_adr_valid_data) > 128)
			{
				start_adr_valid_data = end_adr_valid_data - 128;
			}
		}
		else if (((end_adr_valid_data > ((play_adr >> 13) + 86) || ((end_adr_valid_data - start_adr_valid_data) < 124)) && start_adr_valid_data > 3) || (filling_step != 0 && filling_step != 6))  // filling the buffer back
		{
			if (filling_step == 0 || filling_step == 6)
			{
				if ((end_adr_valid_data - start_adr_valid_data) > 127)
				{
					end_adr_valid_data = start_adr_valid_data + 124;
				}
				start_adr_valid_data -= 4;
				appStats.start(PLAYFILE_SEEK);
				playFile.seek((32768UL * start_adr_valid_data) + 44);
				appStats.end(PLAYFILE_SEEK);
				filling_step = 1;
			}
			else if (filling_step == 1)
			{
				appStats.start(PLAYFILE_READ);
				playFile.read(PCM[start_adr_valid_data & 0x7F][0], 32768);
				filling_step = 2;
				appStats.end(PLAYFILE_READ);
			}
			else if (filling_step == 2)
			{
				appStats.start(PLAYFILE_READ);
				playFile.read(PCM[(start_adr_valid_data + 1) & 0x7F][0], 32768);
				filling_step = 3;
				appStats.end(PLAYFILE_READ);
			}
			else if (filling_step == 3)
			{
				appStats.start(PLAYFILE_READ);
				playFile.read(PCM[(start_adr_valid_data + 2) & 0x7F][0], 32768);
				filling_step = 4;
				appStats.end(PLAYFILE_READ);
			}
			else if (filling_step == 4)
			{
				appStats.start(PLAYFILE_READ);
				playFile.read(PCM[(start_adr_valid_data + 3) & 0x7F][0], 32768);
				filling_step = 5;
				appStats.end(PLAYFILE_READ);
			}
			else if (filling_step == 5)
			{
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
	        // flush PV state
      audio.startI2SInterrupt();
    } 

  }

	/*  
  if(loop_active && CUE_ADR<LOOP_OUT && (play_adr/294)>=LOOP_OUT)					//return to cue for loop mode
		{	
		CALL_CUE();
		//offset_adress = 128-mem_offset_adress;	
		ftp = 1;	
		}
	else if(ftp!=0 && ftp<15)
		{
		ftp++;	
		}
	else if(ftp==15)	
		{
		offset_adress = 0;1
		ftp = 0;		
		}
		*/
	
	if(loop_active && CUE_ADR < LOOP_OUT && (play_adr/294) >= LOOP_OUT && (reverse==0))  // forward loop
	{	
		CALL_CUE();
		ftp = 1;	
	}
	else if(loop_active && CUE_ADR < LOOP_OUT && (play_adr/294) <= CUE_ADR && (reverse==1))  // reverse loop
	{	
		SEEK_AUDIOFRAME(LOOP_OUT*294);
		ftp = 1;	
	}
	else if(ftp != 0 && ftp < 15)
	{
		ftp++;	
	}
	else if(ftp == 15)	
	{
		offset_adress = 0;
		ftp = 0;		
	}
			
	if(CUE_OPERATION==CUE_NEED_SET)
		{
		if(QUANTIZE && dSHOW==WAVEFORM)				//add calculate bars in background process
			{
			if(((play_adr/294)>(BEATGRID[bars-1]+((BEATGRID[bars] - BEATGRID[bars-1])/2))) || bars==0)	
				{
				SET_CUE(BEATGRID[bars]);
				}
			else
				{
				SET_CUE(BEATGRID[bars-1]);			
				}				
			}
		else
			{
			SET_CUE(play_adr/294);	
			}
		CUE_OPERATION = 0;	
		}
	else if(CUE_OPERATION==CUE_NEED_CALL)
		{
		CALL_CUE();
		CUE_OPERATION = 0;			
		}
	else if(CUE_OPERATION==MEMORY_NEED_NEXT_SET)
		{
			/*
		if(numCuePoints>0)
			{
				
			JJ=0;
			while(MEMORY_adr[0][JJ]<=(play_adr/294) && (JJ<numCuePoints-1))
				{
				JJ++;	
				}
			if((play_adr/294)<MEMORY_adr[0][JJ])
				{
				if(loop_active)				//deactivate loop
					{
					loop_active = 0;
					LOOP_OUT = 0;			
					}
				//SET_MEMORY_CUE_1(MEMORY_adr[0][JJ]);
				CUE_OPERATION = MEMORY_NEED_SET_PART2;	
				}
			else
				{
				CUE_OPERATION = 0;	
				}
				
			}
		else
			{
			CUE_OPERATION = 0;	
			}
			*/
		}	
	else if(CUE_OPERATION==MEMORY_NEED_PREVIOUS_SET)
		{
			/*
		if(numCuePoints>0)
			{
			JJ = numCuePoints-1;
			while(MEMORY_adr[0][JJ]>=(play_adr/294) && (JJ>0))
				{
				JJ--;	
				}
			if((play_adr/294)>MEMORY_adr[0][JJ])
				{
				if(loop_active)				//deactivate loop
					{
					loop_active = 0;
					LOOP_OUT = 0;			
					}	
				//SET_MEMORY_CUE_1(MEMORY_adr[0][JJ]);
				CUE_OPERATION = MEMORY_NEED_SET_PART2;	
				}
			else
				{
				CUE_OPERATION = 0;	
				}
			}
		else
			{
			CUE_OPERATION = 0;	
			}	
			*/		
		}	
	else if(CUE_OPERATION==MEMORY_NEED_SET_PART2 && ((end_adr_valid_data-start_adr_valid_data)>64))	
		{
		//SET_MEMORY_CUE_2();
		offset_adress = 0;		
		CUE_OPERATION = 0;
		}
  appStats.end(MAIN_LOOP);
}

	

FASTRUN void SAI_IRQHandler(void)
{

  I2S_TCSR_REG &= ~I2S_TCSR_FRIE;  // Disable interrupt temporarily

  uint16_t left = (SAMPLE[1] << 8) | SAMPLE[0];
  uint16_t right = (SAMPLE[3] << 8) | SAMPLE[2];
  

  __DSB();  // completes when all explicit memory accesses before this instruction complete
  
  I2S_TDR0_REG = (uint32_t)left << 16;
  I2S_TDR0_REG = (uint32_t)right << 16;

  
  //advancePosition_claude_optimized();
  advancePosition_rezo();
  //Serial.printf("play_adr: %ld, position: %ld, step_position: %ld, end_of_track: %d\n", play_adr, position, step_position, end_of_track);
	
  
  I2S_TCSR_REG |= 0x00040000;     // Clear error flag
  I2S_TCSR_REG |= I2S_TCSR_FRIE;  // Re-enable interrupt

}








FASTRUN void advancePosition_rezo() {
  if(((play_adr+step_position+3)<=(294*all_long)))						//change all_long extract!
			{
			end_of_track = 0;	
			}
		else
			{
			end_of_track = 1;		
			}			
		
		
	if(Tbuffer[19]&0x8 && ((slip_play_adr+((slip_position+pitch_for_slip)/10000))<(294*all_long)) && slip_play_enable)					//SLIP MODE ENABLE
		{
		slip_position+= pitch_for_slip;
		slip_play_adr+=slip_position/10000;	
		slip_position = slip_position%10000;	
		}
		
	position+= pitch;
	
	if(position>9999)	
		{
		step_position = position/10000;				
		if(reverse==0 && end_of_track==0)					
			{			
			play_adr+= step_position;	
			if(step_position==1)
				{
				LR[0][0] = LR[0][1];
				LR[1][0] = LR[1][1];
				LR[0][1] = LR[0][2];
				LR[1][1] = LR[1][2];
				LR[0][2] = LR[0][3];
				LR[1][2] = LR[1][3];					
				}
			else
				{
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
			}
		else if(reverse==1 && play_adr>=step_position)
			{
			play_adr-= step_position;
			if(step_position==1)
				{
				LR[0][0] = LR[0][1];
				LR[1][0] = LR[1][1];
				LR[0][1] = LR[0][2];
				LR[1][1] = LR[1][2];
				LR[0][2] = LR[0][3];
				LR[1][2] = LR[1][3];
				sdram_adr = (play_adr)&0xFFFFF;	
				LR[0][3] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][0];							
				LR[1][3] = PCM[(sdram_adr>>13)+offset_adress][sdram_adr&0x1FFF][1];		
				}
			else
				{
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

#if defined (TEENSY41)
if(Tbuffer[19]&0x8 && ((slip_play_adr+((slip_play_adr+pitch_for_slip)/10000))<(294*all_long)) && slip_play_enable)					//SLIP MODE ENABLE
		{
		slip_play_adr+= pitch_for_slip;
		slip_play_adr+=slip_play_adr/10000;	
		slip_play_adr = slip_play_adr%10000;	
		}	
#endif
  
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

#if defined(RDI_DEVELOPMENTS_REV3) || defined(TEENSY41)
    #define VOLUME_FACTOR 600 // 98%, 3277 is 90% reduced volume 
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


#if defined (TEENSY41)
uint8_t calculateCRC(uint8_t* data, size_t length) {
  uint16_t crc = 0;
  for (size_t i = 0; i < length; i++) {
    crc += data[i];
  }
  return crc%256;
}


void DMA2_Stream5_IRQHandler(void){	

		uint32_t ptch;
		uint8_t acc_t;
		if(CheckRXCRC() == 1) {
		
		if((Rbuffer[14]&0x1) && PLAY_BUTTON_pressed==0)										///////////PLAY button
			{
			if(lock_control==0)	
				{
				if(CUE_BUTTON_pressed==0)
					{
					if(play_enable)
						{
						play_enable = 0;		
						change_speed = NEED_DOWN;
						}
					else
						{
						if(Tbuffer[19]&0x8)					//SLIP MODE ENABLE
							{	
							play_adr = slip_play_adr;	
							change_speed = NO_CHANGE;
							slip_play_enable = 1;	
							}
						else if(CUE_ADR!=(play_adr/294) && (Rbuffer[12]&0x20)==0)			//when playback starts from any adress and touch disable
							{
							change_speed = NEED_UP;
							}
						else																//when playback starts from CUE adress
							{
							change_speed = NO_CHANGE;	
							}
						play_enable = 1;	
						}
					}
				else
					{
					keep_to_play = 1;	
					}
				}
			PLAY_BUTTON_pressed = 1;	
			}
		else if((Rbuffer[14]&0x1)==0 && PLAY_BUTTON_pressed==1)
			{
			PLAY_BUTTON_pressed = 0;	
			}
		else if((Rbuffer[14]&0x2) && CUE_BUTTON_pressed==0)										///////////CUE button
			{
			if(lock_control==0)	
				{	
				if(play_enable && ((Rbuffer[12]&0x20)==0))								//return to CUE, when track playing				play && touch disable
					{
					pitch = 0;	
					play_enable = 0;		
					if(Tbuffer[19]&0x8)					//SLIP MODE ENABLE
						{	
						slip_play_enable = 0;		
						slip_play_adr = 294*CUE_ADR;	
						}	
					CUE_OPERATION = CUE_NEED_CALL;			
					}	
				else if((play_enable==0) && (CUE_ADR!=(play_adr/294)))			//Set new CUE, when track stopped		
					{
					LOOP_OUT = 0;	
					CUE_OPERATION = CUE_NEED_SET;		
					}
				else if((play_enable==0) && (CUE_ADR==(play_adr/294)))				//return to CUE adress, when track stopped
					{
					change_speed = NO_CHANGE;	
					play_adr = 294*CUE_ADR;		
					if(Tbuffer[19]&0x8)					//SLIP MODE ENABLE
						{			
						slip_play_adr = play_adr;
						slip_play_enable = 1;	
						}	
					play_enable = 1;	
					}
				else if(play_enable && ((Rbuffer[12]&0x20)!=0) && (Tbuffer[19]&0x20))				//Set new CUE, when track played and press jog and JOG in Vinyl MODE
					{
					LOOP_OUT = 0;	
					CUE_OPERATION = CUE_NEED_SET;	
					play_enable = 0;		
					}
				}
			CUE_BUTTON_pressed = 1;	
			}
		else if((Rbuffer[14]&0x2)==0 && CUE_BUTTON_pressed==1)
			{
			if(lock_control==0)	
				{	
				if(keep_to_play==0)		//button play not pressed	
					{
					play_enable = 0;
					pitch = 0;		
					slip_play_enable = 0;	
					play_adr = 294*CUE_ADR;	
					if(Tbuffer[19]&0x8)					//SLIP MODE ENABLE
						{	
						slip_play_adr = play_adr;	
						}
		//			if((Tbuffer[19]&0x20)==0)   //CDJ mode
		//				{
		//				pitch = 0;	
		//				}
					}
				else
					{
					keep_to_play = 0;	
					}
				offset_adress = 0;																///	   temporary operation		
				}
			CUE_BUTTON_pressed = 0;	
			}
	/*
		else if((Rbuffer[14]&0x4) && REALTIME_CUE_BUTTON_pressed==0)										///////////REALTIME CUE button
			{
			if(lock_control==0)	
				{	
				if((play_enable==1) && (CUE_ADR!=(play_adr/294)) && loop_active==0)			//Set new CUE, when track play		
					{
					LOOP_OUT = 0;	
					CUE_OPERATION = CUE_NEED_SET;	
					}
				}
			REALTIME_CUE_BUTTON_pressed = 1;	
			}
		else if((Rbuffer[14]&0x4)==0 && REALTIME_CUE_BUTTON_pressed==1)
			{
			REALTIME_CUE_BUTTON_pressed = 0;	
			}		
			
			


		else if((Rbuffer[14]&0x08) && LOOP_OUT_BUTTON_pressed==0)										///////////LOOP OUT button
			{
			if(lock_control==0)	
				if(loop_active==0 && CUE_ADR<play_adr/294)
					{
					if(QUANTIZE && dSHOW==WAVEFORM)
						{
						if(((play_adr/294)>(BEATGRID[bars-1]+((BEATGRID[bars] - BEATGRID[bars-1])/2))) || bars==0)	
							{
							LOOP_OUT = BEATGRID[bars];								//next bar >> |
							}
						else
							{
							if(CUE_ADR==BEATGRID[bars-1])
								{
								LOOP_OUT = BEATGRID[bars];								//next bar >> |	
								}
							else	
								{
								LOOP_OUT = BEATGRID[bars-1];							//previous bar <<	|
								}
							CUE_OPERATION = CUE_NEED_CALL;		
							}	
						}
					else
						{
						LOOP_OUT = play_adr/294;
						CUE_OPERATION = CUE_NEED_CALL;	
						}	
					loop_active = 1;	
					}
			LOOP_OUT_BUTTON_pressed = 1;	
			}
		else if((Rbuffer[14]&0x08)==0 && LOOP_OUT_BUTTON_pressed==1)
			{
			LOOP_OUT_BUTTON_pressed = 0;	
			}			
		else if((Rbuffer[14]&0x10) && RELOOP_BUTTON_pressed==0)										///////////RELOOP button
			{
			if(lock_control==0)	
				{		
				if(loop_active)
					{
					loop_active = 0;			
					}
				else if(loop_active==0 && CUE_ADR<LOOP_OUT)
					{
					loop_active = 1;	
					}
				if(dSHOW==WAVEFORM)							//Redraw cue on dynamic waveform
					{
					//forcibly_redraw = 1;
					}		
				}	
			RELOOP_BUTTON_pressed = 1;	
			}
		else if((Rbuffer[14]&0x10)==0 && RELOOP_BUTTON_pressed==1)
			{
			RELOOP_BUTTON_pressed = 0;	 
			}		
	*/	

			else if((Rbuffer[14]&0x4) && REALTIME_CUE_BUTTON_pressed==0)                       // LOOP IN button
				{
				if(lock_control==0)	
					{	
					if(loop_active)
						{
						loop_in_active_pressed = !loop_in_active_pressed;
						}
					else
						{
						if((play_enable==1) && (CUE_ADR!=(play_adr/294)))
							{
							LOOP_OUT = 0;	
							CUE_OPERATION = CUE_NEED_SET;
							loop_pending = 1;
							}
						}
					}
				REALTIME_CUE_BUTTON_pressed = 1;	
				}
			else if((Rbuffer[14]&0x4)==0 && REALTIME_CUE_BUTTON_pressed==1)
				{
				REALTIME_CUE_BUTTON_pressed = 0;	
				}		

			else if((Rbuffer[14]&0x08) && LOOP_OUT_BUTTON_pressed==0)                          // LOOP OUT button
				{
				if(lock_control==0)	
					{
					if(loop_active)
						{
						loop_out_active_pressed = !loop_out_active_pressed;
						}
					else if(CUE_ADR<play_adr/294)
						{
						if(QUANTIZE && dSHOW==WAVEFORM)
							{
							if(((play_adr/294)>(BEATGRID[bars-1]+((BEATGRID[bars] - BEATGRID[bars-1])/2))) || bars==0)	
								{
								LOOP_OUT = BEATGRID[bars];
								}
							else
								{
								if(CUE_ADR==BEATGRID[bars-1])
									{
									LOOP_OUT = BEATGRID[bars];
									}
								else	
									{
									LOOP_OUT = BEATGRID[bars-1];
									}
								CUE_OPERATION = CUE_NEED_CALL;		
								}	
							}
						else
							{
							LOOP_OUT = play_adr/294;
							CUE_OPERATION = CUE_NEED_CALL;	
							}
						loop_pending = 0;
						loop_active = 1;	
						}
					}
				LOOP_OUT_BUTTON_pressed = 1;	
				}
			else if((Rbuffer[14]&0x08)==0 && LOOP_OUT_BUTTON_pressed==1)
				{
				LOOP_OUT_BUTTON_pressed = 0;	
				}			

			else if((Rbuffer[14]&0x10) && RELOOP_BUTTON_pressed==0)                            // RELOOP button
				{
				if(lock_control==0)	
					{		
					if(loop_active)
						{
						loop_active = 0;
						loop_in_active_pressed = 0;    // clear toggle states
						loop_out_active_pressed = 0;
						}
					else if(loop_active==0 && CUE_ADR<LOOP_OUT)
						{
						loop_active = 1;	
						}
					if(dSHOW==WAVEFORM)
						{
						//forcibly_redraw = 1;
						}		
					}	
				RELOOP_BUTTON_pressed = 1;	
				}
			else if((Rbuffer[14]&0x10)==0 && RELOOP_BUTTON_pressed==1)
				{
				RELOOP_BUTTON_pressed = 0;	 
				}
						
		if((Rbuffer[12]&0x2)==0 && REVERSE_SWITCH_pressed==0)					///////////reverse switch position
			{
			Tbuffer[17] |= 0x20;					//enable red led reverse
			if(slip_mode==1)						//SLIP+REVERSE MODE
				{
				if(Tbuffer[19]&0x8)			//SLIP MODE ENABLE
					{
					keep_slip = 1;	
					}
				else
					{
					Tbuffer[19] |= 0x8;	
					if(play_enable)
						{
						slip_play_enable = 1;	
						}
					slip_play_adr = play_adr; 	
					}					
				}
			REVERSE_SWITCH_pressed = 1;	
			}
		else if((Rbuffer[12]&0x2)!=0 && REVERSE_SWITCH_pressed==1)
			{				
			if(Tbuffer[19]&0x8)					//SLIP MODE ENABLE
				{	
				play_adr = slip_play_adr;	
				}		
			if(keep_slip)
				{
				keep_slip = 0;	
				}
			else if(slip_mode==1)					//SLIP MODE OFF
				{
				slip_play_enable = 0;	
				Tbuffer[19] &= 0xF7;	
				}
			Tbuffer[17] &= 0xDF;					//disable red led reverse
			REVERSE_SWITCH_pressed = 0;		
			}	
			
			
			///////////////////////////////////////////////////JOG MECHANICAL PROCESS///////////////////////////////////////////////
			
		if(inertial_rotation)
			{
			if(((Tbuffer[17]&0x20)==0 && (((Rbuffer[12]&0xC0)==0) || ((Rbuffer[12]&0x40) && pitch<potenciometer_tempo))) || 
				((Tbuffer[17]&0x20) && (((Rbuffer[12]&0xC0)==0x40) || ((Rbuffer[12]&0x40)==0 && pitch<potenciometer_tempo))))		//if rotation foward and stopped
				{
				inertial_rotation = 0;	
				}
			}	

			
		if(play_enable || (need_call_to_cue==3 && ((Rbuffer[12]&0x80)==0 || (Rbuffer[12]&0x20)!=0)))			//touch disable && rotation disable or play enable
			{
			need_call_to_cue = 0;	
			}	
		else if((Rbuffer[12]&0x20)==0 && need_call_to_cue==2)
			{
			pitch = 0;	
			play_adr = 294*CUE_ADR;	
			need_call_to_cue = 3;	
			}
		
		
		if(((Rbuffer[12]&0x20)!=0 || (play_enable==0 && (CUE_ADR!=(play_adr/294))) || inertial_rotation) && (Tbuffer[19]&0x20 && !loop_out_active_pressed && !loop_in_active_pressed))				/////////////(touch enable	|| play_enable==0) && Vinyl mode enable
			{
			pitch_for_slip = potenciometer_tempo;	
			
			if(JOG_PRESSED==0)
				{
				if((Rbuffer[12]&0x20)!=0) 			//touch enable
					{
					change_speed = NEED_DOWN;	
					JOG_PRESSED = 2;	
					}
				else
					{
			
					JOG_PRESSED = 1;
					}				
				}	
				
			if(play_enable==0)	
				{
				if((Rbuffer[12]&0x20)!=0 && (CUE_ADR==(play_adr/294)) && need_call_to_cue==0)				//touch enable + play_enable==0 + CUE_ADR==(play_adr/294)
					{
					need_call_to_cue = 1;	
					}		
				//if(need_call_to_cue==1 && (Rbuffer[12]&0x20)==0 && play_enable==0 && (Rbuffer[12]&0x80)==0)			//touch disable		//problem on XDJ-RX2
				else if(need_call_to_cue==1 && (Rbuffer[12]&0x20)==0)			//touch disable	_/
					{
					need_call_to_cue = 2;	
					}					
				}
				
			if(Rbuffer[12]&0x80)					//rotation detect
				{
				if(need_call_to_cue<2)
					{					
					change_speed = NO_CHANGE;	
					
					if((Rbuffer[12]&0x40) && end_of_track)				//foward rotation + end_of_track
						{
						pitch = 0;
						change_speed = NO_CHANGE;	
						}
					else
						{
						ptch = (256*Rbuffer[10]+Rbuffer[11]);
						if(ptch<86)
							{
							ptch = 86;	
							}
						ptch = 5574324/ptch;
						pitch = ptch;						
						}
					pitch_for_slip = potenciometer_tempo; 	
					inertial_rotation = 1;						
					}
				}		
			else if(change_speed==NO_CHANGE) 
				{			
				pitch = 0;	
				}

			if(change_speed==NO_CHANGE)
				{	
				if(Rbuffer[12]&0x40)				//foward/reverse rotation
					{
					reverse = 0;
					}
				else
					{
					reverse = 1;	
					}		
				}
			else
				{
				if(Tbuffer[17]&0x20)					//reverse diode enable
					{
					reverse = 1;
					}
				else
					{
					reverse = 0;	
					}	
				}
			Tbuffer[23] |= 0x20;				//touch enable circle on display		
			}
		else if((Rbuffer[12]&0xA0)==0 && !loop_out_active_pressed && !loop_in_active_pressed)				///////////////////////touch disable and rotation disable
			{			
			pitch_for_slip = potenciometer_tempo;
			if(JOG_PRESSED>0) //jog PRESSED -> UNPRESSED
				{
				if(Tbuffer[19]&0x8)					//SLIP MODE ENABLE
					{	
					change_speed = NO_CHANGE;	
					}
				else if(JOG_PRESSED==2 && play_enable)
					{
					change_speed = NEED_UP;	
					}					
				JOG_PRESSED = 0;	
				}
			if(play_enable)
				{	
				if(end_of_track && (Tbuffer[17]&0x20)==0)					//stop on end (to remove noise at the end of the track)
					{
					change_speed = NO_CHANGE;	
					pitch = 0;	
					}	
				else if(change_speed==NO_CHANGE)
					{
					pitch = potenciometer_tempo;	
					}
			
				if(Tbuffer[17]&0x20)					//reverse diode enable
					{
					reverse = 1;
					}
				else
					{
					reverse = 0;	
					}		
				}
			if(Tbuffer[23]&0x20)					//jog UNPRESSED
				{
				if(Tbuffer[19]&0x8)					//SLIP MODE ENABLE
					{	
					play_adr = slip_play_adr;	
					}	
				Tbuffer[23] &= 0xDF;				//disable touch circle on display
				}	
			}
		else if((Rbuffer[12]&0x80) && play_enable && !loop_out_active_pressed && !loop_in_active_pressed)						//rotation detected			(pitch bend)	
			{
			if(end_of_track==0)
				{
				ptch = (256*Rbuffer[10]+Rbuffer[11]);
				if(ptch>139)
					{
					ptch = ptch-139;	
					}
				else
					{
					ptch = 1;	
					}
				ptch = 150000/ptch;
					
				if(ptch>4225)
					{
					ptch = 4225;	
					}	
				if((Rbuffer[12]&0x40 && (Tbuffer[17]&0x20)==0) || ((Rbuffer[12]&0x40)==0 && Tbuffer[17]&0x20))		//foward rotation and reverse off OR reverse rotation and reverse on (pitch bend)			
					{
					ptch+= potenciometer_tempo;
					if(ptch>20000)
						{
						ptch = 20000;	
						}
					pitch = ptch;	
					}
				else if(((Rbuffer[12]&0x40)==0 && (Tbuffer[17]&0x20)==0) || (Rbuffer[12]&0x40 && Tbuffer[17]&0x20))	 //reverse rotation and reverse off OR foward rotation and reverse on(pitch bend)	
					{
					if(ptch<potenciometer_tempo)
						{
						pitch = potenciometer_tempo - ptch;
						}
					else
						{
						pitch = 0;	
						}
					}		
				}
			else
				{
				pitch = 0;		
				}			
				
			if(Tbuffer[17]&0x20)					//reverse diode enable
				{
				reverse = 1;
				}
			else
				{
				reverse = 0;	
				}	
			if(Tbuffer[23]&0x20)					//jog UNPRESSED
				{
				if(Tbuffer[19]&0x8)					//SLIP MODE ENABLE
					{	
					play_adr = slip_play_adr;	
					}	
				Tbuffer[23] &= 0xDF;				//disable touch circle on display	
				}	
			else
				{
				pitch_for_slip = pitch;		
				}
			}

			else if((Rbuffer[12]&0x80) && play_enable && (loop_out_active_pressed || loop_in_active_pressed))						//rotation stop detected			(pitch bend return)	
			{

				static uint16_t jog_pulse_prev = 0;		// accumulate fractional ticks to allow for slower jog speeds
				if(loop_in_active_pressed || loop_out_active_pressed)
    			{
				uint16_t jog_pulse = 256*Rbuffer[8]+Rbuffer[9];

				if(jog_pulse != jog_pulse_prev)
					{
					if(Rbuffer[12]&0x40)       // reverse
						{
						if(loop_in_active_pressed && (CUE_ADR<LOOP_OUT))
							CUE_ADR++;
						if(loop_out_active_pressed)
							LOOP_OUT++;
						
						}
					else                       // forward
						{
						if(loop_in_active_pressed && CUE_ADR > 0 )
							CUE_ADR--;
						if(loop_out_active_pressed && LOOP_OUT > 0 && (LOOP_OUT>CUE_ADR))
							LOOP_OUT--;
						}

					jog_pulse_prev = jog_pulse;
					}
				}
			}
			

		if(dma_cnt<5)									////////////////////////////////////////cicle divider////////////////////////////
			{
			dma_cnt++;		
			previous_adc_pitch+= (Rbuffer[7]+256*Rbuffer[6]);	
			previous_adc_DD+= (Rbuffer[5]+256*Rbuffer[4]);
			if(dma_cnt==1)
				{
				if(lock_control==0)	
					{	
					acc_t = Rbuffer[2]>>1;
					if(acc_t<4)
						{
						acceleration_DOWN = LOG_TABLE[0];	
						}
					else if(acc_t<12)
						{
						acceleration_DOWN = ((LOG_TABLE[1]*(acc_t-3)+LOG_TABLE[0]*(11-acc_t))>>3);	
						}
					else if(acc_t<28)
						{
						acceleration_DOWN = ((LOG_TABLE[2]*(acc_t-11)+LOG_TABLE[1]*(27-acc_t))>>4);	
						}
					else if(acc_t<44)
						{
						acceleration_DOWN = ((LOG_TABLE[3]*(acc_t-27)+LOG_TABLE[2]*(43-acc_t))>>4);	
						}
					else if(acc_t<76)
						{
						acceleration_DOWN = ((LOG_TABLE[4]*(acc_t-43)+LOG_TABLE[3]*(75-acc_t))>>5);	
						}
					else if(acc_t<92)
						{
						acceleration_DOWN = ((LOG_TABLE[5]*(acc_t-75)+LOG_TABLE[4]*(91-acc_t))>>4);	
						}
					else if(acc_t<108)
						{
						acceleration_DOWN = ((LOG_TABLE[6]*(acc_t-91)+LOG_TABLE[5]*(107-acc_t))>>4);	
						}	
					else if(acc_t<124)
						{
						acceleration_DOWN = ((LOG_TABLE[7]*(acc_t-107)+LOG_TABLE[6]*(123-acc_t))>>4);	
						}			
					else
						{
						acceleration_DOWN = 2;	
						}
					}
				}
			else if(dma_cnt==2)
				{
				if((Rbuffer[16]&0x4) && TRACK_NEXT_BUTTON_pressed==0) 							///////////TRACK NEXT Button
					{
					if(lock_control==0)	
						{	
						track_need_load = 1;
						}
					TRACK_NEXT_BUTTON_pressed = 1;	
					}
				else if((Rbuffer[16]&0x4)==0 && TRACK_NEXT_BUTTON_pressed==1)	
					{
					TRACK_NEXT_BUTTON_pressed = 0;	
					}	
				else if((Rbuffer[16]&0x2) && TRACK_PREVIOUS_BUTTON_pressed==0) 			///////////TRACK PREVIOUS Button
					{
					if(lock_control==0)	
						{	
						track_need_load = 2;
						}
					TRACK_PREVIOUS_BUTTON_pressed = 1;	
					}
				else if((Rbuffer[16]&0x2)==0 && TRACK_PREVIOUS_BUTTON_pressed==1)	
					{
					TRACK_PREVIOUS_BUTTON_pressed = 0;	
					}	
				else if((Rbuffer[18]&0x20) && TIME_MODE_BUTTON_pressed==0) 					///////////TIME MODE Button
					{	
					TIME_MODE_BUTTON_pressed = 1;	
					}
				else if((Rbuffer[18]&0x20)==0 && TIME_MODE_BUTTON_pressed==1)	
					{
					if(REMAIN_ENABLE)
						{
						REMAIN_ENABLE = 0;	
						}
					else
						{
						REMAIN_ENABLE = 1;	
						}
					time_mode_need_update = 1;		
					TIME_MODE_BUTTON_pressed = 0;	
					}	
				else if((Rbuffer[18]&0x40) && QUANTIZE_BUTTON_pressed==0) 					///////////QUANTIZE Button
					{
						Serial.println("QUANTIZE Button Pressed");
					if(QUANTIZE)
						{
						QUANTIZE = 0;	
						}
					else
						{
						QUANTIZE = 1;	
						}	
					quantize_mode_need_update = 1;	
					QUANTIZE_BUTTON_pressed = 1;	
					}
				else if((Rbuffer[18]&0x40)==0 && QUANTIZE_BUTTON_pressed==1)	
					{
						Serial.println("QUANTIZE Button Released");		
					QUANTIZE_BUTTON_pressed = 0;	
					}
				}
			else if(dma_cnt==3)
				{
				if(lock_control==0)	
					{	
					acc_t = Rbuffer[3]>>1;
					if(acc_t<4)
						{
						acceleration_UP = LOG_TABLE[0];	
						}
					else if(acc_t<12)
						{
						acceleration_UP = ((LOG_TABLE[1]*(acc_t-3)+LOG_TABLE[0]*(11-acc_t))>>3);	
						}
					else if(acc_t<28)
						{
						acceleration_UP = ((LOG_TABLE[2]*(acc_t-11)+LOG_TABLE[1]*(27-acc_t))>>4);	
						}
					else if(acc_t<44)
						{
						acceleration_UP = ((LOG_TABLE[3]*(acc_t-27)+LOG_TABLE[2]*(43-acc_t))>>4);	
						}
					else if(acc_t<76)
						{
						acceleration_UP = ((LOG_TABLE[4]*(acc_t-43)+LOG_TABLE[3]*(75-acc_t))>>5);	
						}
					else if(acc_t<92)
						{
						acceleration_UP = ((LOG_TABLE[5]*(acc_t-75)+LOG_TABLE[4]*(91-acc_t))>>4);	
						}
					else if(acc_t<108)
						{
						acceleration_UP = ((LOG_TABLE[6]*(acc_t-91)+LOG_TABLE[5]*(107-acc_t))>>4);	
						}	
					else if(acc_t<124)
						{
						acceleration_UP = ((LOG_TABLE[7]*(acc_t-107)+LOG_TABLE[6]*(123-acc_t))>>4);	
						}			
					else
						{
						acceleration_UP = 2;	
						}
					}
				}	
			else if(dma_cnt==4)
				{
				if((Rbuffer[16]&0x10) && SEARCH_FF_BUTTON_pressed==0) 							///////////SEARCH FF>> Button
					{
					if(lock_control==0)	
						{	
						if(play_enable & play_adr<(all_long+100000))	
							{
							SEEK_AUDIOFRAME(play_adr+100000);	
							}
						else if(play_enable==0 & play_adr/294<(all_long+1))
							{	
							play_adr+=294;
							}
						}
					SEARCH_FF_BUTTON_pressed = 1;	
					}
				else if((Rbuffer[16]&0x10)==0 && SEARCH_FF_BUTTON_pressed==1)	
					{
					SEARCH_FF_BUTTON_pressed = 0;	
					}	
				else if((Rbuffer[16]&0x8) && SEARCH_REW_BUTTON_pressed==0) 							///////////SEARCH <<REW Button
					{
					if(lock_control==0)	
						{		
						if(play_enable & play_adr>100000)	
							{
							SEEK_AUDIOFRAME(play_adr-100000);
							}
						else if(play_enable==0 & play_adr>294)
							{	
							play_adr-=294;
							}
						}	
					SEARCH_REW_BUTTON_pressed = 1;	
					}
				else if((Rbuffer[16]&0x8)==0 && SEARCH_REW_BUTTON_pressed==1)	
					{
					SEARCH_REW_BUTTON_pressed = 0;	
					}		

				if(track_play_now!=0)						//////////////////////////////LEDS/////////////////////////
					{
					Tbuffer[17] |= 0x2;				//CUE led on
						
					if(play_enable)
						{
						Tbuffer[17] |= 0x3;				//PLAY and CUE led on	
						}
					else											//Play led blink
						{	
						if(TIM_PLAY_LED)	
							{
							Tbuffer[17] |= 0x1;	
							}
						else
							{
							Tbuffer[17] &= ~0x01;// &= 0xFE;	
							}
						if(TIM_CUE_LED)	
							{
							Tbuffer[17] |= 0x2;	
							}
						else if(CUE_ADR!=play_adr/294)  
							{
							Tbuffer[17] &= 0xFD;	
							}	
						}
					/*	
					if(loop_active)
						{
						if(LOOP_LEDS_BLINK%4==0)	
							{
							Tbuffer[17] |= 0x0C;	
							}
						else if(LOOP_LEDS_BLINK%4==2)
							{
							Tbuffer[17] &= 0xF3;	
							}
						}	
					else
						{
						if(TIM_REALTIME_CUE_LED)	
							{
							Tbuffer[17] |= 0x0C;		
							}
						else
							{
							Tbuffer[17] &= 0xFB;	
							}				
						}
					*/
				
					if(loop_active)
   					{
					// Loop-in LED (bit 2)
					if(loop_in_active_pressed)
						Tbuffer[17] &= 0xFB;                   // solid off
					else
						{
						uint8_t in_phase = loop_out_active_pressed ? LOOP_LEDS_BLINK%2 : LOOP_LEDS_BLINK%4;
						if(in_phase == 0)
							Tbuffer[17] |= 0x04;
						else if(in_phase == 2)
							Tbuffer[17] &= 0xFB;
						}

					// Loop-out LED (bit 3)
					if(loop_out_active_pressed)
						Tbuffer[17] &= 0xF7;                   // solid off
					else
						{
						uint8_t out_phase = loop_in_active_pressed ? LOOP_LEDS_BLINK%2 : LOOP_LEDS_BLINK%4;
						if(out_phase == 0)
							Tbuffer[17] |= 0x08;
						else if(out_phase == 2)
							Tbuffer[17] &= 0xF7;
						}
					}
				else if(loop_pending)
					{
					if(LOOP_LEDS_BLINK%4 == 0)
						Tbuffer[17] |= 0x04;
					else if(LOOP_LEDS_BLINK%4 == 2)
						Tbuffer[17] &= 0xFB;
					}
				else
					{
					if(TIM_REALTIME_CUE_LED)	
						Tbuffer[17] |= 0x0C;
					else
						Tbuffer[17] &= 0xFB;				
					}
					///Reloop LED (mk3 only)
					if(loop_active || CUE_ADR<LOOP_OUT)
						{
						Tbuffer[17] |= 0x10;							//RELOOP EXIT LED ON
						}
					else
						{
						Tbuffer[17] &= 0xEF;							//RELOOP EXIT LED OFF	
						}
					}
				}		
			else if(dma_cnt==5)
				{			
					/*
				if((Rbuffer[17]&0x4) && JOG_MODE_BUTTON_pressed==0) 							///////////Jog Mode button
					{
					if(Tbuffer[19]&0x20)			//VINYL => CDJ
						{
						Tbuffer[19] &= 0xDF;	
						Tbuffer[19] |= 0x40;
						Tbuffer[23] &= 0xE7;
						}	
					else												//CDJ => VINYL
						{
						Tbuffer[19] &= 0xBF;	
						Tbuffer[19] |= 0x20;
						Tbuffer[23] |= 0x18;	
						}
					JOG_MODE_BUTTON_pressed = 1;	
					}
				else if((Rbuffer[17]&0x4)==0 && JOG_MODE_BUTTON_pressed==1)	
					{
					JOG_MODE_BUTTON_pressed = 0;	
					}
					*/

					#define LONG_PRESS_THRESHOLD 200  // 500ms at 1kHz loop rate

				if ((Rbuffer[17] & 0x4) && JOG_MODE_BUTTON_pressed == 0)           // Button just pressed
					{
					JOG_MODE_BUTTON_pressed = 1;
					JOG_MODE_BUTTON_hold_ticks = 0;
					}
				else if ((Rbuffer[17] & 0x4) && JOG_MODE_BUTTON_pressed == 1)      // Button held
					{
					JOG_MODE_BUTTON_hold_ticks++;

					if (JOG_MODE_BUTTON_hold_ticks == LONG_PRESS_THRESHOLD)         // Long press detected
						{
						display_mode ^= 1;
						//Serial.print("Display mode: ");
						//Serial.println(display_mode ? "ALT" : "DEFAULT");
						Tbuffer[20] = display_mode? 0x01 : 0x00; // Set display mode in Tbuffer
						}
					}
				else if ((Rbuffer[17] & 0x4) == 0 && JOG_MODE_BUTTON_pressed == 1) // Button released
					{
					if (JOG_MODE_BUTTON_hold_ticks < LONG_PRESS_THRESHOLD)          // Short press — jog mode toggle
						{
						if (Tbuffer[19] & 0x20)        // VINYL => CDJ
							{
							Tbuffer[19] &= 0xDF;
							Tbuffer[19] |= 0x40;
							Tbuffer[23] &= 0xE7;
							}
						else                           // CDJ => VINYL
							{
							Tbuffer[19] &= 0xBF;
							Tbuffer[19] |= 0x20;
							Tbuffer[23] |= 0x18;
							}
						}
					JOG_MODE_BUTTON_pressed = 0;
					JOG_MODE_BUTTON_hold_ticks = 0;
					}
				else if((Rbuffer[17]&0x20) && TEMPO_RESET_BUTTON_pressed==0) 							///////////TEMPO RESET Button
					{
					if(Tbuffer[19]&0x10)				//ON_RESET => OFF_RESET
						{
						Tbuffer[19] &= 0xEF;
						}	
					else												//OFF_RESET => ON_RESET
						{
						Tbuffer[19] |= 0x10;
						}
					TEMPO_RESET_BUTTON_pressed = 1;	
					}
				else if((Rbuffer[17]&0x20)==0 && TEMPO_RESET_BUTTON_pressed==1)	
					{
					TEMPO_RESET_BUTTON_pressed = 0;	
					}	
				else if((Rbuffer[17]&0x8) && TEMPO_RANGE_BUTTON_pressed==0) 							///////////TEMPO RANGE Button
					{
					if(tempo_range<3)
						{
							Serial.printf("Tempo Range Increase: %d -> ", tempo_range);
						tempo_range++;
						}
					else
						{
						tempo_range = 0;	
						}
					tempo_range_need_update = 1;	
					TEMPO_RANGE_BUTTON_pressed = 1;	
					}
				else if((Rbuffer[17]&0x8)==0 && TEMPO_RANGE_BUTTON_pressed==1)	
					{
					TEMPO_RANGE_BUTTON_pressed = 0;	
					}	
				else if((Rbuffer[17]&0x1) && CALL_NEXT_BUTTON_pressed==0) 							///////////CALL NEXT Button	>
					{
					if(lock_control==0)	
						{		
						CUE_OPERATION = MEMORY_NEED_NEXT_SET;
						}
					CALL_NEXT_BUTTON_pressed = 1;	
					}
				else if((Rbuffer[17]&0x1)==0 && CALL_NEXT_BUTTON_pressed==1)	
					{
					CALL_NEXT_BUTTON_pressed = 0;	
					}
				else if((Rbuffer[17]&0x2) && CALL_PREVIOUS_BUTTON_pressed==0) 							///////////CALL PREVIOUS Button <
					{
					if(lock_control==0)	
						{		
						CUE_OPERATION = MEMORY_NEED_PREVIOUS_SET;
						}
					CALL_PREVIOUS_BUTTON_pressed = 1;	
					}
				else if((Rbuffer[17]&0x2)==0 && CALL_PREVIOUS_BUTTON_pressed==1)	
					{
					CALL_PREVIOUS_BUTTON_pressed = 0;	
					}
				
				else if((Rbuffer[18] & 0x1) && WAVE_NEXT_BUTTON_pressed == 0)              /////////// WAVE NEXT Button >
				{
					if(lock_control == 0)
					{
						if(dynamicWaveformZOOM < 16)
						{
							dynamicWaveformZOOM *= 2;
						}
					}
					WAVE_NEXT_BUTTON_pressed = 1;
				}
				else if((Rbuffer[18] & 0x1) == 0 && WAVE_NEXT_BUTTON_pressed == 1)
				{
					WAVE_NEXT_BUTTON_pressed = 0;
				}
				else if((Rbuffer[18] & 0x2) && WAVE_PREVIOUS_BUTTON_pressed == 0)          /////////// WAVE PREVIOUS Button 
				{
					if(lock_control == 0)
					{
						if(dynamicWaveformZOOM > 1)
						{
							dynamicWaveformZOOM /= 2;
						}
					}
					WAVE_PREVIOUS_BUTTON_pressed = 1;
				}
				else if((Rbuffer[18] & 0x2) == 0 && WAVE_PREVIOUS_BUTTON_pressed == 1)
				{
					WAVE_PREVIOUS_BUTTON_pressed = 0;
				}	

				if((Rbuffer[17]&0x10) && SLIP_MODE_BUTTON_pressed==0) 							///////////SLIP MODE Button
					{
					if(Tbuffer[19]&0x8)					//ON_SLIP_MODE => OFF_SLIP_MODE
						{
						if(slip_mode==1 && keep_slip==0 && (Tbuffer[17]&0x20))
							{
							keep_slip = 1;
							 			
							}
						else
							{	
							if(keep_slip)
								{
								keep_slip = 0;	
								}
							else
								{
								slip_play_enable = 0;	
								Tbuffer[19] &= 0xF7;
						
								}									
							}							
						}	
					else												//OFF_SLIP_MODE => ON_SLIP_MODE
						{	
						Tbuffer[19] |= 0x8;	
						if(play_enable)
							{
							slip_play_enable = 1;	
							}
						slip_play_adr = play_adr; 	
						}
					SLIP_MODE_BUTTON_pressed = 1;	
					}
				else if((Rbuffer[17]&0x10)==0 && SLIP_MODE_BUTTON_pressed==1)	
					{
					SLIP_MODE_BUTTON_pressed = 0;	
					}		
				}			
			}
		else
			{
			dma_cnt = 0;	
			if(Tbuffer[19]&0x10)				//TEMPRO RESET ON
				{
				potenciometer_tempo = 10000;	
				}
			else				//////////////////////////////////////////TEMPO CALCULATION
				{	
				previous_adc_pitch = previous_adc_pitch/5;
				previous_adc_DD = previous_adc_DD/5;
				if(previous_adc_pitch>(pitch_center+64) || (64+previous_adc_pitch)<pitch_center)		//
					{																																									//
					pitch_center = previous_adc_pitch;																								//
					}																																									//
				if(previous_adc_DD>DD+64 || 64+previous_adc_DD<DD)																	/////fluctuating filter with hysteresis
					{																																									//
					DD = previous_adc_DD;																															//
					}																																									//
				if(((pitch_center+128)>DD) && ((pitch_center-128)<DD))	
					{
					potenciometer_tempo = 10000;	
					}
				else if((pitch_center+128)<=DD)					/////pitch>0%
					{
					if(tempo_range==0)										//	6%
						{
						potenciometer_tempo = 2*((DD - pitch_center - 128)/108);
						if(potenciometer_tempo>600)
							{
							potenciometer_tempo = 600;	
							}	
						}	
					else if(tempo_range==1)										//	10%	
						{
						potenciometer_tempo = 5*((DD - pitch_center - 128)/162);
						if(potenciometer_tempo>1000)
							{
							potenciometer_tempo = 1000;	
							}
						}	
					else if(tempo_range==2)										//	16%	
						{
						potenciometer_tempo = 5*((DD - pitch_center - 128)/101);
						if(potenciometer_tempo>1600)
							{
							potenciometer_tempo = 1600;	
							}
						}	
					else																		//	WIDE	
						{
						potenciometer_tempo = 50*((DD - pitch_center - 128)/162);
						if(potenciometer_tempo>10000)
							{
							potenciometer_tempo = 10000;	
							}
						}	
					potenciometer_tempo = 10000 + potenciometer_tempo;	
					}
				else if((pitch_center-128)>=DD)				/////pitch<0%
					{
					if(tempo_range==0)										//	6%	
						{	
						potenciometer_tempo = 2*((pitch_center - 128 - DD)/108);
						if(potenciometer_tempo>600)
							{
							potenciometer_tempo = 600;	
							}
						}	
					else if(tempo_range==1)										//	10%	
						{	
						potenciometer_tempo = 5*((pitch_center - 128 - DD)/162);
						if(potenciometer_tempo>1000)
							{
							potenciometer_tempo = 1000;	
							}
						}
					else if(tempo_range==2)										//	16%	
						{	
						potenciometer_tempo = 5*((pitch_center - 128 - DD)/101);
						if(potenciometer_tempo>1600)
							{
							potenciometer_tempo = 1600;	
							}
						}	
					else																			//	WIDE
						{	
						potenciometer_tempo = 50*((pitch_center - 128 - DD)/162);
						if(potenciometer_tempo>10000)
							{
							potenciometer_tempo = 10000;	
							}
						}
					potenciometer_tempo = 10000 - potenciometer_tempo;	
					}				
				}
			if(previous_potenciometer_tempo != potenciometer_tempo)
				{
				previous_potenciometer_tempo = potenciometer_tempo;	
				tempo_need_update = 1;
				}
			previous_adc_DD = 0;
			previous_adc_pitch = 0;	
			}
				
			
		if(load_animation_enable)
			{
			//Tbuffer[21] = 0;					//disable red cue marker
			//Tbuffer[23] &= 0xDF;			//disable touch circle on display		
			//Tbuffer[25] = 137;				//command load animation
			}
		else if(track_play_now==0)
			{
			//Tbuffer[25] = 0;
			//Tbuffer[21] = 0;	
			//Tbuffer[17] &= 0xFC;			//PLAY & CUE leds off
			//Tbuffer[23] &= 0xDF;				//disable touch circle on display		
			}
		else
			{
			if(TMPSLP)
				{
				Tbuffer[21] = RED_CIRCLE_CUE_ADR;		
				TMPSLP = 0;	
				}
			else
				{
				if(Tbuffer[19]&0x08)					//SLIP MODE ENABLE
					{	
					zi = ((slip_play_adr/588)%135)+1;	
					Tbuffer[21] = (1000*zi/1589)+1;	
					}
				TMPSLP = 1;	
				}	
			zi = ((play_adr/588)%135)+1;		
			Tbuffer[25] = zi;
			}

		}
			CheckTXCRC();
			/*
			Serial.printf ("\nRECEIVED ");
			
			for (int i=0; i<27; i++)
			{
				Serial.printf ("%02x ", Rbuffer[i]);
			}
			Serial.printf ("\nSENT     ");
			for (int i=0; i<27; i++)
			{
				Serial.printf ("%d ", Tbuffer[i]);
			}
			Serial.printf ("\n");
			*/
	
			SPI_SLAVE.prepare_for_slave_transfer(Tbuffer, Rbuffer, 27);
			SPI_SLAVE.flag_transaction_completed = 0;
}	

	


void ledTIMER()
	{	
	if(change_speed==NEED_UP)
		{
		if(end_of_track)
			{
			change_speed = NO_CHANGE;
			pitch = 0;			
			}
		else
			{
			if(pitch<potenciometer_tempo-acceleration_UP)
				{
				pitch+=acceleration_UP;	
				}
			else
				{
				change_speed = NO_CHANGE;
				pitch = potenciometer_tempo;	
				}		
			}
		}
	else if(change_speed==NEED_DOWN)
		{
		if(end_of_track)
			{
			change_speed = NO_CHANGE;
			pitch = 0;			
			}
		else	
			{
			if(pitch>acceleration_DOWN)
				{
				pitch-=acceleration_DOWN;	
				}
			else
				{
				change_speed = NO_CHANGE;
				pitch = 0;	
				}		
			}
		}
				
	if(timer_time<124)
		{
		timer_time++;	
		}
	else
		{
		timer_time = 0;
				
		if(REALTIME_CUE_LED_BLINK<16)
			{
			if(REALTIME_CUE_LED_BLINK%4==0)
				{
				TIM_REALTIME_CUE_LED = 0;
				}
			else if(REALTIME_CUE_LED_BLINK%4==2)
				{
				TIM_REALTIME_CUE_LED = 1;
				}
			REALTIME_CUE_LED_BLINK++;	
			}
			
		LOOP_LEDS_BLINK++;					//loop leds	
		
	
 if(LED_SD_timer<7)
			{
			LED_SD_timer++;		
			if(LED_SD_timer==4)
				{
				TIM_PLAY_LED = 1;
				TIM_CUE_LED = 0;		
				}
			else if(LED_SD_timer==2 || LED_SD_timer==6)
				{
				TIM_CUE_LED = 1;					
				}	
			}
		else
			{
			if(track_play_now==0)
				{
				TIM_PLAY_LED = 0;	
				}
			else
				{
				TIM_PLAY_LED = 0;	
				TIM_CUE_LED = 0;	
				}
			LED_SD_timer = 0;	
			}
		}
}
////////////////////////////////


/////////////////////////////////////////////	
//CAL CUE,seek audio frame
//
//
//

void SEEK_AUDIOFRAME(uint32_t seek_adr)
	{
	if(seek_adr>(294*all_long))
		{
		return;	
		}
	seek_adr &= 0xFFFFE000;	

	if(playFile.seek(((seek_adr<<2)+44)))
		{
		end_adr_valid_data = (seek_adr>>13);
		start_adr_valid_data = end_adr_valid_data; 	
		play_adr = seek_adr;	
		if(Tbuffer[19]&0x8)					//SLIP MODE ENABLE
			{	
			slip_play_adr = play_adr;	
			}			
		}
	return;	
	}


void CALL_CUE(void)
	{
	uint32_t seek_adr = 294*CUE_ADR;
	seek_adr &= 0xFFFFE000;	
	if(playFile.seek(((seek_adr<<2)+44)))
		{
		end_adr_valid_data = (seek_adr>>13);
		start_adr_valid_data = end_adr_valid_data; 	
		play_adr = 294*CUE_ADR;		
		if(Tbuffer[19]&0x8)					//SLIP MODE ENABLE
			{	
			slip_play_adr = play_adr;	
			}			
		}
	offset_adress = 128-mem_offset_adress;	
	}
	
void SET_CUE(uint32_t nf_adr)
	{
	uint16_t c_adr;	
	uint32_t copy_cnt = 0;				//internal counter
	uint32_t AIS = 0;							//adress in samples 44k for CUE	
		
	CUE_ADR = nf_adr;	
	AIS = (294*CUE_ADR)&0xFFFFE000;							//rounding up to 8192
	AIS-=81920;
	mem_offset_adress = (AIS&0xFFFFF)>>13;
		
	for(copy_cnt=0;copy_cnt<327680;copy_cnt++)
		{
		PCM[(copy_cnt>>14)+128][(copy_cnt>>1)&0x1FFF][copy_cnt&0x01] = PCM[((copy_cnt>>14)+mem_offset_adress)&0x7F][(copy_cnt>>1)&0x1FFF][copy_cnt&0x01];	
		}
	//DrawCueMarker(1+(400*CUE_ADR/all_long));	
	c_adr = ((nf_adr/2)%135)+1;	
	RED_CIRCLE_CUE_ADR = (1000*c_adr/1589)+1;
	if(dSHOW==WAVEFORM)							//Redraw cue on dynamic waveform
		{
		//forcibly_redraw = 1;
		}	
	REALTIME_CUE_LED_BLINK = 0;					//start blink led
	return;	
	}	

void g_process_serial_rx (uint16_t x)
{
    OurSerial1WireT4toT4dataExch.process_serial_rx(x);
}

void serialTimerISR()
{
	OurSerial1WireT4toT4dataExch.go_send_PZDsendBuffer (MasterAddress, SlaveAddress);
}


//////////////////////////////////////
//Function Checksum for TX package	
//
void CheckTXCRC()
	{
	uint8_t sdata = 141;
	uint8_t bt = 17;
	while(bt<26)
		{
		sdata+=Tbuffer[bt];	
		bt++;	
		}
	Tbuffer[26] = sdata;
	return;	
	}
	
	
//////////////////////////////////////
//Function Checksum for RX package	
//
uint8_t CheckRXCRC(void)
	{
	if(Rbuffer[0]==1 &&	Rbuffer[1]==16 && Rbuffer[25]==0 && Rbuffer[26]==0)
		{
		return 1;	
		}
	else
		{
		return 0;	
		}
	}

#endif