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
//
// TODO:
//  - Decide on brownout detection and watchdog timer
//  - In theory, a reset could happen when the main loop is processing
//    something. Now, the main loop will happily overwrite the state set
//    by the reset detection, but this should somehow be prevented
//    (perhaps set just a reset flag in the OVF ISR and let the main
//    loop do all of the other setup?)
//  - Overview documentation

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

// Offset of the unique ID within the EEPROM
uint8_t const ID_SIZE = 4;
// Size of the unique ID
uint8_t const ID_OFFSET = 0;

// Protocol timings
#define US_TO_CLOCKS(x) (unsigned long)(x * F_CPU / 8 / 1000000)
#define RESET_SAMPLE US_TO_CLOCKS(1400)
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
    AV_ACK1 = 0x3,
    AV_ACK2 = 0x4,
    AV_NACK1 = 0x5,
    AV_NACK2 = 0x6,
    AV_READY = 0x7,
    AV_STALL = 0x8,

    // Mask for the action global to get one of the above values
    ACTION_MASK = 0xf,

    // These are action flags that can be combined with the above values

    // This action needs to sample the bit value
    AF_SAMPLE = 0x80,
    // This action needs to pull the line low
    AF_LINE_LOW = 0x40,
    // When FLAG_MUTE is set, the AF_SAMPLE and AF_LINE_LOW bits should
    // be ignored for this bit
    AF_MUTE = 0x20,

    // These are the complete values (action value plus any relevant
    // action flags). The action global variable is always set to one of
    // these values.
    ACTION_IDLE = AV_IDLE,
    ACTION_STALL = AV_STALL | AF_LINE_LOW,
    ACTION_SEND = AV_SEND,
    ACTION_SEND_HIGH = AV_SEND | AF_MUTE,
    ACTION_SEND_LOW = AV_SEND | AF_LINE_LOW | AF_MUTE,
    ACTION_SEND_HIGH_CHECK_COLLISION = AV_SEND | AF_SAMPLE | AF_MUTE,
    ACTION_RECEIVE = AV_RECEIVE | AF_SAMPLE,
    ACTION_ACK1 = AV_ACK1 | AF_LINE_LOW | AF_MUTE,
    ACTION_ACK2 = AV_ACK2 | AF_MUTE,
    ACTION_NACK1 = AV_NACK1 | AF_MUTE,
    ACTION_NACK2 = AV_NACK2 | AF_LINE_LOW | AF_MUTE,
    ACTION_READY = AV_READY | AF_SAMPLE,
};

// Constants for the current state for the high level protocol handler
enum {
    // Idle - Waiting for the next reset to participate (again)
    STATE_IDLE,
    // Bus was reset, receiving the address byte (or broadcast command)
    STATE_RECEIVE_ADDRESS,
    // BC_CMD_ENUMERATE received, bus enumeration in progress
    STATE_ENUMERATE,
    // We are adressed, receiving targeted command
    STATE_RECEIVE_COMMAND,
    // CMD_READ_EEPROM received, now receiving read address
    STATE_READ_EEPROM_RECEIVE_ADDR,
    // CMD_READ_EEPROM and read address received, now reading
    STATE_READ_EEPROM_SEND_DATA,
    // CMD_WRITE_EEPROM received, now receiving write address
    STATE_WRITE_EEPROM_RECEIVE_ADDR,
    // CMD_WRITE_EEPROM and write address received, now writing
    STATE_WRITE_EEPROM_RECEIVE_DATA,
};

