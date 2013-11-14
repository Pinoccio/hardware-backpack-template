# vim: set sw=4 sts=4 et fileencoding=utf-8

from bitstring import Bits, BitArray, pack
import math

from eeprom import minifloat

class Descriptor:
    def effective_name(self):
        """
        Return the name for this descriptor.
        Returns the default if no name was set.
        """
        try:
            name = self.name
        except AttributeError:
            # This descriptor does not support a name
            return None
        if not name:
            try:
                return getattr(self.__class__, 'default_name')
            except AttributeError:
                raise ValueError("Name cannot be empty for {}".format(self.__class__.__name__))
        return self.name

    def append_minifloat(self, eeprom, data, format, unit, value):
        if value is None:
            e = s = 0
        else:
            (e, s, rounded) = format.encode(value)
            if value != rounded:
                eeprom.roundings.append("{0} {2} to {1} {2}".format(value, rounded, unit))
        data.append(pack('uint:n', e, n = format.ebits))
        data.append(pack('uint:n', e, n = format.sbits))

class SpiSlaveDescriptor(Descriptor):
    descriptor_type = 0x1
    default_name = "spi"
    speed_format = minifloat.MinifloatFormat(4, 4, 6, math.floor)
    speed_unit = 'Mhz'

    def __init__(self, speed, ss_pin, name = ""):
        self.name = name
        self.speed = speed
        self.ss_pin = ss_pin

    def encode(self, eeprom, data):
        data.append(pack('uint:8', self.descriptor_type))
        data.append(pack('bool', bool(self.name)))
        data.append(pack('pad:1')) # reserved
        data.append(pack('uint:6', self.ss_pin))
        self.append_minifloat(eeprom, data, self.speed_format, self.speed_unit, self.speed)
        eeprom.append_string(data, self.name)

class UartDescriptor(Descriptor):
    descriptor_type = 0x2
    default_name = "uart"

    # List of UART speeds and minimal layout version required.
    # List index is the encoded value.
    speeds = [
        (None, 1),
        (300, 1),
        (600, 1),
        (1200, 1),
        (2400, 1),
        (4800, 1),
        (9600, 1),
        (19200, 1),
        (38400, 1),
        (57600, 1),
        (115200, 1),
    ]

    def __init__(self, rx_pin, tx_pin, speed, name = ""):
        self.name = name
        self.rx_pin = rx_pin
        self.tx_pin = tx_pin
        self.speed = speed

    def encoded_speed(self, eeprom):
        for (i, (speed, version)) in enumerate(self.speeds):
            if (self.speed == speed):
                if (eeprom.layout_version >= version):
                    return i
                else:
                    raise ValueError("UART speed of {} requires layout version {}".format(speed, version))

        raise ValueError("Unsupported UART speed {}".format(self.speed))

    def encode(self, eeprom, data):
        data.append(pack('uint:8', self.descriptor_type))
        data.append(pack('pad:2')) # reserved
        data.append(pack('uint:6', self.tx_pin))
        data.append(pack('pad:2')) # reserved
        data.append(pack('uint:6', self.rx_pin))
        data.append(pack('bool', bool(self.name)))
        data.append(pack('pad:3')) # reserved
        data.append(pack('uint:4', self.encoded_speed(eeprom)))
        eeprom.append_string(data, self.name)

class IOPinDescriptor(Descriptor):
    descriptor_type = 0x3

    def __init__(self, pin, name):
        self.name = name
        self.pin = pin

    def encode(self, eeprom, data):
        data.append(pack('uint:8', self.descriptor_type))
        data.append(pack('pad:2')) # reserved
        data.append(pack('uint:6', self.pin))
        eeprom.append_string(data, self.name)

class PowerUsageDescriptor(Descriptor):
    descriptor_type = 0x5
    usage_format = minifloat.MinifloatFormat(4, 4, -4, math.ceil)
    usage_unit = 'μA'

    def __init__(self, pin, minimum, typical, maximum):
        self.pin = pin
        self.minimum = minimum
        self.typical = typical
        self.maximum = maximum

    def encode(self, eeprom, data):
        data.append(pack('uint:8', self.descriptor_type))
        data.append(pack('pad:2')) # reserved
        data.append(pack('uint:6', self.pin))
        self.append_minifloat(eeprom, data, self.usage_format, self.usage_unit, self.minimum)
        self.append_minifloat(eeprom, data, self.usage_format, self.usage_unit, self.typical)
        self.append_minifloat(eeprom, data, self.usage_format, self.usage_unit, self.maximum)

class EmptyDescriptor(Descriptor):
    descriptor_type = 0xff

    def __init__(self, length):
        self.length = length

    def encode(self, eeprom, data):
        # Just output the descriptor_type length times
        for _ in range(self.length):
            data.append(pack('uint:8', self.descriptor_type))

class GroupDescriptor(Descriptor):
    descriptor_type = 0x4

    def __init__(self, name, descriptors):
        self.name = name
        self.descriptors = descriptors

    def encode(self, eeprom, data):
        eeprom.offsets[data.len // 8] = "Group " + self.name
        data.append(pack('uint:8', self.descriptor_type))
        eeprom.append_string(data, self.name)

        for d in self.descriptors:
            eeprom.offsets[data.len // 8] = d.__class__.__name__ + " " + (d.effective_name() or "")
            d.encode(eeprom, data)
