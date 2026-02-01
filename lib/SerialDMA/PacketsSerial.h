
/*
 * PacketSerial.h
 *
 */

#ifndef PACKET_SERIAL_H_
#define PACKET_SERIAL_H_

#include "DMA_UART.h"

#define BUFFER_SIZE 256
#define PZD_DWORDS_BUFFER_SIZE 8

enum {MASTER, SLAVE};
class PacketsSerial
{
public:

	PacketsSerial();
	~PacketsSerial();

  bool begin (int boardAddress_, DMA_UART *S, uint32_t BaudRate = 1500000);
  void reset_packet_counters ();
  DMA_UART *OurSerial = NULL;
  void process_serial_rx (uint16_t r);

  void mmemcpyb (uint8_t *to, uint8_t *from, int n);
  void mmemcpydw (uint32_t *to, uint32_t *from, int n = PZD_DWORDS_BUFFER_SIZE);

  uint32_t cyclic_packets_sent = 0;
  uint32_t cyclic_packets_received = 0;
  uint32_t cyclic_packets_received_crc_error = 0;

  uint8_t PacketSerialRole = SLAVE;
  uint32_t PZDreceive[PZD_DWORDS_BUFFER_SIZE] = {0};
  uint32_t PZDsend[PZD_DWORDS_BUFFER_SIZE] = {0};
  bool flag_new_packet_received = 0;
  void go_send_PZDsendBuffer (uint8_t src_addr, uint8_t dest_addr);
  
private:

  uint8_t tt_buffer[PZD_DWORDS_BUFFER_SIZE*4];
  int tt_len = 0;

  void addCRCandSend (uint16_t *CRC16w, uint16_t x);
  void send_packet (uint8_t *bytes, uint8_t src_addr, uint8_t dest_addr, int len);
  void Add_to_CRC16 (uint16_t *CRC16w, uint8_t newx);
  uint16_t Calc_CRC16 (volatile uint8_t *bp, int Nbytes);
  void mmemcpy (uint8_t *to, uint8_t *from, int n);
  void copy_tx_packet_buffer (uint8_t *t_packet, int t_packet_len, uint8_t *tt_buffer, int *tt_len);

  volatile uint8_t our_addr = 0;  // master = address 0
  volatile uint8_t rx_source_addr = 0;
  volatile uint8_t rx_parse_state = 0;
  volatile int rx_expected_len = 0;
  volatile uint8_t rx_buffer[BUFFER_SIZE+4] __attribute__((aligned(4)));
  volatile int rx_index = 0;

};

#endif
