#include <avr/io.h>

#define BOOT_TIMEOUT_MS 4000 // 4s - timeout for bootloader to receive BOOT
#define PROGRAMMING_TIMEOUT_MS 10000 // 10s - timeout for programming
// #define BOOT_TIMEOUT_MS 15000 // 15s
#define BOOTSIZE 4096 // 4KB (if BOOT fuses are changed, this must be changed)
#define BOOT_START ((uint32_t)FLASHEND + 1) - BOOTSIZE
#define F_CPU 16000000UL // 16MHz (if clock fuses are changed, this must be changed)

// onboard status LED
#define LED_PIN PB5
#define SET_LED DDRB |= (1 << LED_PIN)
#define LED_ON PORTB |= (1 << LED_PIN)
#define LED_OFF PORTB &= ~(1 << LED_PIN)

// radio
#define RADIO_DDR DDRD
#define RADIO_PORT PORTD
#define RADIO_PIN PIND
#define RADIO_RX_PIN PD6
#define RADIO_TX_PIN PD5