/**
 * CS3102 Coursework P2 : Simple, Reliable Transport Protocol (SRTP)
 *
 * saleem (edited January 2024, February 2023)
 * sjm55 (checked February 2024)
 * 210016688 (edited March 2024)
 *
 * Finite State Machine (FSM) for SRTP.
 */

#ifndef __srtp_fsm_h__
#define __srtp_fsm_h__

typedef enum SRTP_state_e {
  SRTP_state_error = -1,    // -1 problem with state
  SRTP_state_closed,        //  0 connection closed
  SRTP_state_listening,     //  1 @server : listening for incoming connections
  SRTP_state_opening,       //  2 @client : sent request to start connection
  SRTP_state_connected,     //  3 connected : connection established
  SRTP_state_transmit,      //  4 transmitting : data being sent
  SRTP_state_receive,       //  5 receiving : data being received
  SRTP_state_closing_i,     //  6 closing : close request sent
  SRTP_state_closing_wait,  //  7 closing : connection received close acknowledgement
  SRTP_state_closing_wait2, //  8 closing : connection received close request
  SRTP_state_closing_r,     //  9 closing : both sides have sent close request and received acknowledgement
} SRTP_state_t;

#endif /* __srtp_fsm_h__ */
