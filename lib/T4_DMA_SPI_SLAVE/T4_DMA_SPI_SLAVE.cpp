#include "T4_DMA_SPI_SLAVE.h"

//#define DEBUG_DMA_TRANSFERS
//#define TRANSFER_COUNT_FIXED

void T4_DMA_SPI_SLAVE::begin(uint8_t spi_mode, uint8_t bit_order, uint8_t pincfg)
{
    pre_begin_miso_portControlRegister = *(portControlRegister(hardware().miso_pin[miso_pin_index]));
    pre_begin_mosi_portControlRegister = *(portControlRegister(hardware().mosi_pin[mosi_pin_index])); 
    pre_begin_sck_portControlRegister = *(portControlRegister(hardware().sck_pin[sck_pin_index]));
    pre_begin_cs_portControlRegister = *(portControlRegister(hardware().cs_pin[cs_pin_index]));
    
    pre_begin_miso_portConfigRegister = *(portConfigRegister(hardware().miso_pin[miso_pin_index]));
    pre_begin_mosi_portConfigRegister = *(portConfigRegister(hardware().mosi_pin [mosi_pin_index]));
    pre_begin_sck_portConfigRegister = *(portConfigRegister(hardware().sck_pin [sck_pin_index]));
    pre_begin_cs_portConfigRegister = *(portConfigRegister(hardware().cs_pin [cs_pin_index]));

    pre_begin_miso_select_input_register = hardware().miso_select_input_register;
    pre_begin_mosi_select_input_register = hardware().mosi_select_input_register;
    pre_begin_sck_select_input_register = hardware().sck_select_input_register;
    pre_begin_cs_select_input_register = hardware().cs_select_input_register;

    hardware().clock_gate_register &= ~hardware().clock_gate_mask;

    CCM_CBCMR = (CCM_CBCMR & ~(CCM_CBCMR_LPSPI_PODF_MASK | CCM_CBCMR_LPSPI_CLK_SEL_MASK)) |  CCM_CBCMR_LPSPI_PODF(2) | CCM_CBCMR_LPSPI_CLK_SEL(1); // pg 714
	
    hardware().clock_gate_register |= hardware().clock_gate_mask;

    port().CR = LPSPI_CR_RST;

	  port().CR = 0;

    //port().CFGR0 = LPSPI_CFGR0_CIRFIFO;
    port().CFGR1 = LPSPI_CFGR1_SAMPLE;
    port().CR = LPSPI_CR_MEN;
    port().CR |= 0x00000200; // FIFO reset

    uint32_t tcr = LPSPI_TCR_FRAMESZ(7) | (uint32_t)hardware().pcs_val[cs_pin_index] << 24;
    port().TCR = tcr;
    
    setDataMode (spi_mode);
    setBitOrder (bit_order);

    Enable_to_be_used_SIN_SOUT_lines (pincfg);

    *(portControlRegister(hardware().cs_pin[cs_pin_index])) = spi_pad_io;
    *(portConfigRegister(hardware().cs_pin [cs_pin_index])) = hardware().cs_mux[cs_pin_index];
    hardware().cs_select_input_register = hardware().cs_select_val[cs_pin_index];

    *(portControlRegister(hardware().sck_pin[sck_pin_index])) = spi_pad_io;
    *(portConfigRegister(hardware().sck_pin[sck_pin_index])) = hardware().sck_mux[sck_pin_index];
    hardware().sck_select_input_register = hardware().sck_select_val[sck_pin_index];

	  port().CCR = 0;

    attachInterruptVector(hardware().irq, hardware().spi_isr);
    NVIC_SET_PRIORITY(hardware().irq, 126);
    NVIC_ENABLE_IRQ(hardware().irq);

		port().IER = LPSPI_IER_FCIE ;
  	//port().TDR = 0;  // pre-load first byte

    if (_dma_state == DMAState::notAllocated) 
    {
      if (!initDMAChannels())
      {
        Serial.printf ("error initDMAChannels for SPI\n");
      }
    }

    have_begun = 1;
}

