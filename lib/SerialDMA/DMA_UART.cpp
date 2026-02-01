/*
  DMA_UART.cpp - Library for Teensy LPUART receiving and transmitting over DMA FIFO buffers
  Created by Sicco Dwars, Oct 20, 2022
*/

#include "DMA_UART.h"
#include <Arduino.h>

#define RX_BUFSIZE_inBytes (2*rx_buffer_size_)
#define TX_BUFSIZE_inBytes (2*tx_buffer_size_)

#define UART_CLOCK 24000000
//#define CTRL_ENABLE         (LPUART_CTRL_TE | LPUART_CTRL_RE | LPUART_CTRL_RIE | LPUART_CTRL_ILIE)
//#define CTRL_TX_ACTIVE      (CTRL_ENABLE | LPUART_CTRL_TIE)
//#define CTRL_TX_COMPLETING  (CTRL_ENABLE | LPUART_CTRL_TCIE)
//#define CTRL_TX_INACTIVE    CTRL_ENABLE 

#define CTRL_ENABLE         (LPUART_CTRL_TE | LPUART_CTRL_RE)
#define CTRL_TX_INACTIVE    CTRL_ENABLE 

extern "C" {
    extern void xbar_connect(unsigned int input, unsigned int output);
}

void (*DMA_UART::process_serial_rx_call)(uint16_t c) = DMA_UART::process_serial_rx_call_DefaultUnused;
void DMA_UART::process_serial_rx_call_DefaultUnused(uint16_t c) {}

uint32_t LPUART_to_DMAMUX_SOURCE_LPUART_TX (int i)
{
  switch (i)
  {
    case 1 : return (DMAMUX_SOURCE_LPUART1_TX);
    case 2 : return (DMAMUX_SOURCE_LPUART2_TX);
    case 3 : return (DMAMUX_SOURCE_LPUART3_TX);
    case 4 : return (DMAMUX_SOURCE_LPUART4_TX);
    case 5 : return (DMAMUX_SOURCE_LPUART5_TX);
    case 6 : return (DMAMUX_SOURCE_LPUART6_TX);
    case 7 : return (DMAMUX_SOURCE_LPUART7_TX);
    case 8 : return (DMAMUX_SOURCE_LPUART8_TX);
  }
  return 0;
}

uint32_t LPUART_to_DMAMUX_SOURCE_LPUART_RX (int i)
{
  switch (i)
  {
    case 1 : return (DMAMUX_SOURCE_LPUART1_RX);
    case 2 : return (DMAMUX_SOURCE_LPUART2_RX);
    case 3 : return (DMAMUX_SOURCE_LPUART3_RX);
    case 4 : return (DMAMUX_SOURCE_LPUART4_RX);
    case 5 : return (DMAMUX_SOURCE_LPUART5_RX);
    case 6 : return (DMAMUX_SOURCE_LPUART6_RX);
    case 7 : return (DMAMUX_SOURCE_LPUART7_RX);
    case 8 : return (DMAMUX_SOURCE_LPUART8_RX);
  }
  return 0;
}

