#include "timer.h"
#include <avr/io.h>
#include <avr/interrupt.h>

static volatile uint32_t _millis = 0;

ISR(TIMER0_COMPA_vect) {
    ++_millis;
}

void timer_init(void) {
    // setup 1ms on timer0
    TCCR0A = (1 << WGM01); // enable CTC mode

    // clock runs at 16MHz
    // using a prescalar of 64 (16MHz/64 = 250kHz)
    
    TCCR0B |= (1 << CS01) | (1 << CS00);
    
    // 1 / 250kHz = 4 * 10^-6 s = 4 us
    // 0.001ms / 4 us = 250 ticks
    // since, it is zero-indexed,
    // we'll need to set the clear/compare to 249
    OCR0A = 249;
    TIMSK0 |= (1 << OCIE0A);

    sei(); // enable interrupts
}

uint32_t millis(void) {
    uint32_t ms;
    // atomically return ms value
    // interrupt itself may change it mid-read
    cli();
    ms = _millis;
    sei();
    return ms;
}

void delay(uint32_t ms) {
    uint32_t start = millis();
    while (millis() - start < ms) {
        // busy wait
    }
}