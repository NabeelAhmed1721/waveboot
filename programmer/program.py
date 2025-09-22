'''
Waveboot Programmer CLI tool

This tool is used to program the bootloader onto the device.
This will commmunicate over serial with a atmega328p that has the programmer firmware installed.
'''

import serial
import serial.tools.list_ports
import time
import glob

# can be increased or decreased depending on the radio
# 6 seems sorta overkill but doesn't hurt
# but increase if a lot of requests are being dropped
REQUEST_ATTEMPTS = 6

def find_serial_ports():
    return [port.device for port in serial.tools.list_ports.comports()]

def connect():
    ports = find_serial_ports()
    if not ports:
        print("No serial ports found!")
        return None
        
    print("Serial ports:")
    for i, port in enumerate(ports):
        print(f"{i+1}) {port}")
    
    try:
        choice = int(input("Select port: ")) - 1
        # I wonder if we can increase baud rate, but 9600 should be fine for now
        ser = serial.Serial(ports[choice], 9600, timeout=1)
        print(f"Connected to {ports[choice]}")
        return ser
    except:
        print("Connection failed")
        return None

def get_reset_code():
    reset_code = input("Enter reset code (press Enter for default 'RESET'): ").strip()
    if not reset_code:
        reset_code = "RESET"
    print(f"Using reset code: '{reset_code}'")
    return reset_code


# TODO: add support for specifying hex file folder
def find_hex_files():
    return glob.glob("*.hex")

def select_hex_file():
    hex_files = find_hex_files()
    if not hex_files:
        print("No .hex files found")
        return None
        
    print("\nHex files:")
    for i, f in enumerate(hex_files):
        print(f"{i+1}) {f}")
    
    try:
        choice = int(input("Select file: ")) - 1
        return hex_files[choice]
    except:
        return None

def hex_to_binary(hex_line):
    ''''
    Convert's Intel Hex to binary reperesentation
    
    <record_type><address high><address low><data_len><data><checksum>
    ^ 21 bytes
    '''
    
    if hex_line.startswith(':'):
        hex_line = hex_line[1:]
    try:
        hex_bytes = bytes.fromhex(hex_line)
        binary_data = bytearray(21)
        copy_len = min(len(hex_bytes), 21)
        binary_data[:copy_len] = hex_bytes[:copy_len]
        return bytes(binary_data)
    except:
        return None

def read_hex_file(filename):
    try:
        with open(filename, 'r') as f:
            # one-liner... this is what python is all about
            return [line.strip() for line in f if line.strip().startswith(':')]
    except:
        return None

def create_radio_loading_bar(current, total, attempt, max_attempts, elapsed_time):
    """Create a radio-themed loading display"""
    # radio static effect
    # beats looking at a blank page, but not really of any functional use
    static_chars = ['·', '•', '○', '-', '●', '◦', '◉', '~']
    static = ''.join([static_chars[int((elapsed_time * 10 + i) % len(static_chars))] for i in range(16)])
    print(f"\r{static}", end="")
    
    progress = current / total
    bar_width = 50
    filled_width = int(bar_width * progress)
    
    bar = '█' * filled_width + '░' * (bar_width - filled_width)
    
    print(f"\n[{bar}] {progress*100:5.1f}%")
    print(f"Line: {current:4d}/{total:<4d}  |  Attempt: {attempt}/{max_attempts}  |  Time: {elapsed_time:6.1f}s")
    print("\033[3A", end="")

def program(ser, hex_filename, reset_code="RESET"):
    hex_lines = read_hex_file(hex_filename)
    if not hex_lines:
        print("Failed to read hex file")
        return False
    
    print(f"Programming with {hex_filename}")
    print(f"Using reset code: '{reset_code}'")
    start_time = time.time()
    
    '''
        All commands are bound to 21 bytes since
        each firmware line is 21 bytes long.
        To keep things consistent, we send the
        RESET and BOOT commands as 21-byte binary
        and pad the rest of the lines with 0x00
    '''
    # Send custom reset code, padded to 21 bytes
    reset_bytes = reset_code.encode('utf-8')[:21]  # Truncate if longer than 21 bytes
    reset_command = reset_bytes + b'\x00' * (21 - len(reset_bytes))
    
    ser.write(reset_command)
    time.sleep(1)  # wait for bootloader to reset
    ser.write(b'BOOT' + b'\x00' * 17)
    
    # wait for RDY
    print("Waiting for bootloader...")
    ready = False
    timeout = time.time() + 10
    buffer = ""
    
    while time.time() < timeout and not ready:
        # not sure if this is the best way to do this
        if ser.in_waiting:
            data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            buffer += data
            while '\n' in buffer:
                line, buffer = buffer.split('\n', 1)
                if "RDY" in line:
                    ready = True
                    break
        # time.sleep(0.1)
    
    if not ready:
        print("Bootloader not ready!")
        return False
    
    print(f"Programming {len(hex_lines)} lines...")
    
    # Send hex lines
    for i, hex_line in enumerate(hex_lines, 1):
        binary_data = hex_to_binary(hex_line)
        if not binary_data:
            print(f"Failed to parse line {i}")
            return False
        
        success = False
        for attempt in range(REQUEST_ATTEMPTS):
            elapsed = time.time() - start_time
            
            # Show the radio-themed loading display
            create_radio_loading_bar(i, len(hex_lines), attempt + 1, REQUEST_ATTEMPTS, elapsed)
            
            ser.write(binary_data)
            
            # TODO: this code is sorta messy, abstract it seperate functions
            # Wait for ACK
            ack_timeout = time.time() + 1
            while time.time() < ack_timeout:
                if ser.in_waiting:
                    data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                    buffer += data
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        # python 3.10 has a nicer way to do this
                        # with match, but I'd rather keep it compatible
                        if "PRG" in line:
                            success = True
                            break
                        elif "DNE" in line:
                            elapsed = time.time() - start_time
                            print(f"\n\n\nProgramming finished in {elapsed:.1f}s\n")
                            return True
                        elif "CHK" in line or "ERR" in line:
                            break
                    if success:
                        break
                # time.sleep(0.1)
            
            if success:
                break
        
            # time.sleep(1)
        
        if not success:
            print(f"Failed at line {i}")
            return False
    
    elapsed = time.time() - start_time
    print(f"All lines sent! {elapsed:.1f}s")
    return True

def main():
    print(r" _       __                 __                __ ");
    print(r"| |     / /___ __   _____  / /_  ____  ____  / /_");
    print(r"| | /| / / __ `/ | / / _ \/ __ \/ __ \/ __ \/ __/");
    print(r"| |/ |/ / /_/ /| |/ /  __/ /_/ / /_/ / /_/ / /_  ");
    print(r"|__/|__/\__,_/ |___/\___/_.___/\____/\____/\__/  ");
    print(r"         programmmer tool for Waveboot bootloader");
    print();
    
    ser = connect()
    if not ser:
        return
    
    reset_code = get_reset_code()

    hex_file = select_hex_file()
    if hex_file:
        program(ser, hex_file, reset_code)
    
    ser.close()

if __name__ == "__main__":
    main()