int DMA_UART::begin(int new_baudrate, uint16_t new_format, int new_RS485_DIR_Pin)
{
    baudrate = new_baudrate;
    format = new_format;
    
    RS485_DIR_Pin = new_RS485_DIR_Pin;

    if (RS485_DIR_Pin > 0)
      pinMode(RS485_DIR_Pin, OUTPUT);
    
    Serial.printf ("Starting Serial (DMA_Serial_%d, LPUART_%d) at %d baud, format=%d, ", 
        hardware->serial_index, hardware->LPUART_index, baudrate, format);

    if (RS485_DIR_Pin < 0) // 1 wire/
        Serial.printf ("RX%d=pin_%d TX%d=pin_%d\n", 
            hardware->serial_index, hardware->tx_pins[tx_pin_index_].pin, hardware->serial_index, hardware->tx_pins[tx_pin_index_].pin);
    else
        Serial.printf ("RX%d=pin_%d TX%d=pin_%d\n", 
            hardware->serial_index, hardware->rx_pins[rx_pin_index_].pin, hardware->serial_index, hardware->tx_pins[tx_pin_index_].pin);

    float base = (float)UART_CLOCK / (float)baudrate;
    float besterr = 1e20;
    int bestdiv = 1;
    int bestosr = 4;
    for (int osr=4; osr <= 32; osr++) {
      float div = base / (float)osr;
      int divint = (int)(div + 0.5f);
      if (divint < 1) divint = 1;
      else if (divint > 8191) divint = 8191;
      float err = ((float)divint - div) / div;
      if (err < 0.0f) err = -err;
      if (err <= besterr) {
        besterr = err;
        bestdiv = divint;
        bestosr = osr;
      }
    }
    hardware->ccm_register |= hardware->ccm_value;
    half_duplex_mode_ = ((format & SERIAL_HALF_DUPLEX) != 0) || (RS485_DIR_Pin < 0);
    if (!half_duplex_mode_)
    {
      *(portControlRegister(hardware->rx_pins[rx_pin_index_].pin)) = IOMUXC_PAD_DSE(7) | IOMUXC_PAD_PKE | IOMUXC_PAD_PUE | IOMUXC_PAD_PUS(3) | IOMUXC_PAD_HYS;
      *(portConfigRegister(hardware->rx_pins[rx_pin_index_].pin)) = hardware->rx_pins[rx_pin_index_].mux_val;
      if (hardware->rx_pins[rx_pin_index_].select_input_register) 
      {
        *(hardware->rx_pins[rx_pin_index_].select_input_register) =  hardware->rx_pins[rx_pin_index_].select_val;   
      } 
  
      *(portControlRegister(hardware->tx_pins[tx_pin_index_].pin)) =  IOMUXC_PAD_SRE | IOMUXC_PAD_DSE(3) | IOMUXC_PAD_SPEED(3);
      *(portConfigRegister(hardware->tx_pins[tx_pin_index_].pin)) = hardware->tx_pins[tx_pin_index_].mux_val;
    } 
    else 
    {
      // Half duplex maybe different pin pad config like PU...    
      *(portControlRegister(hardware->tx_pins[tx_pin_index_].pin)) =  IOMUXC_PAD_SRE | IOMUXC_PAD_DSE(3) | IOMUXC_PAD_SPEED(3) 
          | IOMUXC_PAD_PKE | IOMUXC_PAD_PUE | IOMUXC_PAD_PUS(3);
      *(portConfigRegister(hardware->tx_pins[tx_pin_index_].pin)) = hardware->tx_pins[tx_pin_index_].mux_val;
    }
    if (hardware->tx_pins[tx_pin_index_].select_input_register) 
    {
      *(hardware->tx_pins[tx_pin_index_].select_input_register) =  hardware->tx_pins[tx_pin_index_].select_val;   
    } 
    //hardware->rx_mux_register = hardware->rx_mux_val;
    //hardware->tx_mux_register = hardware->tx_mux_val;
  
    port->BAUD = LPUART_BAUD_OSR(bestosr - 1) | LPUART_BAUD_SBR(bestdiv)
      | (bestosr <= 8 ? LPUART_BAUD_BOTHEDGE : 0);
    port->PINCFG = 0;
    port->CTRL &= 0xfff3ffff; // clear TE and RE
    port->FIFO |= LPUART_FIFO_TXFE | LPUART_FIFO_RXFE;
    uint32_t ctrl = CTRL_TX_INACTIVE;
    ctrl |= (format & (LPUART_CTRL_PT | LPUART_CTRL_PE) );  // configure parity - turn off PT, PE, M and configure PT, PE
    if (format & 0x04) 
      ctrl |= LPUART_CTRL_M;   // 9 bits (might include parity)
    if ((format & 0x0F) == 0x04) 
      ctrl |=  LPUART_CTRL_R9T8; // 8N2 is 9 bit with 9th bit always 1
    // Bit 5 TXINVERT
    if (format & 0x20) 
      ctrl |= LPUART_CTRL_TXINV;   // tx invert
  
    // Now see if the user asked for Half duplex:
    if (half_duplex_mode_) 
      ctrl |= (LPUART_CTRL_LOOPS | LPUART_CTRL_RSRC);
  
    // write out computed CTRL
    port->CTRL = ctrl;
  
    // Bit 3 10 bit - Will assume that begin already cleared it.
    // process some other bits which change other registers.
    if (format & 0x08)  
      port->BAUD |= LPUART_BAUD_M10;
  
    // Bit 4 RXINVERT 
    uint32_t c = port->STAT & ~LPUART_STAT_RXINV;
    if (format & 0x10) 
      c |= LPUART_STAT_RXINV;    // rx invert
    port->STAT = c;
  
    // bit 8 can turn on 2 stop bit mote
    if ( format & 0x100) 
      port->BAUD |= LPUART_BAUD_SBNS;  
    
    port->CTRL |= 1<<22;          // enable interrupt when tx done
    port->CTRL |= 1<<21;          // enable interrupt when rx done
    port->FIFO |= 0x01 << 10;    // RXIDEN Enable RDRF assertion due to partially filled FIFO when receiver is idle for 1 character
    port->FIFO |= 0x08;          // RXFE(Receive FIFO Enable)  
    port->FIFO &= ~0x80;          // TXFE(Transmit FIFO Disable)  
    port->BAUD |= 0x200000;      // RDMAE(Receiver Full DMA Enable)
    port->BAUD |= 0x800000;      // TDMAE(Transmitter Empty DMA Enable)
    port->WATER &= ~0x30000;     // WATER MARK set to 0
 
    read_buf_tail = 0;
    write_buf_head = 0;
    write_buf_used = 0;

    for(uint16_t i=0; i<rx_buffer_size_; i++)
      rx_buffer_[i] = 0xffff;
     
    rx_dmachannel_->begin();
    rx_dmachannel_->source(port->DATA);
    rx_dmachannel_->destinationCircular(rx_buffer_, RX_BUFSIZE_inBytes);
    rx_dmachannel_->transferSize(2);
    rx_dmachannel_->transferCount(1);
    
    rx_dmachannel_->triggerAtHardwareEvent(LPUART_to_DMAMUX_SOURCE_LPUART_RX (hardware->LPUART_index));
    rx_dmachannel_->enable(); 

    tx_dmachannel_->begin();
    tx_dmachannel_->sourceCircular(tx_buffer_, TX_BUFSIZE_inBytes);
    tx_dmachannel_->destination(port->DATA);
    tx_dmachannel_->transferSize(2);
    tx_dmachannel_->disableOnCompletion();
    tx_dmachannel_->triggerAtHardwareEvent(LPUART_to_DMAMUX_SOURCE_LPUART_TX (hardware->LPUART_index));
    tx_dmachannel_->interruptAtCompletion();

    tx_dmachannel_->attachInterrupt (hardware->isr_dma, hardware->irq_dma_priority);
  
    attachInterruptVector(hardware->irq, hardware->isr_uart);
    NVIC_SET_PRIORITY(hardware->irq, hardware->irq_uart_priority);
    NVIC_ENABLE_IRQ(hardware->irq);
 
    return 1;
}

