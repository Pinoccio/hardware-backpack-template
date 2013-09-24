// Copyright (C) 2013  Matthijs Kooijman <matthijs@stdin.nl>
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
//
// Compile using:
//   avr-g++ -mmcu=attiny13 -Os backpack.c -o backpack.elf
//   avr-objcopy -O ihex backpack.elf backpack.hex
//
// To flash:
//   avrdude -c stk500 -p attiny13 -P /dev/ttyUSB0 -U flash:w:backpack.hex
//
// Fuse settings are 0xff and 0x29:
//   avrdude -c stk500 -p attiny13 -P /dev/ttyUSB0 -U hfuse:w:0xff:m -U lfuse:w:0x29:m
// TODO: Decide on brownout detection and watchdog timer

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/cpufunc.h>
#include <avr/sleep.h>
#include <stdbool.h>

// 4.8Mhz oscillator with CKDIV8 fuse set
#define F_CPU (4800000/8)

// Debug macro, generate a short pulse on the given pin (B0, B2 or B4)
#define pulse(pin) \
    PORTB &= ~(1 << pin); \
    _NOP(); _NOP(); _NOP(); \
    PORTB |= (1 << pin);

// Offset of the unique ID within the EEPROM
uint8_t const ID_SIZE = 4;
// Size of the unique ID
uint8_t const ID_OFFSET = 0;

#define US_TO_CLOCKS(x) (unsigned long)(x * F_CPU / 8 / 1000000)
#define RESET_SAMPLE US_TO_CLOCKS(1800)
//#define START_DELAY US_TO_CLOCKS(32)
#define DATA_WRITE US_TO_CLOCKS(900)
#define DATA_SAMPLE US_TO_CLOCKS(450)

// Broadcast commands (i.e., special addresses sent over the wire)
enum {
    // Start bus enumeration
    BC_CMD_ENUMERATE = 0xaa,
};

// Targeted commands (i.e,. sent over the wire after the address)
enum {
    CMD_READ_EEPROM = 0x01,
    CMD_WRITE_EEPROM = 0x02,
};

// Constants for the current action the low level protocol handler
// Some of these can be combined.
enum {
    ACTION_IDLE = 0,
    ACTION_RECEIVE = 1,
    ACTION_SEND = 2,
    ACTION_STALL = 4,
    ACTION_ACK = 8,
};

// Constants for the current state for the high level protocol handler
enum {
    // Idle - Waiting for the next reset to participate (again)
    STATE_IDLE,
    // Bus was reset, reading the address byte (or broadcast command)
    STATE_READ_ADDRESS,
    // BC_CMD_ENUMERATE received, bus enumeration in progress
    STATE_ENUMERATE,
    // We are adressed, receiving targeted command
    STATE_READ_COMMAND,
    // CMD_READ_EEPROM received, now receiving read address
    STATE_READ_EEPROM_ADDR,
    // CMD_READ_EEPROM and read address received, now reading
    STATE_READ_EEPROM_READ,
    // CMD_WRITE_EEPROM received, now receiving write address
    STATE_WRITE_EEPROM_ADDR,
    // CMD_WRITE_EEPROM and write address received, now writing
    STATE_WRITE_EEPROM_WRITE,
};

// Putting global variables in fixed registers saves a lot of
// instructions for loading and storing their values to memory.
// Additionally, if _all_ globals are in registers (or declared with
// __attribute__ ((section (".noinit")))), gcc will omit the bss clear
// loop (saving another 16 bytes). Only call-used registers are
// available, so that's effectively r2-r17. Using all of those will
// probably kill the compiler, though.

register uint8_t byte_buf asm("r2");
register uint8_t next_bit asm("r3");
register uint8_t next_byte asm("r4");

register uint8_t bus_addr asm("r5");
register bool mute asm("r6");

register uint8_t action asm("r7");
register uint8_t state asm("r8");

