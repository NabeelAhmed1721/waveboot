/**
 * `radio` is a lightweight rewrite of the RadioHead library.
 * It is designed to be used with Waveboot and optimized to use 
 * as little space/memory as possible. Thus, it only supports
 * ASK radios (for now) and atmega328p (also for now).
 * 
 * Credit: Copyright (C) 2014 Mike McCauley
 * Rewritten by Nabeel Ahmed
 */

#pragma once

#include <stdint.h>

#define RADIO_MAX_PAYLOAD_LEN 67
#define RADIO_HEADER_LEN 4
#define RADIO_MAX_MESSAGE_LEN (RADIO_MAX_PAYLOAD_LEN - RADIO_HEADER_LEN - 3)
#define RADIO_START_SYMBOL 0xB38
#define PREAMBLE_LEN 8
#define MAX_PAYLOAD_LEN 67
#define RADIO_SPEED 2000
#define DEFAULT_ADDRESS 0xFF // wild card address

// PLL values
// TODO: check if even needed
#define RADIO_RX_SAMPLES_PER_BIT 8
#define RADIO_RX_RAMP_LEN 160
#define RADIO_RAMP_TRANSITION RADIO_RX_RAMP_LEN / 2
#define RADIO_RAMP_ADJUST 9
#define RADIO_RAMP_INC (RADIO_RX_RAMP_LEN / RADIO_RX_SAMPLES_PER_BIT)
#define RADIO_RAMP_INC_RETARD (RADIO_RAMP_INC - RADIO_RAMP_ADJUST)
#define RADIO_RAMP_INC_ADVANCE (RADIO_RAMP_INC + RADIO_RAMP_ADJUST)

enum RadioMode {
    Idle,
    Tx,
    Rx
};

class Radio {
    private:
        volatile RadioMode mode; // volatile because it can be changed in ISR
        uint8_t address;
        void setAddress(uint8_t address);
        // tx
        uint8_t tx_header_to;
        uint8_t tx_header_from;
        uint8_t tx_header_id;
        uint8_t tx_header_flags;
        uint8_t tx_index;
        uint8_t tx_bit;
        uint8_t tx_sample;
        uint8_t tx_buffer_len;
        uint8_t tx_buffer[(MAX_PAYLOAD_LEN * 2) + PREAMBLE_LEN];
        void transmit_timer();
        // rx
        volatile uint8_t rx_header_to;
        volatile uint8_t rx_header_from;
        volatile uint8_t rx_header_id;
        volatile uint8_t rx_header_flags;
        volatile uint8_t rx_last_sample;
        volatile uint8_t rx_integrator;
        volatile uint8_t rx_active;
        volatile uint16_t rx_bits;
        volatile uint8_t rx_bit_count;
        volatile uint8_t rx_pll_ramp;
        volatile bool rx_buffer_full;
        volatile bool rx_buffer_valid;
        volatile uint8_t rx_count;
        void validate_rx_buffer();
        uint8_t rx_buffer_len;
        uint8_t rx_buffer[MAX_PAYLOAD_LEN];
        void receive_timer();

        // TODO: precompute these values
        // including timerCalc because might be useful in the future
        // if we want to change the speed
        static uint8_t timerCalc(uint16_t speed, uint16_t max_ticks, uint16_t *nticks);
        static uint16_t updateCRC(uint16_t crc, uint8_t data);
        static uint8_t convert_to_4bit_symbols(uint8_t symbol);

    public:
        Radio();
        bool init();
        bool available();
        bool recv(uint8_t* buf, uint8_t* len);
        bool send(const uint8_t* data, uint8_t len);
        bool wait_packet_send();
        void handle_timer_interrupt();

        // modes
        void set_mode_idle();
        void set_mode_rx();
        void set_mode_tx();
};