void DMA_UART::end()
{
  process_serial_rx_call = process_serial_rx_call_DefaultUnused;

  if (!(hardware->ccm_register & hardware->ccm_value)) 
    return;
  while (busy_txing) 
    yield();  // wait for buffered data to send
  
  port->CTRL = 0; // disable the TX and RX ...

  // Not sure if this is best, but I think most IO pins default to Mode 5? which appears to be digital IO? 
  *(portConfigRegister(hardware->rx_pins[rx_pin_index_].pin)) = 5;
  *(portConfigRegister(hardware->tx_pins[tx_pin_index_].pin)) = 5;

  NVIC_DISABLE_IRQ(hardware->irq);

  rx_dmachannel_->detachInterrupt();
  tx_dmachannel_->detachInterrupt();
  rx_dmachannel_->disable();
  tx_dmachannel_->disable();

  read_buf_tail = 0;
  write_buf_head = 0;
  write_buf_used = 0;
}

void DMA_UART::setRX(uint8_t pin)
{
  if (pin != hardware->rx_pins[rx_pin_index_].pin) 
  {
    for (uint8_t rx_pin_new_index = 0; rx_pin_new_index < cnt_rx_pins; rx_pin_new_index++) 
    {
      if (pin == hardware->rx_pins[rx_pin_new_index].pin) 
      {
        // new pin - so lets maybe reset the old pin to INPUT? and then set new pin parameters
        // only change IO pins if done after begin has been called. 
        if ((hardware->ccm_register & hardware->ccm_value)) 
        {
          *(portConfigRegister(hardware->rx_pins[rx_pin_index_].pin)) = 5;

          // now set new pin info.
          *(portControlRegister(hardware->rx_pins[rx_pin_new_index].pin)) =  IOMUXC_PAD_DSE(7) | IOMUXC_PAD_PKE | IOMUXC_PAD_PUE | IOMUXC_PAD_PUS(3) | IOMUXC_PAD_HYS;;
          *(portConfigRegister(hardware->rx_pins[rx_pin_new_index].pin)) = hardware->rx_pins[rx_pin_new_index].mux_val;
          if (hardware->rx_pins[rx_pin_new_index].select_input_register) 
          {
            *(hardware->rx_pins[rx_pin_new_index].select_input_register) =  hardware->rx_pins[rx_pin_new_index].select_val;   
          }
        }   
        rx_pin_index_ = rx_pin_new_index;
        return;  // done. 
      }
    }
    // If we got to here and did not find a valid pin there.  Maybe see if it is an XBar pin... 
    for (uint8_t i = 0; i < Hcount_pin_to_xbar_info; i++) 
    {
      if (Hpin_to_xbar_info[i].pin == pin) 
      {
        // So it is an XBAR pin set the XBAR..
        //Serial.printf("ACTS XB(%d), X(%u %u), MUX:%x\n", i, Hpin_to_xbar_info[i].xbar_in_index, 
        //      hardware->xbar_out_lpuartX_trig_input,  Hpin_to_xbar_info[i].mux_val);
        CCM_CCGR2 |= CCM_CCGR2_XBAR1(CCM_CCGR_ON);
        xbar_connect(Hpin_to_xbar_info[i].xbar_in_index, hardware->xbar_out_lpuartX_trig_input);

        // We need to update port register to use this as the trigger
        port->PINCFG = LPUART_PINCFG_TRGSEL(1);  // Trigger select as alternate RX

        //  configure the pin. 
        *(portControlRegister(pin)) = IOMUXC_PAD_DSE(7) | IOMUXC_PAD_PKE | IOMUXC_PAD_PUE | IOMUXC_PAD_PUS(3) | IOMUXC_PAD_HYS;;
        *(portConfigRegister(pin)) = Hpin_to_xbar_info[i].mux_val;
        port->MODIR |= LPUART_MODIR_TXCTSE;
        if (Hpin_to_xbar_info[i].select_input_register) *(Hpin_to_xbar_info[i].select_input_register) = Hpin_to_xbar_info[i].select_val;
        //Serial.printf("SerialX::begin stat:%x ctrl:%x fifo:%x water:%x\n", port->STAT, port->CTRL, port->FIFO, port->WATER );
        //Serial.printf("  PINCFG: %x MODIR: %x\n", port->PINCFG, port->MODIR); 
        return;
      }
    }
  }
}

