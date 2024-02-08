/**
 * CS3102 Coursework P2 : Simple, Reliable Transport Protocol (SRTP)
 *
 * saleem (edited January 2024, February 2023)
 * sjm55 (checked February 2024)
 * 210016688 (edited March 2024)
 *
 * API for SRTP.
 * Uses UDP underneath.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h> // This should have 'void perror(const char *s);'
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include "srtp-common.h"
#include "srtp-fsm.h"
#include "srtp-packet.h"
#include "srtp-pcb.h"
#include "srtp.h"

#include "byteorder64.h"
#include "d_print.h"

extern Srtp_Pcb_t G_pcb;

#define DEBUG 0

#define DYNAMIC_RTO 1

#define SEED 42
#define DROP_RANDOM 1
#define DROP_OPEN_REQ 0
#define DROP_OPEN_ACK 0
#define DROP_DATA_REQ 0
#define DROP_DATA_ACK 0
#define DROP_CLOSE_REQ 0
#define DROP_CLOSE_ACK 0

#define DROP_RATE_NUM 100 // Used for random packet dropping at large scale
#define DROP_RATE_DEN 100000
// #define DROP_RATE_NUM 1 // Used for random packet dropping at small scale
// #define DROP_RATE_DEN 2

int timeout_count = 0;
int max_re_tx = 5;

void *G_data = 0;
uint16_t G_data_size = 0;
socklen_t l = sizeof(struct sockaddr);

// Calculate RTO
int trip_count = 0;
uint32_t k = 4;
uint32_t v_n = 0;
uint32_t s_n = 0;
uint32_t t_n = 0;

// Function prototypes
void send_open_req();
void send_data_req();
void send_close_req();

/**
 * return : error - -1
 *          success - socket descriptor
 * 
 * Create a socket.
 */
