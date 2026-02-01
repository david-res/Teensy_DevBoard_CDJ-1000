/*
  DMA_UART.h - Library for Teensy LPUART receiving and transmitting over DMA FIFO buffers
  Created by Sicco Dwars, Oct 20, 2022

*/
#ifndef DMA_UART_h
#define DMA_UART_h

#include "DMAChannel.h"
#include "HardwareSerial.h"
#include "imxrt.h"
#include "core_pins.h"

#define SERIAL_7E1 0x02
#define SERIAL_7O1 0x03
#define SERIAL_8N1 0x00
#define SERIAL_8E1 0x06
#define SERIAL_8O1 0x07
#define SERIAL_7E1_RXINV 0x12
#define SERIAL_7O1_RXINV 0x13
#define SERIAL_8N1_RXINV 0x10
#define SERIAL_8E1_RXINV 0x16
#define SERIAL_8O1_RXINV 0x17
#define SERIAL_7E1_TXINV 0x22
#define SERIAL_7O1_TXINV 0x23
#define SERIAL_8N1_TXINV 0x20
#define SERIAL_8E1_TXINV 0x26
#define SERIAL_8O1_TXINV 0x27
#define SERIAL_7E1_RXINV_TXINV 0x32
#define SERIAL_7O1_RXINV_TXINV 0x33
#define SERIAL_8N1_RXINV_TXINV 0x30
#define SERIAL_8E1_RXINV_TXINV 0x36
#define SERIAL_8O1_RXINV_TXINV 0x37
#define SERIAL_9N1 0x84
#define SERIAL_9E1 0x8E
#define SERIAL_9O1 0x8F
#define SERIAL_9N1_RXINV 0x94
#define SERIAL_9E1_RXINV 0x9E
#define SERIAL_9O1_RXINV 0x9F
#define SERIAL_9N1_TXINV 0xA4
#define SERIAL_9E1_TXINV 0xAE
#define SERIAL_9O1_TXINV 0xAF
#define SERIAL_9N1_RXINV_TXINV 0xB4
#define SERIAL_9E1_RXINV_TXINV 0xBE
#define SERIAL_9O1_RXINV_TXINV 0xBF

// We have 1/2 bit stop setting
#define SERIAL_2STOP_BITS 0x100
#define SERIAL_8E2 (SERIAL_8E1 | SERIAL_2STOP_BITS)
#define SERIAL_8O2 (SERIAL_8O1 | SERIAL_2STOP_BITS)
#define SERIAL_8E2_RXINV (SERIAL_8E1_RXINV | SERIAL_2STOP_BITS)
#define SERIAL_8O2_RXINV (SERIAL_8O1_RXINV | SERIAL_2STOP_BITS)
#define SERIAL_8E2_TXINV (SERIAL_8E1_TXINV | SERIAL_2STOP_BITS)
#define SERIAL_8O2_TXINV (SERIAL_8O1_TXINV | SERIAL_2STOP_BITS)
#define SERIAL_8E2_RXINV_TXINV (SERIAL_8E1_RXINV_TXINV | SERIAL_2STOP_BITS)
#define SERIAL_8O2_RXINV_TXINV (SERIAL_8O1_RXINV_TXINV | SERIAL_2STOP_BITS)
#define SERIAL_8N2 (SERIAL_8N1 | SERIAL_2STOP_BITS)
#define SERIAL_8N2_RXINV (SERIAL_8N1_RXINV | SERIAL_2STOP_BITS)
#define SERIAL_8N2_TXINV (SERIAL_8N1_TXINV | SERIAL_2STOP_BITS)
#define SERIAL_8N2_RXINV_TXINV (SERIAL_8N1_RXINV_TXINV | SERIAL_2STOP_BITS)

