// Sketch for testing the backpack bus prototype, scout / master side
//
// Copyright (c) 2013, Matthijs Kooijman <matthijs@stdin.nl>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#define BP_BUS_PIN 2

#define RESET_DELAY 2000
#define START_DELAY 100
#define VALUE_DELAY 550
#define SAMPLE_DELAY 250
#define IDLE_DELAY 50

// Should perhaps be read from EEPROM, but for now hardcoding is fine
#define EEPROM_SIZE 64

#include "../protocol.h"
#include "crc.h"

typedef enum {
    OK,
    TIMEOUT,
    NACK,
    NACK_NO_SLAVE_CODE, // NACK received, but failed to read error code
    NO_ACK_OR_NACK,
    ACK_AND_NACK,
    PARITY_ERROR,
} error_code;

struct status {
    // What went wrong?
    error_code code;

    // If code == NACK, what error code did the client send?
    uint8_t slave_code;
};




bool bp_wait_for_free_bus(status *status) {
    uint8_t timeout = 255;
    while(timeout--) {
        if (digitalRead(BP_BUS_PIN) == HIGH)
            return true;
    }

    if (status)
        status->code = TIMEOUT;
    Serial.println("Bus stays low too long!");
    return false;
}


bool bp_reset(status *status = NULL) {
    if (!bp_wait_for_free_bus(status))
        return false;
    pinMode(BP_BUS_PIN, OUTPUT);
    digitalWrite(BP_BUS_PIN, LOW);
    delayMicroseconds(RESET_DELAY);
    pinMode(BP_BUS_PIN, INPUT);
    delayMicroseconds(IDLE_DELAY);
    return true;
}

bool bp_write_bit(uint8_t bit, status *status = NULL) {
    if (!bp_wait_for_free_bus(status))
        return false;
    pinMode(BP_BUS_PIN, OUTPUT);
    digitalWrite(BP_BUS_PIN, LOW);
    delayMicroseconds(START_DELAY);
    if (bit)
        pinMode(BP_BUS_PIN, INPUT);
    delayMicroseconds(VALUE_DELAY);
    pinMode(BP_BUS_PIN, INPUT);
    delayMicroseconds(IDLE_DELAY);
    return true;
}

bool bp_read_bit(uint8_t *value, status *status = NULL) {
    if (!bp_wait_for_free_bus(status))
        return false;
    pinMode(BP_BUS_PIN, OUTPUT);
    digitalWrite(BP_BUS_PIN, LOW);
    delayMicroseconds(START_DELAY);
    pinMode(BP_BUS_PIN, INPUT);
    delayMicroseconds(SAMPLE_DELAY);
    *value = digitalRead(BP_BUS_PIN);
    delayMicroseconds(VALUE_DELAY - SAMPLE_DELAY);
    // If a slave pulls the line low, wait for him to finish (to
    // prevent the idle time from disappearing because of a slow
    // slave), but don't wait forever.
    if (!bp_wait_for_free_bus(status))
        return false;
    delayMicroseconds(IDLE_DELAY);
    return true;
}

bool bp_read_ready(status *status = NULL) {
    int timeout = 20;
    while (timeout--) {
        uint8_t value;
        if (!bp_read_bit(&value, status))
            return false;

        // Ready bit?
        if (value == HIGH)
            return true;
    }
    Serial.println("Stall timeout");
    if (status)
        status->code = TIMEOUT;
    return false;
}

bool bp_read_byte(uint8_t *b, status *status = NULL);

bool bp_read_ack_nack(status *status = NULL) {
    uint8_t first, second;
    bool ok = true;
    ok = ok && bp_read_bit(&first, status);
    ok = ok && bp_read_bit(&second, status);

    if (!ok)
        return false;

    // Acks are sent as 01, nacks as 10. Since the 0 is dominant during
    // a bus conflict, a reading of 00 means both an ack and a nack was
    // sent.
    if (first == LOW && second == LOW) {
        Serial.println("Both ACK and NAK received");
        if (status)
            status->code = ACK_AND_NACK;
        ok = false;
    } else if (second == LOW) {
        Serial.println("NAK received");
        if (status) {
            // Read error code from the slave
            if (bp_read_byte(&status->slave_code)) {
                status->code = NACK;
                Serial.print("Slave error code: "); Serial.println(status->slave_code);
            } else {
                Serial.println("---> Failed to receive error code after NAK");
                status->code = NACK_NO_SLAVE_CODE;
            }
        }
        ok = false;
    } else if (first != LOW) {
        Serial.println("No ACK received");
        if (status)
            status->code = NO_ACK_OR_NACK;
        ok = false;
    }

    return ok;
}

