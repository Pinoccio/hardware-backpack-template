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

struct timings {
    unsigned reset;
    unsigned start;
    unsigned value;
    unsigned sample;
    unsigned idle;
    unsigned next_bit;
};

timings timings_to_test[] = {
    // Minimum timings
    {   .reset = 1800,
        .start = 50,
        .value = 550,
        .sample = 250,
        .idle = 50,
        .next_bit = 700,
    },
    // Typical timings
    {   .reset = 2000,
        .start = 100,
        .value = 550,
        .sample = 250,
        .idle = 50,
        // Directly after the idle time
        .next_bit = 0,
    },
    // Maximum timings
    {   .reset = 2200,
        .start = 200,
        .value = 500,
        .sample = 200,
        .idle = 50,
        .next_bit = 1100,
    }
};

// The maximum time after which the slave should go back to idle
#define NEXT_BIT_TIMEOUT 1700

timings *current_timings;

// Should perhaps be read from EEPROM, but for now hardcoding is fine
#define EEPROM_SIZE 64
// Ofset of the unique ID within the EEPROM
#define UNIQUE_ID_OFFSET 1

#define lengthof(x) (sizeof(x)/sizeof(*x))

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

const char *error_code_str[] = {
    [OK] = "OK",
    [TIMEOUT] = "TIMEOUT",
    [NACK] = "NACK",
    [NACK_NO_SLAVE_CODE] = "NACK_NO_SLAVE_CODE",
    [NO_ACK_OR_NACK] = "NO_ACK_OR_NACK",
    [ACK_AND_NACK] = "ACK_AND_NACK",
    [PARITY_ERROR] = "PARITY_ERROR",
};

struct status {
    // What went wrong?
    error_code code;

    // If code == NACK, what error code did the client send?
    uint8_t slave_code;
};

uint8_t ids[4][8];
uint8_t eeproms[4][EEPROM_SIZE];

// Where to introduce a parity error? This indicates the number of
// bytes to be sent normally before a parity error is introduced
uint8_t parity_error_byte = -1;

// How many bytes without parity error are left in the current
// transaction? Automatically reset to parity_error_byte by test_reset.
uint8_t parity_error_left;

// When was the start of the most recent bit?
unsigned long bit_start = 0;

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
    delayMicroseconds(current_timings->reset);
    pinMode(BP_BUS_PIN, INPUT);
    delayMicroseconds(current_timings->idle);
    return true;
}

bool bp_write_bit(uint8_t bit, status *status = NULL) {
    while(micros() - bit_start < current_timings->next_bit) /* wait */;
    if (!bp_wait_for_free_bus(status))
        return false;
    bit_start = micros();
    pinMode(BP_BUS_PIN, OUTPUT);
    digitalWrite(BP_BUS_PIN, LOW);
    delayMicroseconds(current_timings->start);
    if (bit)
        pinMode(BP_BUS_PIN, INPUT);
    delayMicroseconds(current_timings->value);
    pinMode(BP_BUS_PIN, INPUT);
    delayMicroseconds(current_timings->idle);
    return true;
}