int create_socket() {
  // Create local address
  memset(&G_pcb.local, 0, sizeof(G_pcb.local));
  G_pcb.local.sin_family = AF_INET;
  G_pcb.local.sin_addr.s_addr = htonl(INADDR_ANY);
  G_pcb.local.sin_port = htons(G_pcb.port);

  // Create socket
  if ((G_pcb.sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
    perror("create_socket() : socket()");
    return -1;
  }
  if (bind(G_pcb.sd, (struct sockaddr *)&G_pcb.local, sizeof(G_pcb.local)) <
      0) {
    perror("create_socket() : bind()");
    return -1;
  }

  return G_pcb.sd;
}

/**
 * fqdn : fully qualified domain name
 * return : error - -1
 *          success - 0
 * 
 * Resolve address from fully qualified domain name.
 */
int resolve_addr(const char *fqdn) {
  // Initialise socket struct to 0
  memset(&G_pcb.remote, 0, sizeof(struct sockaddr_in));
  // Create IPv4 struct with 0
  struct in_addr ip_addr;
  ip_addr.s_addr = (in_addr_t)0;

  // Try to convert fqdn to IP address
  if (inet_aton(fqdn, &ip_addr) == 0) {
    // If failed, try to get IP address from fqdn
    struct hostent *hp = gethostbyname(fqdn);
    if (hp == (struct hostent *)0) {
      fprintf(stderr, "openSocket(): gethostname()\n");
      return -1;
    } else {
      // Copy found IP address into IPv4 struct
      memcpy((void *)&ip_addr.s_addr, (void *)*hp->h_addr_list,
             sizeof(ip_addr.s_addr));
    }
  }

  // No IP address found
  if (ip_addr.s_addr == (in_addr_t)0) {
    fprintf(stderr, "resolveAddr(): inet_aton()\n");
    return -1;
  }

  // Set up remote endpoint
  G_pcb.remote.sin_family = AF_INET;
  G_pcb.remote.sin_addr.s_addr = ip_addr.s_addr;
  G_pcb.remote.sin_port = htons(G_pcb.port);

  return 0;
}

/**
 * Recalibrate RTO using new RTT.
*/
void recalibrate_rto() {
  if (!DYNAMIC_RTO) {
    G_pcb.rto = SRTP_RTO_FIXED;
  }

  if (trip_count == 1) {
    v_n = G_pcb.rtt/2;
    s_n = G_pcb.rtt;
    t_n = s_n + k * v_n;
    if (t_n < 1000000) {
      G_pcb.rto = 1000000;
    } else if (t_n > 60000000) {
      G_pcb.rto = 60000000;
    } else {
      G_pcb.rto = t_n;
    }
  } else {
    uint32_t temp;
    if (G_pcb.rtt > s_n) {
      temp = G_pcb.rtt - s_n;
    } else {
      temp = s_n - G_pcb.rtt;
    }
    v_n = 0.75 * v_n + 0.75 * temp;
    s_n = 0.875 * s_n + 0.125 * G_pcb.rtt;
    t_n = s_n + k * v_n;
    if (t_n < 1000000) {
      G_pcb.rto = 1000000;
    } else if (t_n > 60000000) {
      G_pcb.rto = 60000000;
    } else {
      G_pcb.rto = t_n;
    }
  }
}

/**
 * Start timeout for handler function to be called when timeout occurs.
 */
void start_timeout(void *handler) {
  struct sigaction sa;
  sa.sa_handler = handler;
  sigaction(SIGALRM, &sa, NULL);
  // Set up the timer
  struct itimerval timer;
  timer.it_value.tv_sec = G_pcb.rto / 1000000;
  timer.it_value.tv_usec = G_pcb.rto % 1000000;
  timer.it_interval.tv_sec = 0;
  timer.it_interval.tv_usec = 0;
  setitimer(ITIMER_REAL, &timer, NULL);
}

/**
 * Stop timeout.
 */
void stop_timeout() {
  // Stop the timer
  struct itimerval timer;
  timer.it_value.tv_sec = 0;
  timer.it_value.tv_usec = 0;
  timer.it_interval.tv_sec = 0;
  timer.it_interval.tv_usec = 0;
  setitimer(ITIMER_REAL, &timer, NULL);
}

/**
 * Continuously send open_req packet until open_ack packet is received or max retries reached.
 */
void send_open_req() {
  if (G_pcb.state != SRTP_state_opening) {
    stop_timeout();
    return;
  }

  if (timeout_count > max_re_tx) {
    fprintf(stderr, "send_open_req() : max retries reached\n");
    G_pcb.state = SRTP_state_closed;
    return;
  }
  timeout_count++;

  // Send open_req packet
  Srtp_Header_t req_hdr;
  req_hdr.type = SRTP_TYPE_open_req;
  req_hdr.seqno = htonl(G_pcb.seq_tx);
  req_hdr.timestamp = hton64(srtp_timestamp());
  req_hdr.length = htons(SRTP_HEADER_SIZE);

  if ((DROP_OPEN_REQ | DROP_RANDOM) && (random() % DROP_RATE_DEN < DROP_RATE_NUM)) {
    fprintf(stderr, "send_open_req(): dropped packet\n");
  } else if (sendto(G_pcb.sd, &req_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&G_pcb.remote, l) < 0) {
    perror("srtp_open() : sendto()");
    G_pcb.state = SRTP_state_error;
  }

  // Update PCB
  if (timeout_count > 1) {
    G_pcb.open_req_re_tx++;
    G_pcb.re_tx++;
  } else {
    G_pcb.open_req_tx++;
  }

  // Print packet
  if (DEBUG) {
    fprintf(stderr, "srtp_open() : sent open_req packet\n");
    fprintf(stderr, "  type = %d\n", req_hdr.type);
    fprintf(stderr, "  seqno = %d\n", ntohl(req_hdr.seqno));
    fprintf(stderr, "  timestamp = %ld\n", ntoh64(req_hdr.timestamp));
    fprintf(stderr, "  length = %d\n", ntohs(req_hdr.length));
  }

  start_timeout(send_open_req);
}

/**
 * Continuously send data_req packet until data_ack packet is received or max retries reached.
 */
void send_data_req() {
  if (G_pcb.state != SRTP_state_transmit) {
    stop_timeout();
    return;
  }

  if (timeout_count > max_re_tx) {
    fprintf(stderr, "send_data_req() : max retries reached\n");
    G_pcb.state = SRTP_state_closed;
    return;
  }
  timeout_count++;

  // Send data_req packet
  Srtp_Packet_t data_pkt;
  data_pkt.header.type = SRTP_TYPE_data_req;
  data_pkt.header.seqno = htonl(G_pcb.seq_tx);
  data_pkt.header.timestamp = hton64(srtp_timestamp());
  data_pkt.header.length = htons(SRTP_HEADER_SIZE + G_data_size);
  memcpy(data_pkt.payload, G_data, G_data_size);


  if ((DROP_DATA_REQ | DROP_RANDOM) && (random() % DROP_RATE_DEN < DROP_RATE_NUM)) {
    fprintf(stderr, "send_data_req(): dropped packet\n");
  } else if (sendto(G_pcb.sd, &data_pkt, SRTP_HEADER_SIZE + G_data_size, 0, (struct sockaddr *)&G_pcb.remote, l) < 0) {
    perror("srtp_tx() : sendto()");
    G_pcb.state = SRTP_state_error;
  }

  // Update PCB
  if (timeout_count > 1) {
    G_pcb.data_req_re_tx++;
    G_pcb.re_tx++;
    G_pcb.data_req_bytes_re_tx += G_data_size;
  } else {
    G_pcb.data_req_tx++;
    G_pcb.data_req_bytes_tx += G_data_size;
  }

  // Print packet
  if (DEBUG) {
    fprintf(stderr, "srtp_tx() : sent data_req packet\n");
    fprintf(stderr, "  type = %d\n", data_pkt.header.type);
    fprintf(stderr, "  seqno = %d\n", ntohl(data_pkt.header.seqno));
    fprintf(stderr, "  timestamp = %ld\n", ntoh64(data_pkt.header.timestamp));
    fprintf(stderr, "  length = %d\n", ntohs(data_pkt.header.length));
  }

  start_timeout(send_data_req);
}

/**
 * Continuously send close_req packet until close_ack packet is received or max retries reached.
*/
void send_close_req() {
  if (G_pcb.state == SRTP_state_closing_r || G_pcb.state == SRTP_state_closed) {
    stop_timeout();
    return;
  }

  if (timeout_count > max_re_tx) {
    fprintf(stderr, "send_close_req() : max retries reached\n");
    G_pcb.state = SRTP_state_closed;
    return;
  }
  timeout_count++;

  // Send close_req packet
  Srtp_Header_t req_hdr;
  req_hdr.type = SRTP_TYPE_close_req;
  req_hdr.seqno = htonl(G_pcb.seq_tx);
  req_hdr.timestamp = hton64(srtp_timestamp());
  req_hdr.length = htons(SRTP_HEADER_SIZE);

  if ((DROP_CLOSE_REQ | DROP_RANDOM) && (random() % DROP_RATE_DEN < DROP_RATE_NUM)) {
    fprintf(stderr, "send_close_req(): dropped packet\n");
  } else if (sendto(G_pcb.sd, &req_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&G_pcb.remote, l) < 0) {
    perror("srtp_close() : sendto()");
    G_pcb.state = SRTP_state_error;
  }

  // Update PCB
  if (timeout_count > 1) {
    G_pcb.close_req_re_tx++;
    G_pcb.re_tx++;
  } else {
    G_pcb.close_req_tx++;
  }

  // Print packet
  if (DEBUG) {
    fprintf(stderr, "srtp_close() : sent close_req packet\n");
    fprintf(stderr, "  type = %d\n", req_hdr.type);
    fprintf(stderr, "  seqno = %d\n", ntohl(req_hdr.seqno));
    fprintf(stderr, "  timestamp = %ld\n", ntoh64(req_hdr.timestamp));
    fprintf(stderr, "  length = %d\n", ntohs(req_hdr.length));
  }

  start_timeout(send_close_req);
}

/**
 * Must be called before any other srtp_zzz() API calls.
 * For use by client and server process.
 */
void srtp_initialise() {
  reset_SrtpPcb();

  G_pcb.state = SRTP_state_closed;
  G_pcb.seq_tx = 0;
  G_pcb.seq_rx = 0;
  G_pcb.rto = 1000000; // 1 second
}

/**
 * port : local port number to be used for socket
 * return : error - srtp-common.h
 *          success - valid socket descriptor
 * 
 * Setup to listen for incoming requests.
 * For use by server process.
 */
int srtp_start(uint16_t port) {
  if (G_pcb.state != SRTP_state_closed) {
    fprintf(stderr, "srtp_start() : connection already open\n");
    return SRTP_ERROR_protocol;
  }

  // Set random seed
  srandom(SEED+7);

  // Open local socket
  G_pcb.port = port;
  if (create_socket() < 0) {
    fprintf(stderr, "srtp_start() : create_socket()\n");
    G_pcb.state = SRTP_state_error;
    return SRTP_ERROR;
  }

  G_pcb.state = SRTP_state_listening;
  return G_pcb.sd;
}

/**
 * sd : socket descriptor as previously provided by srtp_start()
 * return : error - srtp-common.h
 *          success - sd, to indicate sd now also is "connected"
 * 
 * Accept incoming connection request.
 * For use by server process.
 */
int srtp_accept(int sd) {
  if (G_pcb.state != SRTP_state_listening) {
    fprintf(stderr, "srtp_accept() : not listening\n");
    return SRTP_ERROR_fsm;
  }

  // Wait for incoming connection, blocking call
  Srtp_Header_t pkt_hdr;
  if (recvfrom(sd, &pkt_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&G_pcb.remote, &l) < 0) {
    perror("srtp_accept() : recvfrom()");
    G_pcb.state = SRTP_state_error;
    return SRTP_ERROR;
  }
  // Set remote port
  G_pcb.remote.sin_port = G_pcb.local.sin_port;

  // Print packet
  if (DEBUG) {
    fprintf(stderr, "srtp_accept() : received open_req packet\n");
    fprintf(stderr, "  type = %d\n", pkt_hdr.type);
    fprintf(stderr, "  seqno = %d\n", ntohl(pkt_hdr.seqno));
    fprintf(stderr, "  timestamp = %ld\n", ntoh64(pkt_hdr.timestamp));
    fprintf(stderr, "  length = %d\n", ntohs(pkt_hdr.length));
  }

  // Check packet type
  if (pkt_hdr.type != SRTP_TYPE_open_req) {
    fprintf(stderr, "srtp_accept() : unexpected packet type\n");
    G_pcb.state = SRTP_state_error;
    return SRTP_ERROR_protocol;
  } else if (ntohl(pkt_hdr.seqno) != G_pcb.seq_rx) {
    fprintf(stderr, "srtp_accept() : unexpected sequence number\n");
    G_pcb.state = SRTP_state_error;
    return SRTP_ERROR_protocol;
  }

  // Update PCB
  G_pcb.open_req_rx++;

  // Send open_ack packet
  Srtp_Header_t ack_hdr;
  ack_hdr.type = SRTP_TYPE_open_ack;
  ack_hdr.seqno = pkt_hdr.seqno;
  ack_hdr.timestamp = pkt_hdr.timestamp;
  ack_hdr.length = htons(SRTP_HEADER_SIZE);

  if ((DROP_OPEN_ACK | DROP_RANDOM) && (random() % DROP_RATE_DEN < DROP_RATE_NUM)) {
    fprintf(stderr, "srtp_accept(): dropped packet\n");
  } else if (sendto(sd, &ack_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&G_pcb.remote, l) < 0) {
    perror("srtp_accept() : sendto()");
    G_pcb.state = SRTP_state_error;
    return SRTP_ERROR;
  }

  // Print packet
  if (DEBUG) {
    fprintf(stderr, "srtp_accept() : sent open_ack packet\n");
    fprintf(stderr, "  type = %d\n", ack_hdr.type);
    fprintf(stderr, "  seqno = %d\n", ntohl(ack_hdr.seqno));
    fprintf(stderr, "  timestamp = %ld\n", ntoh64(ack_hdr.timestamp));
    fprintf(stderr, "  length = %d\n", ntohs(ack_hdr.length));
  }
  
  // Update PCB
  G_pcb.seq_rx = ntohl(pkt_hdr.seqno) + 1;
  G_pcb.open_ack_tx++;
  G_pcb.state = SRTP_state_connected;
  G_pcb.start_time = srtp_timestamp();

  return sd;
}

/**
 * port : local and remote port number to be used for socket
 * return : error - SRTP_ERROR
 *          success - valid socket descriptor
 * 
 * Connect to remote server.
 * For use by client process.
 */
int srtp_open(const char *fqdn, uint16_t port) {
  if (G_pcb.state != SRTP_state_closed) {
    fprintf(stderr, "srtp_start() : connection already open\n");
    return SRTP_ERROR_protocol;
  }

  // Set random seed
  srandom(SEED-7);

  // Open local socket
  G_pcb.port = port;
  if (create_socket() < 0) {
    fprintf(stderr, "srtp_start() : create_socket()\n");
    G_pcb.state = SRTP_state_error;
    return SRTP_ERROR;
  }

  // Resolve remote address
  if (resolve_addr(fqdn) < 0) {
    fprintf(stderr, "srtp_open() : resolve_addr()\n");
    G_pcb.state = SRTP_state_error;
    return SRTP_ERROR;
  }

  // Update PCB
  G_pcb.state = SRTP_state_opening;

  // Send open_req packet
  send_open_req();

  // Wait for open_ack packet
  Srtp_Header_t ack_hdr;
  struct sockaddr_in temp_remote;
  // If max retries reached and no response, connection is closed
  while (1) {
    int n = recvfrom(G_pcb.sd, &ack_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&temp_remote, &l);
    if (n < 0) {
      if (G_pcb.state != SRTP_state_opening) {
        return SRTP_ERROR;
      }

      // If interrupted by signal, continue waiting
      if (errno == EINTR) {
        continue;
      }

      perror("srtp_open() : recvfrom()");
      G_pcb.state = SRTP_state_error;
      return SRTP_ERROR;
    } else {
      break;
    }
  }

  // Stop timer
  stop_timeout();
  timeout_count = 0;
  G_pcb.re_tx = 0;
  trip_count++;

  // Print packet
  if (DEBUG) {
    fprintf(stderr, "srtp_open() : received open_ack packet\n");
    fprintf(stderr, "  type = %d\n", ack_hdr.type);
    fprintf(stderr, "  seqno = %d\n", ntohl(ack_hdr.seqno));
    fprintf(stderr, "  timestamp = %ld\n", ntoh64(ack_hdr.timestamp));
    fprintf(stderr, "  length = %d\n", ntohs(ack_hdr.length));
  }

  // Check packet type
  if (ack_hdr.type != SRTP_TYPE_open_ack) {
    fprintf(stderr, "srtp_open() : unexpected packet type\n");
    G_pcb.state = SRTP_state_error;
    return SRTP_ERROR_protocol;
  } else if (ntohl(ack_hdr.seqno) != G_pcb.seq_rx) {
    fprintf(stderr, "srtp_open() : unexpected sequence number\n");
    G_pcb.state = SRTP_state_error;
    return SRTP_ERROR_protocol;
  }

  // Update PCB
  G_pcb.seq_tx++;
  G_pcb.open_ack_rx++;
  G_pcb.state = SRTP_state_connected;
  G_pcb.start_time = srtp_timestamp();
  G_pcb.rtt = srtp_timestamp() - ntoh64(ack_hdr.timestamp);
  recalibrate_rto();

  return G_pcb.sd;
}

/**
 * sd : socket descriptor
 * data : buffer with bytestream to transmit
 * data_size : number of bytes to transmit
 * return : error - SRTP_ERROR
 *          success - number of bytes transmitted
 * 
 * Send data packet.
 * For use by client and server process.
*/
int srtp_tx(int sd, void *data, uint16_t data_size) {
  if (G_pcb.state != SRTP_state_connected) {
    fprintf(stderr, "srtp_tx() : not connected\n");
    return SRTP_ERROR_fsm;
  }

  // Check data size
  if (data_size > SRTP_MAX_DATA_SIZE) {
    fprintf(stderr, "srtp_tx() : data size too large\n");
    return SRTP_ERROR_api;
  }

  // Set global data
  G_data = data;
  G_data_size = data_size;

  // Update PCB
  G_pcb.state = SRTP_state_transmit;

  // Send data packet
  send_data_req();

  Srtp_Header_t ack_hdr;
  struct sockaddr_in temp_remote;
  // Wait for data_ack packet
  while (G_pcb.state == SRTP_state_transmit) {
    while (1) {
      int n = recvfrom(sd, &ack_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&temp_remote, &l);
      if (n < 0) {
        if (G_pcb.state != SRTP_state_transmit) {
          return SRTP_ERROR;
        }

        // If interrupted by signal, continue waiting
        if (errno == EINTR) {
          continue;
        }

        perror("srtp_tx() : recvfrom()");
        G_pcb.state = SRTP_state_error;
        return SRTP_ERROR;
      } else {
        break;
      }
    }

    // print packet
    if (DEBUG) {
      fprintf(stderr, "srtp_tx() : received packet\n");
      fprintf(stderr, "  type = %d\n", ack_hdr.type);
      fprintf(stderr, "  seqno = %d\n", ntohl(ack_hdr.seqno));
      fprintf(stderr, "  timestamp = %ld\n", ntoh64(ack_hdr.timestamp));
      fprintf(stderr, "  length = %d\n", ntohs(ack_hdr.length));
    }

    // Discard packet if not from remote
    if (temp_remote.sin_addr.s_addr != G_pcb.remote.sin_addr.s_addr ||
        temp_remote.sin_port != G_pcb.remote.sin_port)
      continue;    

    // Correct acknowledgement
    if (ack_hdr.type == SRTP_TYPE_data_ack && ntohl(ack_hdr.seqno) == G_pcb.seq_tx)
      break;


    // Old data packet, acknowledge and discard
    if (ack_hdr.type == SRTP_TYPE_data_req && ntohl(ack_hdr.seqno) < G_pcb.seq_rx) {
      G_pcb.data_req_dup_rx++;
      G_pcb.data_req_bytes_dup_rx += ntohs(ack_hdr.length) - SRTP_HEADER_SIZE;

      ack_hdr.type = SRTP_TYPE_data_ack;
      ack_hdr.seqno = ack_hdr.seqno;
      ack_hdr.timestamp = hton64(srtp_timestamp());
      ack_hdr.length = htons(SRTP_HEADER_SIZE);
      
      if ((DROP_DATA_ACK | DROP_RANDOM) && (random() % DROP_RATE_DEN < DROP_RATE_NUM)) {
        fprintf(stderr, "srtp_tx(): dropped packet\n");
      } else if (sendto(G_pcb.sd, &ack_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&G_pcb.remote, l) < 0) {
        perror("srtp_tx() : sendto()");
        G_pcb.state = SRTP_state_error;
        return SRTP_ERROR;
      }
      G_pcb.data_ack_re_tx++;
      timeout_count = 1;
      continue;
    }

    // Old open request, acknowledge and discard
    if (ack_hdr.type == SRTP_TYPE_open_req && ntohl(ack_hdr.seqno) < G_pcb.seq_tx) {
      G_pcb.open_req_dup_rx++;

      Srtp_Header_t _ack_hdr;
      _ack_hdr.type = SRTP_TYPE_open_ack;
      _ack_hdr.seqno = ack_hdr.seqno;
      _ack_hdr.timestamp = ack_hdr.timestamp;
      _ack_hdr.length = htons(SRTP_HEADER_SIZE);

      if ((DROP_OPEN_ACK | DROP_RANDOM) && (random() % DROP_RATE_DEN < DROP_RATE_NUM)) {
        fprintf(stderr, "srtp_tx(): dropped packet\n");
      } else if (sendto(G_pcb.sd, &_ack_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&G_pcb.remote, l) < 0) {
        perror("srtp_tx() : sendto()");
        G_pcb.state = SRTP_state_error;
        return SRTP_ERROR;
      }
      G_pcb.open_ack_re_tx++;
      timeout_count = 1;
      continue;
    }

    if (ack_hdr.type == SRTP_TYPE_data_req) {
      G_pcb.data_req_dup_rx++;
      continue;
    } else if (ack_hdr.type == SRTP_TYPE_data_ack) {
      G_pcb.data_ack_dup_rx++;
      continue;
    } else if (ack_hdr.type == SRTP_TYPE_open_req) {
      G_pcb.open_req_dup_rx++;
      continue;
    } else if (ack_hdr.type == SRTP_TYPE_open_ack) {
      G_pcb.open_ack_dup_rx++;
      continue;
    }

    fprintf(stderr, "srtp_tx() : unexpected sequence number\n");
    G_pcb.state = SRTP_state_error;
    return SRTP_ERROR_protocol;
  }

  // Stop timer
  stop_timeout();
  timeout_count = 0;
  trip_count++;
  G_pcb.re_tx = 0;

  // Update PCB
  G_pcb.seq_tx++;
  G_pcb.data_ack_rx++;
  G_pcb.rtt = srtp_timestamp() - ntoh64(ack_hdr.timestamp);
  recalibrate_rto();
  G_pcb.state = SRTP_state_connected;

  return data_size;
}

/**
 * sd : socket descriptor
 * data : buffer to store bytestream received
 * data_size : size of buffer
 * return : error - SRTP_ERROR
 *          success - number of bytes received
 * 
 * Receive data packet.
 * For use by client and server process.
*/
int srtp_rx(int sd, void *data, uint16_t data_size) {
  if (G_pcb.state != SRTP_state_connected) {
    fprintf(stderr, "srtp_rx() : not connected\n");
    return SRTP_ERROR_fsm;
  }

  // Update PCB
  G_pcb.state = SRTP_state_receive;

  Srtp_Packet_t data_pkt;
  struct sockaddr_in temp_remote;
  // Wait for data_req packet
  while (G_pcb.state == SRTP_state_receive) {
    if (recvfrom(sd, &data_pkt, SRTP_MAX_PACKET_SIZE, 0, (struct sockaddr *)&temp_remote, &l) < 0) {
      perror("srtp_rx() : recvfrom()");
      G_pcb.state = SRTP_state_error;
      return SRTP_ERROR;
    }

    // Discard packet if not from remote
    if (temp_remote.sin_addr.s_addr != G_pcb.remote.sin_addr.s_addr ||
        temp_remote.sin_port != G_pcb.remote.sin_port)
      continue;

    // Correct data packet
    if (data_pkt.header.type == SRTP_TYPE_data_req && ntohl(data_pkt.header.seqno) == G_pcb.seq_rx)
      break;

    // Old data packet, acknowledge and discard
    if (data_pkt.header.type == SRTP_TYPE_data_req && ntohl(data_pkt.header.seqno) < G_pcb.seq_rx) {
      G_pcb.data_req_dup_rx++;
      G_pcb.data_req_bytes_dup_rx += ntohs(data_pkt.header.length) - SRTP_HEADER_SIZE;

      Srtp_Header_t ack_hdr;
      ack_hdr.type = SRTP_TYPE_data_ack;
      ack_hdr.seqno = data_pkt.header.seqno;
      ack_hdr.timestamp = data_pkt.header.timestamp;
      ack_hdr.length = htons(SRTP_HEADER_SIZE);

      if ((DROP_DATA_ACK | DROP_RANDOM) && (random() % DROP_RATE_DEN < DROP_RATE_NUM)) {
        fprintf(stderr, "srtp_rx(): dropped packet\n");
      } else if (sendto(G_pcb.sd, &ack_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&G_pcb.remote, l) < 0) {
        perror("srtp_rx() : sendto()");
        G_pcb.state = SRTP_state_error;
        return SRTP_ERROR;
      }
      G_pcb.data_ack_re_tx++;
      continue;
    }

    // Old open request, acknowledge and discard
    if (data_pkt.header.type == SRTP_TYPE_open_req && ntohl(data_pkt.header.seqno) < G_pcb.seq_rx) {
      G_pcb.open_req_dup_rx++;

      Srtp_Header_t ack_hdr;
      ack_hdr.type = SRTP_TYPE_open_ack;
      ack_hdr.seqno = data_pkt.header.seqno;
      ack_hdr.timestamp = data_pkt.header.timestamp;
      ack_hdr.length = htons(SRTP_HEADER_SIZE);

      if ((DROP_OPEN_ACK | DROP_RANDOM) && (random() % DROP_RATE_DEN < DROP_RATE_NUM)) {
        fprintf(stderr, "srtp_rx(): dropped packet\n");
      } else if (sendto(G_pcb.sd, &ack_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&G_pcb.remote, l) < 0) {
        perror("srtp_tx() : sendto()");
        G_pcb.state = SRTP_state_error;
        return SRTP_ERROR;
      }
      G_pcb.open_ack_re_tx++;

      continue;
    }

    if (data_pkt.header.type == SRTP_TYPE_data_req) {
      G_pcb.data_req_dup_rx++;
      continue;
    } else if (data_pkt.header.type == SRTP_TYPE_data_ack) {
      G_pcb.data_ack_dup_rx++;
      continue;
    } else if (data_pkt.header.type == SRTP_TYPE_open_req) {
      G_pcb.open_req_dup_rx++;
      continue;
    } else if (data_pkt.header.type == SRTP_TYPE_open_ack) {
      G_pcb.open_ack_dup_rx++;
      continue;
    }

    fprintf(stderr, "srtp_rx() : unexpected sequence number\n");
    G_pcb.state = SRTP_state_error;
    return SRTP_ERROR_protocol;
  }

  if (ntohs(data_pkt.header.length) - SRTP_HEADER_SIZE > data_size) {
    fprintf(stderr, "srtp_rx() : data size too large\n");
    return SRTP_ERROR_api;
  }
  // Copy data to buffer
  memcpy(data, data_pkt.payload, ntohs(data_pkt.header.length) - SRTP_HEADER_SIZE);

  // Update PCB
  G_pcb.data_req_bytes_rx += ntohs(data_pkt.header.length) - SRTP_HEADER_SIZE;
  G_pcb.data_req_rx++; 

  // Acknowledge data packet
  Srtp_Header_t ack_hdr;
  ack_hdr.type = SRTP_TYPE_data_ack;
  ack_hdr.seqno = data_pkt.header.seqno;
  ack_hdr.timestamp = data_pkt.header.timestamp;
  ack_hdr.length = htons(SRTP_HEADER_SIZE);

  if ((DROP_DATA_ACK | DROP_RANDOM) && (random() % DROP_RATE_DEN < DROP_RATE_NUM)) {
    fprintf(stderr, "srtp_rx(): dropped packet\n");
  } else if (sendto(G_pcb.sd, &ack_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&G_pcb.remote, l) < 0) {
    perror("srtp_rx() : sendto()");
    G_pcb.state = SRTP_state_error;
    return SRTP_ERROR;
  }

  // Print packet
  if (DEBUG) {
    fprintf(stderr, "srtp_rx() : sent data_ack packet\n");
    fprintf(stderr, "  type = %d\n", ack_hdr.type);
    fprintf(stderr, "  seqno = %d\n", ntohl(ack_hdr.seqno));
    fprintf(stderr, "  timestamp = %ld\n", ntoh64(ack_hdr.timestamp));
    fprintf(stderr, "  length = %d\n", ntohs(ack_hdr.length));
  }

  // Update PCB
  G_pcb.seq_rx++;
  G_pcb.data_ack_tx++;
  G_pcb.state = SRTP_state_connected;

  return data_size;
}

/**
 * port : local and remote port number to be used for socket
 * return : error - SRTP_ERROR
 *          success - SRTP_SUCCESS
 * 
 * Close connection.
 * For use by client and server process.
*/
int srtp_close(int sd) {
  if (G_pcb.state != SRTP_state_connected) {
    fprintf(stderr, "srtp_rx() : not connected\n");
    return SRTP_ERROR_fsm;
  }

  // Update PCB
  G_pcb.state = SRTP_state_closing_i;

  // Send close_req packet
  send_close_req();

  Srtp_Header_t ack_hdr;
  struct sockaddr_in temp_remote;
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(G_pcb.sd, &readfds);
  struct timeval timeout;
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  // Wait for close_ack packet
  while (G_pcb.state != SRTP_state_closing_r) {
    while (1) {
      int n = recvfrom(G_pcb.sd, &ack_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&temp_remote, &l);
      if (n < 0) {
        if (G_pcb.state == SRTP_state_closed) {
          return SRTP_ERROR;
        }

        // If interrupted by signal, continue waiting
        if (errno == EINTR) {
          continue;
        }

        perror("srtp_close() : select()");
        G_pcb.state = SRTP_state_error;
        return SRTP_ERROR;
      } else {
        break;
      }
    }

    // print packet
    if (DEBUG) {
      fprintf(stderr, "srtp_close() : received packet\n");
      fprintf(stderr, "  type = %d\n", ack_hdr.type);
      fprintf(stderr, "  seqno = %d\n", ntohl(ack_hdr.seqno));
      fprintf(stderr, "  timestamp = %ld\n", ntoh64(ack_hdr.timestamp));
      fprintf(stderr, "  length = %d\n", ntohs(ack_hdr.length));
    }

    // Discard packet if not from remote
    if (temp_remote.sin_addr.s_addr != G_pcb.remote.sin_addr.s_addr ||
        temp_remote.sin_port != G_pcb.remote.sin_port)
      continue;

    // Old open acknowledgement, discard
    if (ack_hdr.type == SRTP_TYPE_open_ack && ntohl(ack_hdr.seqno) < G_pcb.seq_tx) {
      G_pcb.open_ack_dup_rx++;
      continue;
    }
    // Old open request, acknowledge and discard
    if (ack_hdr.type == SRTP_TYPE_open_req && ntohl(ack_hdr.seqno) < G_pcb.seq_tx) {
      G_pcb.open_req_dup_rx++;

      ack_hdr.type = SRTP_TYPE_open_ack;
      ack_hdr.seqno = ack_hdr.seqno;
      ack_hdr.timestamp = hton64(srtp_timestamp());
      ack_hdr.length = htons(SRTP_HEADER_SIZE);

      if (DROP_OPEN_ACK && (random() % DROP_RATE_DEN < DROP_RATE_NUM)) {
        fprintf(stderr, "srtp_close(): dropped packet\n");
      } else if (sendto(G_pcb.sd, &ack_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&G_pcb.remote, l) < 0) {
        perror("srtp_close() : sendto()");
        G_pcb.state = SRTP_state_error;
        return SRTP_ERROR;
      }
      G_pcb.open_ack_re_tx++;
      timeout_count = 1;
      continue;
    }
    // Old data packet, acknowledge and discard
    if (ack_hdr.type == SRTP_TYPE_data_req && ntohl(ack_hdr.seqno) < G_pcb.seq_rx) {
      G_pcb.data_req_dup_rx++;
      G_pcb.data_req_bytes_dup_rx += ntohs(ack_hdr.length) - SRTP_HEADER_SIZE;

      ack_hdr.type = SRTP_TYPE_data_ack;
      ack_hdr.seqno = ack_hdr.seqno;
      ack_hdr.timestamp = hton64(srtp_timestamp());
      ack_hdr.length = htons(SRTP_HEADER_SIZE);

      if (DROP_DATA_ACK && (random() % DROP_RATE_DEN < DROP_RATE_NUM)) {
        fprintf(stderr, "srtp_close(): dropped packet\n");
      } else if (sendto(G_pcb.sd, &ack_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&G_pcb.remote, l) < 0) {
        perror("srtp_close() : sendto()");
        G_pcb.state = SRTP_state_error;
        return SRTP_ERROR;
      }
      timeout_count = 1;
      G_pcb.data_ack_re_tx++;
      continue;
    }
    // Old acknowledgement, discard
    if (ack_hdr.type == SRTP_TYPE_data_ack && ntohl(ack_hdr.seqno) < G_pcb.seq_tx) {
      G_pcb.data_ack_dup_rx++;
      continue;
    }

    if (G_pcb.state == SRTP_state_closing_i) {
      // Correct acknowledgement
      if (ack_hdr.type == SRTP_TYPE_close_ack && ntohl(ack_hdr.seqno) == G_pcb.seq_tx) {
        G_pcb.close_ack_rx++;
        G_pcb.seq_tx++;
        G_pcb.state = SRTP_state_closing_wait;
        trip_count++;
        stop_timeout();
        G_pcb.rtt = srtp_timestamp() - ntoh64(ack_hdr.timestamp);
        recalibrate_rto();
        continue;
      }

      // Close request
      if (ack_hdr.type == SRTP_TYPE_close_req && ntohl(ack_hdr.seqno) == G_pcb.seq_rx) {
        G_pcb.close_req_rx++;

        ack_hdr.type = SRTP_TYPE_close_ack;
        ack_hdr.seqno = ack_hdr.seqno;
        ack_hdr.timestamp = hton64(srtp_timestamp());
        ack_hdr.length = htons(SRTP_HEADER_SIZE);

        if (DROP_CLOSE_ACK && (random() % DROP_RATE_DEN < DROP_RATE_NUM)) {
          fprintf(stderr, "srtp_close(): dropped packet\n");
        } else if (sendto(G_pcb.sd, &ack_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&G_pcb.remote, l) < 0) {
          perror("srtp_close() : sendto()");
          G_pcb.state = SRTP_state_error;
          return SRTP_ERROR;
        }
        G_pcb.close_ack_tx++;
        G_pcb.seq_rx = ntohl(ack_hdr.seqno) + 1;
        timeout_count = 1;
        G_pcb.state = SRTP_state_closing_wait2;
        continue;
      }

      // Old acknowledgement, discard
      if (ack_hdr.type == SRTP_TYPE_data_ack && ntohl(ack_hdr.seqno) < G_pcb.seq_tx) {
        G_pcb.data_ack_dup_rx++;
        continue;
      }

      // Old data packet, acknowledge and discard
      if (ack_hdr.type == SRTP_TYPE_data_req && ntohl(ack_hdr.seqno) < G_pcb.seq_rx) {
        G_pcb.data_req_dup_rx++;
        G_pcb.data_req_bytes_dup_rx += ntohs(ack_hdr.length) - SRTP_HEADER_SIZE;

        ack_hdr.type = SRTP_TYPE_data_ack;
        ack_hdr.seqno = ack_hdr.seqno;
        ack_hdr.timestamp = hton64(srtp_timestamp());
        ack_hdr.length = htons(SRTP_HEADER_SIZE);

        if (DROP_DATA_ACK && (random() % DROP_RATE_DEN < DROP_RATE_NUM)) {
          fprintf(stderr, "srtp_close(): dropped packet\n");
        } else if (sendto(G_pcb.sd, &ack_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&G_pcb.remote, l) < 0) {
          perror("srtp_close() : sendto()");
          G_pcb.state = SRTP_state_error;
          return SRTP_ERROR;
        }
        timeout_count = 1;
        G_pcb.data_ack_re_tx++;
        continue;
      }

      fprintf(stderr, "srtp_close() : unexpected sequence number\n");
      G_pcb.state = SRTP_state_error;
      return SRTP_ERROR_protocol;
    }

    if (G_pcb.state == SRTP_state_closing_wait) {
      // Correct close request
      if (ack_hdr.type == SRTP_TYPE_close_req && ntohl(ack_hdr.seqno) == G_pcb.seq_rx) {
        G_pcb.close_req_rx++;

        ack_hdr.type = SRTP_TYPE_close_ack;
        ack_hdr.seqno = ack_hdr.seqno;
        ack_hdr.timestamp = hton64(srtp_timestamp());
        ack_hdr.length = htons(SRTP_HEADER_SIZE);

        if (DROP_CLOSE_ACK && (random() % DROP_RATE_DEN < DROP_RATE_NUM)) {
          fprintf(stderr, "srtp_close(): dropped packet\n");
        } else if (sendto(G_pcb.sd, &ack_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&G_pcb.remote, l) < 0) {
          perror("srtp_close() : sendto()");
          G_pcb.state = SRTP_state_error;
          return SRTP_ERROR;
        }
        G_pcb.close_ack_tx++;
        G_pcb.seq_rx = ntohl(ack_hdr.seqno) + 1;
        timeout_count = 1;
        G_pcb.state = SRTP_state_closing_r;
        continue;
      }

      // Old close acknowledgement, discard
      if (ack_hdr.type == SRTP_TYPE_close_ack && ntohl(ack_hdr.seqno) < G_pcb.seq_tx) {
        G_pcb.close_ack_dup_rx++;
        continue;
      }

      // Old data packet, acknowledge and discard
      if (ack_hdr.type == SRTP_TYPE_data_req && ntohl(ack_hdr.seqno) < G_pcb.seq_rx) {
        G_pcb.data_req_dup_rx++;
        G_pcb.data_req_bytes_dup_rx += ntohs(ack_hdr.length) - SRTP_HEADER_SIZE;

        ack_hdr.type = SRTP_TYPE_data_ack;
        ack_hdr.seqno = ack_hdr.seqno;
        ack_hdr.timestamp = hton64(srtp_timestamp());
        ack_hdr.length = htons(SRTP_HEADER_SIZE);

        if (DROP_DATA_ACK && (random() % DROP_RATE_DEN < DROP_RATE_NUM)) {
          fprintf(stderr, "srtp_close(): dropped packet\n");
        } else if (sendto(G_pcb.sd, &ack_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&G_pcb.remote, l) < 0) {
          perror("srtp_close() : sendto()");
          G_pcb.state = SRTP_state_error;
          return SRTP_ERROR;
        }
        timeout_count = 1;
        G_pcb.data_ack_re_tx++;
        continue;
      }

      // Old acknowledgement, discard
      if (ack_hdr.type == SRTP_TYPE_data_ack && ntohl(ack_hdr.seqno) < G_pcb.seq_tx) {
        G_pcb.data_ack_dup_rx++;
        continue;
      }

      fprintf(stderr, "srtp_close() : unexpected sequence number\n");
      G_pcb.state = SRTP_state_error;
      return SRTP_ERROR_protocol;
    }

    if (G_pcb.state == SRTP_state_closing_wait2) {
      // Close acknowledgement
      if (ack_hdr.type == SRTP_TYPE_close_ack && ntohl(ack_hdr.seqno) == G_pcb.seq_tx) {
        G_pcb.close_ack_rx++;
        G_pcb.seq_tx++;
        trip_count++;
        G_pcb.state = SRTP_state_closed;
        G_pcb.rtt = srtp_timestamp() - ntoh64(ack_hdr.timestamp);
        recalibrate_rto();
        stop_timeout();
        return SRTP_SUCCESS;
      }

      // Old close request, acknowledge and discard
      if (ack_hdr.type == SRTP_TYPE_close_req && ntohl(ack_hdr.seqno) < G_pcb.seq_tx) {
        G_pcb.close_req_dup_rx++;

        ack_hdr.type = SRTP_TYPE_close_ack;
        ack_hdr.seqno = ack_hdr.seqno;
        ack_hdr.timestamp = hton64(srtp_timestamp());
        ack_hdr.length = htons(SRTP_HEADER_SIZE);

        if (DROP_CLOSE_ACK && (random() % DROP_RATE_DEN < DROP_RATE_NUM)) {
          fprintf(stderr, "srtp_close(): dropped packet\n");
        } else if (sendto(G_pcb.sd, &ack_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&G_pcb.remote, l) < 0) {
          perror("srtp_close() : sendto()");
          G_pcb.state = SRTP_state_error;
          return SRTP_ERROR;
        }
        timeout_count = 1;
        G_pcb.close_ack_re_tx++;
        continue;
      }

      // Old data packet, acknowledge and discard
      if (ack_hdr.type == SRTP_TYPE_data_req && ntohl(ack_hdr.seqno) < G_pcb.seq_rx) {
        G_pcb.data_req_dup_rx++;
        G_pcb.data_req_bytes_dup_rx += ntohs(ack_hdr.length) - SRTP_HEADER_SIZE;

        ack_hdr.type = SRTP_TYPE_data_ack;
        ack_hdr.seqno = ack_hdr.seqno;
        ack_hdr.timestamp = hton64(srtp_timestamp());
        ack_hdr.length = htons(SRTP_HEADER_SIZE);

        if (DROP_DATA_ACK && (random() % DROP_RATE_DEN < DROP_RATE_NUM)) {
          fprintf(stderr, "srtp_close(): dropped packet\n");
        } else if (sendto(G_pcb.sd, &ack_hdr, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&G_pcb.remote, l) < 0) {
          perror("srtp_close() : sendto()");
          G_pcb.state = SRTP_state_error;
          return SRTP_ERROR;
        }
        timeout_count = 1;
        G_pcb.data_ack_re_tx++;
        continue;
      }

      // Old acknowledgement, discard
      if (ack_hdr.type == SRTP_TYPE_data_ack && ntohl(ack_hdr.seqno) < G_pcb.seq_tx) {
        G_pcb.data_ack_dup_rx++;
        continue;
      }

      fprintf(stderr, "srtp_close() : unexpected sequence number\n");
      G_pcb.state = SRTP_state_error;
      return SRTP_ERROR_protocol;
    }

    fprintf(stderr, "srtp_close() : unexpected state\n");
    G_pcb.state = SRTP_state_error;
    return SRTP_ERROR_protocol;
  }

  G_pcb.finish_time = srtp_timestamp();

  // Keep connection alive for a short time allowing for retransmissions of close_ack
  Srtp_Header_t close_packet;
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  while (1) {
    int ret = select(G_pcb.sd + 1, &readfds, NULL, NULL, &timeout);

    if (ret < 0) {
      perror("srtp_close() : select()");
      G_pcb.state = SRTP_state_error;
      return SRTP_ERROR;
    } else if (ret > 0) {
      if (recvfrom(G_pcb.sd, &close_packet, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&temp_remote, &l) < 0) {
        perror("srtp_close() : recvfrom()");
        G_pcb.state = SRTP_state_error;
        return SRTP_ERROR;
      }

      // print packet
      if (DEBUG) {
        fprintf(stderr, "srtp_close() : received close_ack packet\n");
        fprintf(stderr, "  type = %d\n", close_packet.type);
        fprintf(stderr, "  seqno = %d\n", ntohl(close_packet.seqno));
        fprintf(stderr, "  timestamp = %ld\n", ntoh64(close_packet.timestamp));
        fprintf(stderr, "  length = %d\n", ntohs(close_packet.length));
      }

      // Discard packet if not from remote
      if (temp_remote.sin_addr.s_addr != G_pcb.remote.sin_addr.s_addr ||
          temp_remote.sin_port != G_pcb.remote.sin_port)
        continue;

      // Discard close_ack packet
      if (close_packet.type == SRTP_TYPE_close_ack && ntohl(close_packet.seqno) == G_pcb.seq_tx)
        continue;

      // Old close request, acknowledge and discard
      if (close_packet.type == SRTP_TYPE_close_req && ntohl(close_packet.seqno) < G_pcb.seq_tx) {
        G_pcb.close_req_dup_rx++;

        close_packet.type = SRTP_TYPE_close_ack;
        close_packet.seqno = close_packet.seqno;
        close_packet.timestamp = hton64(srtp_timestamp());
        close_packet.length = htons(SRTP_HEADER_SIZE);

        if (DROP_CLOSE_ACK && (random() % DROP_RATE_DEN < DROP_RATE_NUM)) {
          fprintf(stderr, "srtp_close(): dropped packet\n");
        } else if (sendto(G_pcb.sd, &close_packet, SRTP_HEADER_SIZE, 0, (struct sockaddr *)&G_pcb.remote, l) < 0) {
          perror("srtp_close() : sendto()");
          G_pcb.state = SRTP_state_error;
          return SRTP_ERROR;
        }
        G_pcb.close_ack_re_tx++;
        continue;
      }
    }

    break;
  }

  G_pcb.state = SRTP_state_closed;
  return SRTP_SUCCESS;
}
