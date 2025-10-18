#ifndef DEVICE_DEFINES_H
#define DEVICE_DEFINES_H

// For I2S Interrupt Enable
#define I2S_TCSR_FRIE  ((uint32_t)0x00000100) 

#if defined(RDI_DEVELOPMENTS_REV3)
    #define I2S_TCSR_REG I2S3_TCSR
    #define I2S_TDR0_REG I2S3_TDR0
    #define SD_FILE_SYS SdFs
    #define IRQ_SAI IRQ_SAI3_TX
    #define FILE_TYPE FsFile
    #define CCM_SAI_CLK_EN CCM_CCGR5_SAI3
#else    
    #define I2S_TCSR_REG I2S1_TCSR
    #define I2S_TDR0_REG I2S1_TDR0
    #define SD_FILE_SYS FS
    #define IRQ_SAI IRQ_SAI1
    #define FILE_TYPE File
    #define CCM_SAI_CLK_EN CCM_CCGR5_SAI1
#endif

#endif // DEVICE_DEFINES_H