void T4_DMA_SPI_SLAVE::end() 
{
	// only do something if we have begun
	if (have_begun) 
  {
		port().CR = 0;  // turn off the enable
    hardware().sck_select_input_register = pre_begin_sck_select_input_register;
	  *(portConfigRegister(hardware().sck_pin [sck_pin_index])) = pre_begin_sck_portConfigRegister;
	  *(portControlRegister(hardware().sck_pin[sck_pin_index])) = pre_begin_sck_portControlRegister;
    hardware().cs_select_input_register = pre_begin_cs_select_input_register;
	  *(portConfigRegister(hardware().cs_pin [cs_pin_index])) = pre_begin_cs_portConfigRegister;
	  *(portControlRegister(hardware().cs_pin[cs_pin_index])) = pre_begin_cs_portControlRegister;
    T4_DMA_SPI_SLAVE::config_pins (0, 0);

    if (_dma_state >= DMAState::notAllocated)  
    {
		  delete _dmaTX; // release it
		  delete _dmaRX; // release it
      _dma_state = DMAState::notAllocated;
    }

    have_begun = 0;
	}
}

void printreg (const char *st, uint32_t reg_address)
{
  uint32_t *p = (uint32_t*)reg_address;
  Serial.printf ("reg %s at %08x is %08x\n", st, reg_address, *p);
}

void T4_DMA_SPI_SLAVE::debug()
{
  int i;
  Serial.printf ("\ndebug LPSPI\n");
  Serial.printf ("irq=%d ", hardware().irq);
  Serial.printf ("clock_gate_register = %08x ", hardware().clock_gate_register);
  Serial.printf ("clock_gate_mask = %08x\n", hardware().clock_gate_mask);
  Serial.printf (" MISO pin : %d options, selected=%d ", CNT_MISO_PINS, miso_pin_index);
  i = miso_pin_index;
//  for (i=0; i<CNT_MISO_PINS; i++)
  {
    Serial.printf ("%3d:", hardware().miso_pin[i]);
    Serial.printf ("%08x-", hardware().miso_mux[i]);
    Serial.printf ("%d-", hardware().miso_select_val[i]);
    Serial.printf ("PAD=%08x ", *(portControlRegister(hardware().miso_pin[i])));
  }
  Serial.printf (" miso_select_input_register = %08x=%08x\n", &hardware().miso_select_input_register, hardware().miso_select_input_register);
  Serial.printf (" MOSI pin : %d options, selected=%d ", CNT_MOSI_PINS, mosi_pin_index);
  i = mosi_pin_index;
//  for (i=0; i<CNT_MOSI_PINS; i++)
  {
    Serial.printf ("%3d:",hardware().mosi_pin[i]);
    Serial.printf ("%08x-",hardware().mosi_mux[i]);
    Serial.printf ("%d-", hardware().mosi_select_val[i]);
    Serial.printf ("PAD=%08x ", *(portControlRegister(hardware().mosi_pin[i])));
  }
  Serial.printf (" mosi_select_input_register = %08x=%08x\n", &hardware().mosi_select_input_register, hardware().mosi_select_input_register);
  Serial.printf (" SCK pin  : %d options, selected=%d ", CNT_SCK_PINS, sck_pin_index);
  i = sck_pin_index;
//  for (i=0; i<CNT_SCK_PINS; i++)
  {
    Serial.printf ("%3d:",hardware().sck_pin[i]);
    Serial.printf ("%08x-",hardware().sck_mux[i]);
    Serial.printf ("%d-", hardware().sck_select_val[i]);
    Serial.printf ("PAD=%08x ", *(portControlRegister(hardware().sck_pin[i])));
  }
  Serial.printf (" sck_select_input_register = %08x=%08x\n", &hardware().sck_select_input_register, hardware().sck_select_input_register);

  Serial.printf (" CS pin   : %d options, selected=%d ", CNT_CS_PINS, cs_pin_index);
  i = cs_pin_index;
//  for (i=0; i<CNT_CS_PINS; i++)
  {
    Serial.printf ("%3d:",hardware().cs_pin[i]);
    Serial.printf ("%08x-",hardware().cs_mux[i]);
    Serial.printf ("%d-", hardware().cs_select_val[i]);
    Serial.printf ("PAD=%08x ", *(portControlRegister(hardware().cs_pin[i])));
  }
  Serial.printf (" cs_select_input_register = %08x=%08x\n", &hardware().cs_select_input_register, hardware().cs_select_input_register);
}