bool bp_read_byte(uint8_t *b, status *status) {
    bool parity_val = 0;
    bool ok = true;
    *b = 0;
    uint8_t next_bit = 0x80;
    uint8_t value;
    while (next_bit && ok) {
        ok = ok && bp_read_bit(&value, status);

        if (value) {
            *b |= next_bit;
            parity_val ^= 1;
        }
        next_bit >>= 1;
    }
    ok = ok && bp_read_bit(&value, status);

    if (ok && value == parity_val) {
        Serial.println("Parity error");
        if (status)
            status->code = PARITY_ERROR;
        return false;
    }

    ok = ok && bp_read_ready(status);

    return ok && bp_read_ack_nack(status);
}

bool bp_write_byte(uint8_t b, status *status = NULL, bool invert_parity = false) {
    bool parity_val = 0;
    bool ok = true;
    uint8_t next_bit = 0x80;
    while (next_bit && ok) {
        if (b & next_bit)
            parity_val ^= 1;
        ok = ok && bp_write_bit(b & next_bit, status);
        next_bit >>= 1;
    }

    if (invert_parity) // for testing
        parity_val = !parity_val;

    ok = ok && bp_write_bit(!parity_val, status);

    ok = ok && bp_read_ready(status);

    return ok && bp_read_ack_nack(status);
}

bool bp_scan(uint8_t result[][UNIQUE_ID_LENGTH], uint8_t *count) {
    bool ok = true;
    status status = {OK, 0};
    ok = ok && bp_reset(&status);
    ok = ok && bp_write_byte(BC_CMD_ENUMERATE, &status);
    if (status.code == NO_ACK_OR_NACK) {
        // Nobody on the bus
        *count = 0;
        return true;
    }
    uint8_t next_addr = FIRST_VALID_ADDRESS;
    uint8_t crc = 0;
    while (ok) {
        uint8_t *id = result[next_addr - FIRST_VALID_ADDRESS];
        for (uint8_t i = 0; i < UNIQUE_ID_LENGTH && ok; ++i) {
            ok = bp_read_byte(&id[i], &status);
            // Nobody responded, meaning all device are enumerated
            if (i == 0 && status.code == NO_ACK_OR_NACK) {
                *count = next_addr - FIRST_VALID_ADDRESS;
                return true;
            }
            crc = crc_update(UNIQUE_ID_CRC_POLY, crc, id[i]);
        }

        if (!ok)
            break;

        if (crc != 0) {
            Serial.print("Unique ID checksum error: ");
            for (uint8_t i = 0; i < UNIQUE_ID_LENGTH; ++i) {
                if (id[i] < 0x10) Serial.print("0");
                Serial.print(id[i], HEX);
            }
            Serial.println();
            return false;
        }

        if (next_addr++ == FIRST_VALID_ADDRESS + *count)
            break;
    }
    return ok;
}

bool bp_read_eeprom(uint8_t addr, uint8_t offset, uint8_t *buf, uint8_t len) {
    bool ok = true;
    bp_reset();
    ok = ok && bp_write_byte(addr);
    ok = ok && bp_write_byte(CMD_READ_EEPROM);
    ok = ok && bp_write_byte(offset);
    while (ok && len--)
        ok = bp_read_byte(buf++);
    return ok;
}

bool bp_write_eeprom(uint8_t addr, uint8_t offset, uint8_t *buf, uint8_t len) {
    bool ok = true;
    bp_reset();
    ok = ok && bp_write_byte(addr);
    ok = ok && bp_write_byte(CMD_WRITE_EEPROM);
    ok = ok && bp_write_byte(offset);
    while (ok && len--) {
        ok = bp_write_byte(*buf++);
    }
    return ok;
}


void setup() {
    Serial.begin(115200);
    pinMode(3, OUTPUT);
    pinMode(4, OUTPUT);
    digitalWrite(3, LOW);
}

uint8_t eeprom_written = false;

void print_scan_result(uint8_t result[][UNIQUE_ID_LENGTH], uint8_t count) {
    for (uint8_t i = 0; i < count; ++ i) {
        Serial.print("Device "); Serial.print(FIRST_VALID_ADDRESS + i, HEX); Serial.print(" found with id: ");
        for (uint8_t j = 0; j < UNIQUE_ID_LENGTH; ++j) {
            if (result[i][j] < 0x10) Serial.print("0");
                Serial.print(result[i][j], HEX);
        }
        Serial.println();
    }
}

