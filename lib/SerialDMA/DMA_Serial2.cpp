#include "DMA_UART.h"
#include <Arduino.h>

#define IRQ_UART_PRIORITY  32  // 0 = highest priority, 255 = lowest
#define IRQ_DMA_PRIORITY  32  // 0 = highest priority, 255 = lowest

// define buffer sizes as 2^N with N at either 0,1,2,3,4,5,6 or 7 for buffer sizes of 1,2,4,8,16,32,64 or 128 elements
#define L2_RX_BUFSIZE 7 
#define L2_TX_BUFSIZE 7 

#define RX_BUFSIZE (1 << L2_RX_BUFSIZE)
#define TX_BUFSIZE (1 << L2_TX_BUFSIZE)
#define RX_BUFMASK (RX_BUFSIZE-1)
#define TX_BUFMASK (TX_BUFSIZE-1)

static DMAChannel RX2_dmachannel = DMAChannel();
static DMAChannel TX2_dmachannel = DMAChannel();

void DMA_Serial2_isr_dma()
{
  DMA_Serial2.isr_dma();
}

void DMA_Serial2_isr_uart()
{
  DMA_Serial2.isr_uart();
}

static uint16_t DMA_UART2_RxBuf[RX_BUFSIZE] __attribute__((aligned(RX_BUFSIZE*2)));  //receive buffer
static uint16_t DMA_UART2_TxBuf[TX_BUFSIZE] __attribute__((aligned(TX_BUFSIZE*2)));  //transmit buffer

static DMA_UART::hardware_t UART4_Hardware = {
	2, 4, IRQ_LPUART4, 
  &DMA_Serial2_isr_uart, 
  &DMA_Serial2_isr_dma, 
	CCM_CCGR1, 
	CCM_CCGR1_LPUART4(CCM_CCGR_ON),
  {{7,2, &IOMUXC_LPUART4_RX_SELECT_INPUT, 2}, {0xff, 0xff, nullptr, 0}},
  {{8,2, &IOMUXC_LPUART4_TX_SELECT_INPUT, 2}, {0xff, 0xff, nullptr, 0}},
	IRQ_UART_PRIORITY, 
  IRQ_DMA_PRIORITY,
	XBARA1_OUT_LPUART4_TRG_INPUT
};

DMA_UART DMA_Serial2(&IMXRT_LPUART4, &UART4_Hardware, DMA_UART2_TxBuf, TX_BUFSIZE, TX_BUFMASK, DMA_UART2_RxBuf, RX_BUFSIZE, RX_BUFMASK, &TX2_dmachannel, &RX2_dmachannel);

#undef IRQ_UART_PRIORITY 
#undef IRQ_DMA_PRIORITY
#undef L2_RX_BUFSIZE 
#undef L2_TX_BUFSIZE
#undef RX_BUFSIZE
#undef TX_BUFSIZE
#undef RX_BUFMASK
#undef TX_BUFMASK
