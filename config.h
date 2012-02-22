#define RANDOM_SEED 0x74ce
#define SONG2   1

// non-zero to use a phototransistor for dark detection
#define USE_PHOTOTRANSISTOR 1

// the photocell is connected to the reset/ADC0 pin
#define PHOTOTRANSISTOR_ON_RESET_PIN  1

// TODO
// non-zero to use the internal temperature for blink speed
#define READ_TEMPERATURE 0

// duration of dark before it's detected (calls to still_dark())
#define	DARK_DEBOUNCE	10

// minimum level at ADC before darkness is detected
#define DARK_MINIMUM    256*9/10

#define PHOTOSHOOT 0

#define BRIGHTER 0

