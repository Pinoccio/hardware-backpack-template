# vim: set sw=4 sts=4 et fileencoding=utf-8

import math

class MinifloatFormat:
    """
    Models a minifloat format. A number of parameters are fixed:
     - There is no sign bit
     - Denormal numbers are included
     - NaN and infinity are not
    Some other parameters can be configured.

    roundf is a function that controls rounding. It should accept a
    single float and just round it to the nearest integer in the wanted
    direction (e.g., pass in math.ceil or math.floor).
    """

    def __init__(self, ebits, sbits, ebias, roundf):
        self.ebits = ebits
        self.sbits = sbits
        self.ebias = ebias
        self.roundf = roundf

    def encode(self, n):
        """
        Encode a value.

        There is no real range checking for now, so:
         - If the value is too big, the exponent returned will be
           2**ebits or more.
         - If the value is too small and is rounded down, (0, 0) will be
           returned.

        Returns (e, s, rounded), where e is the encoded exponent, s is
        the encoded significant and rounded is the actual (rounded)
        value they encode.
        """

        if n == 0:
            return (0, 0, 0)

        if n < 0:
            raise ValueError("Cannot encode negative values")

        (significand, exponent) = math.frexp(n)
        # frexp returns signifcand normalized to 0.1ssss, but we need
        # 1.ssss
        significand *= 2
        exponent -= 1

        # Calculate the actual encoded exponent value
        e = exponent + self.ebias
        if e < 1:
            # Use a denormal number, which has a significand of the form
            # 0.ssss, but also has an effective e of 1. So, reduce the
            # significant until it is right for an e of 1.
            while e < 1:
                e += 1
                significand /= 2

            # Get the sbits most significant bits of the fractional part
            s = self.roundf(significand * (2 ** self.sbits))
            # Set e to 0 to indicate a denormal number
            e = 0

            rounded = s / (2 ** self.sbits) * (2 ** (1 - self.ebias))
        else:
            # Strip the (implicit) leading 1 and get the sbits most
            # significant bits of the fractional part
            s = self.roundf((significand - 1) * (2 ** self.sbits))

            # If we round up and end up out of range for s, increase the
            # exponent instead.
            if s == (2 ** self.sbits):
                s = 0
                e += 1

            rounded = (1 + s / (2 ** self.sbits)) * (2 ** (e - self.ebias))

        return e, s, rounded
