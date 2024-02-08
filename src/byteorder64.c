/*
  Byte order for 64-bit integers in memory.

  Saleem Bhatti <https://saleem.host.cs.st-andrews.ac.uk/>

  Jan2024, Jan2023 revised (simplified code)
  sjm55 (checked February 2024)
*/

/*
  There does not appear to be a standard endian check mechanism,
  or a standard host-to-network / network-to-host API for dealing
  with 64-bit integer values.

  Linux does have a set of functions in:

    #include <endian.h>
    man 3 endian

  but these are not POSIX compliant.

  So, some examples of how to check/convert 64-bit value are below.

  This has been tested on AMD64, Intel64, and Raspberry-Pi/ARM-Cortex-72,
  all of which are little-endian.

  (I did not have a real big-endian machine to test this with, but
  have checked with ppc64 cross-compile and emulation on linux.)

*/

#include "byteorder64.h"

static uint16_t S_endian_u16_v = 0xaabb;
static uint8_t  *S_endian_u8_p = (uint8_t *) &S_endian_u16_v;

/* BIG ENDIAN : most significant byte in low order address */
/* LITTLE ENDIAN : least significant byte in low order address */

/* Network Byte Order is BIG ENDIAN, i.e. most significant byte "first" */

#define IS_BIG_ENDIAN    (S_endian_u8_p[0] == 0xaa)
#define IS_LITTLE_ENDIAN (S_endian_u8_p[0] == 0xbb)

int isBigEndian()    { return IS_BIG_ENDIAN ? 1 : 0; }
int isLittleEndian() { return IS_LITTLE_ENDIAN ? 1 : 0; }

/* uncomment the line below to use bit-fiddling instead of struct/memory */
// #define USE_UNION

#ifdef USE_UNION

typedef union uint_64_8x8_u {
  uint64_t v64;
  uint8_t  v8[8];
} uint_64_8x8_t;

uint64_t
reverseByteOrder64(uint64_t v64)
{
  uint_64_8x8_t v, v_r;
  v.v64 = v64;

  v_r.v8[0] = v.v8[7];
  v_r.v8[1] = v.v8[6];
  v_r.v8[2] = v.v8[5];
  v_r.v8[3] = v.v8[4];
  v_r.v8[4] = v.v8[3];
  v_r.v8[5] = v.v8[2];
  v_r.v8[6] = v.v8[1];
  v_r.v8[7] = v.v8[0];

  return v_r.v64;
}

#else /* USE_UNION */

uint64_t
reverseByteOrder64(uint64_t v64)
{
  uint64_t v_r;

  v_r = (v64 & 0xff00000000000000) >> 56 |
        (v64 & 0x00ff000000000000) >> 40 |
        (v64 & 0x0000ff0000000000) >> 24 |
        (v64 & 0x000000ff00000000) >>  8 |
        (v64 & 0x00000000ff000000) <<  8 |
        (v64 & 0x0000000000ff0000) << 24 |
        (v64 & 0x000000000000ff00) << 40 |
        (v64 & 0x00000000000000ff) << 56;

  return v_r;
}

#endif

uint64_t
hton64(uint64_t v64) { return IS_BIG_ENDIAN ? v64 : reverseByteOrder64(v64); }

uint64_t
ntoh64(uint64_t v64) { return IS_BIG_ENDIAN ? v64 : reverseByteOrder64(v64); }
