#include "program.h"
#include "config.h"
#include "timer.h"
#include "radio.h"
#include <avr/boot.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>

// the bootloader should never store code past FLASHEND - 3 bytes
#define RECOVERY_BYTES_ADDR (FLASHEND - 3)  // 4 bytes at the end of application flash
#define RECOVERY_BYTES 0xDEADBEEF

static bool write_page(uint32_t page_address, const uint8_t* data, uint16_t len) {
    // no safety, beforing running this function!!
    // safety should be checked before calling this function!
    // if (page_address >= BOOT_START) return false;

    // disable interrupts when doing SPM operations
    cli();

    // erase page
    boot_page_erase(page_address);
    boot_spm_busy_wait();

    // words are filled in word chunks (16 bits)
    // atmega328p is little-endian
    for (uint16_t i = 0; i < len; i += 2) {
        uint16_t word;
        if (i + 1 < len) {
            word = data[i] | (data[i + 1] << 8);
        } else {
            // last odd byte, high byte = 0xFF
            word = data[i] | (0xFF << 8);
        }
        boot_page_fill(page_address + i, word);
    }

    boot_page_write(page_address);
    boot_spm_busy_wait();

    // re-enable flash execution
    boot_rww_enable();
    sei();

    // idk if this is needed tbh
    // uses extra clock cycles but it guarantees flash is not booted 
    // into in a corrupted state
    // just reads back each byte from flash and compare
    // for (uint16_t i = 0; i < len; i++) {
    //     uint8_t flash_b = pgm_read_byte_near((uint16_t)(page_address + i));
    //     if (flash_b != data[i]) return false;
    // }
    return true;
}

// when programming, we need to set the recovery bytes to 0xDEADBEEF
// that way, if we crash, or if firmware lines stop being received,
// we know the flash is corrupted and we shouldn't boot into it
// in this state, the device will continously wait for BOOT so 
// it can write a new firmware
static void set_recovery_state(bool is_programming) {
    uint32_t recovery_page_addr = RECOVERY_BYTES_ADDR & ~(SPM_PAGESIZE - 1);
    uint16_t offset = RECOVERY_BYTES_ADDR - recovery_page_addr;
    uint8_t page_buffer[SPM_PAGESIZE];

    // read current page into buffer
    for (uint16_t i = 0; i < SPM_PAGESIZE; i++) {
        page_buffer[i] = pgm_read_byte_near((uint16_t)(recovery_page_addr + i));
    }

    if (is_programming) {
        // Set recovery bytes in little-endian format
        page_buffer[offset + 0] = RECOVERY_BYTES & 0xFF;
        page_buffer[offset + 1] = (RECOVERY_BYTES >> 8) & 0xFF;
        page_buffer[offset + 2] = (RECOVERY_BYTES >> 16) & 0xFF;
        page_buffer[offset + 3] = (RECOVERY_BYTES >> 24) & 0xFF;
    } else {
        page_buffer[offset + 0] = 0xFF;
        page_buffer[offset + 1] = 0xFF;
        page_buffer[offset + 2] = 0xFF;
        page_buffer[offset + 3] = 0xFF;
    }

    write_page(recovery_page_addr, page_buffer, SPM_PAGESIZE);
}

bool check_recovery_bytes(void) {
    uint32_t recovery_bytes = 0;
    
    // read 4 bytes
    recovery_bytes |= (uint32_t)pgm_read_byte_near((uint16_t)(RECOVERY_BYTES_ADDR + 0));
    recovery_bytes |= (uint32_t)pgm_read_byte_near((uint16_t)(RECOVERY_BYTES_ADDR + 1)) << 8;
    recovery_bytes |= (uint32_t)pgm_read_byte_near((uint16_t)(RECOVERY_BYTES_ADDR + 2)) << 16;
    recovery_bytes |= (uint32_t)pgm_read_byte_near((uint16_t)(RECOVERY_BYTES_ADDR + 3)) << 24;
    
    return recovery_bytes == RECOVERY_BYTES;
}