enum {
    // When this flag is set, this slave will no longer participate on
    // the bus, but it will still keep its state synchronized. This is
    // used during bus enumeration, when this slave has "lost" a
    // conflict and needs to wait out the current enumeration round
    // before trying to send its id again.
    FLAG_MUTE = 1,
    // The (odd) parity bit for all bits sent or received so far
    FLAG_PARITY = 2,
    // If this flag is set, the slave has completed bus enumeration
    // succesfully and the bus address in bus_addr is valid.
    FLAG_ENUMERATED = 4,
    // If this flag is set, during any high bits sent the slave will
    // check the bus for collision (e.g., when another slave is sending
    // a low bit). If collision is detected, FLAG_MUTE is set.
    FLAG_CHECK_COLLISION = 8,
    // After the ACK/NACK bit, switch to the send action and start
    // sending the byte in byte_buf. Only considered when FLAG_IDLE is
    // not set.
    FLAG_SEND = 32,
    // After the ACK/NACK bit, switch to idle and drop off the bus
    FLAG_IDLE = 64,
    // After sending the ACK/NACK bit, clear FLAG_MUTE and
    // FLAG_CLEAR_MUTE
    FLAG_CLEAR_MUTE = 128,
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

// The action to take for the next or current bit
register uint8_t action asm("r7");

// The higher level protocol state. Only valid when
// action != ACTION_IDLE.
register uint8_t state asm("r8");

// Register that is used by TIM0_COMPA_vect() and
// TIM0_COMPA_vect_do_work() to pass on the sampled value. This needs to
// happen in a global variable, since we can't clobber any other
// registers in an ISR
register uint8_t sample_val asm("r16");

// Register that contains the reset value for TCNT0. By putting this
// inside a register, the naked INT0 ISR can write it to TCNT0
// immediately, without having to mess with freeing up a register and
// loading the constant into it.
register uint8_t tcnt0_init asm("r17");

// This is the naked ISR that is called on INT0 interrupts. It
// immediately clears the TCNT0 register so the time from the interrupt
// to the timer restart (and thus timer compare match) becomes
// deterministic. If we would do this in a normal ISR, there would be a
// delay depending on the length of the prologue generated by the
// compiler (which would again depend on the number of registers used,
// and thus saved, in the ISR).
ISR(INT0_vect, ISR_NAKED) {
    // Reset the TCNT0 register.
    asm("out %0, %1" : : "I"(_SFR_IO_ADDR(TCNT0)), "r"(tcnt0_init));

    // Jump to the function that will do the real work. Since that is
    // declared as an ISR, it will also properly do all the register
    // saving required.
    asm("rjmp __vector_bit_start");
}

// Handle the start of a bit. Called by the INT0 ISR after resetting
// timer.
// Declared as an ISR so it will properly save all registers, to allow
// calling it from the real ISR. Name starts with __vector to fool gcc
// into not giving the "appears to be a misspelled signal handler"
// warning.
ISR(__vector_bit_start)
{
    // Note that the falling edge interrupt is _always_ enabled, so if a
    // falling edge occurs before the previous bit period is processed (e.g.
    // before the timer interrupt fired), then the previous period is
    // effectively ignored. This can only happen when a device violates the
    // protocol.

    // Clear any interrupt flags that might have been set while the
    // timer interrupts were disabled
    TIFR0 = (1 << OCF0B) | (1 << OCF0A) | (1 << TOV0);

    // Disable any pending timer interrupts from the previous bit, but
    // always enable the interrupt to detect a reset pulse
    TIMSK0 = (1 << TOIE0);

    // If we were powered-down, we'll have been set to a
    // level-triggered interrupt instead of an edge-triggered one,
    // since a edge-triggered one can wake us up. We'll need to set
    // it to edge-triggered to prevent being flooded with
    // interrupts.
    // Make INT0 falling edge-triggered (note that this assumes
    // ISC00 is not set)
    MCUCR |= (1<<ISC01);
    set_sleep_mode(SLEEP_MODE_IDLE);

    // Don't bother doing either of these when we're muted
    if ((flags & FLAG_MUTE) && (action & AF_MUTE))
        action &= ~(AF_LINE_LOW | AF_SAMPLE);

    if ((action & AF_LINE_LOW)) {
        // Pull the line low and enable a timer to release it again
        DDRB |= (1 << PINB1);
        TIMSK0 |=  (1 << OCIE0B);
    }

    if (action & AF_SAMPLE) {
        // Schedule a timer to sample the line
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
        // Note that the called ISR returns with reti instead of the
        // regulat ret, causing interrupts to be enabled in the process.
        // Since we're the last instruction in the function, this
        // shouldn't be a problem (and shouldn't happen in practice
        // anyway).
        // Finally, note that we don't call the real ISR, but the
        // function that does the work, skipping a few instructions and
        // skipping the bus sampling.
        asm("rcall __vector_sample");
        // Interrupts are enabled here!
    }
}

ISR(TIM0_COMPB_vect, ISR_NAKED)
{
    // Release bus
    asm("cbi %0, %1"   : : "I"(_SFR_IO_ADDR(DDRB)), "I"(PINB1));
    asm("reti");
}

// This is the naked ISR that is called on TIM0_COMPA interrupts. It
// immediately takes a sample of the bus.
// If we would do this in a normal ISR, there would be a delay depending
// on the length of the prologue generated by the compiler (which would
// again depend on the number of registers used, and thus saved, in the
// ISR).
ISR(TIM0_COMPA_vect, ISR_NAKED)
{
    // Sample bus (and all other pins at the same time). Use a global
    // register variable to store the value, so we don't need to save
    // some register's value and restore it below...
    asm("in %0, %1" : "=r"(sample_val) : "I"(_SFR_IO_ADDR(PINB)));

    // Jump to TIM0_COMPA_vect_do_work, which will handle the real saving of
    // registers
    asm("rjmp __vector_sample");
}

// Declared as an ISR so it will properly save all registers, allowing
// to call or jump to it from real ISRs. Name starts with __vector to
// fool gcc into not giving the "appears to be a misspelled signal
// handler" warning.
ISR(__vector_sample)
{
    switch (action & ACTION_MASK) {
    case AV_RECEIVE:
        // Read and store bit value
        if (sample_val & (1 << PINB1)) {
            // When reading the parity bit, next_bit is 0 and this is a
            // no-op
            byte_buf |= next_bit;
            // Toggle the parity flag on every 1 received, including the
            // parity bit
            flags ^= FLAG_PARITY;
        }

        if (next_bit) {
            next_bit >>= 1;
        } else {
            // Full byte and parity bit received
            if (flags & FLAG_PARITY) {
                // Parity is not ok, skip the STALL state and go
                // straight to ready (and NACK and IDLE after that)
                action = ACTION_READY;
                flags |= FLAG_IDLE;
            } else {
                // Parity is ok, let the mainloop decide what to do next
                action = ACTION_STALL;
            }
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

        if (!next_bit) {
            // Just sent the parity bit
            action = ACTION_STALL;
            break;
        }

        // Send next bit, or parity bit
        next_bit >>= 1;

        bool val;
prepare_next_bit:
        if (next_bit) {
            // Send the next bit
            val = (byte_buf & next_bit);
        } else {
            // next_bit == 0 means to send the parity bit
            val = (flags & FLAG_PARITY);
        }

        if (!val) {
            // Pull the line low
            action = ACTION_SEND_LOW;
        } else if (flags & FLAG_CHECK_COLLISION) {
            // Leave the line high, but check for collision
            action = ACTION_SEND_HIGH_CHECK_COLLISION;
            flags ^= FLAG_PARITY;
        } else {
            // Just leave the line high
            action = ACTION_SEND_HIGH;
            flags ^= FLAG_PARITY;
        }
        break;
    case AV_ACK1:
        action = ACTION_ACK2;
        break;
    case AV_NACK1:
        action = ACTION_NACK2;
        break;
    case AV_ACK2:
    case AV_NACK2:
        // Prepare for sending or receiving the next byte (or go to
        // idle, but next_bit won't matter anyway).
        flags &= ~FLAG_PARITY;
        next_bit = 0x80;

        // Clear FLAG_MUTE when requested
        if (flags & FLAG_CLEAR_MUTE)
            flags &= ~(FLAG_MUTE | FLAG_CLEAR_MUTE);

        // Decide upon the next action, IDLE, SEND or RECEIVE.
        if (flags & FLAG_IDLE) {
            action = ACTION_IDLE;
        } else if (flags & FLAG_SEND) {
            // Set up the first bit
            goto prepare_next_bit;
        } else {
            action = ACTION_RECEIVE;
            byte_buf = 0;
        }

        break;

    case AV_READY:
        // Sample the line to see if anyone else is perhaps stalling the
        // bus. If so, keep trying to send our ready bit until everyone
        // is ready.
        if (!(sample_val & (1 << PINB1)))
            break;

        if (!(flags & FLAG_PARITY)) {
            // Parity was ok
            action = ACTION_ACK1;
        } else {
            // Parity didn't check out when receiving, or main loop
            // intentionally broke it
            action = ACTION_NACK1;
        }

        break;
    }
}

ISR(TIM0_OVF_vect)
{
    uint8_t val = PINB & (1 << PINB1);
    if (!val) {
        // Bus is still low, this is a reset pulse (regardless of what
        // state we were in previously!)
        state = STATE_RECEIVE_ADDRESS;
        action = ACTION_RECEIVE;
        // These are normally initialized after sending the ack/nack
        // bit, but we're skipping that after a reset.
        byte_buf = 0;
        next_bit = 0x80;

        // Clear all flags, except for the enumeration status
        flags &= FLAG_ENUMERATED;
    } else {
        // Since it seems the bus is idle, let's power down instead of
        // only sleeping. Since we can only wake up from powerdown on a
        // low-level triggered interrupt, we can only go into powerdown
        // when the bus is high.
        set_sleep_mode(SLEEP_MODE_PWR_DOWN);

        // Make INT0 low-level triggered (note that this assumes ISC00
        // is not set)
        MCUCR &= ~(1<<ISC01);
    }

    // Disable all timer interrupts
    TIMSK0 = 0;
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
    action = ACTION_IDLE;
    // Set ports to output for debug
    PORTB = DDRB = (1 << PINB0) | (1 << PINB2) | (1 << PINB4);

    // On an INT0 interrupt, the counter is reset to tcnt_init, so it
    // overflows after RESET_SAMPLE ticks. OCR0A and OCR0B are set so
    // their interrupts fire after DATA_SAMPLE and DATA_RELEASE ticks
    // respectively
    tcnt0_init = (0xff - RESET_SAMPLE);
    OCR0B = tcnt0_init + DATA_WRITE;
    OCR0A = tcnt0_init + DATA_SAMPLE;

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
        case STATE_RECEIVE_ADDRESS:
            // Read the first byte after a reset, which is either a
            // broadcast command or a bus address
            if (byte_buf == BC_CMD_ENUMERATE) {
                state = STATE_ENUMERATE;
                flags |= FLAG_CHECK_COLLISION;
                flags |= FLAG_SEND;
                flags &= ~FLAG_ENUMERATED;
                next_byte = ID_OFFSET;
                bus_addr = 0;
                // Don't change out of STALL, let the next iteration
                // prepare the first byte
            } else if ((flags & FLAG_ENUMERATED) && byte_buf == bus_addr) {
                // We're addressed, find out what the master wants
                action = ACTION_READY;
                state = STATE_RECEIVE_COMMAND;
            } else {
                // We're not addressed, stop paying attention
                action = ACTION_IDLE;
            }
            break;
        case STATE_RECEIVE_COMMAND:
            // We were addressed and the master has just sent us a
            // command
            switch (byte_buf) {
                case CMD_READ_EEPROM:
                    state = STATE_READ_EEPROM_RECEIVE_ADDR;
                    action = ACTION_READY;
                    break;
                case CMD_WRITE_EEPROM:
                    state = STATE_WRITE_EEPROM_RECEIVE_ADDR;
                    action = ACTION_READY;
                    break;
                default:
                    // Unknown command
                    action = ACTION_IDLE;
                    break;
            }
            break;
        case STATE_READ_EEPROM_RECEIVE_ADDR:
            // We're running CMD_READ_EEPROM and just received the
            // EEPROM addres to read from
            next_byte = byte_buf;
            flags |= FLAG_SEND;
            state = STATE_READ_EEPROM_SEND_DATA;
            // Don't change out of STALL, let the next iteration
            // prepare the first byte
            break;
        case STATE_WRITE_EEPROM_RECEIVE_ADDR:
            // We're running CMD_WRITE_EEPROM and just received the
            // EEPROM address to write
            next_byte = byte_buf;
            state = STATE_WRITE_EEPROM_RECEIVE_DATA;
            action = ACTION_READY;
            break;
        case STATE_WRITE_EEPROM_RECEIVE_DATA:
            // Write the byte received, but refuse to write our id
            if (next_byte < ID_OFFSET || next_byte >= ID_OFFSET + ID_SIZE)
                EEPROM_write(next_byte, byte_buf);
            next_byte++;
            action = ACTION_READY;
            break;
        case STATE_ENUMERATE:
            if (next_byte == ID_OFFSET + ID_SIZE) {
                // Entire address sent
                if (flags & FLAG_MUTE) {
                    // Another device had a lower id, so try again
                    // on the next round
                    next_byte = ID_OFFSET;
                    bus_addr++;
                    // Stop muting _after_ sending the ack/nack bit for
                    // the current (last) byte
                    flags |= FLAG_CLEAR_MUTE;
                } else {
                    // We have the lowest id sent during this round,
                    // so claim the current bus address and stop
                    // paying attention
                    state = STATE_IDLE;
                    flags |= FLAG_IDLE;
                    flags |= FLAG_ENUMERATED;
                    action = ACTION_READY;
                    break;
                }
            }
            // FALLTHROUGH
        case STATE_READ_EEPROM_SEND_DATA:
            // Read and send next EEPROM byte (but don't bother while
            // we're muted)
            if (!(flags & FLAG_MUTE) || (flags & FLAG_CLEAR_MUTE))
                byte_buf = EEPROM_read(next_byte);
            next_byte++;

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
