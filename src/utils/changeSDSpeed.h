//Change SD card speed, derived from v2.1.2 SdFat\src\SdCard\SdioTeensy.h & .cpp
#ifndef TR_SDCARDSPEED_H
#define TR_SDCARDSPEED_H

#include <Arduino.h>
#include "device_defines.h"

#define IOMUXC_SW_PAD_CTL_PAD_PKE       ((0x1)<<12)
#define IOMUXC_SW_PAD_CTL_PAD_PUE       ((0x1)<<13)
#define IOMUXC_SW_PAD_CTL_PAD_SPEED(n)  (((n)&0x3)<<6)
#define IOMUXC_SW_PAD_CTL_PAD_PUS(n)    (((n)&0x3)<<14)
#define IOMUXC_SW_PAD_CTL_PAD_DSE(n)    (((n)&0x7)<<3)

#define SDHC_PRSSTAT              (USDHC1_PRES_STATE) // Present State register
#define SDHC_SYSCTL               (USDHC1_SYS_CTRL) // System Control register
#define SDHC_SYSCTL_DTOCV(n)      MAKE_REG_SET(n,0xF,16) //(uint32_t)(((n) & 0xF)<<16)  // Data Timeout Counter Value
#define SDHC_SYSCTL_DTOCV_MASK    MAKE_REG_MASK(0xF,16) //((uint32_t)0x000F0000)
#define SDHC_SYSCTL_SDCLKFS(n)    MAKE_REG_SET(n,0xFF,8) //(uint32_t)(((n) & 0xFF)<<8)  // SDCLK Frequency Select
#define SDHC_SYSCTL_SDCLKFS_MASK  MAKE_REG_MASK(0xFF,8) //((uint32_t)0x0000FF00)
#define SDHC_SYSCTL_DVS(n)        MAKE_REG_SET(n,0xF,4) //(uint32_t)(((n) & 0xF)<<4)  // Divisor
#define SDHC_SYSCTL_DVS_MASK      MAKE_REG_MASK(0xF,4)  //((uint32_t)0x000000F0)
#define SDHC_PRSSTAT_SDSTB        MAKE_REG_MASK(0x1,3) //((uint32_t)0x00000008)   // SD Clock Stable

#define MAKE_REG_MASK(m,s) (((uint32_t)(((uint32_t)(m) << s))))
#define MAKE_REG_SET(x,m,s) (((uint32_t)(((uint32_t)(x) & m) << s)))

//External
uint32_t getSdCardClockSpeed();
void setSDCardClock(uint32_t khzSpeed, bool useSDIO2);

//Internal
void enableSDCardGPIO(bool enable, bool useSDIO2);
void SDCardGPIOMux(uint8_t mode, bool useSDIO2);
uint32_t baseSDClock();
void setSDClock(uint32_t kHzMax, bool useSDIO2);
#endif //TR_SDCARDSPEED_H