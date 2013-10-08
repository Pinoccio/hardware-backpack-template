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

// 4.8Mhz oscillator with CKDIV8 fuse set
#define F_CPU (4800000/8)

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/cpufunc.h>
#include <avr/sleep.h>
#include <util/delay.h>
#include <stdbool.h>


// Debug macro, generate a short pulse on the given pin (B0, B2 or B4)
#define pulse(pin) \
    PORTB &= ~(1 << pin); \
    _NOP(); _NOP(); _NOP(); \
    PORTB |= (1 << pin);

// Helper macro
#define EXPAND_AND_STRINGIFY(x) __STRINGIFY(x)
#define EXPAND_AND_CONCAT(x, y) __CONCAT(x, y)

// Offset of the unique ID within the EEPROM
uint8_t const ID_SIZE = 4;
// Size of the unique ID
uint8_t const ID_OFFSET = 0;

#define US_TO_CLOCKS(x) (unsigned long)(x * F_CPU / 8 / 1000000)
#define RESET_SAMPLE US_TO_CLOCKS(1400)
//#define START_DELAY US_TO_CLOCKS(32)
#define DATA_WRITE US_TO_CLOCKS(600)
#define DATA_SAMPLE US_TO_CLOCKS(300)

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
enum {
    // These are the actual action values, which define the action to
    // take for the current bit

    AV_IDLE = 0x0,
    AV_SEND = 0x1,
    AV_RECEIVE = 0x2,
    AV_SEND_NACK = 0x3,
    AV_SEND_PARITY = 0x4,
    AV_SEND_ACK = 0x5,
    AV_CHECK_PARITY = 0x6,
    AV_READY = 0x7,
    AV_STALL = 0x8,

    // Mask for the action global to get one of the above values
    ACTION_MASK = 0xf,

    // These are action flags that can be combined with the above values
    AF_SAMPLE = 0x80,
    AF_LINE_LOW = 0x40,

