SERIAL=/dev/ttyUSB0
PROGRAMMER=stk500
GCC_MCU=attiny13
AVRDUDE_PART=t13

-include Makefile.local

all:
	avr-gcc -mmcu=$(GCC_MCU) -Os backpack.c -o backpack.elf -fwhole-program
	avr-objcopy -O ihex backpack.elf backpack.hex
	avrdude -c $(PROGRAMMER) -p $(AVRDUDE_PART) -P $(SERIAL) -U flash:w:backpack.hex

dump:
	avr-objdump -D backpack.elf | less
