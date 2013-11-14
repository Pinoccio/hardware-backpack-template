# vim: set sw=4 sts=4 et fileencoding=utf-8

from eeprom import EEPROM
from eeprom.pins import ScoutV1Pins
from eeprom.descriptors import *

# Use the pins names as they are on the V1.0 Pinoccio scout
pins = ScoutV1Pins

# This defines an example EEPROM contents for the wifi backpack (not
# final data yet).
#
# Should this perhaps be defined in an ini file instead of executable
# python code?
contents = EEPROM(
    layout_version        = 1,
    eeprom_size           = 64,
    bus_protocol_version  = 1,
    model                 = 0xabcd,
    serial                = 1,
    hardware_revision     = 3,
    firmware_version      = 1,
    name                  = "wifi",
    groups = [
        GroupDescriptor(
            name = "wifi",
            descriptors = [
                SpiSlaveDescriptor(
                    ss_pin = pins.D7,
                    speed = None, # TODO
                ),

                UartDescriptor(
                    rx_pin = pins.TX1,
                    tx_pin = pins.RX1,
                    speed = 115200,
                ),

                IOPinDescriptor(
                    name   = "pgm",
                    pin    = pins.D6,
                ),
                PowerUsageDescriptor(
                    pin     = pins.VBAT,
                    # TODO: These are dummy values
                    minimum = 0,
                    typical = 1,
                    maximum = 100,
                ),
            ],
        ),

        GroupDescriptor(
            name = "sd",
            descriptors = [
                SpiSlaveDescriptor(
                    ss_pin = pins.D8,
                    speed = None, # TODO
                ),
                PowerUsageDescriptor(
                    pin     = pins.V33,
                    # TODO: These are dummy values
                    minimum = 0,
                    typical = 1,
                    maximum = 100,
                ),
            ],
        ),

        GroupDescriptor(
            name = "eep",
            descriptors = [
                SpiSlaveDescriptor(
                    ss_pin = pins.SS,
                    # TODO, is this really 33Mhz?
                    speed = 33,
                ),
                PowerUsageDescriptor(
                    pin     = pins.V33,
                    # TODO: These are dummy values
                    minimum = 0,
                    typical = 1,
                    maximum = 100,
                ),
            ],
        ),
    ]
)

# Print offset: binary hex
def pretty(bs, eeprom):
    for i, byte in enumerate(bs.cut(8)):
        if i in eeprom.offsets:
            print(eeprom.offsets[i])

        val = byte.uint
        out = "     {0:02x}: {1:08b} {1:02x}".format(i, val)
        val &= 0x7f
        if (val >= 0x20 and val < 0x7f):
            out += " \"{}\"".format(chr(val))
        print(out)

encoded, roundings = contents.encode()
pretty(encoded, contents)
if (roundings):
    print("Roundings:")
    print("\n".join(roundings))