static uint8_t bit_bucket;

//=========================================================================
// Init the DMA channels
//=========================================================================
bool T4_DMA_SPI_SLAVE::initDMAChannels() 
{
	// Allocate our channels. 
	_dmaTX = new DMAChannel();
	if (_dmaTX == nullptr) 
  		return false;
	
	_dmaRX = new DMAChannel();
	if (_dmaRX == nullptr) 
  {
		delete _dmaTX; // release it
		_dmaTX = nullptr; 
		return false;
	}

	// Let's setup the RX chain
	_dmaRX->disable();
	_dmaRX->source((volatile uint8_t&)port().RDR);
	_dmaRX->disableOnCompletion();
	_dmaRX->triggerAtHardwareEvent(hardware().rx_dma_channel);
  //Serial.printf("RX DMA channel %d, trigger at hardware event %d\n", hardware().rx_dma_channel, hardware().tx_dma_channel);
	_dmaRX->attachInterrupt(hardware().dma_rxisr, 2*16);
	_dmaRX->interruptAtCompletion();

	// We may be using settings chain here so lets set it up. 
	// Now lets setup TX chain.  Note if trigger TX is not set
	// we need to have the RX do it for us.
	_dmaTX->disable();
	_dmaTX->destination((volatile uint8_t&)port().TDR);
	//_dmaTX->disableOnCompletion();

	//if (hardware().tx_dma_channel) 
		_dmaTX->triggerAtHardwareEvent(hardware().tx_dma_channel);
  //else 
   //_dmaTX->triggerAtTransfersOf(*_dmaRX);

	_dma_state = DMAState::idle;  // Should be first thing set!
	return true;
}

#ifndef TRANSFER_COUNT_FIXED
inline void DMAChanneltransferCount(DMAChannel * dmac, unsigned int len) 
{
	// note does no validation of length...
	DMABaseClass::TCD_t *tcd = dmac->TCD;
	if (!(tcd->BITER & DMA_TCD_BITER_ELINK)) 
		tcd->BITER = len & 0x7fff;
  else 
		tcd->BITER = (tcd->BITER & 0xFE00) | (len & 0x1ff);

	tcd->CITER = tcd->BITER; 
}
#else 
inline void DMAChanneltransferCount(DMAChannel * dmac, unsigned int len) 
{
	dmac->transferCount(len);
}
#endif

#ifdef DEBUG_DMA_TRANSFERS
void dumpDMA_TCD(DMABaseClass *dmabc)
{
	Serial.printf("%x %x:", (uint32_t)dmabc, (uint32_t)dmabc->TCD);
	Serial.printf("SA:%x SO:%d AT:%x NB:%x SL:%d DA:%x DO: %d CI:%x DL:%x CS:%x BI:%x\n", 
    (uint32_t)dmabc->TCD->SADDR, dmabc->TCD->SOFF, dmabc->TCD->ATTR, dmabc->TCD->NBYTES, dmabc->TCD->SLAST, 
    (uint32_t)dmabc->TCD->DADDR, dmabc->TCD->DOFF, dmabc->TCD->CITER, dmabc->TCD->DLASTSGA, dmabc->TCD->CSR, dmabc->TCD->BITER);
}
#endif


