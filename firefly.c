#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include "firefly.h"

const prog_uint8_t about[] = "Jar of Fireflies\n"
            "Design and Implementation by Xander Hudson (jar@synoptic.org)\n"
            "Idea and Inspiration by Kayobi Tierney\n"
            "Special thanks to Katie Horn for getting me to think about electronics";
const prog_uint8_t version[] = "$Revision: 1.41 $ $Date: 2007/01/10 04:35:55 $";

#if USE_PHOTOTRANSISTOR
// PIN_F (reset/PB5) used as ADC0, thus an input
#  define LEDS_OFF ~(_BV(PIN_A) | _BV(PIN_B) | _BV(PIN_F))
#  define DDRB_DEFAULT ~(_BV(PIN_F))
#else
#  define LEDS_OFF ~(_BV(PIN_A) | _BV(PIN_B))
#  define DDRB_DEFAULT 0xFF
#endif

#define WATCHDOG_OK  WDTCR |= _BV(WDIE)

uint8_t pickapin (uint8_t);
const Song *pickasong (void);
uint8_t randwaitval (void);
uint8_t randbits (uint8_t);
uint8_t pickmaster (void);
uint8_t randscaleval (void);
void showbootup (void);

#define ch1playing ((volatile io_reg*) _SFR_MEM_ADDR(FLAGS0))->b0
#define ch1resting ((volatile io_reg*) _SFR_MEM_ADDR(FLAGS0))->b1
#define ch2playing ((volatile io_reg*) _SFR_MEM_ADDR(FLAGS0))->b2
#define ch2resting ((volatile io_reg*) _SFR_MEM_ADDR(FLAGS0))->b3
#define need_mood ((volatile io_reg*) _SFR_MEM_ADDR(FLAGS0))->b4
#define ch1noteready ((volatile io_reg*) _SFR_MEM_ADDR(FLAGS0))->b6
#define ch2noteready ((volatile io_reg*) _SFR_MEM_ADDR(FLAGS0))->b7

volatile uint8_t ch1restcount;
volatile uint8_t ch2restcount;

volatile uint8_t ch1nextnote;
volatile uint8_t ch2nextnote;

volatile uint8_t ch1scale;
volatile uint8_t ch2scale;
volatile uint8_t ch1nextscale;
volatile uint8_t ch2nextscale;

register uint8_t ch1bright asm ("r2");
register uint8_t ch2bright asm ("r3");
register uint8_t ch1err asm ("r4");
register uint8_t ch2err asm ("r5");
register uint8_t ch1pin asm ("r6");
register uint8_t ch2pin asm ("r7");
register uint8_t portval asm ("r8");

/*  mood, mood_count                                                */
/*  This value changes slowly over time and is added to the random          */
/*  wait timer.  Having this change slowly adds a sense of 'moodiness'      */
/*  where the fireflys will go through phases of being active or lethargic. */

uint8_t mood = 2;
uint16_t mood_count = 30;   // WDT cycles before changing mood

volatile uint8_t masterpinmask;

static void still_dark(void);
static void wait_for_dark(void);
#if USE_PHOTOTRANSISTOR
volatile uint8_t waiting_for_dark;	// true if in the "wait for dark" state.
volatile uint8_t is_dark;	        // true if it still seems to be dark.
volatile uint8_t force_dark;
static uint8_t read_adc(uint8_t);
#else
#   define is_dark          1
#   define waiting_for_dark 0
#endif

#if READ_TEMPERATURE
static uint8_t read_temperature(void);
#endif

/* Intentionally crash the program leaving a visual clue.  The  */
/* watchdog should then come and reboot us.                     */
#define FREAKOUT  for (;;) {PORTB = LEDS_OFF; PORTB = ~LEDS_OFF; } 


ISR(WDT_vect) {
    still_dark();
	if (ch1resting && (--ch1restcount == 0)) ch1resting = 0;
	if (ch2resting && (--ch2restcount == 0)) ch2resting = 0;
	if (!need_mood && (--mood_count == 0)) need_mood = 1;
}

