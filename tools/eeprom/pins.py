# vim: set sw=4 sts=4 et fileencoding=utf-8

"""
Pin assignments on a v1.0 Pinoccio scout
"""

class PinMap:
    pass

class ScoutV1(PinMap):
    name = 'ScoutV1'
    pins = {
        'NC'    : 0,
        'VUSB'  : 1,
        'BKPK'  : 2,
        'RST'   : 3,
        'SCK'   : 4,
        'MISO'  : 5,
        'MOSI'  : 6,
        'SS'    : 7,
        'RX0'   : 8,
        'TX0'   : 9,
        'D2'    : 10,
        'D3'    : 11,
        'D4'    : 12,
        'D5'    : 13,
        'D6'    : 14,
        'D7'    : 15,
        'D8'    : 16,
        '3V3'   : 17,
        'GND'   : 18,
        'VBAT'  : 19,
        'RX1'   : 20,
        'TX1'   : 21,
        'SCL'   : 22,
        'SDA'   : 23,
        'REF'   : 24,
        'A0'    : 25,
        'A1'    : 26,
        'A2'    : 27,
        'A3'    : 28,
        'A4'    : 29,
        'A5'    : 30,
        'A6'    : 31,
        'A7'    : 32,
    }
