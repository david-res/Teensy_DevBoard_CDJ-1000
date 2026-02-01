
/*
 * PacketsSerial.cpp
 */

#include "PacketsSerial.h"
#include <util/atomic.h>
#include "Arduino.h"

PacketsSerial::PacketsSerial()
{
}

bool PacketsSerial::begin (int boardAddress_, DMA_UART *S, uint32_t BaudRate)
{
  OurSerial = S;
  OurSerial->begin (BaudRate, SERIAL_9N1, -1);  

	our_addr = boardAddress_;
	
	return 1;
}

void PacketsSerial::reset_packet_counters ()
{    
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) 
    {
      cyclic_packets_sent = 0;
      cyclic_packets_received = 0;
      cyclic_packets_received_crc_error = 0;
    }    
}

PacketsSerial::~PacketsSerial()
{
  if (OurSerial)
    OurSerial->end();
}

void PacketsSerial::Add_to_CRC16 (uint16_t *CRC16w, uint8_t newx)
{
    uint8_t x;
    x = (*CRC16w >> 8) ^ newx;
    x ^= x>>4;
    *CRC16w = (*CRC16w << 8) ^ ((uint16_t)(x << 12)) ^ ((uint16_t)(x <<5)) ^ ((uint16_t)x);
}

uint16_t PacketsSerial::Calc_CRC16 (volatile uint8_t *bp, int Nbytes)
{
    uint16_t CRC16w = 0xffff;
    while (Nbytes--)
        Add_to_CRC16(&CRC16w, *bp++);
    return CRC16w;
}

void PacketsSerial::mmemcpyb (uint8_t *to, uint8_t *from, int n)
{
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) 
  {
    while (n--)
      *to++ = *from++;
  }
}

void PacketsSerial::mmemcpydw (uint32_t *to, uint32_t *from, int n)
{
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) 
  {
    while (n--)
      *to++ = *from++;
  }
}

void PacketsSerial::copy_tx_packet_buffer (uint8_t *t_packet, int t_packet_len, uint8_t *tt_buffer, int *tt_len)
{
    if (t_packet_len <= PZD_DWORDS_BUFFER_SIZE*4)
    {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) 
        {
          for (int i=0; i<t_packet_len; i++)
            tt_buffer[i] = *t_packet++;
          *tt_len = t_packet_len;
        }
    }
}

void PacketsSerial::addCRCandSend (uint16_t *CRC16w, uint16_t x)
{
    Add_to_CRC16 (CRC16w, x & 0xff);
    OurSerial->write(x); 
}
 
void PacketsSerial::send_packet (uint8_t *bytes, uint8_t src_addr, uint8_t dest_addr, int len)
{
// Payload length is len bytes, len is 1..255, len=0 means it is 256 
// send a packet of 1..256 bytes of payload. Payload is wrapped as:  
// mode is 0..3, in first byte b7,6
// source address is 0..7 in first byte b5,4,3 
// destination address is 0..7 in first byte b2,1,0, 
// destination address at 0 (first byte is 0bxxxxx000) is for broadcast message to all nodes (addressed node shall not reply)
// packet starts with b9 set in first byte for <mode,source_adr,dest_adr>, all other bytes with b9 cleared
// packet bytes are <mode,source_adr,dest_adr> <len> <bytes[0]> .... <bytes[len-1]> <crc16_low_byte> <crc16_hi_byte>
// crc16 over all packet bytes, so over <dest_adr,len> and all payload bytes, but not over crc16_low_byte
    uint8_t first_byte;
    uint8_t second_byte;
    uint8_t CRC_hiByte;

    if ((len>=1) && (len <= BUFFER_SIZE) && (OurSerial != NULL))
    {
        // len=0 means it is BUFFER_SIZE bytes
        if (len == BUFFER_SIZE)
            second_byte = 0;
        else
            second_byte = len;

        first_byte = (dest_addr & 0x7) | ((src_addr & 0x7)<<3);
        OurSerial->write((uint16_t)0x1ff);  // dummy char

        uint16_t CRC16w = 0xffff;
        addCRCandSend (&CRC16w, 0x100 | first_byte); // first UART 'byte' with bit9 set. That's the flag for start of a packet
        addCRCandSend (&CRC16w, second_byte);

        while (len--)
           addCRCandSend (&CRC16w, *bytes++);  
        CRC_hiByte = CRC16w >> 8;
        addCRCandSend (&CRC16w, CRC16w & 0xff);
        addCRCandSend (&CRC16w, CRC_hiByte);
        OurSerial->trigger_tx();
    } 
}