// Half duplex support
#define SERIAL_HALF_DUPLEX 0x200
#define SERIAL_7E1_HALF_DUPLEX (SERIAL_7E1 | SERIAL_HALF_DUPLEX)
#define SERIAL_7O1_HALF_DUPLEX (SERIAL_7O1 | SERIAL_HALF_DUPLEX)
#define SERIAL_8N1_HALF_DUPLEX (SERIAL_8N1 | SERIAL_HALF_DUPLEX)

// bit0: parity, 0=even, 1=odd
// bit1: parity, 0=disable, 1=enable
// bit2: mode, 1=9bit, 0=8bit
// bit3: mode10: 1=10bit, 0=8bit
// bit4: rxinv, 0=normal, 1=inverted
// bit5: txinv, 0=normal, 1=inverted
// bit6: unused
// bit7: actual data goes into 9th bit

// bit8: 2 stop bits 
// bit9: Half Duplex Mode


extern "C" {
  extern void DMA_Serial1_isr_uart();
  extern void DMA_Serial2_isr_uart();
  extern void DMA_Serial3_isr_uart();
  extern void DMA_Serial4_isr_uart();
  extern void DMA_Serial5_isr_uart();
  extern void DMA_Serial6_isr_uart();
  extern void DMA_Serial7_isr_uart();
  extern void DMA_Serial8_isr_uart();

  extern void DMA_Serial1_isr_dma();
  extern void DMA_Serial2_isr_dma();
  extern void DMA_Serial3_isr_dma();
  extern void DMA_Serial4_isr_dma();
  extern void DMA_Serial5_isr_dma();
  extern void DMA_Serial6_isr_dma();
  extern void DMA_Serial7_isr_dma();
  extern void DMA_Serial8_isr_dma();
}

typedef struct _Hpin_to_xbar_info
{
  const uint8_t pin;    // The pin number
  const uint8_t xbar_in_index; // What XBar input index. 
  const uint32_t mux_val;  // Value to set for mux;
  volatile uint32_t *select_input_register; // Which register controls the selection
  const uint32_t select_val; // Value for that selection
} Hpin_to_xbar_info_t;

extern const Hpin_to_xbar_info_t Hpin_to_xbar_info[];
extern const uint8_t Hcount_pin_to_xbar_info;

class DMA_UART
{
  public:
    static const uint8_t cnt_tx_pins = 2;
    static const uint8_t cnt_rx_pins = 2;
    typedef struct 
    {
      const uint8_t     pin;    // The pin number
      const uint32_t    mux_val;  // Value to set for mux;
      volatile uint32_t *select_input_register; // Which register controls the selection
      const uint32_t    select_val; // Value for that selection
    } pin_info_t;

    typedef struct 
    {
      uint8_t serial_index; // which object are we? 0 based
      uint8_t LPUART_index; // which LPUART hardware are we? 1 based
      IRQ_NUMBER_t irq;
      void (*isr_uart)(void);
      void (*isr_dma)(void);
      volatile uint32_t &ccm_register;
      const uint32_t ccm_value;
      pin_info_t rx_pins[cnt_rx_pins];
      pin_info_t tx_pins[cnt_tx_pins];

      const uint16_t irq_uart_priority;
      const uint16_t irq_dma_priority;

      const uint8_t xbar_out_lpuartX_trig_input;
    } hardware_t;

    constexpr DMA_UART(
      IMXRT_LPUART_t *myport, 
      const hardware_t *myhardware, 
      volatile uint16_t *_tx_buffer, 
      size_t _tx_buffer_size, 
      uint32_t _tx_buffer_mask,
      volatile uint16_t *_rx_buffer, 
      size_t _rx_buffer_size,
      uint32_t _rx_buffer_mask,
      DMAChannel *_tx_dmachannel, 
      DMAChannel *_rx_dmachannel
      ) :
      port(myport), 
      hardware(myhardware),
      tx_buffer_(_tx_buffer), 
      tx_buffer_size_(_tx_buffer_size),  
      tx_buffer_mask_(_tx_buffer_mask),  
      rx_buffer_(_rx_buffer), 
      rx_buffer_size_(_rx_buffer_size),
      rx_buffer_mask_(_rx_buffer_mask),
      tx_dmachannel_ (_tx_dmachannel),
      rx_dmachannel_ (_rx_dmachannel) { }
    
