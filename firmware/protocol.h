#ifndef _PROTOCOL_H
#define _PROTOCOL_H
// Broadcast commands (i.e., special addresses sent over the wire)
enum {
    // Start bus enumeration
    BC_CMD_ENUMERATE = 0xff,
};

// Targeted commands (i.e,. sent over the wire after the address)
enum {
    CMD_READ_EEPROM = 0x01,
    CMD_WRITE_EEPROM = 0x02,
};

uint8_t const FIRST_VALID_ADDRESS = 0x1;

#endif // PROTOCOL_H