void PacketsSerial::process_serial_rx (uint16_t r)
{
//  uint32_t *p;
  r &= 0x1ff;
//Serial.printf (" r%03x ", r);    
  
  if (r & 0x100)
  {
    uint8_t dest_addr = r & 7;
    if (dest_addr == our_addr)
    {
        rx_source_addr = (r >> 3) & 7;
        rx_parse_state = 1;
        rx_index = 0;
        rx_buffer[rx_index++] = r & 0xff;
    }
  }
  else
  {
    if (rx_parse_state >= 1)
      if ((r>=0) && (r<=255))
      {
        switch (rx_parse_state)
        {
          case 1:
              rx_expected_len = r;
              if (!rx_expected_len)
                  rx_expected_len = BUFFER_SIZE;
              rx_expected_len += 2 + 2; // for header (dest_adr, len) and for 2 bytes crc16
              rx_buffer[rx_index++] = r;
              rx_parse_state = 2;
 //Serial.printf ("rx_expected_len=%d\n", rx_expected_len);
            break; 
          case 2: 
              if (rx_index < (BUFFER_SIZE+4)) 
              {
                  rx_buffer[rx_index++] = r;
                  if (rx_index == rx_expected_len)
                  {
                      uint16_t CRCcalc = Calc_CRC16 (rx_buffer, rx_expected_len-2);
 //Serial.printf ("rx_expected_len=%d CRCcalc=%04x Act=%02x%02x\n",rx_expected_len,  CRCcalc, rx_buffer[rx_index-1], rx_buffer[rx_index-2]);
                      if ((rx_buffer[rx_index-2] == (CRCcalc & 0xff)) &&
                          (rx_buffer[rx_index-1] == (CRCcalc >> 8)))
                      {
                          int len = rx_expected_len - 4;
                          if (len > 0)
                              switch (rx_buffer[0] >> 6)
                              {
                                case 0: // cyclic interrogation, PZDs
                                    if (len > (PZD_DWORDS_BUFFER_SIZE*4))
                                      len = PZD_DWORDS_BUFFER_SIZE*4;
//Serial.printf ("rx_expected_len=%d\n",len);
                                    mmemcpyb ((uint8_t*)PZDreceive, (uint8_t*)(rx_buffer+2), len);
                                    cyclic_packets_received++;
                                    flag_new_packet_received = 1;
                                    if (PacketSerialRole == SLAVE) // slaves must respond when addressed by master with mode=00
                                        go_send_PZDsendBuffer(our_addr, rx_source_addr);
                                    break;
                                case 1:
                                    break;
                                case 2:
                                    break;
                                case 3: 
                                    break;
                              }
                      }
                      else
                          cyclic_packets_received_crc_error++;

                    rx_parse_state = 0;
                  }
              }
              else
                  rx_parse_state = 0;
              break;
        }
    }
  }
}

void PacketsSerial::go_send_PZDsendBuffer (uint8_t src_addr, uint8_t dest_addr) 
{
    int len = 0;
    copy_tx_packet_buffer ((uint8_t*)PZDsend, 4*PZD_DWORDS_BUFFER_SIZE, tt_buffer, &len);
    send_packet (tt_buffer, src_addr, dest_addr, len);
    cyclic_packets_sent++;
}