ISR(TIM0_COMPA_vect) {
    static uint8_t x;

    if (waiting_for_dark)
		return;

    // Do this right up front so there's no jitter from all
    // the complex logic to follow.
    PORTB=portval;
    
    // x rolls around to zero naturally every 256 cycles.  It's on
    // the start of one of these cycles that we change the note that
    // we're playing.
    if ( x-- == 0 ) {
        if (ch1playing) {
            // We're playing a song. Is there another note
            // for us to read? If not, shut down.
            if (ch1noteready) {  
                ch1bright = ch1nextnote;
                ch1noteready = 0;
            } else {
                // We ran out of notes to play.
                ch1bright = 0;
                // Has our rest duration been chosen yet?
                if (ch1restcount != 0) {  
                    ch1playing = 0;   
                    ch1resting = 1;
                } // if not, go through the loop again.
            }
        } else if (!ch1resting && ch1noteready) {  
                // We weren't playing a song but now we see
                // that we're clear to start.
                ch1bright = ch1nextnote;
                ch1noteready = 0;
                ch1playing = 1;
        }

        if (ch2playing) {
            if (ch2noteready) {
                ch2bright = ch2nextnote;
                ch2noteready = 0;
            } else {
                ch2bright = 0;
                // Has our rest duration been chosen yet?
                if (ch2restcount != 0) {  
                    ch2playing = 0;   
                    ch2resting = 1;
                } // if not, go through the loop again.
            }
        } else if (!ch2resting && ch2noteready) {
                ch2bright = ch2nextnote;
                ch2noteready = 0;
                ch2playing = 1;
        }
        
        // These shouldn't be neccessary since the error
        // value should wrap around to 0 after 256 cycles
        // anyways, but leave these in until I'm able to
        // verify that.

        if (ch1playing) ch1err = 0;
        if (ch2playing) ch2err = 0;

        // If either channel is playing, turn on the master pin.    
        if (ch1playing || ch2playing) 
            portval = LEDS_OFF | masterpinmask;
        else 
            portval = LEDS_OFF;    // lights off.
    }

    // Here is where we do the actual PWM, which is defined as
    // an inline assembler macro.
    if ( ch1playing ) pwm(ch1bright, ch1err, portval, ch1pin);
    if ( ch2playing ) pwm(ch2bright, ch2err, portval, ch2pin);

}


/*
 * Do all the startup-time peripheral initializations.
 */
static void
ioinit(void)
{ 
    wdt_enable(WDTO_500MS);   // Set watchdog timeout

	WATCHDOG_OK; 	// Set watchdog to generate interrupt.

    // Set all pins as outputs.
    // Drive PIN_A and PIN_B low, everybody else high.
    DDRB = DDRB_DEFAULT;
    PORTB= LEDS_OFF;

    PRR |= _BV(PRTIM1);  // Turn off Timer1 to save power.
    PRR |= _BV(PRUSI);   // Turn off USI clock
    PRR |= _BV(PRADC);   // Turn off ADC

#if USE_PHOTOTRANSISTOR
    // disable digital input buffer on ADC pin to reduce power consumption
    DIDR0 = _BV(ADC0D);
#endif

    /*
     * Set up the 8-bit timer 0.
     *
     * Timer0 will be set in CTC up with a prescalar of 8 and an OCR0A copmare 
     * value of 25. 
     * A core clock frequency of 8mhz / 4 means we should get an interrupt 
     * frequency of about 39hz.  (freq = 976.563 / (OCR0A))
     */

    TCCR0A = _BV(WGM01);  // Set CTC mode, timer counts 0 - OCRA

    //TCCR0B = _BV(CS01) | _BV(CS00); // Clock/64
    //TCCR0B = _BV(CS00); // Clock
    TCCR0B = _BV(CS01); // Clock/8
    OCR0A = 25;

    clock_prescale_set(clock_div_4);

    TIMSK = _BV(OCIE0A);

    WATCHDOG_OK;
}

