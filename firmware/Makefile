SERIAL=/dev/ttyUSB0
PROGRAMMER=stk500
GCC_MCU=attiny13
AVRDUDE_PART=t13
CFLAGS=-Wall -mmcu=$(GCC_MCU) -Os

# Don't use jump tables, they make the code bigger
CFLAGS+=-fno-jump-tables

-include Makefile.local

all: upload

backpack.S: backpack.c Makefile
	avr-gcc -S $(CFLAGS) $< -o $@ -fwhole-program

backpack.elf: backpack.c Makefile
	avr-gcc -g $(CFLAGS) $< -o $@ -fwhole-program

backpack.hex: backpack.elf
	avr-objcopy -O ihex $< $@

upload: backpack.hex
	avrdude -c $(PROGRAMMER) -p $(AVRDUDE_PART) -P $(SERIAL) -U flash:w:$<

dump: backpack.elf
	avr-objdump -S $< | less

size: backpack.elf
	avr-size -C $<

read_eeprom:
	avrdude -c $(PROGRAMMER) -p $(AVRDUDE_PART) -P $(SERIAL) -V -U eeprom:r:-:h

# Output assembly with addresses removed for easy diffing
diffdump: backpack.elf
	avr-objdump -S $< | sed 's/^ *[0-9a-f]*://' | sed -r 's/([0-9a-f]{2} )*\s*((brne|rjmp|rcall|breq)\s*).*/...\t\t\2.../'