bool bp_read_bit(uint8_t *value, status *status = NULL) {
    while(micros() - bit_start < current_timings->next_bit) /* wait */;
    if (!bp_wait_for_free_bus(status))
        return false;
    bit_start = micros();
    pinMode(BP_BUS_PIN, OUTPUT);
    digitalWrite(BP_BUS_PIN, LOW);
    delayMicroseconds(current_timings->start);
    pinMode(BP_BUS_PIN, INPUT);
    delayMicroseconds(current_timings->sample);
    *value = digitalRead(BP_BUS_PIN);
    delayMicroseconds(current_timings->value - current_timings->sample);
    // If a slave pulls the line low, wait for him to finish (to
    // prevent the idle time from disappearing because of a slow
    // slave), but don't wait forever.
    if (!bp_wait_for_free_bus(status))
        return false;
    delayMicroseconds(current_timings->idle);
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

    randomSeed(analogRead(0));
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

bool test_progress(const char *msg) {
    Serial.println(msg);
    return true;
}

bool test_progress(const char *msg, uint8_t b) {
    Serial.print(msg);
    Serial.print("0x");
    Serial.println(b, HEX);
    return true;
}

void test_println_status(const status *s) {
    if (s->code < lengthof(error_code_str))
        Serial.print(error_code_str[s->code]);
    else
        Serial.print(s->code);
    if (s->code == NACK) {
        Serial.print(", slave error code: 0x");
        Serial.print(s->slave_code);
    }
    Serial.println();
}

void test_print_failed(const char *msg, const status *s, const status *expected = NULL) {
    Serial.print("---> ");
    Serial.println(msg);
    Serial.print("---> Status was: ");
    test_println_status(s);
    if (expected) {
        Serial.print("---> Expected: ");
        test_println_status(expected);
    }
    while(Serial.read() != -1) /* Do nothing */;
    Serial.println("---> Press a key to continue testing");
    while(Serial.read() == -1) /* Do nothing */;
}

bool test_check_status(const status *s, const status *expected) {
    if (s->code != expected->code) {
        test_print_failed("Unexpected status", s, expected);
        return false;
    } else if (expected->code == NACK && s->slave_code != expected->slave_code) {
        test_print_failed("Unexpected slave error code", s, expected);
        return false;
    }
    return true;
}

bool test_reset() {
    status s = {OK};
    parity_error_left = parity_error_byte;
    if (!bp_reset(&s)) {
        test_print_failed("Reset failed", &s);
        return false;
    }
    test_progress("Reset");
    return true;
}

bool test_read_byte(uint8_t *b, status *expected, bool progress = true) {
    status s = {OK};
    bool ok = bp_read_byte(b, &s);
    if (progress) {
        if (ok)
            test_progress("Read byte: ", *b);
        else
            test_progress("Failed to read byte");
    }

    return test_check_status(&s, expected);
}

bool test_empty_bus();

bool test_write_byte(uint8_t b, status *expected, bool progress = true) {
    status s = {OK};
    if (progress)
        test_progress("Writing byte: ", b);
    if (parity_error_left-- == 0) {
        status expect_parity = {NACK, ERR_PARITY};
        bool ok = test_progress("Introduced parity error");
        bp_write_byte(b, &s, true);
        ok = ok && test_check_status(&s, &expect_parity);
        ok = ok && test_empty_bus();
        // Even if the parity error was handled as expected, don't
        // continue with the rest of the testcase
        return false;
    } else {
        bp_write_byte(b, &s);
        return test_check_status(&s, expected);
    }
}

bool test_address(uint8_t addr, status *expected) {
    test_progress("Sending address: ", addr);
    return test_write_byte(addr, expected, false);
}

bool test_cmd(uint8_t addr, uint8_t cmd, status *expected) {
    status expect_ok = {OK};
    bool ok = test_address(addr, &expect_ok);
    ok = ok && test_progress("Sending command: ", cmd);
    return ok && test_write_byte(cmd, expected, false);
}

bool test_empty_bus() {
    status expect_no_reply = {NO_ACK_OR_NACK};
    uint8_t b;
    return test_read_byte(&b, &expect_no_reply);
}

bool test_timeout() {
    while(micros() - bit_start < NEXT_BIT_TIMEOUT) /* wait */;
    return test_empty_bus();
}

// Send an unknown command
void test_unknown_command(uint8_t addr, uint8_t cmd) {
    status expect_unknown = {NACK, ERR_UNKNOWN_COMMAND};
    bool ok = test_reset();
    ok = ok && test_cmd(addr, cmd, &expect_unknown);
    ok = ok && test_empty_bus();
}

// Send an out-of-bound EEPROM address
void test_invalid_read_address(uint8_t addr, uint8_t eeprom_addr) {
    status expect_ok = {OK, 0};
    status expect_invalid_read = {NACK, ERR_READ_EEPROM_INVALID_ADDRESS};
    bool ok = test_reset();
    ok = ok && test_cmd(addr, CMD_READ_EEPROM, &expect_ok);
    ok = ok && test_write_byte(eeprom_addr, &expect_invalid_read);
    ok = ok && test_empty_bus();
}

// Read overflow into an invalid address
void test_read_overflow(uint8_t addr) {
    status expect_ok = {OK, 0};
    status expect_invalid_read = {NACK, ERR_READ_EEPROM_INVALID_ADDRESS};
    uint8_t b;
    bool ok = test_reset();
    ok = ok && test_cmd(addr, CMD_READ_EEPROM, &expect_ok);
    ok = ok && test_write_byte(EEPROM_SIZE - 1, &expect_ok);
    ok = ok && test_read_byte(&b, &expect_ok);
    ok = ok && test_read_byte(&b, &expect_invalid_read);
    ok = ok && test_empty_bus();
}


// Write overflow into an invalid address
void test_write_overflow(uint8_t addr) {
    status expect_ok = {OK, 0};
    status expect_invalid_write = {NACK, ERR_WRITE_EEPROM_INVALID_ADDRESS};
    bool ok = test_reset();
    ok = ok && test_cmd(addr, CMD_WRITE_EEPROM, &expect_ok);
    ok = ok && test_write_byte(EEPROM_SIZE - 1, &expect_ok);
    ok = ok && test_write_byte(eeproms[addr - FIRST_VALID_ADDRESS][EEPROM_SIZE - 1], &expect_ok);
    ok = ok && test_write_byte(0, &expect_invalid_write);
    ok = ok && test_empty_bus();
}

// Write read-only byte
void test_write_readonly(uint8_t addr, uint8_t eeprom_addr) {
    status expect_ok = {OK, 0};
        status expect_read_only = {NACK, ERR_WRITE_EEPROM_READ_ONLY};
    bool ok = test_reset();
    ok = ok && test_cmd(addr, CMD_WRITE_EEPROM, &expect_ok);
    ok = ok && test_write_byte(eeprom_addr, &expect_ok);
    ok = ok && test_write_byte(eeproms[addr - FIRST_VALID_ADDRESS][eeprom_addr] + 1, &expect_read_only);
    ok = ok && test_empty_bus();
}

// Write read-only bytes with unchanged value
void test_write_unchanged_readonly(uint8_t addr, uint8_t eeprom_addr) {
    status expect_ok = {OK, 0};
    bool ok = test_reset();
    ok = ok && test_cmd(addr, CMD_WRITE_EEPROM, &expect_ok);
    ok = ok && test_write_byte(eeprom_addr, &expect_ok);
    ok = ok && test_write_byte(eeproms[addr - FIRST_VALID_ADDRESS][eeprom_addr], &expect_ok);
    ok = ok && test_timeout();
}

// Address an unknown slave
void test_unassigned_address(uint8_t addr) {
    status expect_no_reply = {NO_ACK_OR_NACK};
    bool ok = test_reset();
    ok = ok && test_address(addr, &expect_no_reply);
    ok = ok && test_empty_bus();
}

void loop() {
    for (uint8_t t = 0; t < lengthof(timings_to_test); ++t) {
        delay(1000);
        uint8_t count = lengthof(ids);

        current_timings = &timings_to_test[t];
        Serial.print("Using timing set: ");
        Serial.println(t);
        long seed = random();
        randomSeed(seed);
        Serial.print("Using random seed: ");
        Serial.println(seed);

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
            } else {
                print_eeprom(FIRST_VALID_ADDRESS + i, eeproms[i], sizeof(*eeproms));
            }
            delay(100);
        }

        for (uint8_t i = 0; i < count; ++i) {
            Serial.print("Testing error conditions on device "); Serial.println(FIRST_VALID_ADDRESS + i);
            Serial.print("Parity error at byte:");
            Serial.println(parity_error_byte);
            Serial.println("Only errors prefixed with ---> are unexpected");
            uint8_t addr = FIRST_VALID_ADDRESS + i;

            test_unknown_command(addr, CMD_RESERVED);
            test_unknown_command(addr, random(CMD_LAST + 1, 256));
            test_invalid_read_address(addr, random(EEPROM_SIZE, 256));
            test_read_overflow(addr);
            test_write_overflow(addr);
            test_write_readonly(addr, UNIQUE_ID_OFFSET + random(0, UNIQUE_ID_LENGTH));
            test_write_unchanged_readonly(addr, UNIQUE_ID_OFFSET + random(0, UNIQUE_ID_LENGTH));
        }
        test_unassigned_address(ADDRESS_RESERVED);
        test_unassigned_address(random(count + 1, BC_FIRST));
    }

    // On every loop, introduce parity errors in different places
    parity_error_byte++;
}

/* vim: set filetype=cpp sw=4 sts=4 expandtab: */