void showbootup (void) {
    // This is a bit of a mystery
    // Wasn't able to find out why the program would
    // break if I tried to something like 
    //       Song *ch1song = &bootsong;
    const Song *ch1song = bootsongs[0];
    uint16_t ch1noteptr;
    uint8_t mpin,cpin;

    const uint8_t masterpins[] = { _BV(PIN_A), _BV(PIN_B) };
    const uint8_t ch1pins[] = { _BV(PIN_C), _BV(PIN_D), _BV(PIN_E) };

    cli();
    ch1restcount = 2;
    ch1resting = 1; 
    sei();

    for (mpin=0; mpin < sizeof(masterpins)/sizeof(uint8_t); mpin++) {
        masterpinmask = masterpins[mpin];

        for (cpin=0; cpin < sizeof(ch1pins)/sizeof(uint8_t); cpin++) {
            ch1pin = ch1pins[cpin];

            for (ch1noteptr=0; ch1noteptr < ch1song->notecount; ch1noteptr++) {
                ch1nextnote = pgm_read_byte(&ch1song->notes[ch1noteptr++]);
                ch1noteready = 1;

                while (ch1noteready == 1) {
                    WATCHDOG_OK;
                    set_sleep_mode(SLEEP_MODE_IDLE);
                    sleep_mode();
                }
            }
        }
    }
    
    cli();
    ch1restcount = 1;   
    sei();

    while (ch1playing) {
        WATCHDOG_OK;
        set_sleep_mode(SLEEP_MODE_IDLE);
        sleep_mode();
    }
    
}

uint8_t randwaitval (void) {
#if PHOTOSHOOT
    return mood + randbits(3) + randbits(3)  + randbits(3); 
#else
    return mood + randbits(4) + randbits(4) + randbits(3) + 
           randbits(2) + randbits(2) - randbits(3); 
#endif
}

uint8_t randscaleval (void) {

#if BRIGHTER
    return (randbits(6) + randbits(5) + randbits(5) + randbits(4) + 
            randbits(3) + 54);
#elif PHOTOSHOOT
    // for photoshoot
    return (randbits(6) + randbits(5) + randbits(4) + randbits(3) + 
            randbits(2) + 34);
#else 
    /* Returns bell-like distributed random value between %23 - 69% centered
        around 46% */
    return (randbits(6) + randbits(5) + randbits(4) + randbits(3) + 
            randbits(2) + 58);
#endif
}

/* pickapin -- choose a pin to play a song on.       */
/*    Basically either C, D, or E.                   */
/*    We take as an argument the pin of the other    */
/*    channel so that we can ensure we don't return  */
/*    a channel currently in use.                    */
uint8_t pickapin (uint8_t inuse) {
    uint8_t pinmask;
    uint8_t myrand = randbits(8);

    /* faster way to do (randbits(8) % 3) */
    myrand = ((uint16_t)((myrand << 1) + myrand)) >> 8;
    
    // 0, 1, and 2 should be only possible values.
    // if we pick a pinmask equal to exclude parameter, fall through
    // to next option (or wrap around to start via default)
    switch ( myrand ) {
        case ( 0 ) : pinmask = _BV(PIN_C); if (pinmask != inuse) break;
        case ( 1 ) : pinmask = _BV(PIN_D); if (pinmask != inuse) break;
        case ( 2 ) : pinmask = _BV(PIN_E); if (pinmask != inuse) break;
        default    : pinmask = _BV(PIN_C); break;
    }
    return pinmask;
}

/* pickmaster -- Pick which pin is going to be master. */
/*    Basically either A or B.  I tend to prefer       */
/*    that we switch masterpins frequently, so I've    */
/*    stacked the deck so that there's simply a        */
/*    1 in 8 chance we keep the same masterpin.        */
uint8_t pickmaster (void) {
    static uint8_t ticker;
    if (randbits(3) == 0) {
        // 1 in 8 chance we don't switch masterpins.  
		// in this case, return the value we gave last time.
        return ( (ticker & 1) ? _BV(PIN_A) : _BV(PIN_B) );
    } else {
        // switch masterpins
        return ( (++ticker & 1) ? _BV(PIN_A) : _BV(PIN_B) );
    }
}


