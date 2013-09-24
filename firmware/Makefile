SERIAL=/dev/ttyUSB0
PROGRAMMER=stk500
GCC_MCU=attiny13
AVRDUDE_PART=t13

-include Makefile.local

all: upload

backpack.elf: backpack.c Makefile
	avr-gcc -Wall -mmcu=$(GCC_MCU) -Os $< -o $@ -fwhole-program

backpack.hex: backpack.elf
	avr-objcopy -O ihex $< $@

upload: backpack.hex
	avrdude -c $(PROGRAMMER) -p $(AVRDUDE_PART) -P $(SERIAL) -U flash:w:$<

dump: backpack.elf
	avr-objdump -D $< | less

size: backpack.elf
	avr-size -C $<

# Output assembly with addresses removed for easy diffing
diffdump: backpack.elf
	avr-objdump -D $< | sed 's/^ *[0-9a-f]*//' | sed -r 's/([0-9a-f]{2} )*\s*((brne|rjmp|rcall|breq)\s*).*/...\t\t\2.../'