// Note that the falling edge interrupt is _always_ enabled, so if a
// falling edge occurs before the previous bit period is processed (e.g.
// before the timer interrupt fired), then the previous period is
// effectively ignored. This can only happen when a device violates the
// protocol.
ISR(INT0_vect)
{
    //PORTB &= ~(1 << PINB0);
    // Reset the timer on every falling edge. Measurements indicate that
    // it takes about 50 μs from the moment the bus is pulled low to
    // we get this far into the interrupt. We assume that the timer
    // interrupt will be delayed by the same amount, so add it twice.
    // TODO: Bare interrupt for more controlled timings?
    TCNT0 = US_TO_CLOCKS(50 * 2);
    // Clear any interrupt flags that might have been set while the
    // timer interrupts were disabled
    TIFR0 = (1 << OCF0B) | (1 << OCF0A);

    // Always enable the OCR0B interrupt to detect a reset pulse
    TIMSK0 |=  (1 << OCIE0B);

    // If we were powered-down, we'll have been set to a
    // level-triggered interrupt instead of an edge-triggered one,
    // since a edge-triggered one can wake us up. We'll need to set
    // it to edge-triggered to prevent being flooded with
    // interrupts.
    // Make INT0 falling edge-triggered (note that this assumes
    // ISC00 is not set)
    MCUCR |= (1<<ISC01);
    set_sleep_mode(SLEEP_MODE_IDLE);

    if (action & ACTION_ACK) {
        // Pull the line low
        DDRB |= (1 << PINB1);
        // Let if float again after some time
        OCR0A = DATA_WRITE;
    } else if (action & ACTION_STALL) {
        ;
    } else if (action & ACTION_RECEIVE) {
        OCR0A = DATA_SAMPLE; //wait a time for reading
        TIMSK0 |=  (1 << OCIE0A);
    } else if (action & ACTION_SEND) {
        if ((byte_buf & next_bit) == 0 && !mute) {
            // Pull the line low
            DDRB |= (1 << PINB1);
            // Let if float again after some time
            OCR0A = DATA_WRITE;
            TIMSK0 |=  (1 << OCIE0A);
        } else {
            // Check for address collisions
            OCR0A = DATA_SAMPLE;
            TIMSK0 |=  (1 << OCIE0A);
        }
    }
}


ISR(TIM0_COMPB_vect)
{
    uint8_t val = PINB & (1 << PINB1);
    if (!val) {
        // Bus is still low, this is a reset pulse (regardless of what
        // state we were in previously!)
        state = STATE_READ_ADDRESS;
        action = ACTION_RECEIVE;
        byte_buf = 0;
        next_bit = 1;
        mute = false;
    } else {
        set_sleep_mode(SLEEP_MODE_PWR_DOWN);

        // Make INT0 low-level triggered (note that this assumes ISC00
        // is not set)
        MCUCR &= ~(1<<ISC01);
    }

    // Disable this timer interrupt
    TIMSK0 &= ~(1 << OCIE0B);
}

ISR(TIM0_COMPA_vect)
{
    uint8_t val = PINB & (1 << PINB1);
    if (action & ACTION_ACK) {
        // Timer means end of period, we should stop pulling the line low
        DDRB &= ~(1 << PINB1);
    } else if (action & ACTION_RECEIVE) {
        // Read and store bit value
        if (val)
            byte_buf |= next_bit;
        next_bit <<= 1;

        // Full byte received? Stall while main loop to process
        if (!next_bit)
            action |= ACTION_STALL;
    } else if (action & ACTION_SEND) {
        if ((DDRB & (1 << PINB1)) == 0 && !val) {
            // We're sending our address, but are not currently pulling the
            // line low. Check if the line is actually high. If not, someone
            // else is pulling the line low, so we drop out of the current
            // address sending round.
            mute = true;
        }

        // If we're pulling the line low to send a 0, timer means end of
        // period, we should stop pulling the line low
        // If we're leaving the line high to send a 1, timer means to
        // check for collision, but it doesn't hurt to "stop" pulling
        // the line low (which we aren't in the first place).
        DDRB &= ~(1 << PINB1);

        next_bit <<= 1;

        // Sent full byte? Stall while the mainloop decides what to do
        if (!next_bit)
            action |= ACTION_STALL;
    }

    // Disable this timer interrupt
    TIMSK0 &= ~(1 << OCIE0A);
}

void EEPROM_write(uint8_t ucAddress, uint8_t ucData)
{
      /* Wait for completion of previous write */
    while(EECR & (1<<EEPE));
    /* Set Programming mode */
    EECR = (0<<EEPM1)|(0>>EEPM0);
    /* Set up address and data registers */
    EEARL = ucAddress;
    EEDR = ucData;
    /* Write logical one to EEMPE */
    EECR |= (1<<EEMPE);
    /* Start eeprom write by setting EEPE */
    EECR |= (1<<EEPE);
}

uint8_t EEPROM_read(uint8_t ucAddress)
{
    /* Wait for completion of previous write */
    while(EECR & (1<<EEPE));
    /* Set up address register */
    EEARL = ucAddress;
    /* Start eeprom read by writing EERE */
    EECR |= (1<<EERE);
    /* Return data from data register */
    return EEDR;
}

#if (__GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 8))
// On GCC < 4.8, there is a bug that can cause writes to global register
// variables be dropped in a function that never returns (e.g., main).
// To prevent triggering this bug, don't inline the setup and loop
// functions, which could cause such writes to show up in main. This
// adds a few bytes of useless program code, but that pales in
// comparison with the bytes saved by using global register variables.
// http://gcc.gnu.org/bugzilla/show_bug.cgi?id=51447
void __attribute__((noinline)) setup(void);
void __attribute__((noinline)) loop(void);
#endif

