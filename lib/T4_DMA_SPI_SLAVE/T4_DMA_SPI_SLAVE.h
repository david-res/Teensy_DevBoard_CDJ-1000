#if !defined(_T4_DMA_SPI_SLAVE_H_)
#define _T4_DMA_SPI_SLAVE_H_

#include "Arduino.h"
#include <DMAChannel.h>
#include <imxrt.h>

#ifndef LSBFIRST
#define LSBFIRST 0
#endif
#ifndef MSBFIRST
#define MSBFIRST 1
#endif

// SDI, SDO are LSPSI shift registers input and output
// SIN and SOUT are microcontroller pins 
#define MODE_4WIRE_SDO_SOUT_SDI_SIN  0
#define MODE_3WIRE_SDIO_SIN          1
#define MODE_3WIRE_SDIO_SOUT         2
#define MODE_4WIRE_SDO_SIN_SDI_SOUT  3
#define MODE_2WIRE_SDI_SIN           4
#define MODE_2WIRE_SDI_SOUT          5
#define MODE_2WIRE_SDO_SIN           6
#define MODE_2WIRE_SDO_SOUT          7

#define MODE_4WIRE          MODE_4WIRE_SDO_SOUT_SDI_SIN
#define MODE_4WIRE_SWAPPED  MODE_4WIRE_SDO_SIN_SDI_SOUT
#define MODE_3WIRE          MODE_3WIRE_SDIO_SIN
#define MODE_3WIRE_SWAPPED  MODE_3WIRE_SDIO_SOUT
#define MODE_2WIRE_IN          MODE_2WIRE_SDI_SIN
#define MODE_2WIRE_IN_SWAPPED  MODE_2WIRE_SDI_SOUT
#define MODE_2WIRE_OUT         MODE_2WIRE_SDO_SIN
#define MODE_2WIRE_OUT_SWAPPED MODE_2WIRE_SDO_SOUT

#define SPI_MODE0 0x00
#define SPI_MODE1 0x04
#define SPI_MODE2 0x08
#define SPI_MODE3 0x0C

#define INDEX_SPI  0
#define INDEX_SPI1 1
#define INDEX_SPI2 2

enum DMAState { notAllocated, idle, active, completed};

class T4_DMA_SPI_SLAVE
{
public:
    // T4.1 has SPI2 pins on memory connectors as well as SDCard
    static const uint8_t CNT_MISO_PINS = 2;
    static const uint8_t CNT_MOSI_PINS = 2;
    static const uint8_t CNT_SCK_PINS = 2;
    static const uint8_t CNT_CS_PINS = 3;

    typedef struct 
    {
      uint16_t spi_port_index;
      IRQ_NUMBER_t irq;
      volatile uint32_t &clock_gate_register;
      const uint32_t clock_gate_mask;
      uint8_t tx_dma_channel;
      uint8_t rx_dma_channel;
      void (*dma_rxisr)(void);
      void (*spi_isr )(void);

      // MISO pins
      const uint8_t  		miso_pin[CNT_MISO_PINS];
      const uint32_t  	miso_mux[CNT_MISO_PINS];    // ALT values, what goes into 
      const uint8_t 		miso_select_val[CNT_MISO_PINS];
      volatile uint32_t 	&miso_select_input_register;

      // MOSI pins
      const uint8_t  		mosi_pin[CNT_MOSI_PINS];
      const uint32_t  	mosi_mux[CNT_MOSI_PINS];
      const uint8_t 		mosi_select_val[CNT_MOSI_PINS];
      volatile uint32_t 	&mosi_select_input_register;

      // SCK pins
      const uint8_t  		sck_pin[CNT_SCK_PINS];
      const uint32_t  	sck_mux[CNT_SCK_PINS];
      const uint8_t 		sck_select_val[CNT_SCK_PINS];
      volatile uint32_t 	&sck_select_input_register;

      // CS (PCS0) pins
      const uint8_t  		cs_pin[CNT_CS_PINS];
      const uint32_t  	cs_mux[CNT_CS_PINS];
      const uint8_t 		cs_select_val[CNT_CS_PINS];
      const uint8_t 		pcs_val[CNT_CS_PINS];
      volatile uint32_t 	&cs_select_input_register;

    } T4_SPI_SLAVE_Hardware_t;

    static const T4_SPI_SLAVE_Hardware_t spiclass_lpspi4_hardware;
    static const T4_SPI_SLAVE_Hardware_t spiclass_lpspi3_hardware;
    static const T4_SPI_SLAVE_Hardware_t spiclass_lpspi1_hardware;