void DMA_UART::setTX(uint8_t pin, bool opendrain)
{
  uint8_t tx_pin_new_index = tx_pin_index_;

  if (pin != hardware->tx_pins[tx_pin_index_].pin) 
  {
    for (tx_pin_new_index = 0; tx_pin_new_index < cnt_tx_pins; tx_pin_new_index++) 
    {
      if (pin == hardware->tx_pins[tx_pin_new_index].pin) 
      {
        break;
      }
    }
    if (tx_pin_new_index == cnt_tx_pins) return;  // not a new valid pid... 
  }

  // turn on or off opendrain mode.
  // new pin - so lets maybe reset the old pin to INPUT? and then set new pin parameters
  if ((hardware->ccm_register & hardware->ccm_value)) 
  {  // only do if we are already active. 
    if (tx_pin_new_index != tx_pin_index_) 
    {
      *(portConfigRegister(hardware->tx_pins[tx_pin_index_].pin)) = 5;
    
      *(portConfigRegister(hardware->tx_pins[tx_pin_new_index].pin)) = hardware->tx_pins[tx_pin_new_index].mux_val;
    }
  }
  // now set new pin info.
  tx_pin_index_ = tx_pin_new_index;
  if (opendrain) 
    *(portControlRegister(pin)) = IOMUXC_PAD_ODE | IOMUXC_PAD_DSE(3) | IOMUXC_PAD_SPEED(3);
  else  
    *(portControlRegister(pin)) = IOMUXC_PAD_SRE | IOMUXC_PAD_DSE(3) | IOMUXC_PAD_SPEED(3);
}


void DMA_UART::isr_dma()
{
  port->CTRL |= LPUART_CTRL_TCIE;          // enable interrupt when tx done
  tx_dmachannel_->clearInterrupt ();
}

