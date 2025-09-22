#include "radio.h"
#include "config.h"
#include <avr/pgmspace.h>
#include <avr/interrupt.h>

// 0x38 and 0x2C are the start symbol ebfore 6-bit conversion
static const uint8_t PROGMEM preamble[PREAMBLE_LEN] = {
    0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x38, 0x2C
};

static const uint8_t PROGMEM symbols[16] = {
    0x0d, 0x0e, 0x13, 0x15, 0x16, 0x19, 0x1a, 0x1c,
    0x23, 0x25, 0x26, 0x29, 0x2a, 0x2c, 0x32, 0x34
};
#define SYMBOL(i) (pgm_read_byte(&symbols[(i)]))

#define NUM_PRESCALERS 7
static const uint16_t PROGMEM prescalers[NUM_PRESCALERS] = {
    0, 1, 8, 64, 256, 1024, 3333
}; 

// singleton used for ISR
// looking for a better way to do this
static Radio* radioRef;

Radio::Radio():
    mode(RadioMode::Idle),
    address(DEFAULT_ADDRESS),
    tx_header_to(DEFAULT_ADDRESS),
    tx_header_from(DEFAULT_ADDRESS),
    tx_header_id(0),
    tx_header_flags(0)
{
    // attach preamble to tx buffer
    for (int i = PREAMBLE_LEN; i != 0; --i) {
        tx_buffer[i - 1] = pgm_read_byte(&preamble[i - 1]);
    }
}

bool Radio::init() {
    // assign singleton
    radioRef = this;

    // set tx as output
    RADIO_DDR |= (1 << RADIO_TX_PIN);
    // set rx as input
    RADIO_DDR &= ~(1 << RADIO_RX_PIN);

    // set mode to idle
    this->set_mode_idle();

    // setup clock (timer1)
    uint16_t ticks;
    uint8_t prescalar;

    prescalar = this->timerCalc(RADIO_SPEED, (uint16_t) -1, &ticks);
    if (!prescalar) return false;

    TCCR1A = 0;
    TCCR1B = (1 << WGM12); // CTC
    TCCR1B |= prescalar;
    OCR1A = ticks;
    TIMSK1 |= (1 << OCIE1A);

    return true;
}

bool Radio::available() {
    if (this->mode == RadioMode::Tx) return false;
    this->set_mode_rx();
    if (this->rx_buffer_full) {
        this->validate_rx_buffer();
        this->rx_buffer_full = false;
    }
    return this->rx_buffer_valid;
}

bool Radio::recv(uint8_t* buffer, uint8_t* len) {
    if (!this->available()) return false;

    if (buffer && len) {
        uint8_t message_len = this->rx_buffer_len - RADIO_HEADER_LEN - 3;
        if (*len > message_len) *len = message_len;

        for (int i = *len; i != 0; --i) {
            buffer[i - 1] = this->rx_buffer[RADIO_HEADER_LEN + 1 + (i - 1)];
        }
    }

    this->rx_buffer_valid = false;
    return true;
}