    // These are the complete values (action value plus any relevant
    // action flags). The action global variable is always set to one of
    // these values.
    ACTION_IDLE = AV_IDLE,
    ACTION_STALL = AV_STALL,
    ACTION_SEND = AV_SEND,
    ACTION_SEND_HIGH = AV_SEND,
    ACTION_SEND_LOW = AV_SEND | AF_LINE_LOW,
    ACTION_SEND_HIGH_CHECK_COLLISION = AV_SEND | AF_SAMPLE,
    ACTION_RECEIVE = AV_RECEIVE | AF_SAMPLE,
    ACTION_CHECK_PARITY = AV_CHECK_PARITY | AF_SAMPLE,
    ACTION_ACK_LOW = AV_SEND_ACK | AF_LINE_LOW,
    ACTION_ACK_HIGH = AV_SEND_ACK,
    ACTION_NACK_LOW = AV_SEND_NACK | AF_LINE_LOW,
    ACTION_NACK_HIGH = AV_SEND_NACK,
    ACTION_SEND_PARITY_HIGH = AV_SEND_PARITY,
    ACTION_SEND_PARITY_LOW = AV_SEND_PARITY | AF_LINE_LOW,
    ACTION_READY = AV_READY,
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

enum {
    FLAG_MUTE = 1,
    // The (odd) parity bit for all bits sent or received so far
    FLAG_PARITY = 2,
    // Does the current byte need parity?
    FLAG_USE_PARITY = 4,
    FLAG_CHECK_COLLISION = 8,
    FLAG_ACK_LOW = 16,
    FLAG_SEND = 32,
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
register uint8_t flags asm("r6");

register uint8_t action asm("r7");
register uint8_t state asm("r8");

register uint8_t timera_action asm("r9");

// Register that is used by TIM0_COMPA_vect() and
// TIM0_COMPA_vect_do_work() to pass on the sampled value. This needs to
// happen in a global variable, since we can't clobber any other
// registers in an ISR
register uint8_t sample_val asm("r16");

// Register that is _always_ zero (unlike r1 which is intended to be
// usually zero, but might be changed by some instructions so can't be
// relied upon to be zero in an interrupt handler
register uint8_t global_zero asm("r17");

#define INT0_vect_do_work EXPAND_AND_CONCAT(INT0_vect, _do_work)

// This is the naked ISR that is called on INT0 interrupts. It
// immediately clears the TCNT0 register so the time from the interrupt
// to the timer restart (and thus timer compare match) becomes
// deterministic. If we would do this in a normal ISR, there would be a
// delay depending on the length of the prologue generated by the
// compiler (which would again depend on the number of registers used,
// and thus saved, in the ISR).
ISR(INT0_vect, ISR_NAKED) {
    // Reset the TCNT0 register.
    asm("out %0, %1" : : "I"(_SFR_IO_ADDR(TCNT0)), "r"(global_zero));

    // Jump to INT0_vect_do_work, which will handle the real saving of
    // registers
    asm("rjmp " EXPAND_AND_STRINGIFY(INT0_vect_do_work));
}

// This is the ISR that does the real work, after the actual interrupt
// handler above has reset TCNT0.
ISR(INT0_vect_do_work)
{
    // Note that the falling edge interrupt is _always_ enabled, so if a
    // falling edge occurs before the previous bit period is processed (e.g.
    // before the timer interrupt fired), then the previous period is
    // effectively ignored. This can only happen when a device violates the
    // protocol.

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

    if (action == ACTION_SEND) {
        if (!(byte_buf & next_bit)) {
            action = ACTION_SEND_LOW;
        } else if (flags & FLAG_CHECK_COLLISION) {
            action = ACTION_SEND_HIGH_CHECK_COLLISION;
        } else {
            action = ACTION_SEND_HIGH;
            flags ^= FLAG_PARITY;
        }
    }

    /* Don't bother doing either of these when we're muted */
    if (flags & FLAG_MUTE)
        action &= ~(AF_LINE_LOW | AF_SAMPLE);

    if ((action & AF_LINE_LOW)) {
        // Pull the line low and schedule a timer to release it again
        OCR0A = DATA_WRITE;
        DDRB |= (1 << PINB1);
        TIMSK0 |=  (1 << OCIE0A);
    } else if (action & AF_SAMPLE) {
        // Schedule a timer to sample the line
        OCR0A = DATA_SAMPLE;
        TIMSK0 |=  (1 << OCIE0A);
    } else {
        // If the only thing that needs to happen in the timer handler
        // is advancing to the next action, we might as well do it right
        // away (note that we can usually _not_ do it right away). We
        // don't want duplicate code, so we pretend the timer interrupt
        // happens directly.
        // We use an assembly call here, because when the compiler sees
        // a regular call, this causes this ISR (the caller) to save
        // _all_ call-clobbered registers, in case the called function
        // might actually clobber them. In this case, this is completely
        // bogus, since we're calling a signal handler which will save
        // everything it touches already (which also means that hiding
        // this function call from the compiler is safe).
        // Note that the obvious alternative (having a next_action()
        // function and call it from both signal handlers has the same
        // problem. We could declare it as a signal handler and do the
        // same trick with an asm call, but that still causes quite some
        // overhead because the function will unconditionally save r0,
        // r1 and the status register, even when it doesn't change it.
        asm("rcall " EXPAND_AND_STRINGIFY(TIM0_COMPA_vect));
    }
}

#define TIM0_COMPA_vect_do_work EXPAND_AND_CONCAT(TIM0_COMPA_vect, _do_work)

// This is the naked ISR that is called on TIM0_COMPA interrupts. It
// immediately releases the bus and takes a sample of the bus, since the
// timer fires to do either of those (and doing the other even when it's
// not needed doesn't hurt).
// If we would do this in a normal ISR, there would be a delay depending
// on the length of the prologue generated by the compiler (which would
// again depend on the number of registers used, and thus saved, in the
// ISR).
ISR(TIM0_COMPA_vect, ISR_NAKED)
{
    // Release bus
    asm("cbi %0, %1" : : "I"(_SFR_IO_ADDR(DDRB)), "I"(PINB1));
    // Sample bus (and all other pins at the same time). Use a global
    // register variable to store the value, so we don't need to save
    // some register's value and restore it below...
    asm("in %0, %1" : "=r"(sample_val) : "I"(_SFR_IO_ADDR(PINB)));

    // Jump to TIM0_COMPA_vect_do_work, which will handle the real saving of
    // registers
    asm("rjmp " EXPAND_AND_STRINGIFY(TIM0_COMPA_vect_do_work));
}

ISR(TIM0_COMPA_vect_do_work)
{
    uint8_t parity, val;

    switch (action & ACTION_MASK) {
    case AV_RECEIVE:
        // Read and store bit value
        if (sample_val & (1 << PINB1)) {
            byte_buf |= next_bit;
            flags ^= FLAG_PARITY;
        }
        next_bit <<= 1;
        if (!next_bit) {
            if (flags & FLAG_USE_PARITY)
                action = ACTION_CHECK_PARITY;
            else
                action = ACTION_STALL;
        }
        break;
    case AV_CHECK_PARITY:
        parity = (flags & FLAG_PARITY);
        val = sample_val & (1 << PINB1);
        if ((val != 0 && parity != 0) || (val == 0 && parity == 0)) {
            if (flags & FLAG_ACK_LOW)
                action = ACTION_ACK_LOW;
            else
                action = ACTION_ACK_HIGH;
        } else {
            if (flags & FLAG_ACK_LOW)
                action = ACTION_NACK_HIGH;
            else
                action = ACTION_NACK_LOW;
        }
        break;
    case AV_SEND:
        if ((action & AF_SAMPLE) && !(sample_val & (1 << PINB1))) {
            // We're sending our address, but are not currently pulling the
            // line low. Check if the line is actually high. If not, someone
            // else is pulling the line low, so we drop out of the current
            // address sending round.
            flags |= FLAG_MUTE;
        }

        next_bit <<= 1;
        action = AV_SEND;
        if (!next_bit) {
            if (flags & FLAG_USE_PARITY) {
                if (flags & FLAG_PARITY)
                    action = ACTION_SEND_PARITY_HIGH;
                else
                    action = ACTION_SEND_PARITY_LOW;
            } else {
                action = ACTION_STALL;
            }
        }
        break;
    case AV_SEND_PARITY:
        action = ACTION_STALL;
        break;
    case AV_SEND_ACK:
        action = ACTION_STALL;
        break;
    case AV_SEND_NACK:
        action = ACTION_IDLE;
        break;

    case AV_READY:
        if (flags & FLAG_SEND) {
            action = ACTION_SEND;
        } else {
            action = ACTION_RECEIVE;
        }
        break;
    }

    // Disable this timer interrupt
    TIMSK0 &= ~(1 << OCIE0A);
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
        flags = 0;
    } else {
        set_sleep_mode(SLEEP_MODE_PWR_DOWN);

        // Make INT0 low-level triggered (note that this assumes ISC00
        // is not set)
        MCUCR &= ~(1<<ISC01);
    }

