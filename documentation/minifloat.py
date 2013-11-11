# vim: set sw=4 sts=4 et fileencoding=utf-8:
#
# This script was used to generate the tables with SPI speed and power
# usage values in EEPROM Layout.rst.

import sys
import math

sbits = 4
ebits = 4

if False:
    # Values are in μA, so no need to multiply before display
    units = ['μA', 'mA', 'A']
    mult = 1
    ebias = -4 # Exponent value of 1 means 5
else:
    # Values are in Mhz, so multiply them to get the proper display scale
    units = ['Hz', 'kHz', 'MHz', 'GHz']
    mult = 10**6
    ebias = 6 # Exponent value of 1 means -5

# Header line
header = "|es| "
line = "====="
for s in range(0, 2**sbits):
    header += "{:11x}".format(s)
    line += "  ========="

print(line)
print(header)
print(line)

for e in range(0, 2**ebits):
    data = "**{:x}**".format(e)
    for s in range(0, 2**sbits):
        if e == 0 and s == 0:
            data += "    Unknown"
        else:
            # Decimal point is before the first bit
            s /= 2**sbits
            # Calculate raw float value
            if e == 0:
                # Denormal numbers have an implicit leading 0. and use
                # the same exponent as e = 1
                exp = 1
            else:
                # Normalized numbers have an implicit leading 1.
                s += 1
                exp = e

            val = mult * s * 2**(exp - ebias)
            scale = int(math.log(val, 1000))
            data += "  {:6.3g}{:3s}".format(val / 1000.0**scale, units[scale])
    print(data)

print(line)
