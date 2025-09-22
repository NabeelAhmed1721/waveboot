/**
  Waveboot focuses on the bootloader and transfer of data.
  As long as you can reset your device on-command, and transmit a "BOOT"
  request during the boot countdown, you'll be able to override the firmware

  This example allows the programmer to also send magic bytes specific to
  a node to allow it to reset on command and be updated.
**/

#include <RH_ASK.h> // <- install library for rx/tx over radio 
#include <SPI.h> // *not actually used*, just needed for RadioHead to compile
#include <string.h>
#include <avr/wdt.h>

#define MAGIC_BYTES "RESET"

RH_ASK rf_driver;

void setup() {
  rf_driver.init();
  pinMode(LED_BUILTIN, OUTPUT);
}

void checkForReset() {
  uint8_t buf[16];
  uint8_t len = sizeof(buf) - 1;
  // this is non-blocking
  // RadioHead fills the rx buffer in a timer1 interrupt
  if (rf_driver.recv(buf, &len)) {
    buf[len] = '\0';
    if (strcmp((char*)buf, MAGIC_BYTES) == 0) {
      // force reset via watchdog
      cli();
      wdt_enable(WDTO_15MS);
      while(1);
    }
  }
}

void loop() {
  checkForReset();
  // toggle LED (... rest of the code)
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  delay(500);
}
