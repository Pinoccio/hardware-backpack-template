# vim: set sw=4 sts=4 et fileencoding=utf-8

# This file uses python-bitstring and crcmod
# (pip install bitstring crcmod)
#
# It also needs python3, because of some string handling

from eeprom import descriptors, pins
import eeprom
import eeprom.descriptors
import yaml
import inspect

from voluptuous import Schema, Url, Range, All, Any, Optional, MultipleInvalid
from voluptuous import Required, Extra, Invalid

# Create a map of name -> class for descriptors
def is_valid_descriptor(o):
    return (inspect.isclass(o) and
            issubclass(o, eeprom.descriptors.Descriptor) and
            hasattr(o, 'descriptor_name'))

descriptor_map = {}
for (name, cls) in inspect.getmembers(eeprom.descriptors, is_valid_descriptor):
    descriptor_map[cls.descriptor_name] = cls

# Create a map of name -> class for pin maps
def is_valid_pin_map(o):
    return (inspect.isclass(o) and
            issubclass(o, eeprom.pins.PinMap) and
            hasattr(o, 'name'))

pin_names_map = {}
for (name, cls) in inspect.getmembers(eeprom.pins, is_valid_pin_map):
    pin_names_map[cls.name] = cls

schema = Schema({
    Required('pin_names'): Any(*pin_names_map.keys()),
    Required('header'): eeprom.EEPROM.header_schema,
    Required('groups'): [
        descriptors.GroupDescriptor.get_schema(),
    ],
})

def pin_validator(pins):
    def validate(value):
        try:
            return pins[value]
        except KeyError:
            raise Invalid('Not a valid pin name: {}'.format(value))
    return validate

def parse(f):
    """
    Parse EEPROM contents, read as YAML from the given file-like object.
    """
    parsed = yaml.safe_load(f)

    try:
        # Validate overall layout (descriptors are more thorougly
        # validated below)
        parsed = schema(parsed)
    except MultipleInvalid as e:
        return (None, map(str, e.errors))

    pins = pin_validator(pin_names_map[parsed['pin_names']].pins)
    layout_version = parsed['header']['layout_version']
    errors = []
    eep = eeprom.EEPROM(parsed['header'])
    for g in parsed['groups']:
        group = descriptors.GroupDescriptor(g)
        for d in g['descriptors']:
            try:
                cls = descriptor_map[d['type']]
            except KeyError as e:
                errors.append("Invalid descriptor type: {}".format(d['type']))
                continue

            desc_schema = cls.get_schema(pins, layout_version)

            try:
                # Validate descriptor
                d = desc_schema(d)
            except MultipleInvalid as e:
                errors.extend(map(str, e.errors))
                continue

            group.add_descriptor(cls(d))

        eep.add_group(group)

    return (eep, errors)
