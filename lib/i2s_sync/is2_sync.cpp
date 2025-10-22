#include "../include/device_defines.h"
#include "i2s_sync.h"

//volatile uint8_t  SAI_IRQ_state = 0;
//static int16_t * txreg = (int16_t *)((uint32_t)&I2S1_TDR0 + 2);
#define SAMPLE_RATE_EXACT 44100.0f // 48000.0f

FLASHMEM void i2s_sync::set_audioClock(int nfact, int32_t nmult, uint32_t ndiv) {// sets PLL4

      CCM_ANALOG_PLL_AUDIO = CCM_ANALOG_PLL_AUDIO_BYPASS | CCM_ANALOG_PLL_AUDIO_ENABLE
            | CCM_ANALOG_PLL_AUDIO_POST_DIV_SELECT(2) // 2: 1/4; 1: 1/2; 0: 1/1
            | CCM_ANALOG_PLL_AUDIO_DIV_SELECT(nfact);

      CCM_ANALOG_PLL_AUDIO_NUM   = nmult & CCM_ANALOG_PLL_AUDIO_NUM_MASK;
      CCM_ANALOG_PLL_AUDIO_DENOM = ndiv & CCM_ANALOG_PLL_AUDIO_DENOM_MASK;

      CCM_ANALOG_PLL_AUDIO &= ~CCM_ANALOG_PLL_AUDIO_POWERDOWN;//Switch on PLL
      while (!(CCM_ANALOG_PLL_AUDIO & CCM_ANALOG_PLL_AUDIO_LOCK)) {}; //Wait for pll-lock

      const int div_post_pll = 1; // other values: 2,4
      CCM_ANALOG_MISC2 &= ~(CCM_ANALOG_MISC2_DIV_MSB | CCM_ANALOG_MISC2_DIV_LSB);
      if(div_post_pll>1) CCM_ANALOG_MISC2 |= CCM_ANALOG_MISC2_DIV_LSB;
      if(div_post_pll>3) CCM_ANALOG_MISC2 |= CCM_ANALOG_MISC2_DIV_MSB;

      CCM_ANALOG_PLL_AUDIO &= ~CCM_ANALOG_PLL_AUDIO_BYPASS;//Disable Bypass
}

