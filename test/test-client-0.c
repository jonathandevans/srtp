/*
  CS3102 Coursework P2 : Simple, Reliable Transport Protocol (SRTP)
  saleem, Jan2024, Feb2023
  sjm55 (checked February 2024)

  Basic test client to show use of API in srtp.h : send then receive a single packet.
*/

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include "srtp.h"
#include "srtp-common.h"
#include "srtp-pcb.h"

int
main(int argc, char *argv[])
{
  uid_t port = getuid(); // same as test-server-0.c
  int sd;
  uint8_t *data;
  int b;
  int32_t v1, *v1_p, v2, *v2_p;
  int c = 0x00310200; // test value, same as test-server-0.c
  char remote[22];

  if (argc != 2) {
    printf("usage: test-client-0 <fqdn>\n");
    exit(0);
  }
  printf("test-client-0\n");

  if (port < 1024)
    port += 1024; // hack, avoid reserved ports, should not happen in labs

  srtp_initialise();
  sd = srtp_open(argv[1], port);
  if (sd < 0) {
    printf("srtp_open() failed for %s\n", argv[1]);
    exit(0);
  }
  SrtpPcb_remote(remote);
  printf("rtp_open() : connected to %s\n", remote);

  data = malloc(SRTP_MAX_DATA_SIZE); /* SRTP_MAX_DATA_SIZE = 1280 */
  v1_p = (int32_t *) data; // first word
  v2_p = (int32_t *) &data[SRTP_MAX_DATA_SIZE - sizeof(int32_t)]; // last word

  /*
    tx : transmit a packet
  */
  memset(data, 0, SRTP_MAX_DATA_SIZE);
  *v1_p = htonl(c); // first word
  *v2_p = htonl(c); // last word
  b = srtp_tx(sd, data, SRTP_MAX_DATA_SIZE);
  if (b != SRTP_MAX_DATA_SIZE)
    printf("srtp_tx() failed\n");
  
  printf("srtp_tx() : %d/%d bytes sent\n", b, SRTP_MAX_DATA_SIZE);

  /*
    rx : receive a packet
  */
  memset(data, 0, SRTP_MAX_DATA_SIZE);
  b = srtp_rx(sd, data, SRTP_MAX_DATA_SIZE);
  if (b < 0)
    printf("srtp_rx() failed\n");

  else {
    /* Is size as expected? */
    if (b == SRTP_MAX_DATA_SIZE)
      { v1 = ntohl(*(v1_p)); v2 = ntohl(*(v2_p)); }
    else { v1 = v2 = -1; }

    /* Is content as expected? */
#define CHECK_DATA(v_, c_) (v_ == c_ ? "(correct)" : "(incorrect)")
    printf("srtp_rx() : %d bytes %s, v1=%d %s, v2=%d %s.\n",
           b, CHECK_DATA(b, SRTP_MAX_DATA_SIZE),
           v1, CHECK_DATA(v1, c),
           v2, CHECK_DATA(v2, c));
  }

  /*
    finish
  */  
  srtp_close(sd);
  SrtpPcb_report();
  free(data);

  return 0;
}
