#include <Arduino.h>
#include <SPI.h>
#include "radio.h" // this is the RadioHead library rewritten (just use RadioHead should also work)

#define FIRMWARE_WIDTH 21

// RADIO BRIDGE PROGRAMMER
// forwards commands from cli tool to remote node via radio

/**
 * | - on device logs
 * > - outbound messages
 * < - inbound messages
 */

Radio driver;

void setup() {
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
  
  Serial.println("|System starting up...");
  
  if (!driver.init()) {
    Serial.println("|Radio init failed!");
    // implement a way to reset programmer
    while(1); // halt on radio failure
  } else {
    Serial.println("|Radio initialized successfully");
  }
  
  Serial.println("|Bridge ready - waiting for commands from Python script");
}

void loop() {
  if (Serial.available()) {
    uint8_t buf[FIRMWARE_WIDTH];
    Serial.readBytes(buf, FIRMWARE_WIDTH);
    
    Serial.print(">Sending: ");
    // print buf as HEX string
    for (int i = 0; i < FIRMWARE_WIDTH; i++) {
      if (i < FIRMWARE_WIDTH - 1) Serial.print("0x");
      Serial.print(buf[i], HEX);
      if (i < FIRMWARE_WIDTH - 1) Serial.print(" ");
    }
    Serial.println();
    
    digitalWrite(LED_BUILTIN, HIGH);
    driver.send((uint8_t*)buf, FIRMWARE_WIDTH); 
    driver.wait_packet_send();
    digitalWrite(LED_BUILTIN, LOW);
    
    Serial.println("|Command sent, waiting for response...");
  }
  
  // 64 should be fine
  uint8_t buf[64];
  uint8_t buflen = sizeof(buf) - 1;
  
  if (driver.recv(buf, &buflen)) {
    buf[buflen] = '\0';
    
    // forward response back to cli
    Serial.print("<Received (");
    Serial.print(buflen);
    Serial.print(" bytes): ");
    Serial.println((char *)buf);
    
    // sorta messy, I don't like using strncmp so much
    if (strncmp((char*)buf, "RDY", 3) == 0) {
      Serial.println("|Bootloader is ready!");
    } else if (strncmp((char*)buf, "PRG", 3) == 0) {
      Serial.println("|Progress acknowledged");
    } else if (strncmp((char*)buf, "DNE", 3) == 0) {
      Serial.println("|Programming completed!");
    } else if (strncmp((char*)buf, "CHK", 3) == 0) {
      Serial.println("|Checksum error reported from remote node");
    } else if (strncmp((char*)buf, "ERR", 3) == 0) {
      Serial.println("|Error reported from remote node");
    }
  }
  
  delay(10); // Small delay to prevent overwhelming the serial interface
}