/* randbits -- implementation of linear feedback shift register. */
uint8_t randbits(uint8_t bits)
{
    static uint16_t lfsr = RANDOM_SEED;

    if (lfsr == 0)
        FREAKOUT;

    if ((lfsr & 0x8000) == 0)
        lfsr = lfsr << 1;
    else {
        lfsr = lfsr << 1;
        lfsr = lfsr ^ 0x1D87;
    }

    // Mask out the bits we want.
    return ((lfsr & 0xFF) & pgm_read_byte(&bitmasks[bits]));
}

const Song *pickasong(void)
{
    // when the number of songs to choose from is a power of two, the
    // optimizer makes this quite fast.  Otherwise it can take hundreds of
    // cycles to
    // do the multiplication.
    return songs[(randbits(8) * (sizeof (songs) / sizeof (Song *))) >> 8];
}


#if (USE_PHOTOTRANSISTOR || READ_TEMPERATURE)

static uint8_t read_adc(uint8_t muxreg)
{
	// enable and set up ADC
    PRR &= ~_BV(PRADC);      // turn on the ADC power
    ADMUX = muxreg | _BV(ADLAR);    // left-justified result
    // ensure that ADC is not busy converting
    loop_until_bit_is_clear(ADCSRA, ADSC);
    ADCSRA = _BV(ADEN) | _BV(ADSC);      // enable ADC and start conversion
    // wait until done converting
	loop_until_bit_is_clear(ADCSRA, ADSC);

	// get ADC result (high 8 bits)
    uint8_t retval = ADCH;

	// disable ADC
    ADCSRA &= ~_BV(ADEN);    // disable ADC
    PRR |= _BV(PRADC);        // turn off the ADC power

    return retval;
}

#endif


#if USE_PHOTOTRANSISTOR

// called from WDT ISR
// sets is_dark if dark enough
// phototransistor between RESET line and GND
static void still_dark(void)
{
    static uint8_t debounce = DARK_DEBOUNCE;

	// read state
    uint8_t photocell = read_adc(PHOTOTRANSISTOR_ADC);  // Vcc as VRef, no conn to AREF.

    if ((photocell > DARK_MINIMUM) || force_dark)
    {
        if (!debounce--)
        {
            debounce = DARK_DEBOUNCE;
            is_dark = 1;
        }
    }
    else
    {
        debounce = DARK_DEBOUNCE;
        is_dark = 0;
    }
    return;
}

static void wait_for_dark(void)
{
    waiting_for_dark = 1;
    TIMSK = 0;  // disable timer 0 compare interrupt
    while (!is_dark) {
        WATCHDOG_OK;
        set_sleep_mode(SLEEP_MODE_PWR_DOWN);
        sleep_mode();
        // wake from WDT interrupt.
    }
    TIMSK = _BV(OCIE0A);    // enable timer 0 interrupt
    waiting_for_dark = 0;
}

#else   /* !USE_PHOTOTRANSISTOR */
static void still_dark(void)        { }
static void wait_for_dark(void)     { }
#endif  /* USE_PHOTOTRANSISTOR */

#if READ_TEMPERATURE

static uint8_t read_temperature(void)
{
    return read_adc(_BV(REFS1) | 0x0F); // 1.1V ref, no external bypass, select temp channel
}

#endif


