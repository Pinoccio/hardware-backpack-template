# vim: set sw=4 sts=4 et fileencoding=utf-8

from bitstring import Bits, BitArray, pack
from voluptuous import Schema, Url, Range, All, Any, Optional, MultipleInvalid
from voluptuous import Required, Extra, IsTrue
import math

from eeprom import minifloat

NotEmpty = IsTrue

class Descriptor:
    @classmethod
    def get_schema(cls):
        # Pass extra = True here instead of using Extra as a key to
        # work around https://github.com/alecthomas/voluptuous/issues/40
        return Schema({
            Required('type'): str
        }, extra = True)

    def __init__(self, d):
        """
        Create a descriptor.

        d is a dict containing attributes, which should validate against
        the schema returned by the get_schema class method on every
        subclass.
        """
        self.d = d

    def effective_name(self):
        """
        Return the name for this descriptor.
        Returns the default if no name was set and None if no default
        was set either (e.g., this descriptor does not have a name).
        """
        try:
            return self.d['name']
        except KeyError:
            return getattr(self.__class__, 'default_name', None)

class SpiSlaveDescriptor(Descriptor):
    descriptor_type = 0x1
    descriptor_name = 'spi-slave'
    default_name = "spi"
    speed_format = minifloat.MinifloatFormat(4, 4, 6, math.floor)
    speed_unit = 'Mhz'

    @classmethod
    def get_schema(cls, pin, version):
        return Schema({
            Required('type')       : cls.descriptor_name,
            Required('ss_pin')     : pin,
            Required('speed')      : Any(float, int),
            Optional('name')       : str,
        })

    def encode(self, eeprom, res):
        res.append(pack('uint:8', self.descriptor_type))
        res.append(pack('bool', 'name' in self.d))
        res.append(pack('pad:1')) # reserved
        res.append(pack('uint:6', self.d['ss_pin']))
        res.append_minifloat(self.speed_format, self.speed_unit, self.d['speed'])
        if ('name' in self.d):
            res.append_string(self.d['name'])

class UartDescriptor(Descriptor):
    descriptor_type = 0x2
    descriptor_name = 'uart'
    default_name = "uart"

    @classmethod
    def get_schema(cls, pin, version):
        return Schema({
            Required('type')       : cls.descriptor_name,
            Required('rx_pin')     : pin,
            Required('tx_pin')     : pin,
            Required('speed')      : Any(*[s for (s, v) in cls.speeds if version >= v]),
            Optional('name')       : str,
        })

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

    def encode(self, eeprom, res):
        res.append(pack('uint:8', self.descriptor_type))
        res.append(pack('pad:2')) # reserved
        res.append(pack('uint:6', self.d['tx_pin']))
        res.append(pack('pad:2')) # reserved
        res.append(pack('uint:6', self.d['rx_pin']))
        res.append(pack('bool', 'name' in self.d))
        res.append(pack('pad:3')) # reserved
        (speeds, _) = zip(*self.speeds)
        res.append(pack('uint:4', speeds.index(self.d['speed'])))
        if ('name' in self.d):
            res.append_string(self.d['name'])

class IOPinDescriptor(Descriptor):
    descriptor_type = 0x3
    descriptor_name = 'io-pin'

    @classmethod
    def get_schema(cls, pin, version):
        return Schema({
            Required('type')       : cls.descriptor_name,
            Required('pin')        : pin,
            Required('name')       : All(str, NotEmpty()),
        })

    def encode(self, eeprom, res):
        res.append(pack('uint:8', self.descriptor_type))
        res.append(pack('pad:2')) # reserved
        res.append(pack('uint:6', self.d['pin']))
        res.append_string(self.d['name'])

class PowerUsageDescriptor(Descriptor):
    descriptor_type = 0x5
    descriptor_name = 'power-usage'
    usage_format = minifloat.MinifloatFormat(4, 4, -4, math.ceil)
    usage_unit = 'μA'

    @classmethod
    def get_schema(cls, pin, version):
        return Schema({
            Required('type')       : cls.descriptor_name,
            Required('pin')        : pin,
            Required('minimum')    : int,
            Required('typical')    : int,
            Required('maximum')    : int,
        })

    def encode(self, eeprom, res):
        res.append(pack('uint:8', self.descriptor_type))
        res.append(pack('pad:2')) # reserved
        res.append(pack('uint:6', self.d['pin']))
        res.append_minifloat(self.usage_format, self.usage_unit, self.d['minimum'])
        res.append_minifloat(self.usage_format, self.usage_unit, self.d['typical'])
        res.append_minifloat(self.usage_format, self.usage_unit, self.d['maximum'])

class EmptyDescriptor(Descriptor):
    descriptor_type = 0xff
    descriptor_name = 'empty'

    @classmethod
    def get_schema(cls, pin, version):
        return Schema({
            Required('type')       : cls.descriptor_name,
            Required('length')     : int,
        })

    def encode(self, eeprom, res):
        # Just output the descriptor_type length times
        for _ in range(self.d['length']):
            res.append(pack('uint:8', self.descriptor_type))

class GroupDescriptor(Descriptor):
    descriptor_type = 0x4

    @classmethod
    def get_schema(cls):
        return Schema({
            Required('name')        : All(str, NotEmpty()),
            Required('descriptors') : [
                Descriptor.get_schema(),
            ]
        })

    def __init__(self, *args, **kwargs):
        self.descriptors = []
        super(GroupDescriptor, self).__init__(*args, **kwargs)

    def encode(self, eeprom, res):
        res.offsets[res.data.len // 8] = self.__class__.__name__ + " " + self.d['name']
        res.append(pack('uint:8', self.descriptor_type))
        res.append_string(self.d['name'])

        for d in self.descriptors:
            res.offsets[res.data.len // 8] = d.__class__.__name__ + " " + (d.effective_name() or "")
            d.encode(eeprom, res)

    def add_descriptor(self, descriptor):
        self.descriptors.append(descriptor)