bool Radio::send(const uint8_t* data, uint8_t len) {
    if (len > RADIO_MAX_MESSAGE_LEN) return false;
    // wait for tx to be ready
    this->wait_packet_send();
    
    uint8_t i;
    uint16_t index = 0;
    uint16_t crc = 0xFFFF;
    uint8_t* message = this->tx_buffer + PREAMBLE_LEN;
    uint8_t count = len + 3 + RADIO_HEADER_LEN; // data + fcs + headers

    // encode message length
    crc = this->updateCRC(crc, count);
    message[index++] = SYMBOL(count >> 4);
    message[index++] = SYMBOL(count & 0x0F);
    
    // encode headers
    crc = this->updateCRC(crc, this->tx_header_to);
    message[index++] = SYMBOL(this->tx_header_to >> 4);
    message[index++] = SYMBOL(this->tx_header_to & 0x0F);
    
    crc = this->updateCRC(crc, this->tx_header_from);
    message[index++] = SYMBOL(this->tx_header_from >> 4);
    message[index++] = SYMBOL(this->tx_header_from & 0x0F);
    
    crc = this->updateCRC(crc, this->tx_header_id);
    message[index++] = SYMBOL(this->tx_header_id >> 4);
    message[index++] = SYMBOL(this->tx_header_id & 0x0F);
    
    crc = this->updateCRC(crc, this->tx_header_flags);
    message[index++] = SYMBOL(this->tx_header_flags >> 4);
    message[index++] = SYMBOL(this->tx_header_flags & 0x0F);

    // encode the message into 6 bit symbols
    for (i = 0; i < len; i++) {
        crc = this->updateCRC(crc, data[i]);
        message[index++] = SYMBOL(data[i] >> 4);
        message[index++] = SYMBOL(data[i] & 0x0F);
    }
    
    crc = ~crc;
    message[index++] = SYMBOL((crc >> 4)  & 0x0F);
    message[index++] = SYMBOL(crc & 0x0F);
    message[index++] = SYMBOL((crc >> 12) & 0x0F);
    message[index++] = SYMBOL((crc >> 8)  & 0x0F);
    // Total number of 6-bit symbols to send
    this->tx_buffer_len = index + PREAMBLE_LEN;

    this->set_mode_tx();

    return true;
}

bool Radio::wait_packet_send() {
    while (this->mode == RadioMode::Tx);
    return true;
}

void Radio::set_mode_idle() {
    if (this->mode == RadioMode::Idle) return;
    // disable tx hardware
    RADIO_PORT &= ~(1 << RADIO_TX_PIN);
    this->mode = RadioMode::Idle;
}

void Radio::set_mode_rx() {
    if (this->mode == RadioMode::Rx) return;
    // disable rx hardware
    RADIO_PORT &= ~(1 << RADIO_TX_PIN);
    this->mode = RadioMode::Rx;
}

void Radio::set_mode_tx() {
    if (this->mode == RadioMode::Tx) return;
    this->tx_index = 0;
    this->tx_bit = 0;
    this->tx_sample = 0;
    this->mode = RadioMode::Tx;
}

// ensure message is complete and uncorrupted
void Radio::validate_rx_buffer()
{
    uint16_t crc = 0xFFFF;
    // The CRC covers the byte count, headers and user data
    for (uint8_t i = 0; i < this->rx_buffer_len; i++) {
        crc = this->updateCRC(crc, this->rx_buffer[i]);
    }

    // CRC when buffer and expected CRC are CRC'd 
    if (crc != 0xF0B8) {
        // Reject and drop the message
        this->rx_buffer_valid = false;
        return;
    }

    // Extract the 4 headers that follow the message length
    this->rx_header_to = this->rx_buffer[1];
    this->rx_header_from = this->rx_buffer[2];
    this->rx_header_id = this->rx_buffer[3];
    this->rx_header_flags = this->rx_buffer[4];
    
    if (
        this->rx_header_to == this->address ||
	    this->rx_header_to == DEFAULT_ADDRESS
    ) {
        this->rx_buffer_valid = true;
    }
}

void Radio::receive_timer() {
    bool rx_sample = (RADIO_PIN & (1 << RADIO_RX_PIN)) != 0;
    if (rx_sample) this->rx_integrator++;

    if (rx_sample != this->rx_last_sample) {
        // transition- advance if ramp > 80, retard if < 80
        this->rx_pll_ramp += ((this->rx_pll_ramp < RADIO_RAMP_TRANSITION) 
                ? RADIO_RAMP_INC_RETARD 
                : RADIO_RAMP_INC_ADVANCE);
        this->rx_last_sample = rx_sample;
    } else {
        this->rx_pll_ramp += RADIO_RAMP_INC;
    }

    if (this->rx_pll_ramp < RADIO_RX_RAMP_LEN) return;

    this->rx_bits >>= 1;
    if (this->rx_integrator >= 5) this->rx_bits |= 0x800;
    this->rx_pll_ramp -= RADIO_RX_RAMP_LEN;
    this->rx_integrator = 0;

    if (this->rx_active) {
        if (++this->rx_bit_count >= 12) {
            uint8_t current_byte = (this->convert_to_4bit_symbols(this->rx_bits & 0x3F)) << 4 |
                this->convert_to_4bit_symbols(this->rx_bits >> 6);

            if (this->rx_buffer_len == 0) {
                this->rx_count = current_byte;
                if (this->rx_count < 7 || this->rx_count > RADIO_MAX_PAYLOAD_LEN) {
                    this->rx_active = false;
                    return;
                }
            }
            this->rx_buffer[this->rx_buffer_len++] = current_byte;

            if (this->rx_buffer_len >= this->rx_count) {
                this->rx_active = false;
                this->rx_buffer_full = true;
                this->set_mode_idle();
            }
            this->rx_bit_count = 0;
        }
    } else if (this->rx_bits == RADIO_START_SYMBOL) {
        this->rx_active = true;
        this->rx_bit_count = 0;
        this->rx_buffer_len = 0;
    }
}