bool T4_DMA_SPI_SLAVE::prepare_for_slave_transfer(const void *TXbuf, void *RXbuf, size_t count) 
{

   port().CR &= ~LPSPI_CR_MEN;
//Serial.printf("SR before flush: %08x\n", port().SR);  // watch bit 24 (MBF)
  port().CR |= LPSPI_CR_RTF | LPSPI_CR_RRF;
  port().CR &= ~(LPSPI_CR_RTF | LPSPI_CR_RRF);
  port().CR |= LPSPI_CR_MEN;
  //Serial.printf("FSR after flush: %08x\n", port().FSR);
  //Serial.printf("TCR: %08x\n", port().TCR);
    // ... rest of function

    

  last_transfer_TXbuf = (void *)TXbuf;
  last_transfer_RXbuf = RXbuf;
  last_transfer_count = count;
  
#ifdef DEBUG_DMA_TRANSFERS
  uint8_t *p = (uint8_t*)TXbuf;
	Serial.printf(">> [%d] bytes: ", count);
  if (TXbuf)
    for (int i=0; i<(int)count; i++)
	    Serial.printf("%02x ", *p++);
  else
    Serial.printf("READONLY");
  Serial.printf("\n");
  db_count = count;
  db_RXbuf = (uint8_t*)RXbuf;
#endif

	if (_dma_state == DMAState::notAllocated) 
  {
		if (!initDMAChannels())
    {
#ifdef DEBUG_DMA_TRANSFERS
    Serial.printf("error:!initDMAChannels()\n");
#endif
			return false;
    }
	}

	if (_dma_state == DMAState::active)
  {
  	_dmaRX->disable();
	  _dmaTX->disable();

#ifdef DEBUG_DMA_TRANSFERS
    Serial.printf("error:already active\n");
#endif
//		return false; // already active
  }

	// lets clear cache before we update sizes...
	if ((uint32_t)TXbuf >= 0x20200000u)
    arm_dcache_flush((uint8_t *)TXbuf, count);
	if ((uint32_t)RXbuf >= 0x20200000u)
    arm_dcache_delete(RXbuf, count);


	// Now handle the cases where the count > then how many we can output in one DMA request
	if (count > MAX_DMA_COUNT) 
  {
		_dma_count_remaining = count - MAX_DMA_COUNT;
		count = MAX_DMA_COUNT;
	} 
  else
		_dma_count_remaining = 0;

	// Now See if caller passed in a source buffer. 
	_dmaTX->TCD->ATTR_DST = 0;		// Make sure set for 8 bit mode
	uint8_t *write_data = (uint8_t*)TXbuf;

	if (TXbuf) 
  {
		_dmaTX->sourceBuffer((uint8_t*)write_data, count);  
		_dmaTX->TCD->SLAST = -(int32_t)count;	// Finish with it pointing to next location
    _dmaTX->TCD->SOFF = 1;        // explicitly force byte increment
    _dmaTX->TCD->ATTR_SRC = 0;   // 8-bit source
    _dmaTX->TCD->CSR   &= ~DMA_TCD_CSR_ESG;  // ← clear scatter-gather
	}
  else
  {
		_dmaTX->source((uint8_t&)_transferWriteFill);   // maybe have setable value
		DMAChanneltransferCount(_dmaTX, count);
	}	

	if (RXbuf) 
  {
		_dmaRX->TCD->ATTR_SRC = 0;		//Make sure set for 8 bit mode...
		_dmaRX->destinationBuffer((uint8_t*)RXbuf, count);
		_dmaRX->TCD->DLASTSGA = 0;		// At end point after our bufffer
	} 
  else 
  {
		_dmaRX->TCD->ATTR_SRC = 0;		//Make sure set for 8 bit mode...
		_dmaRX->destination((uint8_t&)bit_bucket);
		DMAChanneltransferCount(_dmaRX, count);
	}

#ifdef DEBUG_DMA_TRANSFERS
	// Lets dump TX, RX
	dumpDMA_TCD(_dmaTX);
	dumpDMA_TCD(_dmaRX);
#endif

	// Make sure port is in 8 bit mode and clear watermark
	//port().TCR = (port().TCR & (LPSPI_TCR_CPOL | LPSPI_TCR_CPHA)) | LPSPI_TCR_FRAMESZ(7); // make sure in 8 bit mode and correct CPOL, CPHA	
  //Serial.printf("FSR after TCR write: %08x\n", port().FSR);

	port().FCR = LPSPI_FCR_RXWATER(0) | LPSPI_FCR_TXWATER(0); // set watermarks to max (15) so we get interrupts on full FIFO

	

	port().SR = 0x3f00;	// clear out all of the other status...
 
  
  bytes_already_received = 0;
	CS_went_high = 0;

  //Serial.printf("SR before enable: %08x\n", port().SR);
  //Serial.printf("FSR before enable: %08x\n", port().FSR);

  port().DER = LPSPI_DER_RDDE | LPSPI_DER_TDDE;   // enable RX DMA only first
  #ifdef DEBUG_DMA_TRANSFERS
  Serial.printf("TX TCD SOFF:%d ATTR:%04x SADDR:%08x\n", 
    _dmaTX->TCD->SOFF,
    _dmaTX->TCD->ATTR,
    (uint32_t)_dmaTX->TCD->SADDR);
  
    Serial.printf("TX TCD:\n");
    Serial.printf("  SADDR:    %08x\n", (uint32_t)_dmaTX->TCD->SADDR);
    Serial.printf("  SOFF:     %d\n",   _dmaTX->TCD->SOFF);
    Serial.printf("  ATTR:     %04x\n", _dmaTX->TCD->ATTR);
    Serial.printf("  NBYTES:   %d\n",   _dmaTX->TCD->NBYTES);
    Serial.printf("  SLAST:    %d\n",   _dmaTX->TCD->SLAST);
    Serial.printf("  DADDR:    %08x\n", (uint32_t)_dmaTX->TCD->DADDR);
    Serial.printf("  DOFF:     %d\n",   _dmaTX->TCD->DOFF);
    Serial.printf("  CITER:    %d\n",   _dmaTX->TCD->CITER);
    Serial.printf("  DLASTSGA: %08x\n", _dmaTX->TCD->DLASTSGA);
    Serial.printf("  CSR:      %04x\n", _dmaTX->TCD->CSR);
    Serial.printf("  BITER:    %d\n",   _dmaTX->TCD->BITER);
    #endif

  _dmaRX->enable();
  _dmaTX->enable();


	_dma_state = DMAState::active;

	return true;
}

