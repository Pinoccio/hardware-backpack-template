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

#include "../protocol.h"
#include "crc.h"

typedef enum {
    OK,
    TIMEOUT,
    NACK,
    NO_ACK_OR_NACK,
    ACK_AND_NACK,
    PARITY_ERROR,
} status;

bool bp_wait_for_free_bus(status *status) {
    uint8_t timeout = 255;
    while(timeout--) {
        if (digitalRead(BP_BUS_PIN) == HIGH)
            return true;
    }

    if (status)
        *status = TIMEOUT;
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
        *status = TIMEOUT;
    return false;
}

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
            *status = ACK_AND_NACK;
        ok = false;
    } else if (second == LOW) {
        Serial.println("NAK received");
        if (status)
            *status = NACK;
        ok = false;
    } else if (first != LOW) {
        Serial.println("No ACK received");
        if (status)
            *status = NO_ACK_OR_NACK;
        ok = false;
    }

    return ok;
}

bool bp_read_byte(uint8_t *b, status *status = NULL) {
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
            *status = PARITY_ERROR;
        return false;
    }

    ok = ok && bp_read_ready(status);

    return ok && bp_read_ack_nack(status);
}

bool bp_write_byte(uint8_t b, status *status = NULL) {
    bool parity_val = 0;
    bool ok = true;
    uint8_t next_bit = 0x80;
    while (next_bit && ok) {
        if (b & next_bit)
            parity_val ^= 1;
        ok = ok && bp_write_bit(b & next_bit, status);
        next_bit >>= 1;
    }
    ok = ok && bp_write_bit(!parity_val, status);

    ok = ok && bp_read_ready(status);

    return ok && bp_read_ack_nack(status);
}

bool bp_scan(uint8_t result[][UNIQUE_ID_LENGTH], uint8_t *count) {
    bool ok = true;
    status status = OK;
    ok = ok && bp_reset(&status);
    ok = ok && bp_write_byte(BC_CMD_ENUMERATE, &status);
    if (status == NO_ACK_OR_NACK) {
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
            if (i == 0 && status == NO_ACK_OR_NACK) {
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

void print_eeprom(uint8_t addr, uint8_t offset, uint8_t *buf, uint8_t len) {
    Serial.print("Device "); Serial.print(addr, HEX); Serial.println(" EEPROM:");
    Serial.print("  ");
    if (!bp_read_eeprom(addr, offset, buf, len))
        return;
    while (len--) {
        if (*buf < 0x10) Serial.print("0");
        Serial.print(*buf++, HEX);
    }
    Serial.println();
}

void loop() {
    uint8_t buf[16];
    uint8_t ids[4][8];
    uint8_t count = sizeof(ids)/sizeof(*ids);
    delay(1000);
    Serial.println("Scanning...");
    digitalWrite(3, HIGH);
    digitalWrite(3, LOW);
    if (!bp_scan(ids, &count))
        return;
    print_scan_result(ids, count);
    delay(100);
    if (!eeprom_written && count) {
        print_eeprom(FIRST_VALID_ADDRESS, 0, buf, sizeof(buf));
        Serial.print("Incrementing all EEPROM bytes of device 0: ");
        for (size_t i = 0; i < sizeof(buf); ++i) {
            buf[i]++;
            if (buf[i] < 0x10) Serial.print("0");
            Serial.print(buf[i], HEX);
        }
        Serial.println();
        bp_write_eeprom(FIRST_VALID_ADDRESS, 0, buf, sizeof(buf));
        eeprom_written = true;
        delay(100);
    }
    Serial.println("Reading EEPROM...");
    for (uint8_t i = 0; i < count; ++i) {
        print_eeprom(FIRST_VALID_ADDRESS + i, 0, buf, sizeof(buf));
        delay(100);
    }
}

/* vim: set filetype=cpp sw=4 sts=4 expandtab: */