    int begin (int new_baudrate = 115200, uint16_t new_format = SERIAL_8N1, int new_RS485_DIR_Pin = 0);
    void end();
    
    int baudrate = 0;
    uint16_t format = 0;
    bool busy_txing = 0;

    void setRX(uint8_t pin);
    void setTX(uint8_t pin, bool opendrain=false);
    
    void write (uint8_t c);
    void write (uint8_t *cs, int n);
    
    void write (char c);
    void write (char *cs, int n);
    
    void write (char *s);

    void write (uint16_t c);
    void write (uint16_t *cs, int n);
    void trigger_tx ();
    void write_debug_message ();
 
    int available ();
    int availableForWrite();
     
    int read ();
    
    void read (uint16_t *cs, int n);
    void read (uint8_t *cs, int n);
     
    void timerAction ();

    IMXRT_LPUART_t * const port; // = (IMXRT_LPUART_t*)LPUART_BASE;
    const hardware_t * const hardware;
    
    volatile uint16_t *tx_buffer_;
    size_t tx_buffer_size_;
    uint32_t tx_buffer_mask_;
    volatile uint16_t *rx_buffer_;
    size_t rx_buffer_size_;
    uint32_t rx_buffer_mask_;
    DMAChannel *tx_dmachannel_;
    DMAChannel *rx_dmachannel_;

    int testcount = 0;
        
    void set_process_serial_rx_call (void (*do_call)(uint16_t c)) __attribute__((always_inline)) 
    {
        process_serial_rx_call = do_call;
    }
    static void (*process_serial_rx_call)(uint16_t c);

  private:
 
    static void process_serial_rx_call_DefaultUnused(uint16_t c);

    uint8_t rx_pin_index_ = 0x0;  // default is always first item
    uint8_t tx_pin_index_ = 0x0;
    uint8_t half_duplex_mode_ = 0; // are we in half duplex mode?

    volatile uint32_t *transmit_pin_baseReg_ = 0;
    uint32_t transmit_pin_bitmask_ = 0;
   
    volatile uint16_t read_buf_tail = 0;
    volatile uint16_t write_buf_head = 0;
    volatile uint16_t write_buf_used = 0;
 
    HardwareSerial *OurSerialPort = NULL;
    int RS485_DIR_Pin = 0;
    int databits = 8;
  
    void isr_uart ();
    friend void DMA_Serial1_isr_uart();
    friend void DMA_Serial2_isr_uart();
    friend void DMA_Serial3_isr_uart();
    friend void DMA_Serial4_isr_uart();
    friend void DMA_Serial5_isr_uart();
    friend void DMA_Serial6_isr_uart();
    friend void DMA_Serial7_isr_uart();
    friend void DMA_Serial8_isr_uart();
    
    void isr_dma ();
    friend void DMA_Serial1_isr_dma();
    friend void DMA_Serial2_isr_dma();
    friend void DMA_Serial3_isr_dma();
    friend void DMA_Serial4_isr_dma();
    friend void DMA_Serial5_isr_dma();
    friend void DMA_Serial6_isr_dma();
    friend void DMA_Serial7_isr_dma();
    friend void DMA_Serial8_isr_dma();
};

extern DMA_UART DMA_Serial1;
extern DMA_UART DMA_Serial2;
extern DMA_UART DMA_Serial3;
extern DMA_UART DMA_Serial4;
extern DMA_UART DMA_Serial5;
extern DMA_UART DMA_Serial6;
extern DMA_UART DMA_Serial7;
extern DMA_UART DMA_Serial8;

#endif