// do the old main loop, but only as long as it's still dark.
void lighted_fireflies(void)
{
    static const Song *ch1song;
    static const Song *ch2song;
    static uint16_t ch1noteptr;
    static uint16_t ch2noteptr;
    static uint8_t temp;

    // main loop.
    while (is_dark) {
        /* If we have a channel playing, take care of its */
        /* housekeeping first.  */
        if (ch1playing || ch2playing) {
            /* If channel is playing but doesn't have its next note */
            /* loaded, load it.  */

            if (ch1playing && !ch1noteready) {
                if (ch1noteptr != ch1song->notecount) {

                    temp = pgm_read_byte(&ch1song->notes[ch1noteptr++]);
                    temp = ((uint16_t) (ch1scale * temp)) >> 8;

                    cli();
                    ch1nextnote = temp;
                    ch1noteready = 1;
                    sei();

                    continue;   // Loop over from start.
                }               // Else end of song.
            }

            if (ch2playing && !ch2noteready) {
                if (ch2noteptr != ch2song->notecount) {
                    temp = pgm_read_byte(&ch2song->notes[ch2noteptr++]);
                    temp = ((uint16_t) (ch2scale * temp)) >> 8;

                    cli();
                    ch2nextnote = temp;
                    ch2noteready = 1;
                    sei();

                    continue;   // Loop over from start.
                }               // Else end of song.
            }

            /* If a channel is playing a song and its restcount is 0, */
            /* we need to pick a new random restcount for it.  */

            if (ch1playing && ch1restcount == 0) {
                temp = randwaitval();
                cli();
                ch1restcount = temp;
                sei();
                continue;       // Loop over from start.
            }

            if (ch2playing && ch2restcount == 0) {
                temp = randwaitval();
                cli();
                ch2restcount = temp;
                sei();
                continue;       // Loop over from start.
            }
        }

        /* If channel is done resting and waiting for a song to */
        /* be queued up, select a song for it.  */
        if (!ch1playing && !ch1resting && !ch1noteready) {
            ch1song = pickasong();
            ch1noteptr = 0;

            ch1pin = pickapin(ch2pin);

            // Now pick a random scaling value so the brightness
            // varies over time.
            ch1scale = randscaleval();

            temp = pgm_read_byte(&ch1song->notes[ch1noteptr++]);
            temp = ((uint16_t) (ch1scale * temp)) >> 8;

            // If the other channel isn't playing lets reselect the
            // master pin.
            if (ch2resting)
                masterpinmask = pickmaster();

            cli();
            ch1nextnote = temp;
            ch1noteready = 1;
            sei();

            continue;           // Loop over from start.
        }

        if (!ch2playing && !ch2resting && !ch2noteready) {
            ch2song = pickasong();
            ch2noteptr = 0;

            ch2pin = pickapin(ch1pin);

            // Now pick a random scaling value so the brightness
            // varies over time.
            ch2scale = randscaleval();

            temp = pgm_read_byte(&ch2song->notes[ch2noteptr++]);
            temp = ((uint16_t) (ch2scale * temp)) >> 8;

            // If the other channel isn't playing lets reselect the
            // master pin.
            if (ch1resting)
                masterpinmask = pickmaster();

            cli();
            ch2nextnote = temp;
            ch2noteready = 1;
            sei();

            continue;           // Loop over from start.
        }

        if (need_mood) {
#if PHOTOSHOOT
            mood = 1;
#else
            mood = randbits(5) + 8;
#endif
            mood_count = 60 * 5 * 2;
            need_mood = 0;

            continue;           // Loop over from start.
        }

        WATCHDOG_OK;

        /* If both channels are resting then go to deep sleep.  */
        /* We shouldn't get to here unless all our housekeeping */
        /* work is done.  */
        if (ch1resting && ch2resting) {
            set_sleep_mode(SLEEP_MODE_PWR_DOWN);
            sleep_mode();
        }
        else {
            set_sleep_mode(SLEEP_MODE_IDLE);
            sleep_mode();
        }
    }
}


int main(void)
{
    ioinit();

    // Zero out register variables.
    ch1bright = ch2bright = ch1err = ch2err = ch1pin = ch2pin = portval = 0;
    ch1restcount = randwaitval();
    ch2restcount = randwaitval();

    // Zero out flags.
    FLAGS0 = 0;

#if USE_PHOTOTRANSISTOR
    waiting_for_dark = 0;
    is_dark = 1;
    force_dark = 1;
#endif

    sei();
    showbootup();

#if USE_PHOTOTRANSISTOR
    force_dark = 0;
#endif

    for (;;) {
        wait_for_dark();

        ch1bright = ch2bright = ch1err = ch2err = ch1pin = ch2pin = portval = 0;
        FLAGS0 = 0;
        ch1resting = 1;
        ch2resting = 1;

        lighted_fireflies();
    }
}
