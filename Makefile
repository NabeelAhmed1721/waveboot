MCU = atmega328p
F_CPU = 16000000UL
BAUD = 19200
TARGET = waveboot
COM ?= COM3

# sources
SRC_DIR = src
SRC = $(SRC_DIR)/$(TARGET).cpp \
          $(SRC_DIR)/timer.cpp \
		  $(SRC_DIR)/program.cpp \
		  $(SRC_DIR)/radio.cpp
        #   $(SRC_DIR)/rh-ask/*.cpp 

# app file for user code
APP ?= app_w_reset.hex

# fuses
LFUSE = 0xFF
# HFUSE = 0xDC
HFUSE = 0xD8
EFUSE = 0xFD

# memory layout
# flash size: (0x000 - 0x7FFF) 32KB (with 16-bit addressing)
# BOOTLOADER_ADDR = 0x7E00 # start of bootloader section
BOOTLOADER_ADDR = 0x7000

# compiler and linker settings	
CC = avr-g++
OBJCOPY = avr-objcopy
CFLAGS = -Wall -Os -mmcu=$(MCU) -DF_CPU=$(F_CPU) -std=c++11
CFLAGS += -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections
CFLAGS += -flto -fwhole-program -mcall-prologues -fno-inline-small-functions
LDFLAGS = -Wl,--section-start=.text=$(BOOTLOADER_ADDR) -Wl,--gc-sections
LDFLAGS += -Wl,--relax -flto -Wl,-s

# output files
ELF = $(TARGET).elf
HEX = $(TARGET).hex

COMBINED_HEX = combined.hex
HEX_MERGE = srec_cat $(APP) -intel $(HEX) -intel -o $(COMBINED_HEX) -intel

# default build
all: build

combine: build
	$(HEX_MERGE)

flash_combined: combine
	avrdude -p $(MCU) -c stk500v1 -P $(COM) -b $(BAUD) -U flash:w:$(COMBINED_HEX):i

build:
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(ELF) $(SRC)
	$(OBJCOPY) -O ihex -R .eeprom $(ELF) $(HEX)

flash: build
	avrdude -p $(MCU) -c stk500v1 -P $(COM) -b $(BAUD) -U flash:w:$(HEX):i

flash_app:
	avrdude -p $(MCU) -c stk500v1 -P $(COM) -b $(BAUD) -U flash:w:$(APP):i

flash_fuses:
	avrdude -p $(MCU) -c stk500v1 -P $(COM) -b $(BAUD) \
		-U lfuse:w:$(LFUSE):m -U hfuse:w:$(HFUSE):m -U efuse:w:$(EFUSE):m
	
dump_flash:
	avrdude -p $(MCU) -c stk500v1 -P $(COM) -b $(BAUD) \
		-U flash:r:flash_dump.hex:i

clean:
	rm -f $(ELF) $(HEX) $(COMBINED_HEX) *.o *.d *.lss