    constexpr T4_DMA_SPI_SLAVE(uintptr_t myport, uintptr_t myhardware) : port_addr(myport), hardware_addr(myhardware) 
    {
      mosi_pin = hardware().mosi_pin[0];
      miso_pin = hardware().miso_pin[0];
      sck_pin = hardware().sck_pin[0];
      cs_pin = hardware().cs_pin[0];
    }

    void begin(uint8_t spi_mode = SPI_MODE0, uint8_t MSBfirst = 1, uint8_t pincfg= MODE_4WIRE_SDO_SOUT_SDI_SIN);
    void setOnComplete(void (*callback)(void)) {
    _onCompleteCallback = callback;
    }


    void debug();

    void end(); 

    void setBitOrder(uint8_t bitOrder);
    void setDataMode(uint8_t dataMode);

    bool pinIsMOSI(uint8_t pin);
    bool pinIsMISO(uint8_t pin);
    bool pinIsSCK(uint8_t pin);
    bool pinIsCS(uint8_t pin);

    void setMOSI(uint8_t pin);
    void setMISO(uint8_t pin);
    void setSCK(uint8_t pin);
    void setCS(uint8_t pin);

    uint8_t miso_pin = 255;
    uint8_t mosi_pin = 255;
    uint8_t sck_pin = 255;
    uint8_t cs_pin = 255;

    void print_pin_use();

    void config_pins (bool use_SDIN_line, bool use_SDOUT_line)
    {
      if (use_SDIN_line)
      {
          hardware().miso_select_input_register = hardware().miso_select_val[miso_pin_index];
          *(portControlRegister(hardware().miso_pin[miso_pin_index])) = spi_pad_io;
          *(portConfigRegister(hardware().miso_pin[miso_pin_index])) = hardware().miso_mux[miso_pin_index];
          SDIN_line_in_use = 1;
      }
      else
      {
          hardware().miso_select_input_register = pre_begin_miso_select_input_register;
          *(portControlRegister(hardware().miso_pin[miso_pin_index])) = pre_begin_miso_portControlRegister;
          *(portConfigRegister(hardware().miso_pin[miso_pin_index])) = pre_begin_miso_portConfigRegister;
          SDIN_line_in_use = 0;
      }

      if (use_SDOUT_line)
      {
          hardware().mosi_select_input_register = hardware().mosi_select_val[mosi_pin_index];
          *(portControlRegister(hardware().mosi_pin[mosi_pin_index])) = spi_pad_io;
          *(portConfigRegister(hardware().mosi_pin[mosi_pin_index])) = hardware().mosi_mux[mosi_pin_index];
          SDOUT_line_in_use = 1;
      }
      else
      {
          hardware().mosi_select_input_register = pre_begin_mosi_select_input_register;
          *(portConfigRegister(hardware().mosi_pin[mosi_pin_index])) = pre_begin_mosi_portConfigRegister;
          *(portControlRegister(hardware().mosi_pin[mosi_pin_index])) = pre_begin_mosi_portControlRegister; 
          SDOUT_line_in_use = 0;
      }
    }

    void Enable_to_be_used_SIN_SOUT_lines (uint32_t PinsConfig) __attribute__((__always_inline__))
    {
      switch (PinsConfig & 7)
      {
        case MODE_4WIRE_SDO_SOUT_SDI_SIN: config_pins (1, 1); break;
        case MODE_3WIRE_SDIO_SIN:         config_pins (1, 0); break;
        case MODE_3WIRE_SDIO_SOUT:        config_pins (0, 1); break;
        case MODE_4WIRE_SDO_SIN_SDI_SOUT: config_pins (1, 1); break;
        case MODE_2WIRE_SDI_SIN:          config_pins (1, 0); break;
        case MODE_2WIRE_SDI_SOUT:         config_pins (1, 0); break;
        case MODE_2WIRE_SDO_SIN:          config_pins (0, 1); break;
        case MODE_2WIRE_SDO_SOUT:         config_pins (0, 1); break;
      }

      uint32_t _pincfg = 0;
      switch (PinsConfig & 7)
      {
        case MODE_4WIRE_SDO_SOUT_SDI_SIN: _pincfg = 0b00; break;
        case MODE_3WIRE_SDIO_SIN:         _pincfg = 0b01; break;
        case MODE_3WIRE_SDIO_SOUT:        _pincfg = 0b10; break;
        case MODE_4WIRE_SDO_SIN_SDI_SOUT: _pincfg = 0b11; break;
        case MODE_2WIRE_SDI_SIN:          _pincfg = 0b00; break;
        case MODE_2WIRE_SDI_SOUT:         _pincfg = 0b11; break;
        case MODE_2WIRE_SDO_SIN:          _pincfg = 0b11; break;
        case MODE_2WIRE_SDO_SOUT:         _pincfg = 0b00; break;
      }
  
      if ((port().CFGR1 & 0x03000000) != (_pincfg << 24))
      {
  	    port().CR &= ~LPSPI_CR_MEN;
        port().CFGR1 &= 0xfcffffff;
        port().CFGR1 |= _pincfg << 24;
        port().CR |= LPSPI_CR_MEN;
      }
    }

//    void wait_transfer_done() __attribute__((optimize("-O0")));
//    void wait_list_transfer_done() __attribute__((optimize("-O0")));
    uint8_t prepare_for_slave_transfer(uint8_t data);
    void inline prepare_for_slave_transfer(void *buf, size_t count) 
    {
      if (count < 1) 
        return;
      prepare_for_slave_transfer(buf, buf, count);
    }
    //bool prepare_for_slave_transfer(uint32_t TXbytes, void *RXbuf, size_t count); // TX data passed as a literal value, 0 to 4 bytes length, not as a pointer
    bool prepare_for_slave_transfer(const void *TXbuf, void *RXbuf, size_t count);  

