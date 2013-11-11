# vim: set sw=4 sts=4 et fileencoding=utf-8

# This file uses python-bitstring and crcmod
# (pip install bitstring crcmod)
#
# It also needs python3, because of some string handling

from bitstring import Bits, BitArray, pack
import crcmod

unique_id_crc = crcmod.mkCrcFun(0x12f, 0, False, 0)
eeprom_crc = crcmod.mkCrcFun(0x1a7d3, 0, False, 0)

class EEPROM:
    checksum_descriptor_type=0x4

    def __init__(self, layout_version, eeprom_size,
                 bus_protocol_version, model, hardware_revision,
                 serial, firmware_version, groups):
        self.layout_version = layout_version
        self.eeprom_size = eeprom_size
        self.bus_protocol_version = bus_protocol_version
        self.model = model
        self.hardware_revision = hardware_revision
        self.serial = serial
        self.firmware_version = firmware_version
        self.groups = groups
        self.data = None

    def check_unique_names(self, descriptors):
        """
        Check if the names in the given list of descriptors are unique.
        """
        names = set()
        for d in descriptors:
            name = d.effective_name()
            if name in names:
                raise ValueError("Duplicate name: {}".format(name))
            names.add(name)

    def check(self):
        """
        Check various aspects of the data. Does not check everything,
        e.g., if a value is too big for its field, it'll error out
        during decoding. This function is called during encoding, so
        there is no need to call it directly.
        """
        self.check_unique_names(self.groups)

        for i, g in enumerate(self.groups):
            self.check_unique_names(g.descriptors)
            if i != 0 and not g.name:
                raise ValueError("Only the first group can have an empty name")

    def encode(self):
        """
        Generate the encoded EEPROM contents as a bytestring.
        """
        self.check()

        data = BitArray()

        self.encode_header(data)
        for g in self.groups:
            g.encode(self, data)

        if (len(data) % 8 != 0):
            raise ValueError("Not an integer number of bytes: {} bits".format(len(data)))

        # append the checksum descriptor
        data.append(pack('uint:8', self.checksum_descriptor_type))
        data.append(pack('uintbe:16', eeprom_crc(data.bytes)))

        if (len(data) // 8  > self.eeprom_size):
            raise ValueError("Encoded eeprom is to big ({} > {})".format(len(data) // 8, self.eeprom_size))

        return data

    def encode_header(self, data):
        data.append(pack('uint:8', self.layout_version))
        data.append(pack('uint:8', self.eeprom_size))
        uid = BitArray()
        uid.append(pack('uint:8', self.bus_protocol_version))
        uid.append(pack('uintbe:16', self.model))
        uid.append(pack('uint:8', self.hardware_revision))
        uid.append(pack('uintbe:24', self.serial))
        # Calculate CRC over unique id
        uid.append(pack('uint:8', unique_id_crc(uid.bytes)))
        data.append(uid)
        data.append(pack('uintbe:16', self.firmware_version))
