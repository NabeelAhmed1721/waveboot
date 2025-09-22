```
 _       __                 __                __ 
| |     / /___ __   _____  / /_  ____  ____  / /_
| | /| / / __ `/ | / / _ \/ __ \/ __ \/ __ \/ __/
| |/ |/ / /_/ /| |/ /  __/ /_/ / /_/ / /_/ / /_
|__/|__/\__,_/ |___/\___/_.___/\____/\____/\__/
```

# Waveboot

Waveboot is a bootloader for the ATmega328P microcontroller to allow for firmware updates over the air. It is designed to be used with amplitude-shift-keyed (ASK) radios. If there's enough interest, radio support can be expanded.

## Features

- ~3KB in size
- OTA firmware updates
- Supports the ATmega328P microcontroller
- Supports the RH_ASK radio
- Resilient to corrupted flash (will not boot into the application if the flash is corrupted)
- Python-based CLI tool to program the bootloader
- Depending on the radio, a device can be programmed from as far away as 100 meters

## Usage

You will need at least two ATmega328P microcontrollers. One will be the bootloader, and the other will relay the firmware updates from your computer to the device.

### Flashing the Bootloader

To flash the bootloader onto the device, the easiest way to do this is to use the [programmer](https://www.adafruit.com/product/462) tool. But if you have another spare arduino, you can use that to program the bootloader using [Arduino as ISP](https://docs.arduino.cc/built-in-examples/arduino-isp/ArduinoISP/).

Running make will compile the bootloader, and bundle it with the `app_w_reset.hex`, which is the example `main.cpp` file compiled. This will give you the full ability to remote reset and write to the device.

```bash
make flash_combined
```

> It is important to note that Waveboto does not reset the device for you. The programmer will send a `RESET` command to the device, but it is ultimately up to the user to interpret this command in the application and reset the device. The example `main.cpp` is an example of how to do this.

### Flashing the Programmer

The programmer is a simple Arduino project that can be flashed onto the device. There's several ways to do this, but the easiest way is to open the `programmer` folder in the Arduino IDE and upload the sketch.

### Wiring Radios

Once the bootloader is flashed, you will need to wire the radios to the microcontrollers. This can be configured in the `config.h` file, however, I've set them up so the reciever uses `PB6` and the transmitter uses `PB5`. I've set it to these pins because pins `PB4` and `PB3` are used by the Arduino as ISP. Again, you're free to change these pins in the `config.h` file. You'll also need to connect the transmitter and receiver to programmer, and this can be configured in the `config.h` in the `programmer/src/config.h` file (I have these set to `PB4` (transmitter) and `PB3` (receiver), sorry for the confusion).

<img width="640" height="320" alt="circuit (1) (1)" src="https://github.com/user-attachments/assets/c271f44c-5b65-4c98-84d3-1e011a4e7087" />

### CLI

To use the CLI, you'll need to install Python and the `pyserial` library. You can do this by running the following command:

```bash
pip install -r requirements.txt
```

Once you have the dependencies installed, you can run the CLI by running the following command:

```bash
python program.py
```

You'll be prompted to select the serial port to use. Once you've selected the port, you can communicate with the remote device by specifying a RESET code (by default `RESET`). Then, you can select a hex file of your choice.

The CLI will then attempt to reset the device using the specified RESET code, and then program the bootloader.

> RESET codes are customizable so users can specify a specific device if you have multiple devices. This way you won't have to worry about resetting the wrong device.

TODO:

- Add support for code-encoded signals (e.g. two RDY's may clash, but if a RDY has extra data it can be used to distinguish between devices. But then this would have larger issues with ASK frequency collisions)

- Add support for external flash backup
