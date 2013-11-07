# This file uses python-bitstring and crcmod
# (pip install bitstring crcmod)
from bitstring import Bits, BitArray
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
        data.append(Bits(uint=self.checksum_descriptor_type, length=8))
        data.append(Bits(uintbe=eeprom_crc(data.bytes), length=16))

        if (len(data) // 8  > self.eeprom_size):
            raise ValueError("Encoded eeprom is to big ({} > {})".format(len(data) // 8, self.eeprom_size))

        return data

    def encode_header(self, data):
        data.append(Bits(uint=self.layout_version, length=8))
        data.append(Bits(uint=self.eeprom_size, length=8))
        uid = BitArray()
        uid.append(Bits(uint=self.bus_protocol_version, length=8))
        uid.append(Bits(uintbe=self.model, length=16))
        uid.append(Bits(uint=self.hardware_revision, length=8))
        uid.append(Bits(uintbe=self.serial, length=24))
        # Calculate CRC over unique id
        uid.append(Bits(uint=unique_id_crc(uid.bytes), length=8))
        data.append(uid)
        data.append(Bits(uintbe=self.firmware_version, length=16))