void print_eeprom(uint8_t addr, uint8_t *buf, uint8_t len) {
    Serial.print("Device "); Serial.print(addr, HEX); Serial.println(" EEPROM:");
    Serial.print("  ");
    while (len--) {
        if (*buf < 0x10) Serial.print("0");
        Serial.print(*buf++, HEX);
    }
    Serial.println();
}

void loop() {
    uint8_t ids[4][8];
    uint8_t eeproms[4][EEPROM_SIZE];
    uint8_t count = sizeof(ids)/sizeof(*ids);
    uint8_t b;
    status status = {OK, 0};
    delay(1000);
    Serial.println("Scanning...");
    digitalWrite(3, HIGH);
    digitalWrite(3, LOW);
    if (!bp_scan(ids, &count)) {
        Serial.println("---> Enumeration failed");
        return;
    }
    print_scan_result(ids, count);
    delay(100);
    Serial.println("Reading EEPROM...");
    for (uint8_t i = 0; i < count; ++i) {
        if (!bp_read_eeprom(FIRST_VALID_ADDRESS + i, 0, eeproms[i], sizeof(*eeproms))) {
            Serial.print("---> EEPROM read failed for device "); Serial.println(FIRST_VALID_ADDRESS + i);
        }
        print_eeprom(FIRST_VALID_ADDRESS + i, eeproms[i], sizeof(*eeproms));
        delay(100);
    }

    for (uint8_t i = 0; i < count; ++i) {
        Serial.print("Testing error conditions on device "); Serial.println(FIRST_VALID_ADDRESS + i);
        Serial.println("Only errors prefixed with ---> are unexpected");

        bp_reset();
        // Send a parity error
        if (!bp_write_byte(FIRST_VALID_ADDRESS + i, &status)) {
            Serial.println("---> Adressing failed?");
        // Write a command with a parity error
        } else if (bp_write_byte(CMD_READ_EEPROM, &status, true)) {
            Serial.println("---> Undetected parity error");
        } else if (status.code != NACK) {
            Serial.print("---> No NACK or no error code received after parity error, but: "); Serial.println(status.code);
        } else if (status.slave_code != ERR_PARITY) {
            Serial.print("---> Not parity error code: "); Serial.println(b);
        // Read a byte to see if anyone is still on the bus...
        } else if ((status.code = OK) == OK &&
                   (bp_read_byte(&b, &status) ||
                    status.code != NO_ACK_OR_NACK)) {
            Serial.println("---> Bus not empty after parity error");
        }

        bp_reset();
        status.code = OK;
        // Send an unknown command
        if (!bp_write_byte(FIRST_VALID_ADDRESS + i, &status)) {
            Serial.println("---> Adressing failed?");
        // Write an unknown command
        } else if (bp_write_byte(CMD_RESERVED, &status) || status.code != NACK) {
            Serial.print("---> No NACK or no error code received after unknown command, but: "); Serial.println(status.code);
        } else if (status.slave_code != ERR_UNKNOWN_COMMAND) {
            Serial.print("---> Not unknown command error code: "); Serial.println(b);
        // Read a byte to see if anyone is still on the bus...
        } else if ((status.code = OK) == OK &&
                   (bp_read_byte(&b, &status) ||
                    status.code != NO_ACK_OR_NACK)) {
            Serial.println("---> Bus not empty after unknown command error");
        }

        bp_reset();
        status.code = OK;
        // Send an out-of-bound EEPROM address
        if (!bp_write_byte(FIRST_VALID_ADDRESS + i, &status)) {
            Serial.println("---> Adressing failed?");
        // Write a read EEPROM command
        } else if (!bp_write_byte(CMD_READ_EEPROM, &status)) {
            Serial.println("---> Read EEPROM command failed");
        // Read from an invalid address
        } else if (bp_write_byte(2 * EEPROM_SIZE, &status)) {
            Serial.println("---> Invalid read address not rejected");
        } else if (status.code != NACK) {
            Serial.print("---> No NACK or no error code received after invalid read address, but: "); Serial.println(status.code);
        } else if (status.slave_code != ERR_READ_EEPROM_INVALID_ADDRESS) {
            Serial.print("---> Not invalid read address error code: "); Serial.println(b);
        // Read a byte to see if anyone is still on the bus...
        } else if ((status.code = OK) == OK &&
                   (bp_read_byte(&b, &status) ||
                    status.code != NO_ACK_OR_NACK)) {
            Serial.println("---> Bus not empty after invalid read address error");
        }

        bp_reset();
        status.code = OK;
        // Read overflow into an invalid address
        if (!bp_write_byte(FIRST_VALID_ADDRESS + i, &status)) {
            Serial.println("---> Adressing failed?");
        // Write a read EEPROM command
        } else if (!bp_write_byte(CMD_READ_EEPROM, &status)) {
            Serial.println("---> Read EEPROM command failed");
        // Read from the last address
        } else if (!bp_write_byte(EEPROM_SIZE - 1, &status)) {
            Serial.println("---> Valid read address rejected");
        // Read first (valid) byte
        } else if (!bp_read_byte(&b, &status)) {
            Serial.println("---> Failed to read valid byte");
        // Read next (invalid) byte
        } else if (bp_read_byte(&b, &status)) {
            Serial.println("---> Overflowed read address not rejected");
        } else if (status.code != NACK) {
            Serial.print("---> No NACK or no error code received after read address overflow, but: "); Serial.println(status.code);
        } else if (status.slave_code != ERR_READ_EEPROM_INVALID_ADDRESS) {
            Serial.print("---> Not invalid read address error code: "); Serial.println(b);
        // Read a byte to see if anyone is still on the bus...
        } else if ((status.code = OK) == OK &&
                   (bp_read_byte(&b, &status) ||
                    status.code != NO_ACK_OR_NACK)) {
            Serial.println("---> Bus not empty after read address overflow error");
        }

        bp_reset();
        status.code = OK;
        // Write overflow into an invalid address
        if (!bp_write_byte(FIRST_VALID_ADDRESS + i, &status)) {
            Serial.println("---> Adressing failed?");
        // Write a read EEPROM command
        } else if (!bp_write_byte(CMD_WRITE_EEPROM, &status)) {
            Serial.println("---> Write EEPROM command failed");
        // Write to the last address
        } else if (!bp_write_byte(EEPROM_SIZE - 1, &status)) {
            Serial.println("---> Valid write address rejected");
        // Write byte (without changing it, b should still contain the
        // byte read above).
        } else if (!bp_write_byte(b, &status)) {
            Serial.println("---> Failed to write valid byte");
        // Write next (invalid) byte
        } else if (bp_write_byte(0, &status)) {
            Serial.println("---> Overflowed write address not rejected");
        } else if (status.code != NACK) {
            Serial.print("---> No NACK or no error code received after write address overflow, but: "); Serial.println(status.code);
        } else if (status.slave_code != ERR_WRITE_EEPROM_INVALID_ADDRESS) {
            Serial.print("---> Not invalid write address error code: "); Serial.println(b);
        // Read a byte to see if anyone is still on the bus...
        } else if ((status.code = OK) == OK &&
                   (bp_read_byte(&b, &status) ||
                    status.code != NO_ACK_OR_NACK)) {
            Serial.println("---> Bus not empty after write address overflow error");
        }

        bp_reset();
        status.code = OK;
        // Write read-only byte
        if (!bp_write_byte(FIRST_VALID_ADDRESS + i, &status)) {
            Serial.println("---> Adressing failed?");
        // Write a read eeprom command
        } else if (!bp_write_byte(CMD_WRITE_EEPROM, &status)) {
            Serial.println("---> Write EEPROM command failed");
        // Write the address of the supported protocol version
        } else if (!bp_write_byte(0, &status)) {
            Serial.println("---> Valid write address rejected");
        // Try to write a different byte into position 0
        } else if (bp_write_byte(ids[i][0] + 1, &status)) {
            Serial.println("---> Writing read-only byte not rejected");
        } else if (status.code != NACK) {
            Serial.print("---> No NACK or no error code received after writing read-only byte, but: "); Serial.println(status.code);
        } else if (status.slave_code != ERR_WRITE_EEPROM_READ_ONLY) {
            Serial.print("---> Not read-only error code: "); Serial.println(b);
        // Read a byte to see if anyone is still on the bus...
        } else if ((status.code = OK) == OK &&
                   (bp_read_byte(&b, &status) ||
                    status.code != NO_ACK_OR_NACK)) {
            Serial.println("---> Bus not empty after read-only error");
        }

        bp_reset();
        status.code = OK;
        // Write read-only byte with unchanged value
        if (!bp_write_byte(FIRST_VALID_ADDRESS + i, &status)) {
            Serial.println("---> Adressing failed?");
        // Write a read eeprom command
        } else if (!bp_write_byte(CMD_WRITE_EEPROM, &status)) {
            Serial.println("---> Read EEPROM command failed");
        // Write the address of the supported protocol version
        } else if (!bp_write_byte(0, &status)) {
            Serial.println("---> Valid write address rejected");
        // Write an unchanged byte into position 0
        } else if (!bp_write_byte(ids[i][0], &status)) {
            Serial.println("---> Writing read-only byte with unchanged value failed");
        }
    }
}

/* vim: set filetype=cpp sw=4 sts=4 expandtab: */