bool T4_DMA_SPI_SLAVE::pinIsMOSI(uint8_t pin)
{
	for (uint32_t i = 0; i < sizeof(hardware().mosi_pin); i++) 
  	if (pin == hardware().mosi_pin[i]) return true;
	return false;
}

bool T4_DMA_SPI_SLAVE::pinIsMISO(uint8_t pin)
{
	for (uint32_t i = 0; i < sizeof(hardware().miso_pin); i++) 
  	if (pin == hardware().miso_pin[i]) return true;
	return false;
}

bool T4_DMA_SPI_SLAVE::pinIsSCK(uint8_t pin)
{
	for (uint32_t i = 0; i < sizeof(hardware().sck_pin); i++) 
  	if (pin == hardware().sck_pin[i]) return true;
	return false;
}

bool T4_DMA_SPI_SLAVE::pinIsCS(uint8_t pin)
{
	for (uint32_t i = 0; i < sizeof(hardware().cs_pin); i++) 
		if (pin == hardware().cs_pin[i]) return true;
	return false;
}

void T4_DMA_SPI_SLAVE::setMOSI(uint8_t pin)
{
		for (uint32_t i = 0; i < sizeof(hardware().mosi_pin); i++) 
    {
			if (pin == hardware().mosi_pin[i] ) 
      {
//			  *(portControlRegister(hardware().mosi_pin[i])) = fastio;
//				*(portConfigRegister(hardware().mosi_pin[i])) = hardware().mosi_mux[i];
//				hardware().mosi_select_input_register = hardware().mosi_select_val[i];
				mosi_pin_index = i;
        mosi_pin = pin;
				return;
			}
	}
}

void T4_DMA_SPI_SLAVE::setMISO(uint8_t pin)
{
		for (uint32_t i = 0; i < sizeof(hardware().miso_pin); i++) 
    {
			if (pin == hardware().miso_pin[i] ) 
      {
//				*(portControlRegister(hardware().miso_pin[i])) = fastio;
//				*(portConfigRegister(hardware().miso_pin[i])) = hardware().miso_mux[i];
//				hardware().miso_select_input_register = hardware().miso_select_val[i];
				miso_pin_index = i;
        miso_pin = pin;
				return;
			}
	}
}

void T4_DMA_SPI_SLAVE::setSCK(uint8_t pin)
{
		for (uint32_t i = 0; i < sizeof(hardware().sck_pin); i++) 
    {
			if (pin == hardware().sck_pin[i] ) 
      {
				sck_pin_index = i;
        sck_pin = pin;
				return;
			}
	}
}

void T4_DMA_SPI_SLAVE::setCS(uint8_t pin)
{
		for (uint32_t i = 0; i < sizeof(hardware().cs_pin); i++) 
    {
			if (pin == hardware().cs_pin[i] ) 
      {
				cs_pin_index = i;
        cs_pin = pin;
				return;
			}
	}
}