    // Disable this timer interrupt
    TIMSK0 &= ~(1 << OCIE0B);
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
    global_zero = 0;
    bus_addr = 0xff;
    flags &= ~FLAG_MUTE;
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
    if (action == ACTION_STALL) {
        // Done receiving or sending a byte
        switch(state) {
        case STATE_READ_ADDRESS:
            if (byte_buf == BC_CMD_ENUMERATE) {
                state = STATE_ENUMERATE;
                flags |= FLAG_CHECK_COLLISION;
                flags |= FLAG_SEND;
                // Don't change out of STALL, let the next iteration
                // prepare the first byte
                next_byte = ID_OFFSET;
                bus_addr = 0;
            } else if (byte_buf == bus_addr) {
                // We're addressed, find out what the master wants
                action = ACTION_READY;
                flags &= ~FLAG_SEND;
                state = STATE_READ_COMMAND;
                byte_buf = 0;
                next_bit = 1;
                // Enable parity for every byte after this one
                flags |= FLAG_USE_PARITY;
                // Use low ack and high nack bits from now on
                flags |= FLAG_ACK_LOW;
                // Odd parity over zero bits is 1
                flags |= FLAG_PARITY;
            } else {
                // We're not addressed, stop paying attention
                action = ACTION_IDLE;
                state = STATE_IDLE;
            }
            break;
        case STATE_READ_COMMAND:
            switch (byte_buf) {
                case CMD_READ_EEPROM:
                    action = ACTION_READY;
                    flags &= ~ACTION_SEND;
                    state = STATE_READ_EEPROM_ADDR;
                    byte_buf = 0;
                    next_bit = 1;
                    // Odd parity over zero bits is 1
                    flags |= FLAG_PARITY;
                    break;
                case CMD_WRITE_EEPROM:
                    state = STATE_WRITE_EEPROM_ADDR;
                    action = ACTION_READY;
                    flags &= ~FLAG_SEND;
                    byte_buf = 0;
                    next_bit = 1;
                    // Odd parity over zero bits is 1
                    flags |= FLAG_PARITY;
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
            // Odd parity over zero bits is 1
            flags |= FLAG_PARITY;
            flags |= FLAG_SEND;
            // Don't change out of STALL, let the next iteration
            // prepare the first byte
            state = STATE_READ_EEPROM_READ;
            break;
        case STATE_WRITE_EEPROM_ADDR:
            next_byte = byte_buf;
            next_bit = 1;
            byte_buf = 0;
            // Odd parity over zero bits is 1
            flags |= FLAG_PARITY;
            flags &= ~FLAG_SEND;
            action = ACTION_READY;
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
            // Odd parity over zero bits is 1
            flags |= FLAG_PARITY;
            action = ACTION_READY;
            break;
        case STATE_ENUMERATE:
            if (next_byte == ID_OFFSET + ID_SIZE) {
                // Entire address sent
                if (flags & FLAG_MUTE) {
                    // Another device had a lower id, so try again
                    // on the next round
                    next_byte = 0;
                    bus_addr++;
                    flags &= ~FLAG_MUTE;
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
            // Odd parity over zero bits is 1
            flags |= FLAG_PARITY;
            if (state == STATE_ENUMERATE)
                action = ACTION_SEND;
            else
                action = ACTION_READY;
            break;
        }

    }

    cli();
    // Only sleep if the main loop isn't supposed to do anything, to
    // prevent deadlock. There's a magic dance here to make sure
    // an interrupt does not set the action to ACTION_STALL after we
    // checked for it but before entering sleep mode
    if (action != ACTION_STALL) {
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
