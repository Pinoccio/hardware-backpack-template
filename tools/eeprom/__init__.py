# vim: set sw=4 sts=4 et fileencoding=utf-8

# This file uses python-bitstring and crcmod
# (pip install bitstring crcmod)
#
# It also needs python3, because of some string handling

from bitstring import Bits, BitArray, pack
import crcmod

from voluptuous import Schema, Url, Range, All, Any, Optional, MultipleInvalid
from voluptuous import Required, Extra, IsTrue

unique_id_crc = crcmod.mkCrcFun(0x12f, 0, False, 0)
eeprom_crc = crcmod.mkCrcFun(0x1a7d3, 0, False, 0)

def Uint(bits, minimum = 0, maximum = None):
    if (maximum is None):
        maximum = (2 ** bits) - 1
    return All(int, Range(minimum, maximum))


NotEmpty = IsTrue

class Encoded:
    def __init__(self):
        self.roundings = []
        self.offsets = {}
        self.data = BitArray()
        self.errors = []

    def append(self, *args, **kwargs):
        self.data.append(*args, **kwargs)

    def append_string(self, s):
        """
        Append a string of characters to the given BitArray. Characters
        are encoded as ASCII, with the MSB set to 1 for the last
        character in the string.
        """
        if s:
            for c in s.encode('ascii'):
                self.data.append(pack('uint:8', c))

            # Set the MSB of the last byte to signal the end of the
            # string
            self.data[-8] = 1;

    def append_minifloat(self, fmt, unit, value):
        if value is None:
            e = s = 0
        else:
            (e, s, rounded) = fmt.encode(value)
            if value != rounded:
                self.roundings.append("{0} {2} to {1} {2}".format(value, rounded, unit))
        self.data.append(pack('uint:n', e, n = fmt.ebits))
        self.data.append(pack('uint:n', e, n = fmt.sbits))

class EEPROM:
    header_schema = Schema({
        Required('layout_version')        : Uint(8, minimum = 1, maximum = 1),
        Required('eeprom_size')           : Uint(8),
        Required('bus_protocol_version')  : Uint(8),
        Required('model')                 : Uint(16),
        Required('serial')                : Uint(24),
        Required('hardware_revision')     : Uint(8),
        Required('firmware_version')      : Uint(8),
        Required('name')                  : All(str, NotEmpty()),
    })

    def __init__(self, d):
        self.d = d
        self.groups = []
        self.data = None

    def add_group(self, group):
        self.groups.append(group)

    def check_unique_names(self, descriptors, res):
        """
        Check if the names in the given list of descriptors are unique.
        """
        names = set()
        for d in descriptors:
            name = d.effective_name()
            if name is None:
                continue
            if name in names:
                res.errors.append("Duplicate name: {}".format(name))
            else:
                names.add(name)

    def check(self, res):
        """
        Check various aspects of the data. Does not check everything,
        e.g., if a value is too big for its field, it'll error out
        during decoding. This function is called during encoding, so
        there is no need to call it directly.
        """
        self.check_unique_names(self.groups, res)
        for g in self.groups:
            self.check_unique_names(g.descriptors, res)

    def encode(self):
        """
        Generate the encoded EEPROM contents as a bytestring.
        """
        # Keep a list of rounded values, for user feedback
        res = Encoded()
        self.check(res)

        self.encode_header(res)
        for g in self.groups:
            g.encode(self, res)

        if (len(res.data) % 8 != 0):
            raise ValueError("Not an integer number of bytes: {} bits".format(len(data)))

        # Now we know how big the EEPROM contents has become, store it
        # at 2 byte offset from the start
        res.data[16:24] = len(res.data) // 8 + 2

        # append the checksum descriptor
        res.offsets[res.data.len // 8] = "Checksum"
        res.append(pack('uintbe:16', eeprom_crc(res.data.bytes)))

        if (len(res.data) // 8  > self.d['eeprom_size']):
            res.errors.append("Encoded eeprom is to big ({} > {})".format(len(res.data) // 8, self.d['eeprom_size']))

        return res

    def encode_header(self, res):
        res.append(pack('uint:8', self.d['layout_version']))
        res.append(pack('uint:8', self.d['eeprom_size']))
        res.append(pack('uint:8', 0)) # Used EEPROM size
        uid = BitArray()
        uid.append(pack('uint:8', self.d['bus_protocol_version']))
        uid.append(pack('uintbe:16', self.d['model']))
        uid.append(pack('uint:8', self.d['hardware_revision']))
        uid.append(pack('uintbe:24', self.d['serial']))
        # Calculate CRC over unique id
        uid.append(pack('uint:8', unique_id_crc(uid.bytes)))
        res.append(uid)
        res.append(pack('uintbe:16', self.d['firmware_version']))
        res.append_string(self.d['name'])

