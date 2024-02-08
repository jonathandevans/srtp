/*
  CS3102 Coursework P2 : Simple, Reliable Transport Protocol (SRTP)
    saleem, Jan2024, Feb2023
    sjm55 (checked Feb 2024)

  Basic test server to show use of API in srtp.h : receive then send a single packet.
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
main(int argc, char **argv)
{
  uid_t port = getuid(); // same as test-client-0.c
  int start_sd, sd;
  uint8_t *data;
  int b;
  int32_t v1, *v1_p, v2, *v2_p;
  uint32_t c = 0x00310200; // test value, same as test-client-0.c
  char remote[22];

  printf("test-server-0\n");

  if (port < 1024)
    port += 1024; // hack, avoid reserved ports, should not happen in labs

  srtp_initialise();

  start_sd = srtp_start(port);
  if (start_sd < 0) {
    printf("start() failed\n");
    exit(0);
  }
  printf("start() OK\n");

  sd = srtp_accept(start_sd);
  if (sd < 0) {
    printf("accept() failed\n");
    exit(0);
  }
  SrtpPcb_remote(remote);
  printf("accept() : connected to %s\n", remote);


  data = malloc(SRTP_MAX_DATA_SIZE); /* SRTP_MAX_DATA_SIZE = 1280 */
  v1_p = (int32_t *) data; // first word
  v2_p = (int32_t *) &data[SRTP_MAX_DATA_SIZE - sizeof(int32_t)]; // last word

  /*
    rx : receive a packet
  */
  memset(data, 0, SRTP_MAX_DATA_SIZE);
  b = srtp_rx(sd, data, SRTP_MAX_DATA_SIZE);

  if (b < 0)
    printf("srtp_rx() failed.\n");

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
    tx : transmit a packet
  */
  *v1_p = htonl(c); // first word
  *v2_p = htonl(c); // last word
  b = srtp_tx(sd, data, SRTP_MAX_DATA_SIZE);
  if (b != SRTP_MAX_DATA_SIZE)
    printf("srtp_tx() failed\n");
  printf("srtp_tx() : %d/%d bytes sent\n", b, SRTP_MAX_DATA_SIZE);

  /*
    finish
  */
  srtp_close(sd);
  SrtpPcb_report();
  free(data);

  return 0;
}
