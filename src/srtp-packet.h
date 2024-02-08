/**
 * CS3102 Coursework P2 : Simple, Reliable Transport Protocol (SRTP)
 *
 * saleem (edited January 2024, February 2023)
 * sjm55 (checked February 2024)
 * 210016688 (edited March 2024)
 *
 * Packet definitions for SRTP.
 */

#ifndef __srtp_packet_h__
#define __srtp_packet_h__

#include "srtp-common.h"

// Bitwise types for SRTP packet types
#define SRTP_TYPE_req       ((uint8_t) 0x01)
#define SRTP_TYPE_ack       ((uint8_t) 0x02)
#define SRTP_TYPE_open      ((uint8_t) 0x10)
#define SRTP_TYPE_close     ((uint8_t) 0x20)
#define SRTP_TYPE_data      ((uint8_t) 0x40)
#define SRTP_TYPE_open_req  (SRTP_TYPE_open  | SRTP_TYPE_req)
#define SRTP_TYPE_open_ack  (SRTP_TYPE_open  | SRTP_TYPE_ack)
#define SRTP_TYPE_close_req (SRTP_TYPE_close | SRTP_TYPE_req)
#define SRTP_TYPE_close_ack (SRTP_TYPE_close | SRTP_TYPE_ack)
#define SRTP_TYPE_data_req  (SRTP_TYPE_data  | SRTP_TYPE_req)
#define SRTP_TYPE_data_ack  (SRTP_TYPE_data  | SRTP_TYPE_ack)

/**
 * SRTP packet header. 16 bytes, 32 bit aligned.
 */
typedef struct Srtp_Header_s {
  uint8_t type; /* SRTP_TYPE_... */
  uint8_t padding;
  uint16_t length; /* length of header + payload */
  uint32_t seqno;
  uint64_t timestamp;
} Srtp_Header_t;

#define SRTP_HEADER_SIZE sizeof(Srtp_Header_t)
#define SRTP_MAX_PAYLOAD_SIZE SRTP_MAX_DATA_SIZE

/**
 * SRTP packet. 16 + SRTP_MAX_PAYLOAD_SIZE bytes, 32 bit aligned.
 */
typedef struct Srtp_Packet_s {
  Srtp_Header_t header;
  uint8_t payload[SRTP_MAX_PAYLOAD_SIZE];
} Srtp_Packet_t;

#define SRTP_MAX_PACKET_SIZE sizeof(Srtp_Packet_t)

#endif /* __srtp_packet_h__ */
