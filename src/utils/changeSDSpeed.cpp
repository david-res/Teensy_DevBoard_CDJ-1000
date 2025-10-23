//Change SD card speed, derived from v2.1.2 SdFat\src\SdCard\SdioTeensy.h & .cpp
#include "device_defines.h"
#include "globals.h"

#include <Arduino.h>
#include "changeSDSpeed.h"
#if defined(RDI_DEVELOPMENTS_REV3)
#include "SdFat.h"
#endif

uint32_t sdcard_dvs = 1;
uint32_t sdcard_sdclkfs = 1;
uint32_t m_sdClkKhz = 0;

FLASHMEM uint32_t getSdCardClockSpeed()
{
  return m_sdClkKhz;
}

FLASHMEM void setSDCardClock(uint32_t khzSpeed, bool useSDIO2)
{
  if (m_sdClkKhz == khzSpeed) {
    Serial.printf("Requested SD card speed %ldMHz, actual frequency %ldMHz, " SER_YELLOW "no change" SER_RESET "\n", khzSpeed / 1'000, m_sdClkKhz / 1'000);
    return;
  }

  //Disable GPIO
  enableSDCardGPIO(false, useSDIO2);

  //Set the SDHC SCK frequency
  setSDClock(khzSpeed, useSDIO2);

  //Enable GPIO
  enableSDCardGPIO(true, useSDIO2);

  Serial.printf("Requested SD card %ldMHz, set to " SER_GREEN "%ldMHz" SER_RESET "\n", khzSpeed / 1'000, m_sdClkKhz / 1'000);
}

FLASHMEM void enableSDCardGPIO(bool enable, bool useSDIO2) {
  const uint32_t CLOCK_MASK = IOMUXC_SW_PAD_CTL_PAD_PKE |
                              IOMUXC_SW_PAD_CTL_PAD_DSE(7) |
                              IOMUXC_SW_PAD_CTL_PAD_SPEED(2);

  const uint32_t DATA_MASK = CLOCK_MASK | IOMUXC_SW_PAD_CTL_PAD_PUE |
                             IOMUXC_SW_PAD_CTL_PAD_PUS(1);
  if (enable) {
    SDCardGPIOMux(useSDIO2 ? 6 : 0, useSDIO2);
    if (useSDIO2 == true) {
      IOMUXC_SW_PAD_CTL_PAD_GPIO_AD_B1_06 = DATA_MASK;   //USDHC2_DAT2
      IOMUXC_SW_PAD_CTL_PAD_GPIO_AD_B1_07 = DATA_MASK;   //USDHC2_DAT3
      IOMUXC_SW_PAD_CTL_PAD_GPIO_AD_B1_08 = DATA_MASK;   //USDHC2_CMD
      IOMUXC_SW_PAD_CTL_PAD_GPIO_AD_B1_09 = CLOCK_MASK;  //USDHC2_CLK
      IOMUXC_SW_PAD_CTL_PAD_GPIO_AD_B1_04 = DATA_MASK;   //USDHC2_DAT0
      IOMUXC_SW_PAD_CTL_PAD_GPIO_AD_B1_05 = DATA_MASK;   //USDHC2_DAT1
    } else {
      IOMUXC_SW_PAD_CTL_PAD_GPIO_SD_B0_04 = DATA_MASK;   // DAT2
      IOMUXC_SW_PAD_CTL_PAD_GPIO_SD_B0_05 = DATA_MASK;   // DAT3
      IOMUXC_SW_PAD_CTL_PAD_GPIO_SD_B0_00 = DATA_MASK;   // CMD
      IOMUXC_SW_PAD_CTL_PAD_GPIO_SD_B0_01 = CLOCK_MASK;  // CLK
      IOMUXC_SW_PAD_CTL_PAD_GPIO_SD_B0_02 = DATA_MASK;   // DAT0
      IOMUXC_SW_PAD_CTL_PAD_GPIO_SD_B0_03 = DATA_MASK;   // DAT1
    }
  } else {
    SDCardGPIOMux(5, useSDIO2);
  }
}

