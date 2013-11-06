from bitstring import Bits, BitArray

class Descriptor:
    def effective_name(self):
        """
        Return the name for this descriptor.
        Returns the default if no name was set.
        """

        if not self.name:
            try:
                return getattr(self.__class__, 'default_name')
            except AttributeError:
                raise ValueError("Name cannot be empty for {}".format(self.__class__.__name__))
        return self.name

class SpiSlaveDescriptor(Descriptor):
    descriptor_type = 0x1
    default_name = "spi"

    def __init__(self, ss_pin, name = ""):
        self.name = name
        self.ss_pin = ss_pin

    def encode(self, eeprom, data):
        data.append(Bits(uint=self.descriptor_type, length = 8))
        data.append(Bits(uint=0, length = 3)) # reserved
        data.append(Bits(uint=self.ss_pin, length = 5))
        data.append(Bits(uint=len(self.name), length = 4))
        data.append(Bits(uint=0, length = 4)) # reserved
        data.append(Bits(bytes=self.name.encode('ascii')))

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
        data.append(Bits(uint=self.descriptor_type, length = 8))
        data.append(Bits(uint=0, length = 3)) # reserved
        data.append(Bits(uint=self.tx_pin, length = 5))
        data.append(Bits(uint=0, length = 3)) # reserved
        data.append(Bits(uint=self.rx_pin, length = 5))
        data.append(Bits(uint=len(self.name), length = 4))
        data.append(Bits(uint=self.encoded_speed(eeprom), length = 4))
        data.append(Bits(bytes=self.name.encode('ascii')))

class IOPinDescriptor(Descriptor):
    descriptor_type = 0x3

    # Usage types:
    MISC = 0

    def __init__(self, pin, usage, name):
        self.name = name
        self.pin = pin
        self.usage = usage

    def encode(self, eeprom, data):
        data.append(Bits(uint=self.descriptor_type, length = 8))
        data.append(Bits(uint=0, length = 3)) # reserved
        data.append(Bits(uint=self.pin, length = 5))
        data.append(Bits(uint=len(self.name), length = 4))
        data.append(Bits(uint=0, length = 4)) # reserved
        data.append(Bits(bytes=self.name.encode('ascii')))

class GroupDescriptor(Descriptor):
    descriptor_type = 0x4
    # Only valid for the first group
    default_name = ""

    def __init__(self, name, descriptors):
        self.name = name
        self.descriptors = descriptors

    def encode(self, eeprom, data):
        if self.name:
            data.append(Bits(uint=self.descriptor_type, length = 8))
            data.append(Bits(uint=len(self.name), length = 4))
            data.append(Bits(uint=0, length = 4)) # reserved
            data.append(Bits(bytes=self.name.encode('ascii')))

        for d in self.descriptors:
            d.encode(eeprom, data)