void T4_DMA_SPI_SLAVE::setBitOrder(uint8_t bitOrder)
{
	hardware().clock_gate_register |= hardware().clock_gate_mask;

	if (bitOrder == LSBFIRST)
		port().TCR |= LPSPI_TCR_LSBF;
	else 
		port().TCR &= ~LPSPI_TCR_LSBF;
}

void T4_DMA_SPI_SLAVE::setDataMode(uint8_t dataMode)
{
	hardware().clock_gate_register |= hardware().clock_gate_mask;

	uint32_t tcr = port().TCR & ~(LPSPI_TCR_CPOL | LPSPI_TCR_CPHA);
	if (dataMode & 0x08) 
    tcr |= LPSPI_TCR_CPOL;

	if (dataMode & 0x04)
     tcr |= LPSPI_TCR_CPHA; 

	port().TCR = tcr;
}

char T4_DMA_SPI_SLAVE::cRT (bool NormallyR)
{
  uint32_t pincfg = port().CFGR1;
  pincfg >>= 24;
  if (NormallyR)
  switch (pincfg & 3)
  {
    case 0: return 'R';
    case 1: return 'R';
    case 2: return 'T';
    case 3: return 'T';
  }

  switch (pincfg & 3)
  {
    case 0: return 'T';
    case 1: return 'T';
    case 2: return 'R';
    case 3: return 'R';
  }
  return '?';
}

char indexToChar(int index)
{
  if (index==0)
    return ' ';
  else
    return '0'+index;
}

void T4_DMA_SPI_SLAVE::print_pin_use()
{
  Serial.printf ("T4_DMA_SPI_SLAVE%c port has SCK on INPUT pin %d, ", indexToChar(hardware().spi_port_index), sck_pin);
  if (SDOUT_line_in_use)
    Serial.printf ("SDOUT on pin %d is %cX, ",mosi_pin, T4_DMA_SPI_SLAVE::cRT(0));
  if (SDIN_line_in_use)
    Serial.printf ("SDIN on pin %d is %cX, ",miso_pin, T4_DMA_SPI_SLAVE::cRT(1));
     
  Serial.printf ("CS on INPUT pin %d\n",  cs_pin);
}

void _T4_DMA_SPI_SLAVE_dma_rxISR(void) {SPI_SLAVE.dma_rxisr();}
void _T4_DMA_SPI_SLAVE_dma_rxISR1(void) {SPI_SLAVE1.dma_rxisr();}
void _T4_DMA_SPI_SLAVE_dma_rxISR2(void) {SPI_SLAVE2.dma_rxisr();}

void _T4_DMA_SPI_SLAVE_spi_ISR(void) {SPI_SLAVE.spi_isr();}
void _T4_DMA_SPI_SLAVE_spi_ISR1(void) {SPI_SLAVE1.spi_isr();}
void _T4_DMA_SPI_SLAVE_spi_ISR2(void) {SPI_SLAVE2.spi_isr();}

/*
SPI_name
      	Teensy41	iMXRT     LPSPI ALT   DAISY NAME  PCS#

MOSI		12	      B0_01     4     ALT3  0     SDO   
MISO		11	      B0_02     4     ALT3  0     SDI   
SCK		  13	      B0_03     4     ALT3  0     SCK   
CS		  10	      B0_00     4     ALT3  0     PCS0  0
CS		  36	      B1_02     4     ALT2  0     PCS2  2
CS		  37	      B1_03     4     ALT2  0     PCS1  1

MOSI1		26	      AD_B1_14  3     ALT2  1     SDO
MISO1		1 	      AD_B0_02  3     ALT7  0     SDI
MISO1		39	      AD_B1_13  3     ALT2  1     SDI
SCK1		27	      AD_B1_15  3     ALT2  1     SCK
CS1		  0	        AD_B0_03  3     ALT7  0     PCS0  0
CS1		  38	      AD_B1_12  3     ALT2  1     PCS0  0

MOSI2		43	      SD_B0_02	1     ALT4  1     SDO
MISO2		42	      SD_B0_03  1     ALT4  1     SDI  
SCK2		45	      SD_B0_00  1     ALT4  1     SCK
CS2		  44	      SD_B0_01  1     ALT4  0     PCS0  0
*/

