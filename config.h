#define RANDOM_SEED 0x74ce
#define SONG2   1

// Define USE_PHOTOTRANSISTOR as non-zero to use a phototransistor for dark
// detection.  The photocell is connected between the reset/ADC0 pin and
// ground. The pull-up resistor on the /RESET pin is only enabled if the
// RSTDISBL fuse is not set, so defining USE_PHOTOTRANSISTOR as 1 will
// represent a continuous load of up to due to the (approximately 60K) pull-up
// resistor on that pin.
// But the load is still minimal: 25uA or so at 1.5V (at which the part will
// be held in reset anyway). That would be over a year of operation on a
// 240mAH CR2032 cell.

#ifndef USE_PHOTOTRANSISTOR
#define USE_PHOTOTRANSISTOR 0
#endif

// TODO
// non-zero to use the internal temperature for blink speed
#define READ_TEMPERATURE 0

// duration of dark before it's detected (calls to still_dark())
#define	DARK_DEBOUNCE	10

// minimum level at ADC before darkness is detected
#define DARK_MINIMUM    256*8/10

#define PHOTOSHOOT 0

#define BRIGHTER 0

