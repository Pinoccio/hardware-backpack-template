SERIAL=/dev/ttyUSB0
PROGRAMMER=stk500
GCC_MCU=attiny13a
CRT_FILE=crttn13a.o
AVRDUDE_PART=t13
CFLAGS=-Wall -mmcu=$(GCC_MCU) -Os

ifeq ($(shell avr-gcc $(CFLAGS) --print-file-name $(CRT_FILE)),$(CRT_FILE))
$(error "Gcc couldn't find $(CRT_FILE), you probably haven't applied the /usr/lib/avr/lib/avr25/tiny-stack workaround suggested in backpack.c? Short version: run ln -s /usr/lib/avr/lib/avr25 /usr/lib/avr/lib/avr25/tiny-stack")
endif

# Don't use jump tables, they make the code bigger
CFLAGS+=-fno-jump-tables

-include Makefile.local

all: upload

backpack.S: backpack.c protocol.h Makefile
	avr-gcc -S $(CFLAGS) $< -o $@ -fwhole-program

backpack.elf: backpack.c protocol.h Makefile
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

# Write a random unique ID to eeprom. Variables can be override on the
# commandline, e.g. make eeprom_id MODEL=0003
eeprom_id: PROTO_VERSION:=01
eeprom_id: MODEL:=$(shell head --bytes=2 /dev/urandom |hexdump -v -e '/1 "%02X"')
eeprom_id: SERIALNO:=$(shell head --bytes=3 /dev/urandom |hexdump -v -e '/1 "%02X"')
eeprom_id: REVISION:=$(shell head --bytes=1 /dev/urandom |hexdump -v -e '/1 "%02X"')
eeprom_id: ID:=$(PROTO_VERSION)$(MODEL)$(REVISION)$(SERIALNO)$(CHECKSUM)
eeprom_id: CHECKSUM:=$(shell printf "%02x" $$(pycrc --width=8 --poly=0x2f --xor-in=0 --reflect-in=false --reflect-out=false --xor-out=0 --check-hexstring $(ID)))
eeprom_id: DATA=$(shell echo $(ID)$(CHECKSUM) | tr '[:upper:]' '[:lower:]' | sed 's/../0x& /g')
eeprom_id:
	avrdude -c $(PROGRAMMER) -p $(AVRDUDE_PART) -P $(SERIAL) -V -U "eeprom:w:$(DATA):m"


# Output assembly with addresses removed for easy diffing
diffdump: backpack.elf
	avr-objdump -S $< | sed 's/^[0-9a-f]* </</' | sed 's/^ *[0-9a-f]*://' | sed -r 's/([0-9a-f]{2} )*\s*((brne|rjmp|rcall|breq|brcc|brlt)\s*).*/...\t\t\2.../'