bool program_flash(Radio &driver) {
    uint8_t buffer[BUFFER_SIZE];
    uint8_t page_buffer[SPM_PAGESIZE]; 
    uint16_t current_page_addr = 0xFFFF;
    bool page_dirty = false;
    bool is_flash_modified = false;
    uint32_t last_update_time = millis();

    /** 
     * TODO: 
     * extra feature
     * rst, boot, updated fail (modified flash) + EEPROM backup -> write EEPROM backup to flash + jump to application */ 

    LED_ON; // LED ON initially

    while (true) {
        uint8_t len = BUFFER_SIZE;
        bool update_received = driver.recv(buffer, &len);
        // check if update is still being received
        // if not, jump to application
        if (!update_received) {
            if (millis() - last_update_time > PROGRAMMING_TIMEOUT_MS) {
                if (is_flash_modified) {
                    return false;
                } else {
                    // no flash modification detected - jump to application
                    // but still remove recovery bytes
                    set_recovery_state(false);
                    return false;
                }
            }
            continue;
        }

        last_update_time = millis();

        LED_OFF;
        delay(50);

        // format of buffer ihex
        // <record_type><address high><address low><data_len><data><checksum>

        uint8_t data_len   = buffer[0];
        uint16_t address   = (buffer[1] << 8) | buffer[2];
        uint8_t record_type = buffer[3];
        uint8_t* data       = &buffer[4];
        uint8_t checksum   = buffer[4 + data_len];

        // checksum is the sum of all bytes
        // then 
        uint8_t calc = data_len + buffer[1] + buffer[2] + record_type;
        for (int i = 0; i < data_len; i++) calc += data[i];
        calc = (~calc + 1);

        if (calc == checksum) {
            switch (record_type) {
                // data
                case 0x00: {
                    // add recovery bytes on first write
                    if (!is_flash_modified) {
                        set_recovery_state(true);
                        is_flash_modified = true;
                    }

                    // nice trick to get the page address
                    // each page is 128 bytes, so we can mask out the lower 7 bits
                    uint16_t page_addr = address & ~(SPM_PAGESIZE - 1);

                    // setup new page
                    if (current_page_addr != page_addr) {
                        // if a previous page was dirty, write it to flash
                        if (page_dirty && current_page_addr != 0xFFFF) {
                            // atomic operation
                            write_page(current_page_addr, page_buffer, SPM_PAGESIZE);
                        }

                        current_page_addr = page_addr;
                        page_dirty = false;
                        // cool trick to save clock cycles
                        // tldr; comparing against 0 is faster than some other value
                        for (int i = SPM_PAGESIZE; i != 0; --i) page_buffer[i - 1] = 0xFF;
                    }

                    uint16_t offset = address - current_page_addr;
                    for (int i = 0; i < data_len && (offset + i) < SPM_PAGESIZE; i++) {
                        page_buffer[offset + i] = data[i];
                        page_dirty = true;
                    }

                    // ack
                    driver.send((const uint8_t*)"PRG", 3);
                    break;
                }

                // eof
                case 0x01: {
                    if (page_dirty && current_page_addr != 0xFFFF) {
                        write_page(current_page_addr, page_buffer, SPM_PAGESIZE);
                    }

                    // success write
                    set_recovery_state(false);

                    driver.send((const uint8_t*)"DNE", 3);
                    driver.wait_packet_send();
                    LED_ON;
                    return true;
                }
                // there's more data types,
                // but I'll implement them as I need them
                // ignore for now
                default:
                    driver.send((const uint8_t*)"PRG", 3);
                    break;
            }
        } else {
            driver.send((const uint8_t*)"CHK", 3);
        }

        driver.wait_packet_send();

        // blink feedback
        LED_ON;
        delay(50);
        LED_OFF;
        delay(50);
        LED_ON;
    }
    return false;
}