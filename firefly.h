
#include "config.h"

#if !(defined(SONG1) || defined(SONG2))
#define SONG1 1
#endif

#ifndef RANDOM_SEED
#define RANDOM_SEED 0x5806
#endif

#include "songs/songs.h"

#include "songs/bootsong.h"

#ifdef SONG1
#include "songs/song1a.h"
#include "songs/song1b.h"
#include "songs/song1c.h"
//#include "songs/song1d.h"  // too fast
#include "songs/song1e.h"
#endif

#ifdef SONG2
#include "songs/song2a.h"
#include "songs/song2b.h"
#include "songs/song2c.h"
#include "songs/song2d.h"
#include "songs/song2e.h"
#include "songs/song2f.h"
#include "songs/song2g.h"
#endif

const Song *bootsongs[] = { &bootsong };

/* Keep the number of songs as a power of 2 so that the compiler       */
/* can optimize the pickasong() routine using bit shift multiplication */

const Song *songs[] = { 
#ifdef SONG1
       &song1a, &song1a, &song1a, &song1a, &song1a,
       &song1b, &song1b, &song1b, &song1b, &song1b,
       &song1c, &song1c, &song1c,
       &song1e, &song1e, &song1e,
#endif

#ifdef SONG2
        &song2a, 
        &song2b, &song2b,
        &song2c, &song2c, &song2c,
        &song2d, &song2d,
        &song2e, &song2e, &song2e,
        &song2f, &song2f,
        &song2g, &song2g, &song2g, 
#endif
        };

/*  The following code impliments some flags using the GPIOR0 general
 *  pupose I/O register.  It's ugly, but doing it this way immediately
 *  shaved over 230 bytes off of my code.
*/

typedef struct {
    uint8_t  b0:1, 
             b1:1,
             b2:1,
             b3:1,
             b4:1,
             b5:1,
             b6:1,
             b7:1;
} io_reg;

/* Pins A and B are always driven high when playing a song.  The other pins,
 * A, D, and E are flickered on an off to drive an individual LED.
 * Due to how the circuit is constructed, only one of A or B can
 * be high at a time, meaning only the LED's directly connected them
 * are compatible with being played at the same time.
 *    The possible simultaneous combinations are 
 *    
 *    if ( A ) { A-C, A-D, A-E }
 *      else   { B-C, B-D, B-E } //B
*/

#define PIN_B 1
#define PIN_A 4

#define PIN_C 3
#define PIN_D 2
#define PIN_E 0

#define FLAGS0 GPIOR0

/* pwm(value, error, result, resultmask) 
 *   Impliments 8-bit PWM with the result being a bit 
 *   (selected by resultmask) set or cleared in result. 
 *   Adopted from Bresenham's line drawing algorithm and heavily
 *   optimized.  I've also heard this called 'delta-sigma' encoding.
 *       error should return to 0 after 256 cycles.
 *       don't change 'value' until the cycle of 256 is done.
 *   If all values are kept in registers, this
 *   takes either 8 or 9 cycles to execute and consumes 20 bytes
 *   of program space.
*/
#define pwm(value, error, result, resultmask)        \
    __asm__ __volatile__ (                           \
            "mov     __tmp_reg__, %5"         "\n\t" \
            "bst     %3, 7"                   "\n\t" \
            "add     %0, %3"                  "\n\t" \
            "brvs    von_%="                  "\n\t" \
            "brts    turnon_%="               "\n\t" \
            "rjmp    turnoff_%="              "\n"   \
"von_%=:"                                     "\n\t" \
            "brts    turnoff_%="              "\n"   \
"turnon_%=:"                                  "\n\t" \
            "com     __tmp_reg__"             "\n\t" \
            "and     %1, __tmp_reg__"         "\n"   \
            "rjmp    done_%="                 "\n"   \
"turnoff_%=:"                                 "\n\t" \
            "or      %1, __tmp_reg__"         "\n\t" \
"done_%=:"                                    "\n\t" \
            : "=&r" (error), "=&r" (result)          \
            : "0" (error), "r"  (value),             \
              "1"  (result),                         \
              "r"  (resultmask)                      \
            );

/* This lookup table is used by the randbits function  */
/* to quickly generate a bitmask for a given number of */
/* requested bits.  Using this table is faster than    */
/* computing a bit mask on the fly.                    */

const prog_uint8_t bitmasks[] = { 
        0x00,
        0x01, 0x03, 0x07, 0x0F,
        0x1F, 0x3F, 0x7F, 0xFF
}; 