void Radio::transmit_timer() {
    if (this->tx_sample++ == 0) {
        if (this->tx_index >= this->tx_buffer_len) {
            this->set_mode_idle();
        } else {
            if (this->tx_buffer[this->tx_index] & (1 << this->tx_bit++)) {
                RADIO_PORT |= (1 << RADIO_TX_PIN);
            } else {
                RADIO_PORT &= ~(1 << RADIO_TX_PIN);
            }

            if (this->tx_bit >= 6) {
                this->tx_bit = 0;
                this->tx_index++;
            }
        }
    }

    if (this->tx_sample > 7) {
        this->tx_sample = 0;
    }
}

void Radio::handle_timer_interrupt() {
    switch (this->mode) {
        case RadioMode::Rx:
            this->receive_timer();
            break;
        case RadioMode::Tx:
            this->transmit_timer();
            break;
        case RadioMode::Idle:
            break;
    }
}

ISR(TIMER1_COMPA_vect) {
    radioRef->handle_timer_interrupt();
}

void Radio::setAddress(uint8_t address) {
    this->address = address;
}

// TODO: precompute these values
// credit: Jim Remington
uint8_t Radio::timerCalc(uint16_t speed, uint16_t max_ticks, uint16_t *nticks) {
    // Clock divider (prescaler) values - 0/3333: error flag
    uint8_t prescaler;
    unsigned long ulticks;

    // Div-by-zero protection
    if (speed == 0) {
        *nticks = 0;
        return 0;
    }

    // test increasing prescaler (divisor), decreasing ulticks until no overflow
    // 1/Fraction of second needed to xmit one bit
    unsigned long inv_bit_time = ((unsigned long) speed) * 8;

    for (prescaler = 1; prescaler < NUM_PRESCALERS; prescaler++) {
        // Integer arithmetic courtesy Jim Remington
        // 1/Amount of time per CPU clock tick (in seconds)
        uint16_t prescalerValue;
        memcpy_P(&prescalerValue, &prescalers[prescaler], sizeof(uint16_t));

        unsigned long inv_clock_time = F_CPU / ((unsigned long)prescalerValue);
        // number of prescaled ticks needed to handle bit time @ speed
        ulticks = inv_clock_time / inv_bit_time;

        // Test if ulticks fits in nticks bitwidth (with 1-tick safety margin)
        if ((ulticks > 1) && (ulticks < max_ticks))
            break; // found prescaler

    }

    // Check for error
    if ((prescaler == 6) || (ulticks < 2) || (ulticks > max_ticks)) {
        *nticks = 0;
        return 0;
    }

    *nticks = ulticks;
    return prescaler;
}

uint16_t Radio::updateCRC(uint16_t crc, uint8_t data) {
    data ^= ((crc) & 0xFF); 
    data ^= data << 4;

    return (
        (((uint16_t)data << 8) | ((crc) >> 8)) ^
        (uint8_t)(data >> 4) ^
        ((uint16_t)data << 3)
    );
}

uint8_t Radio::convert_to_4bit_symbols(uint8_t symbol) {
    uint8_t i;
    uint8_t count;

    for (i = (symbol >> 2) & 8, count = 8; count--; i++) {
        if (symbol == SYMBOL(i)) return i;
    }

    return 0;
}