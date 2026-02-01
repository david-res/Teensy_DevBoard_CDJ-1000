#ifndef PIN_DEFINES_H
#define PIN_DEFINES_H

/////////////////
//Pin definitions
/////////////////
#if defined(NT35510)
#define TFT_BL            0     // 0 - Backlight pin for TFT
#endif

#define TFT_BL            0     // 0 - Backlight pin for TFT
#define TSR_CS           1     // 1 - XPT2046 SPI touchscreen chip select
#define TFT_WR            36     // 8 - Write-strobe pin
#define TFT_RD           37     //37 - Read-strobe pin  (-1 to disable, tie to 3.3v)
#define TFT_CS           -1     //-1 - Chip select pin for TFT display. -1 to disable, tie to ground
#define TFT_RST           34     // 9 - Hardware reset pin for TFT. Set to -1 for software reset only
#define TFT_DC           35     //10 - Data/command pin for TFT
#define SPI_MOSI         11     //11 - Documentation purposes, hardwired SPI MOSI
#define SPI_MISO         12     //12 - Documentation purposes, hardwired SPI MISO
#define SPI_SCLK         13     //13 - Documentation purposes, hardwired SPI CLK

#if defined(SSD1963) 
#define TFT_TEAR         33     //30 - Tearing pin for SSD1963 (BuyDisplay)
#endif

//GPIO6 - bit 2 - pin 01, bit 3 - pin 00, bit 12 - pin 24, bit 13 - pin 25
//GPIO6 - bits 16..23, for 8 bit parallel
#define TFT_D0           19     //04  -> GPIO9-06   |   GIPO6-16  ->  19
#define TFT_D1           18     //05  -> GPIO9-08   |   GIPO6-17  ->  18
#define TFT_D2           14     //06  -> GPIO7-10   |   GIPO6-18  ->  14
#define TFT_D3           15     //07  -> GPIO7-17   |   GIPO6-19  ->  15
#define TFT_D4           40     //08  -> GPIO7-16   |   GIPO6-20  ->  40
#define TFT_D5           41     //09  -> GPIO7-11   |   GIPO6-21  ->  41
#define TFT_D6           17     //10  -> GPIO7-00   |   GIPO6-22  ->  17
#define TFT_D7           16     //11  -> GPIO7-02   |   GIPO6-23  ->  16
//GPIO6 - bits 24..31, for 16 bit parallel
#define TFT_D8           22     //12  -> GPIO7-01   |   GIPO6-24  ->  22
#define TFT_D9           23     //13  -> GPIO7-03   |   GIPO6-25  ->  23
#define TFT_D10          20     //14  -> GPIO6-18   |   GIPO6-26  ->  20
#define TFT_D11          21     //15  -> GPIO6-19   |   GIPO6-27  ->  21
#define TFT_D12          38     //16  -> GPIO6-23   |   GIPO6-28  ->  38
#define TFT_D13          39     //17  -> GPIO6-22   |   GIPO6-29  ->  39
#define TFT_D14          26     //18  -> GPIO6-17   |   GIPO6-30  ->  26
#define TFT_D15          27     //19  -> GPIO6-16   |   GIPO6-31  ->  27

#define I2S2_IN           5     // 5 - Documentation purposes, I2S2 input
#define I2S2_OUT          2     // 2 - Documentation purposes, I2S2 transmit pin for audio
#define I2S2_LRCLK        3     // 3 - Documentation purposes, I2S2 LRCLK
#define I2S2_BCLK         4     // 4 - Documentation purposes, I2S2 BCLK
#define I2S2_MCLK        33     // 4 - Documentation purposes, I2S MCL

#endif //PIN_DEFINES_H