void DMA_UART::isr_uart()
{
  if ((port->CTRL & LPUART_CTRL_TCIE) && (port->STAT & LPUART_STAT_TC))
  {
    if (RS485_DIR_Pin > 0)
        digitalWrite(RS485_DIR_Pin, LOW);
    else
      if (RS485_DIR_Pin < 0)
         port->CTRL &= ~LPUART_CTRL_TXDIR;  // disable transmitter pin (for single wire Rx/Tx mode)
    
    busy_txing = 0;
    port->CTRL &= ~LPUART_CTRL_TCIE;
  }

  while (available ())
  {
      uint16_t ch = read ();
      process_serial_rx_call(ch);
  }

}

int DMA_UART::availableForWrite ()
{
  uint32_t HEAD = ((uint32_t)tx_dmachannel_->TCD->SADDR >> 1) & tx_buffer_mask_;
  if (HEAD == write_buf_head)
  {
    if (write_buf_used) 
      return 0;
    else
      return tx_buffer_size_;
  }
  if (HEAD < write_buf_head)
    HEAD += tx_buffer_size_;

  return HEAD - write_buf_head;
}

void DMA_UART::write (uint16_t c)
{
  while (busy_txing)
    yield();


//  while (!availableForWrite ())
//    yield();
  
  if (write_buf_used < tx_buffer_size_)
  {
    tx_buffer_[write_buf_head++] = c;
    
    write_buf_used++;
    if (write_buf_head >= tx_buffer_size_)
      write_buf_head = 0;
  }
}

void DMA_UART::write (uint8_t c)
{
  write ((uint16_t)c);
}

void DMA_UART::write (char c)
{
  write ((uint16_t)c);
}

void DMA_UART::write (uint8_t *cs, int n)
{
  while (n--)
    write ((uint16_t)*cs++);
}

void DMA_UART::write (char *cs, int n)
{
  while (n--)
    write ((uint16_t)*cs++);
}

void DMA_UART::write (uint16_t *cs, int n)
{
  while (n--)
    write (*cs++);
}

void DMA_UART::write (char *st)
{
  while (*st)
    write ((uint16_t)*st++);
}

void DMA_UART::trigger_tx ()
{
  if ((write_buf_used) && (!busy_txing))
  {
    tx_dmachannel_->transferCount(write_buf_used);
    if (RS485_DIR_Pin > 0)
        digitalWrite(RS485_DIR_Pin, HIGH); 
    else
      if (RS485_DIR_Pin < 0)
        port->CTRL |= LPUART_CTRL_TXDIR; // enable transmitter

    busy_txing = 1;
    port->CTRL &= ~(1<<22);  // disable interrupt when tx done
    tx_dmachannel_->enable();
    write_buf_used = 0;
  }
}

void DMA_UART::write_debug_message ()
{  
  char st[100];
  sprintf (st, "Testing DMA_Serial_%d via LPUART_%d RXpin=%d TXpin=%d cnt=%d\r\n", hardware->serial_index, hardware->LPUART_index, 
      hardware->rx_pins[rx_pin_index_].pin, hardware->tx_pins[tx_pin_index_].pin, testcount++); 
  write (st);
  trigger_tx ();
}

int DMA_UART::available ()
{
  uint32_t HEAD = ((uint32_t)rx_dmachannel_->TCD->DADDR >> 1) & rx_buffer_mask_;
  if (HEAD == read_buf_tail)
  {
    if (rx_buffer_[read_buf_tail] == 0xffff)
      return 0;
    else
      return rx_buffer_size_;
  }
  
  if (HEAD < read_buf_tail)
    HEAD += rx_buffer_size_;

  return HEAD - read_buf_tail;
}

int DMA_UART::read ()
{
  if (rx_buffer_[read_buf_tail] == 0xffff)
    return -1;
  
  int ch = rx_buffer_[read_buf_tail] & 0x1ff;
  rx_buffer_[read_buf_tail] = 0xffff;
  if (++read_buf_tail >= rx_buffer_size_)
     read_buf_tail = 0;
  return ch;
}

void DMA_UART::read (uint16_t *cs, int n)
{
  while (n--)
    *cs++ = read();   
}

void DMA_UART::read (uint8_t *cs, int n)
{
  while (n--)
    *cs++ = read();   
}