FLASHMEM void SDCardGPIOMux(uint8_t mode, bool useSDIO2) {
  if (useSDIO2 == true) {
    IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_06 = mode;  //USDHC2_DAT2
    IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_07 = mode;  //USDHC2_DAT3
    IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_08 = mode;  //USDHC2_CMD
    IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_09 = mode;  //USDHC2_CLK
    IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_04 = mode;  //USDHC2_DAT0
    IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_05 = mode;  //USDHC2_DAT1
  } else {
    IOMUXC_SW_MUX_CTL_PAD_GPIO_SD_B0_04 = mode;  // DAT2
    IOMUXC_SW_MUX_CTL_PAD_GPIO_SD_B0_05 = mode;  // DAT3
    IOMUXC_SW_MUX_CTL_PAD_GPIO_SD_B0_00 = mode;  // CMD
    IOMUXC_SW_MUX_CTL_PAD_GPIO_SD_B0_01 = mode;  // CLK
    IOMUXC_SW_MUX_CTL_PAD_GPIO_SD_B0_02 = mode;  // DAT0
    IOMUXC_SW_MUX_CTL_PAD_GPIO_SD_B0_03 = mode;  // DAT1
  }
}

FLASHMEM uint32_t baseSDClock() {
  uint32_t divider = ((CCM_CSCDR1 >> 11) & 0x7) + 1;
  return (528000000U * 3) / ((CCM_ANALOG_PFD_528 & 0x3F) / 6) / divider;
}

FLASHMEM void setSDClock(uint32_t kHzMax, bool useSDIO2) {
  const uint32_t SDCARD_DVS_LIMIT = 0X10;
  const uint32_t SDCLKFS_LIMIT = 0X100;
  sdcard_dvs = 1;
  sdcard_sdclkfs = 1;
  uint32_t maxSdclk = 1000 * kHzMax;
  uint32_t base = baseSDClock();

  while ((base/(sdcard_sdclkfs*SDCARD_DVS_LIMIT) > maxSdclk) && (sdcard_sdclkfs < SDCLKFS_LIMIT)) {
    sdcard_sdclkfs <<= 1;
  }
  while ((base/(sdcard_sdclkfs*sdcard_dvs) > maxSdclk) && (sdcard_dvs < SDCARD_DVS_LIMIT)) {
    sdcard_dvs++;
  }

  m_sdClkKhz = base / (1000 * sdcard_sdclkfs * sdcard_dvs);

  sdcard_sdclkfs >>= 1;
  sdcard_dvs--;

  #if defined(RDI_DEVELOPMENTS_REV3)
  IMXRT_USDHC_t *m_psdhc;
  if (useSDIO2 == true) {
    m_psdhc = (IMXRT_USDHC_t*)IMXRT_USDHC2_ADDRESS;
  } else {
    m_psdhc = (IMXRT_USDHC_t*)IMXRT_USDHC1_ADDRESS;
  }

  //Change dividers.
  uint32_t sysctl = m_psdhc->SYS_CTRL & ~(SDHC_SYSCTL_DTOCV_MASK | SDHC_SYSCTL_DVS_MASK | SDHC_SYSCTL_SDCLKFS_MASK);
  m_psdhc->SYS_CTRL = sysctl | SDHC_SYSCTL_DTOCV(0x0E) | SDHC_SYSCTL_DVS(sdcard_dvs) | SDHC_SYSCTL_SDCLKFS(sdcard_sdclkfs);

  //Wait until the SDHC clock is stable.
  while (!(m_psdhc->PRES_STATE & SDHC_PRSSTAT_SDSTB)) {}
#else
  //Change dividers.
  uint32_t sysctl = SDHC_SYSCTL & ~(SDHC_SYSCTL_DTOCV_MASK | SDHC_SYSCTL_DVS_MASK | SDHC_SYSCTL_SDCLKFS_MASK);
  SDHC_SYSCTL = sysctl | SDHC_SYSCTL_DTOCV(0x0E) | SDHC_SYSCTL_DVS(sdcard_dvs) | SDHC_SYSCTL_SDCLKFS(sdcard_sdclkfs);

  while (!(SDHC_PRSSTAT & SDHC_PRSSTAT_SDSTB)) {}
#endif
}
