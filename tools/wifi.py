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
    groups = [
        GroupDescriptor(
            name = "wifi",
            descriptors = [
                SpiSlaveDescriptor(
                    ss_pin = pins.D7,
                    CPOL = 0,
                    CPHA = 0,
                    lsb_first = 0,
                ),

                UartDescriptor(
                    rx_pin = pins.TX1,
                    tx_pin = pins.RX1,
                    speed = 115200,
                ),

                IOPinDescriptor(
                    # TODO: Better / shorter name?
                    name   = "upgrade",
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
                    CPOL = 0,
                    CPHA = 0,
                    lsb_first = 0,
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
                    CPOL = 0,
                    CPHA = 0,
                    lsb_first = 0,
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
def pretty(bs):
    for i, byte in enumerate(bs.cut(8)):
        val = byte.uint
        out = "{0:02x}: {1:08b} {1:02x}".format(i, val)
        val &= 0x7f
        if (val >= 0x20 and val < 0x7f):
            out += " \"{}\"".format(chr(val))
        print(out)

pretty(contents.encode())