void DMA_UART::timerAction () 
{
    while (rx_buffer_[read_buf_tail] != 0xffff)
    {
      uint16_t ch = rx_buffer_[read_buf_tail] & 0x1ff;
      rx_buffer_[read_buf_tail] = 0xffff;
      if (++read_buf_tail >= rx_buffer_size_)
        read_buf_tail = 0;

      Serial.printf ("%c(0x%03x,%d) ", ch, ch, hardware->serial_index);
//      Serial.printf (" DADDR=%08lx ", rx_dmachannel_->TCD->DADDR);
    }

// Serial.printf ("RX%d available=%d\n", hardware->serial_index, available());
// Serial.printf ("TX%d available=%d\n", hardware->serial_index, availableForWrite());
}

const Hpin_to_xbar_info_t PROGMEM Hpin_to_xbar_info[] = {
  {0,  17, 1, &IOMUXC_XBAR1_IN17_SELECT_INPUT, 0x1},
  {1,  16, 1, nullptr, 0},
  {2,   6, 3, &IOMUXC_XBAR1_IN06_SELECT_INPUT, 0x0},
  {3,   7, 3, &IOMUXC_XBAR1_IN07_SELECT_INPUT, 0x0},
  {4,   8, 3, &IOMUXC_XBAR1_IN08_SELECT_INPUT, 0x0},
  {5,  17, 3, &IOMUXC_XBAR1_IN17_SELECT_INPUT, 0x0},
  {7,  15, 1, nullptr, 0 },
  {8,  14, 1, nullptr, 0},
  {30, 23, 1, &IOMUXC_XBAR1_IN23_SELECT_INPUT, 0x0},
  {31, 22, 1, &IOMUXC_XBAR1_IN22_SELECT_INPUT, 0x0},
  {32, 10, 1, nullptr, 0},
  {33,  9, 3, &IOMUXC_XBAR1_IN09_SELECT_INPUT, 0x0},

#ifdef ARDUINO_TEENSY41
  {36, 16, 1, nullptr, 0},
  {37, 17, 1, &IOMUXC_XBAR1_IN17_SELECT_INPUT, 0x3},
  {42,  7, 3, &IOMUXC_XBAR1_IN07_SELECT_INPUT, 0x1},
  {43,  6, 3, &IOMUXC_XBAR1_IN06_SELECT_INPUT, 0x1},
  {44,  5, 3, &IOMUXC_XBAR1_IN05_SELECT_INPUT, 0x1},
  {45,  4, 3, &IOMUXC_XBAR1_IN04_SELECT_INPUT, 0x1},
  {46,  9, 3, &IOMUXC_XBAR1_IN09_SELECT_INPUT, 0x1},
  {47,  8, 3, &IOMUXC_XBAR1_IN08_SELECT_INPUT, 0x1}
#elif defined(ARDUINO_TEENSY_MICROMOD)
  {34,  7, 3, &IOMUXC_XBAR1_IN07_SELECT_INPUT, 0x1},
  {35,  6, 3, &IOMUXC_XBAR1_IN06_SELECT_INPUT, 0x1},
  {36,  5, 3, &IOMUXC_XBAR1_IN05_SELECT_INPUT, 0x1},
  {37,  4, 3, &IOMUXC_XBAR1_IN04_SELECT_INPUT, 0x1},
  {38,  8, 3, &IOMUXC_XBAR1_IN08_SELECT_INPUT, 0x1},
  {39,  9, 3, &IOMUXC_XBAR1_IN09_SELECT_INPUT, 0x1}
#else 
  {34,  7, 3, &IOMUXC_XBAR1_IN07_SELECT_INPUT, 0x1},
  {35,  6, 3, &IOMUXC_XBAR1_IN06_SELECT_INPUT, 0x1},
  {36,  5, 3, &IOMUXC_XBAR1_IN05_SELECT_INPUT, 0x1},
  {37,  4, 3, &IOMUXC_XBAR1_IN04_SELECT_INPUT, 0x1},
  {38,  9, 3, &IOMUXC_XBAR1_IN09_SELECT_INPUT, 0x1},
  {39,  8, 3, &IOMUXC_XBAR1_IN08_SELECT_INPUT, 0x1}
#endif
};

const uint8_t PROGMEM Hcount_pin_to_xbar_info = sizeof(Hpin_to_xbar_info)/sizeof(Hpin_to_xbar_info[0]);