  	friend void _T4_DMA_SPI_SLAVE_dma_rxISR(void);
	  friend void _T4_DMA_SPI_SLAVE_dma_rxISR1(void);
	  friend void _T4_DMA_SPI_SLAVE_dma_rxISR2(void);
	  inline void dma_rxisr(void);

    inline void spi_isr (void);
    friend void _T4_DMA_SPI_SLAVE_spi_ISR(void);
    friend void _T4_DMA_SPI_SLAVE_spi_ISR1(void);
    friend void _T4_DMA_SPI_SLAVE_spi_ISR2(void);

    bool busy();
    int dma_state();
    bool CS_went_high = 0;
    uint32_t bytes_already_received = 0;
    uint32_t us_CS_deassert = 0;
    uint32_t prev_us_CS_deassert = 0;
    uint32_t us_last_DMA_receive = 0;
    uint32_t us_first_DMA_receive = 0;
  
    uint32_t delta_us_first_to_last();
    uint32_t delta_us_since_previous(); 

    double f_SCK_estimate();

    bool auto_repeat = 0;
    bool flag_transaction_completed = 0;

private:
    IMXRT_LPSPI_t & port() { return *(IMXRT_LPSPI_t *)port_addr; }
    const T4_SPI_SLAVE_Hardware_t & hardware() { return *(const T4_SPI_SLAVE_Hardware_t *)hardware_addr; }

    uintptr_t port_addr;
    uintptr_t hardware_addr;

    uint8_t miso_pin_index = 0;
    uint8_t mosi_pin_index = 0;
    uint8_t sck_pin_index = 0;
    uint8_t cs_pin_index = 0;

    bool initDMAChannels();
    enum {MAX_DMA_COUNT=32767};
    volatile int _dma_state = notAllocated;
    uint32_t	_dma_count_remaining = 0;	// How many bytes left to output after current DMA completes
    DMAChannel *_dmaTX = nullptr;
    DMAChannel *_dmaRX = nullptr;
    uint8_t _transferWriteFill = 0xFF;
    size_t db_count = 0;
    uint8_t *db_RXbuf = nullptr;

    void add_to_buffer(int16_t x);

    uint32_t pre_begin_miso_portControlRegister = 0;
    uint32_t pre_begin_mosi_portControlRegister = 0;
    uint32_t pre_begin_sck_portControlRegister = 0;
    uint32_t pre_begin_cs_portControlRegister = 0;
    
    uint32_t pre_begin_miso_portConfigRegister = 0;
    uint32_t pre_begin_mosi_portConfigRegister = 0;
    uint32_t pre_begin_sck_portConfigRegister = 0;
    uint32_t pre_begin_cs_portConfigRegister = 0;

    uint32_t pre_begin_miso_select_input_register = 0;
    uint32_t pre_begin_mosi_select_input_register = 0;
    uint32_t pre_begin_sck_select_input_register = 0;
    uint32_t pre_begin_cs_select_input_register = 0;

    bool have_begun = 0;
    bool SDIN_line_in_use = 0;
    bool SDOUT_line_in_use = 0;
    char cRT (bool NormallyR);

    uint32_t spi_pad_io = IOMUXC_PAD_HYS | IOMUXC_PAD_PUS(3) |IOMUXC_PAD_PUE | IOMUXC_PAD_PKE | IOMUXC_PAD_DSE(7) | IOMUXC_PAD_SPEED(0);

    void *last_transfer_TXbuf = nullptr;
    void *last_transfer_RXbuf = nullptr;
    size_t last_transfer_count = 0;

    void (*_onCompleteCallback)(void) = nullptr;

};

extern T4_DMA_SPI_SLAVE SPI_SLAVE;
extern T4_DMA_SPI_SLAVE SPI_SLAVE1;
extern T4_DMA_SPI_SLAVE SPI_SLAVE2;

#endif