const T4_DMA_SPI_SLAVE::T4_SPI_SLAVE_Hardware_t T4_DMA_SPI_SLAVE::spiclass_lpspi4_hardware = {
  INDEX_SPI,
  IRQ_LPSPI4,
  
  CCM_CCGR1, CCM_CCGR1_LPSPI4(CCM_CCGR_ON),
	DMAMUX_SOURCE_LPSPI4_TX, DMAMUX_SOURCE_LPSPI4_RX, 

  _T4_DMA_SPI_SLAVE_dma_rxISR, 
  _T4_DMA_SPI_SLAVE_spi_ISR,

	12, 255,  // MISO
	3 | 0x10, 0,
	0, 0,
	IOMUXC_LPSPI4_SDI_SELECT_INPUT,
	
  11, 255,  // MOSI
	3 | 0x10, 0,
	0, 0, 
	IOMUXC_LPSPI4_SDO_SELECT_INPUT,
	
  13, 255,  // SCK
	3 | 0x10, 0,
	0, 0,
	IOMUXC_LPSPI4_SCK_SELECT_INPUT,

  10, 36, 37, // CS
	3 | 0x10, 2 | 0x10, 2 | 0x10,
  0, 0, 0,

  0, 2, 1,

	IOMUXC_LPSPI4_PCS0_SELECT_INPUT,
};

const T4_DMA_SPI_SLAVE::T4_SPI_SLAVE_Hardware_t T4_DMA_SPI_SLAVE::spiclass_lpspi3_hardware = {
  INDEX_SPI1,
  IRQ_LPSPI3,
  
  CCM_CCGR1, CCM_CCGR1_LPSPI3(CCM_CCGR_ON),
	DMAMUX_SOURCE_LPSPI3_TX, DMAMUX_SOURCE_LPSPI3_RX, 
  
  _T4_DMA_SPI_SLAVE_dma_rxISR1, 
  _T4_DMA_SPI_SLAVE_spi_ISR1,

	1, 39,    // MISO
	7 | 0x10, 2 | 0x10,
	0, 1,
	IOMUXC_LPSPI3_SDI_SELECT_INPUT,
	
  26, 255,  // MOSI
	2 | 0x10, 0,
	1, 0,
	IOMUXC_LPSPI3_SDO_SELECT_INPUT,
	
  27, 255,  // SCK
	2 | 0x10, 0,
	1,  0,
	IOMUXC_LPSPI3_SCK_SELECT_INPUT,

  0, 38,  255, // CS
	7 | 0x10, 2 | 0x10, 0,
	0, 1, 0,

  0, 0, 0,

	IOMUXC_LPSPI3_PCS0_SELECT_INPUT,
};

const T4_DMA_SPI_SLAVE::T4_SPI_SLAVE_Hardware_t T4_DMA_SPI_SLAVE::spiclass_lpspi1_hardware = {
  INDEX_SPI2,
  IRQ_LPSPI1,
  
  CCM_CCGR1, CCM_CCGR1_LPSPI1(CCM_CCGR_ON),
	DMAMUX_SOURCE_LPSPI1_TX, DMAMUX_SOURCE_LPSPI1_RX,
  
  _T4_DMA_SPI_SLAVE_dma_rxISR2, 
  _T4_DMA_SPI_SLAVE_spi_ISR2,

	42, 54,   // MISO
	4 | 0x10, 3 | 0x10,
	1, 0,
	IOMUXC_LPSPI1_SDI_SELECT_INPUT,
	
  43, 50,   // MOSI
	4 | 0x10, 3 | 0x10,
	1, 0,
	IOMUXC_LPSPI1_SDO_SELECT_INPUT,
	
  45, 49,  // SCK
	4 | 0x10, 3 | 0x10,
	1, 0, 
	IOMUXC_LPSPI1_SCK_SELECT_INPUT,

  44, 255, 255, // CS
	4 | 0x10, 0, 0,
	0, 0, 0,

	0, 0, 0,

	IOMUXC_LPSPI1_PCS0_SELECT_INPUT,
};

