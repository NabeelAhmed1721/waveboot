'''
NOT NEEDED ANYMORE. I used this to encode the firmware hex file into C arrays
so I could paste it into `main.cpp`. This was used to initially test programming.

Now, the CLI tool pipes the firmware to the programmer via serial UART.

[cli] -> [programmer] -> [tx radio] -> [rx radio] -> [bootloader]
'''
 

import os
import sys

def validate_checksum(byte_count, address, record_type, data, checksum):
    '''
    calculate checksum
    sum of byte_count + address (2 bytes) + record_type + data (byte_count bytes)
    2's complement of least significant byte of sum
    '''
    
    total = byte_count + (address >> 8) + (address & 0xFF) + record_type
    for i in range(0, len(data), 2):
        total += int(data[i:i+2], 16)
    total = total & 0xFF  # least significant byte
    calculated_checksum = ((~total + 1) & 0xFF)  # 2's complement
    return calculated_checksum == checksum

def format_c_as_array(hex_line: str, target_width: int = 21):
    byte_array = []
    
    for i in range(0, len(hex_line), 2):
        if i + 1 < len(hex_line):
            byte_val = hex_line[i:i+2]
            byte_array.append('0x' + byte_val.lower())
    
    # pad with zeros to reach width
    while len(byte_array) < target_width:
        byte_array.append('0x00')
    
    return '{ ' + ', '.join(byte_array) + ' }'

def encode(line: str):
    # validate
    # find start code and remove all text before it
    start_index = line.find(':')
    if start_index == -1:
        print("Invalid line (no start code `:`): " + line)
        sys.exit(1)

    hex_data = line[start_index + 1:]  # remove everything before start code

    # get values for validation
    byte_count = int(hex_data[0:2], 16)
    address = int(hex_data[2:6], 16)
    record_type = int(hex_data[6:8], 16)
    data = hex_data[8:8 + byte_count * 2]
    checksum = int(hex_data[8 + byte_count * 2:10 + byte_count * 2], 16)

    # check size of data
    if len(data) != byte_count * 2:
        print("Invalid line (data length does not match byte count): " + line)
        sys.exit(1)

    # check checksum
    if not validate_checksum(byte_count, address, record_type, data, checksum):
        print("Invalid line (checksum does not match): " + line)
        sys.exit(1)
    
    # return the raw hex data
    return hex_data

if __name__ == "__main__":
    # print arguments
    if len(sys.argv) <= 1:
        print("Hex file path not specified")
        print("Usage: python encode.py <path-to-hex-file>")
        sys.exit(1)

    # check if path and file exists
    hex_path = sys.argv[1]
    if not os.path.exists(hex_path):
        print("File not found: " + hex_path)
        sys.exit(1)

    # read hex file
    with open(hex_path, 'r') as hex_file:
        for line in hex_file:
            line = line.strip()
            if line:  # skip empty lines
                hex_data = encode(line)
                print(format_c_as_array(hex_data))