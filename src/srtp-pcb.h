#ifndef __srtp_pcb_h__
#define __srtp_pcb_h__

#include <inttypes.h>
#include <netinet/in.h>

#include "srtp-fsm.h"

/*
  CS3102 Coursework P2 : Simple, Reliable Transport Protocol (SRTP)
  saleem, Jan2024, Feb2023
  sjm55 (checked February 2024)

*/

typedef struct Srtp_Pcb_s { /* protocol control block - only one connection */

  int      sd; /* socket descriptor */
  uint16_t port; /* used for local and remote */
  struct   sockaddr_in local; /* local endpoint, IPv4 only */
  struct   sockaddr_in remote; /* local endpoint, IPv4 only */

  SRTP_state_t state; /* SRTP_state_x values from srtp-fsm.h */ 

  // seq and ack numbers all start at 1, packets (not bytes, like TCP)
  uint32_t seq_tx; /* most recent tx (transmitted) sequence number used */
  uint32_t seq_rx; /* next expected sequence number to rx */

  uint16_t re_tx; /* most recent re-transmission count */

  uint32_t rtt; /* most recent rtt [us], assume < ~4x10^9us (i.e. < ~400s) */
  uint32_t rto; /* current retransmission timeout value [us] */

  /*
    The following uint64_t counters are for convenience only, and are not
    needed for the operation of the protocol.

    If (and only if) these are updated appropriately, then SrtpPcb_report()
    works correctly, and can be useful for debugging.
  */

  /* update start_time when open() is called (client), or accept() succeeds (server) */
  uint64_t start_time; // as given by srtp_timestamp()
  uint64_t finish_time; // as given by srtp_timestamp()

  uint64_t open_req_rx; // +1 for each open_req received
  uint64_t open_req_dup_rx; // +1 for each duplicate open_req received
  uint64_t open_ack_rx; // +1 for each open_ack received
  uint64_t open_ack_dup_rx; // +1 for each duplicate open_ack received

  uint64_t open_req_tx; // +1 for each open_req transmitted
  uint64_t open_req_re_tx; // +1 for each open_req retransmitted
  uint64_t open_ack_tx; // +1 for each open_ack transmitted
  uint64_t open_ack_re_tx; // +1 for each duplicate open_ack retransmitted

  uint64_t data_req_rx; // +1 for each data_req received
  uint64_t data_req_bytes_rx; // +1 for each data_req received, payload bytes only, excluding duplicates
  uint64_t data_ack_rx; // +1 for each data_ack received
  uint64_t data_req_dup_rx; // +1 for each duplicate data_req received
  uint64_t data_req_bytes_dup_rx; // +1 for each duplicate data_req received, payload bytes only
  uint64_t data_ack_dup_rx; // +1 for each duplicate data_ack received

  uint64_t data_req_tx; // +1 for each data_req transmitted
  uint64_t data_req_bytes_tx; // +1 for each data_req transmitted, payload bytes only, excluding re-transmissions
  uint64_t data_ack_tx; // +1 for each data_ack transmitted
  uint64_t data_req_re_tx; // +1 for each data_req retransmitted
  uint64_t data_req_bytes_re_tx; // +1 for each data_req transmitted, payload bytes only
  uint64_t data_ack_re_tx; // +1 for each data_ack retransmitted

  uint64_t close_req_rx; // +1 for each close_req received
  uint64_t close_req_dup_rx; // +1 for each duplicate close_req received
  uint64_t close_ack_rx; // +1 for each close_ack received
  uint64_t close_ack_dup_rx; // +1 for each duplicate close_ack received

  uint64_t close_req_tx; // +1 for each close_req transmitted
  uint64_t close_req_re_tx; // +1 for each close_req retransmitted
  uint64_t close_ack_tx; // +1 for each close_ack transmitted
  uint64_t close_ack_re_tx; // +1 for each close_ack transmitted

} Srtp_Pcb_t;

void reset_SrtpPcb(); // reset G_pcb to starting values

/* debugging help */
void SrtpPcb_report(); // print PCB information
void SrtpPcb_local(char *s); // s must point to a space of >= 22 bytes
void SrtpPcb_remote(char *s); // s must point to a space of >= 22 bytes

#endif /* __srtp_pcb_h__ */

