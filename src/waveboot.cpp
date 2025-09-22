/**
 * Author: Nabeel Ahmed
 * Waveboot is a bootloader for AVR microcontrollers.
 * Supported devices:
 * - ATmega328P
 */
#include <avr/boot.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <avr/sfr_defs.h>
#include <avr/io.h>

#include "config.h"
#include "timer.h"
#include "radio.h"
#include "program.h"

typedef void (*app_entry_t)(void) __attribute__((noreturn));

static inline void map_vectors_to_bootloader(void) {
    // move vectors to bootloader section
    MCUCR = (1 << IVCE);
    MCUCR = (1 << IVSEL);
}

static inline void map_vectors_to_application(void) {
    // move vectors back to application section
    MCUCR = (1 << IVCE);
    MCUCR = 0x00;
}

static void jump_to_application(void) {
    // disable all interrupts
    cli();

    // Disable ALL timer interrupts
    TIMSK0 = 0;
    TIMSK1 = 0;

    TCCR0A = 0;
    TCCR0B = 0;
    OCR0A = 0;
    
    // RadioHead timers
    TCCR1A = 0;
    TCCR1B = 0;

    // Reset ALL I/O ports to power-on defaults
    DDRB = 0;
    PORTB = 0;

    // switch back to application vectors (do this last)
    map_vectors_to_application();
    
    // Reset stack pointer to top of RAM 0x08FF
    // ATmega328P has 2KB SRAM from 0x0100 to 0x08FF
    asm volatile (
        "ldi r30, 0xFF \n\t"    // load SPL with 0xFF 
        "out %0, r30 \n\t"      // set SPL
        "ldi r30, 0x08 \n\t"    // load SPH with 0x08     
        "out %1, r30 \n\t"      // set SPH
        "jmp 0x0000 \n\t"       // jump to application        
        :
        :   "I" (_SFR_IO_ADDR(SPL)), 
            "I" (_SFR_IO_ADDR(SPH))
        : "r30"
    );
    
    // Should never reach here
    while(1);
}

static bool listen_for_boot_signal(Radio &driver, uint32_t timeout) {
    uint32_t start_listen_time = millis();

    while ((millis()) < start_listen_time + timeout) {
        uint8_t buf[4];
        uint8_t buf_len = sizeof(buf);

        if (driver.recv(buf, &buf_len)) {
            if (buf_len >= 4 &&
                buf[0] == 'B' &&
                buf[1] == 'O' &&
                buf[2] == 'O' &&
                buf[3] == 'T') {
                return true;
            }
        }
    }

    return false;
}

void bootloader_main(void) {
    // switch to bootloader vectors
    map_vectors_to_bootloader();
    
    // disable watchdog sequence
    cli();
    wdt_reset();
    MCUSR &= ~(1 << WDRF);
    WDTCSR |= (1 << WDCE) | (1 << WDE);
    WDTCSR = 0x00;
    
    // Initialize timer system (this won't enable interrupts now)
    timer_init();
    sei();

    // onboard-LED as output
    SET_LED;
    
    // [x] 1. initialize the radio
    Radio driver;
    // RH_ASK driver(2000, 6, 5, 4, false);

    if (!driver.init()) {
        // Radio init failed - jump to app
        if (!check_recovery_bytes()) {
            // recovery bytes don't exist - good to go
            jump_to_application();
        } else {
            // recovery bytes exist - cooked
            // wait for user intervention
            while (true) {
                // LED_ON;
                // delay(100);
                // LED_OFF;
                // delay(100);
            }
        }
        return;
    }

    while (true) {
        bool is_corrupted = check_recovery_bytes();
        bool magic_recieved = false;

        if (is_corrupted) {
            // flash is corrupted - infinite wait for BOOT signal
            while (true) {
                // check forever for 10s every 1s
                if (listen_for_boot_signal(driver, 10000)) {
                    magic_recieved = true;
                    break;
                }
                delay(1000);
            }
        } else {
            // normal boot sequence
            LED_OFF;
            magic_recieved = listen_for_boot_signal(driver, BOOT_TIMEOUT_MS);

            if (!magic_recieved) {
                LED_OFF;
                jump_to_application();
                return;
            }
        }

        if (magic_recieved) {
            // blink lights to acknowledge BOOT received
            for (int i = 0; i < 5; i++) {
                LED_ON;
                delay(50);
                LED_OFF;
                delay(50);
            }

            // return "ready" acknowledgment
            const char* ack = "RDY";
            driver.send((const uint8_t*)ack, 3);
            driver.wait_packet_send();

            // enter programming mode
            bool success = program_flash(driver);
            
            LED_OFF;

            if (success) {
                // Programming successful - jump to application
                jump_to_application();
                return; // Should never reach here
            }
            
            // Programming failed - loop will check corruption state again
            // If corrupted, it will enter infinite wait mode
            // If not corrupted, it will timeout and jump to app
        }
    }
}

int main(void) {
    // Always enter bootloader first, then decide what to do
    bootloader_main();
    return 0;
}