FLASHMEM void i2s_sync::config_sai1()
{
  Serial.println("Entering config_sai1");
  Serial.flush();

  // From AudioOutputI2S::config_i2s
  CCM_CCGR5 |= CCM_SAI_CLK_EN(CCM_CCGR_ON);  
  //PLL:
  int fs = AUDIO_SAMPLE_RATE_EXACT;
  // PLL between 27*24 = 648MHz und 54*24=1296MHz
  int n1 = 4; //SAI prescaler 4 => (n1*n2) = multiple of 4
  int n2 = 1 + (24000000 * 27) / (fs * 256 * n1);

  double C = ((double)fs * 256 * n1 * n2) / 24000000;
  int c0 = C;
  int c2 = 10000;
  int c1 = C * c2 - (c0 * c2);
  set_audioClock(c0, c1, c2);

#if defined(RDI_DEVELOPMENTS_REV3)
      // clear SAI3_CLK register locations
      CCM_CSCMR1 = (CCM_CSCMR1 & ~(CCM_CSCMR1_SAI3_CLK_SEL_MASK))
            | CCM_CSCMR1_SAI3_CLK_SEL(2); // &0x03 // (0,1,2): PLL3PFD0, PLL5, PLL4,
      CCM_CS1CDR = (CCM_CS1CDR & ~(CCM_CS1CDR_SAI3_CLK_PRED_MASK | CCM_CS1CDR_SAI3_CLK_PODF_MASK))
            | CCM_CS1CDR_SAI3_CLK_PRED(n1-1)
            | CCM_CS1CDR_SAI3_CLK_PODF(n2-1);

      //Select MCLK
      IOMUXC_GPR_GPR1 = (IOMUXC_GPR_GPR1 & ~(IOMUXC_GPR_GPR1_SAI3_MCLK3_SEL_MASK))
            | (IOMUXC_GPR_GPR1_SAI3_MCLK_DIR | IOMUXC_GPR_GPR1_SAI3_MCLK3_SEL(0));	

      // Pin configuration
      IOMUXC_SW_MUX_CTL_PAD_GPIO_SD_B1_04 = 8;  // SAI3_MCLK
      IOMUXC_SW_MUX_CTL_PAD_GPIO_SD_B1_03 = 8;  // SAI3_TX_BCLK
      IOMUXC_SW_MUX_CTL_PAD_GPIO_SD_B1_02 = 8;  // SAI3_TX_SYNC

      // TX only - no need to sync to RX
      int tsync = 0;  // TX is asynchronous (generates its own clock)

      I2S3_TMR = 0;
      I2S3_TCR1 = I2S_TCR1_RFW(2);  // Changed from 1 to 2 - wait until FIFO has space for 2 words
      I2S3_TCR2 = I2S_TCR2_SYNC(tsync) | I2S_TCR2_BCP
            | (I2S_TCR2_BCD | I2S_TCR2_DIV((1)) | I2S_TCR2_MSEL(1));
      I2S3_TCR3 = I2S_TCR3_TCE;
      I2S3_TCR4 = I2S_TCR4_FRSZ((2-1)) | I2S_TCR4_SYWD((32-1)) | I2S_TCR4_MF
            | I2S_TCR4_FSD | I2S_TCR4_FSE | I2S_TCR4_FSP;
      I2S3_TCR5 = I2S_TCR5_WNW((32-1)) | I2S_TCR5_W0W((32-1)) | I2S_TCR5_FBT((32-1));

      // Enable transmitter only
      I2S3_RCSR = 0;  // RX disabled 
      
      I2S3_TCSR = 0;  // Clear first
      I2S3_TCSR |= I2S_TCSR_FR;
      
      // TX_DATA pin
      IOMUXC_SW_MUX_CTL_PAD_GPIO_SD_B1_01 = 8;  // SAI3_TX_DATA0
#else
  // clear SAI1_CLK register locations
  CCM_CSCMR1 = (CCM_CSCMR1 & ~(CCM_CSCMR1_SAI1_CLK_SEL_MASK))
       | CCM_CSCMR1_SAI1_CLK_SEL(2); // &0x03 // (0,1,2): PLL3PFD0, PLL5, PLL4
  CCM_CS1CDR = (CCM_CS1CDR & ~(CCM_CS1CDR_SAI1_CLK_PRED_MASK | CCM_CS1CDR_SAI1_CLK_PODF_MASK))
       | CCM_CS1CDR_SAI1_CLK_PRED(n1-1) // &0x07
       | CCM_CS1CDR_SAI1_CLK_PODF(n2-1); // &0x3f

  // Select MCLK
  IOMUXC_GPR_GPR1 = (IOMUXC_GPR_GPR1 & ~(IOMUXC_GPR_GPR1_SAI1_MCLK1_SEL_MASK))
    | (IOMUXC_GPR_GPR1_SAI1_MCLK_DIR | IOMUXC_GPR_GPR1_SAI1_MCLK1_SEL(0));

  CORE_PIN23_CONFIG = 3;  // MCLK
  CORE_PIN20_CONFIG = 3;  // RX_SYNC   (LRCLK)
  CORE_PIN21_CONFIG = 3;  // RX_BCLK

  int rsync = 0;
  int tsync = 1;

  I2S1_TMR = 0;
  //I2S1_TCSR = (1<<25); //Reset
  I2S1_TCR1 = I2S_TCR1_RFW(2);  // Changed from 1 to 2 - wait until FIFO has space for 2 words
  I2S1_TCR2 = I2S_TCR2_SYNC(tsync) | I2S_TCR2_BCP // sync=0; tx is async;
        | (I2S_TCR2_BCD | I2S_TCR2_DIV((1)) | I2S_TCR2_MSEL(1));
  I2S1_TCR3 = I2S_TCR3_TCE;
  I2S1_TCR4 = I2S_TCR4_FRSZ((2-1)) | I2S_TCR4_SYWD((32-1)) | I2S_TCR4_MF
        | I2S_TCR4_FSD | I2S_TCR4_FSE | I2S_TCR4_FSP;
  I2S1_TCR5 = I2S_TCR5_WNW((32-1)) | I2S_TCR5_W0W((32-1)) | I2S_TCR5_FBT((32-1));

  I2S1_RMR = 0;
  //I2S1_RCSR = (1<<25); //Reset
  I2S1_RCR1 = I2S_RCR1_RFW(1);
  I2S1_RCR2 = I2S_RCR2_SYNC(rsync) | I2S_RCR2_BCP  // sync=0; rx is async;
        | (I2S_RCR2_BCD | I2S_RCR2_DIV((1)) | I2S_RCR2_MSEL(1));
  I2S1_RCR3 = I2S_RCR3_RCE;
  I2S1_RCR4 = I2S_RCR4_FRSZ((2-1)) | I2S_RCR4_SYWD((32-1)) | I2S_RCR4_MF
        | I2S_RCR4_FSE | I2S_RCR4_FSP | I2S_RCR4_FSD;
  I2S1_RCR5 = I2S_RCR5_WNW((32-1)) | I2S_RCR5_W0W((32-1)) | I2S_RCR5_FBT((32-1));
  // End from AudioOutputI2S::config_i2s

  // From AudioOutputI2Sslave::begin
  I2S1_RCSR |= I2S_RCSR_RE | I2S_RCSR_BCE;
  I2S1_TCSR = 0;  // Clear first
  I2S1_TCSR |= I2S_TCSR_FR;
  IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_13 = 3;   //CORE_PIN7_CONFIG  = 3;  // TX_DATA0
#endif

  Serial.println("Leaving config_sai1");
  Serial.flush();

}

FASTRUN void i2s_sync::begin(CBF audio_irq){
      Serial.println("In audio.begin");
      Serial.flush();
      config_sai1();
      attachInterruptVector(IRQ_SAI, audio_irq);
      NVIC_ENABLE_IRQ(IRQ_SAI); 
      NVIC_SET_PRIORITY(IRQ_SAI, 128);
  
      Serial.println("Audio started");
}


FLASHMEM void i2s_sync::stop(){}