T4_DMA_SPI_SLAVE SPI_SLAVE((uintptr_t)&IMXRT_LPSPI4_S, (uintptr_t)&T4_DMA_SPI_SLAVE::spiclass_lpspi4_hardware);
T4_DMA_SPI_SLAVE SPI_SLAVE1((uintptr_t)&IMXRT_LPSPI3_S, (uintptr_t)&T4_DMA_SPI_SLAVE::spiclass_lpspi3_hardware);
T4_DMA_SPI_SLAVE SPI_SLAVE2((uintptr_t)&IMXRT_LPSPI1_S, (uintptr_t)&T4_DMA_SPI_SLAVE::spiclass_lpspi1_hardware);

bool T4_DMA_SPI_SLAVE::busy() 
{
  return (port().SR & (1<<24));
}

int T4_DMA_SPI_SLAVE::dma_state()
{
  return (_dma_state);
}

//-------------------------------------------------------------------------
// DMA RX ISR
//-------------------------------------------------------------------------
void T4_DMA_SPI_SLAVE::dma_rxisr() 
{
#ifdef DEBUG_DMA_TRANSFERS
  Serial.printf("DMA INTERRUPT\n");
#endif
	_dmaRX->clearInterrupt();
	_dmaTX->clearComplete();
	_dmaRX->clearComplete();

	if (_dma_count_remaining) 
  {
		// What do I need to do to start it back up again...
		// We will use the BITR/CITR from RX as TX may have prefed some stuff
		if (_dma_count_remaining > MAX_DMA_COUNT) 
    	_dma_count_remaining -= MAX_DMA_COUNT;
		else 
    {
			DMAChanneltransferCount(_dmaTX, _dma_count_remaining);
			DMAChanneltransferCount(_dmaRX, _dma_count_remaining);
			_dma_count_remaining = 0;
		}
		_dmaRX->enable();
		_dmaTX->enable();
	} 
  else 
  {
    us_last_DMA_receive = micros();

		port().FCR = LPSPI_FCR_RXWATER(0) | LPSPI_FCR_TXWATER(0); // _spi_fcr_save;	// restore the FSR status... 
 		port().DER = 0;		// DMA no longer doing TX (or RX)

		// With:
    port().CR &= ~LPSPI_CR_MEN;
    port().CR |= LPSPI_CR_RTF | LPSPI_CR_RRF;
    port().CR &= ~(LPSPI_CR_RTF | LPSPI_CR_RRF);
    port().CR |= LPSPI_CR_MEN;
		port().SR = 0x3f00;	// clear out all of the other status...

		_dma_state = DMAState::completed;   // set back to 1 in case our call wants to start up dma again
    
    flag_transaction_completed = 1;
    if (_onCompleteCallback) {
                _onCompleteCallback();
            }

    if (auto_repeat)
    {
      prepare_for_slave_transfer (last_transfer_TXbuf, last_transfer_RXbuf, last_transfer_count);
    }


#ifdef DEBUG_DMA_TRANSFERS
  uint8_t *p = (uint8_t*)db_RXbuf;
	Serial.printf("<< [%d]: ", db_count);
  if (p)
    for (int i=0; i<(int)db_count; i++)
      Serial.printf("%02x ", *p++);
  else
    Serial.printf("WRITEONLY");

  Serial.printf("\n");
#endif
  }
}

void T4_DMA_SPI_SLAVE::spi_isr() 
{
    uint32_t us_now = micros();
    uint32_t sr = port().SR;

    if (sr & LPSPI_SR_FCF)  // Only Frame Complete fires now
    {
            CS_went_high = 1;
            prev_us_CS_deassert = us_CS_deassert;
            us_CS_deassert = us_now;
    }


    port().SR = sr & 0x3f03;  // Clear flags
}

uint32_t T4_DMA_SPI_SLAVE::delta_us_first_to_last() 
{
  if (bytes_already_received>1)
    return bytes_already_received * (us_last_DMA_receive - us_first_DMA_receive) / (bytes_already_received-1);
  else
    return 0;
}

uint32_t T4_DMA_SPI_SLAVE::delta_us_since_previous() 
{
  return (us_CS_deassert - prev_us_CS_deassert);
}

double T4_DMA_SPI_SLAVE::f_SCK_estimate()
{
  double f = 0.0;
  double T = (double)T4_DMA_SPI_SLAVE::delta_us_first_to_last();
  T /= bytes_already_received * 8;
  if (T != 0.0)
    f = 1.0 / T;
  return f;
}