void setup(void)
{
    bus_addr = 0xff;
    mute = false;
    action = ACTION_IDLE;
    state = STATE_IDLE;
    // Set ports to output for debug
    PORTB = DDRB = (1 << PINB0) | (1 << PINB2) | (1 << PINB4);

    // OCR0B interrupt is always used for a reset, so set a fixed
    // compare value. OCR0A timeout depends on the context.
    OCR0B = RESET_SAMPLE;

    // Enable INT0 interrupt
    GIMSK=(1<<INT0);

    // Set timer to /8 prescaler, which, together with the CLKDIV8 fuse,
    // makes 4.8Mhz / 8 / 8 = 75kHz
    TCCR0B=(1<<CS01);

    // Enable sleeping. The datasheet recommends only setting this bit
    // at the moment you actually want to sleep, "to prevent
    // accidentally putting the system in sleep mode", but why would we
    // use the sleep instruction if we didn't want to sleep? Silly bit.
    sleep_enable();

    sei();
}

void loop(void)
{
    if (action == (ACTION_RECEIVE | ACTION_STALL)) {
        // Done receiving a byte
        switch(state) {
        case STATE_READ_ADDRESS:
            if (byte_buf == BC_CMD_ENUMERATE) {
                state = STATE_ENUMERATE;
                action = ACTION_SEND | ACTION_STALL;
                next_byte = ID_OFFSET;
                bus_addr = 0;
            } else if (byte_buf == bus_addr) {
                // We're addressed, find out what the master wants
                action = ACTION_RECEIVE;
                state = STATE_READ_COMMAND;
                byte_buf = 0;
                next_bit = 1;
                pulse(PINB0);
            } else {
                // We're not addressed, stop paying attention
                action = ACTION_IDLE;
                state = STATE_IDLE;
            }
            break;
        case STATE_READ_COMMAND:
            switch (byte_buf) {
                case CMD_READ_EEPROM:
                    action = ACTION_RECEIVE;
                    state = STATE_READ_EEPROM_ADDR;
                    byte_buf = 0;
                    next_bit = 1;
                    break;
                case CMD_WRITE_EEPROM:
                    state = STATE_WRITE_EEPROM_ADDR;
                    action = ACTION_RECEIVE;
                    byte_buf = 0;
                    next_bit = 1;
                    break;
                default:
                    // Unknown command
                    action = ACTION_IDLE;
                    state = STATE_IDLE;
                    break;
            }
            break;
        case STATE_READ_EEPROM_ADDR:
            next_byte = byte_buf;
            next_bit = 1;
            action = ACTION_SEND | ACTION_STALL;
            state = STATE_READ_EEPROM_READ;
            break;
        case STATE_WRITE_EEPROM_ADDR:
            next_byte = byte_buf;
            next_bit = 1;
            byte_buf = 0;
            action = ACTION_RECEIVE;
            state = STATE_WRITE_EEPROM_WRITE;
            break;
        case STATE_WRITE_EEPROM_WRITE:
            pulse(PINB4);
            // Write the byte received, but refuse to write our id
            if (next_byte >= ID_OFFSET + ID_SIZE)
                EEPROM_write(next_byte, byte_buf);
            // Advance to the next byte (even when we refused to
            // write).
            next_byte++;
            next_bit = 1;
            byte_buf = 0;
            action = ACTION_RECEIVE;
            break;
        }
    }

    if (action == (ACTION_SEND | ACTION_STALL)) {
        switch(state) {
        case STATE_ENUMERATE:
            if (next_byte == ID_OFFSET + ID_SIZE) {
                // Entire address sent
                if (mute) {
                    // Another device had a lower id, so try again
                    // on the next round
                    next_byte = 0;
                    bus_addr++;
                    state = STATE_ENUMERATE;
                    mute = false;
                } else {
                    // We have the lowest id sent during this round,
                    // so claim the current bus address and stop
                    // paying attention
                    state = STATE_IDLE;
                    action = ACTION_IDLE;
                    break;
                }
            }
            /* FALLTHROUGH */
        case STATE_READ_EEPROM_READ:
            // Read and send next EEPROM byte
            byte_buf = EEPROM_read(next_byte);
            next_byte++;
            next_bit = 1;
            action = ACTION_SEND;
            break;
        }

    }

    cli();
    // Only sleep if the main loop isn't supposed to do anything, to
    // prevent deadlock. There's a magic dance here to make sure
    // an interrupt does not set the ACTION_STALL bit after we
    // checked for it but before entering sleep mode
    if (!(action & ACTION_STALL)) {
        // The instruction after sei is guaranteed to execute before
        // any interrupts are triggered, so we can be sure the sleep
        // mode is entered, with interrupts enabled, but before any
        // interrupts fire (possibly leaving it again directly if an
        // interrupt is pending).
        sei();
        sleep_cpu();
    }
    sei();
}

int main(void)
{
    setup();
    while(true)
        loop();
}

/* vim: set sw=4 sts=4 expandtab: */
