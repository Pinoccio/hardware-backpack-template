.. |vdots| unicode:: U+22EE
.. |es| replace:: :sub:`e`\\\ :sup:`s`


===============================
Pinoccio Backpack EEPROM Layout
===============================
Every backpack (expansion board) intended for use with the Pinoccio scout board
contains a small amount of EEPROM, which can be read and written through
a single wire interface called the backpack bus. The protocol used over
this bus is specified separately, this document details the layout of
the EEPROM contents.

This EEPROM will contain information about the backpack, its unique
identifier and the resources it uses. Additionally, the scout board can
use any remaining EEPROM space to store arbitrary other data (*e.g.*
configuration or calibration data).

This document describes version 1.0-draft of the layout. It is still a
draft and as such open for change.

================
Global Structure
================
The EEPROM consists of two main parts: A fixed-size header, followed by
an arbitrary number of variable-length descriptors.

The format and size of the header is fixed (for a given EEPROM layout
version). The size and format of each descriptor is determined by its
first byte, which indicates the descriptor type.

In the last descriptor, there is a CRC checksum calculated over the
entire EEPROM contents up to that byte.

Any data after the checksum descriptor should not be parsed. It is
recommended to write 0xff bytes after the checksum descriptor to prevent
accidentally interpreting them as data.

----------
Endianness
----------
Most data in the EEPROM layout occupies only one byte or less, so is
endian-neutral. Any fields that do occupy multiple fields use the big
endian layout, meaning the first byte contains the most-significant
bits, counting downward.

.. admonition:: Rationale: Endianness

        The big endian layout is more intuitive than the little endian
        format, in the sense that bit and byte order are the same. This
        mostly makes it easier to make sense of a raw EEPROM dump, which
        is probably something that will happen regularly during
        development.

        The AVR architecture has a tendency towards little endian,
        though the hardware does not really contain any realy multi-byte
        registers or instructions where endianness comes into play. It
        seems the avr-gcc implements multi-byte operations as
        little endian, but it seems there is no performance gain in
        using little endian over big endian: both will generate the same
        instructions handling the endianness "manually".

======
Header
======
The header contains the following info. Offset and size is in bytes.

.. table:: EEPROM header
        :class: align-center

        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        + offset   + 7          | 6          | 5          | 4          | 3          | 2          | 1          | 0          |
        +==========+============+============+============+============+============+============+============+============+
        | 0        | EEPROM layout version                                                                                 |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 1        | EEPROM size                                                                                           |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 2        || Backpack unique identifier                                                                           |
        +----------+| (8 bytes)                                                                                            +
        | 3        |                                                                                                       |
        +----------+                                                                                                       +
        | 4        |                                                                                                       |
        +----------+                                                                                                       +
        | 5        |                                                                                                       |
        +----------+                                                                                                       +
        | 6        |                                                                                                       |
        +----------+                                                                                                       +
        | 7        |                                                                                                       |
        +----------+                                                                                                       +
        | 8        |                                                                                                       |
        +----------+                                                                                                       +
        | 9        |                                                                                                       |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | a        | Firmware version                                                                                      |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        || b       || last?     || Backpack name                                                                           |
        || |vdots| || |vdots|   || |vdots|                                                                                 |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+

The backpack unique id contains a few subfields. See the Pinoccio
Backpack Bus Protocol specification for how the id is composed.

The Backpack unique id is read-only, meaning it cannot be changed
through the Backpack Bus protocol.

The firmware version listed is about the code running inside the
microcontroller in the backpack, that listens to the backpack bus and
allows accessing the EEPROM. This specification does not mandate any
particular form for this field, but a simple incrementing number seems
reasonable. Furthermore, this version does not need to be globally
unique. Instead, it is expected to be meaningful only within the same
backpack model.

The backpack name gives a short name for the backpack, that can be used
to identify the backpack when multiple backpacks are connected to the
scout. In general, this name should be the same for all backpacks with
the same model identifier (regardless of hardware version). It's also
not forbidden for two backpacks with different model identifiers to use
the same name. The name is mostly intended for user display, actual code
running on the scout should use the model identifier to select backpacks
to talk to instead. The format of the name is the same as that used for
descriptor names, see below.

.. admonition:: Rationale: Firmware version

        Including a firmware version is mostly informative, but it
        could help to debug problems later on.

        Different backpack models will likely have different firmwares,
        with firmware-specific features. They will likely base on the
        same basic firmware, but development will eventually branch off,
        so just making this version backpack model-specific makes sense.

        An alternative would be to include a basic version and
        model-specific version number, but that would require more bits
        (doing 4.4 leaves only 16 version numbers on each side, which is
        probably not enough on the longer term).

===========
Descriptors
===========
A descriptor starts with a single type byte, which defines the layout of
the rest of the descriptor.

The length of the descriptor is implicit: The combination of the
EEPROM layout version and the descriptor type defines how the length is
calculated from the descriptor data.

.. admonition:: Rationale: No explicit descriptor length

        It seems obvious to also explicitely store the length of the
        descriptor, but it seems that's not really needed. A scout needs
        to know about the layout of a descriptor to be able to use it,
        so it will also know the descriptor length.

        An explicit length would help for future compatibility (a scout
        could skip an unknown descriptor because its length is
        explicitely stored), but we don't really need this - we can
        easily expect a user to upgrade the firmware whenever they add a
        too-new backpack. Skipping an unknown descriptor is probably not
        very helpful, since it could cause a resource conflict to go
        undetected and cause problems.

        An explicit length could also be useful when the slave needs to
        read its own EEPROM but is only interested in specific
        descriptors. However, even with explicit lengths this is
        probably quite complicated and it's easier to just read from
        hardcoded offsets.

Most of these descriptors will be describing I/O resources used by the
backpack, but they are not necessarily limited to just that. Other
information can also be added as descriptors (no examples yet).

-----------
Pin numbers
-----------
When a field contains a pin number, it can identify any of the
pinoccio's pins. This numbering happens based on the physical location
of the pin, regardless of the actual pin function.

Pins are numbered 1-32. Pin 1 is the pin top left, pin 17 is the pin top
right, looking at the component side of the board. On the v1.0 pinoccio
boards, the top is where the USB connector is. Pin number 0 means "not
connected".

All pin numbers are stored in a 6-bit field, which has some values to
spare for future expansion. However, in general a one or two bits above
every pin number should be kept as reserved for future expansion.

.. admonition:: Rationale: Numbered pins

        It might seem weird to allow specifying all of the Pinoccio's
        pins in the resource descriptors. For the I/O pins (D0 - D8, A0
        - A7, TX1, RX1, SCK, MISO, MOSI, SSN, SCL, SDA) this is obvious,
        but the other pins like GND, VBAT etc. should not normally be
        declared inside a resource descriptor.

        However, in the future, newer versions of the scout might change
        the pin assignments, so including only the sensible pins based
        on the current pin assignments is asking for trouble.

        Another way to look at this is that a backpack should declare
        what physical pins it is using for what purpose, regardless of
        how these pins are assigned on the pinoccio scout board.

        The downside of this is that we'll need a physical to logical
        pin number translation on the scout (to get at pin numbers
        digitalWrite will understand). However, it's better to have
        such a sane translation now, then to do a
        logical-pins-on-scout-v1-to-logical-pins-on-scout-v5 translation
        table later, which will drive us crazy...

.. admonition:: Rationale: Not connected pin number

        Including a pin number for "not connected" is expected to be
        useful in a few situations:

        - When a pin is optional and can be connected through a solder
          jumper, this allows explicitely indicating that a pin is
          disconnected (as opposed to not supported at all). When two
          variants of a backpack are available, this could allow both
          to have the same EEPROM structure and offsets, while still
          showing the difference.
        - Similar to the above, if a user removes a soldered jumper, he
          will not have to remove the entirre descriptor but can just
          flip a few bits.
        - Sometimes a particular resource will be only partially
          connected. Consider a UART that only has its TX pin connected,
          for example.

------
Groups
------
The group descriptor type can be used to group the other descriptors.

Groups are typically used to group subparts of a backpack and can help
to remove redundancy in descriptor names.

Any descriptors following a group descriptor, up to the next group
descriptor are considered to be inside the group. Any descriptors before
the first group descriptor are considered to be inside an implicitly
declared group with an empty name.

.. admonition:: Group-less descriptors

        Instead of grouping all group-less descriptors together in a
        single group, we could also specify that they will each get
        their own group. For example, on the wifi backpack, you could
        have::

          group: wifi
                  spi
                  uart
                  pin: upgrade
          spi: eep
          spi: sd

        This would create three groups, "wifi", "eep" and "sd", where
        the latter two just contain a single spi descriptor with the
        default name.

        Moving the given name from the descriptor to the implicitly
        created group and using the default name for the descriptor will
        always work (wrt to uniqueness), since the descriptor will
        always end up alone it is group.

        In the current specification, you'd have to add two more
        explicit group descriptors, or have inconsistently named
        descriptors: (wifi, spi) for the wifi spi and ("", eep) for the
        eeprom spi.

        A possible complication here is the power usage descriptors: If
        you want to add a power usage descriptor to every group, you'd
        still have to add explicit group descriptors...

        However, for a backpack that just contains a single device (say,
        the wifi backpack without the eeprom and sd), you'd want to
        write something like::

          spi
          uart
          pin: upgrade

        In the current spec, you'd get three descriptors: ("", spi),
        ("", uart) and ("", upgrade). Creating implicit groups is not
        possible for spi and uart (for lack of a name, creating (spi,
        spi) or (upgrade, upgrade) is really unhelpful). Basing the
        creation of this implicit group on wether a descriptor is
        probably confusing.

        Of course, we could just say that if a backpack contains just a
        single part, but needs multiple descriptors, it should always
        just explicitely declare a single group (even if it just has a
        generic name like "dev").

        The above example then just becomes:

          group: wifi
                  spi
                  uart
                  pin: upgrade

----------------
Descriptor names
----------------
Most descriptors can contain a string, which defines a short name for the
resource. This can be used by the user to easily access different pins
using a short name, as well as by library code running on the scout to
distinguish different resources.

Sometimes names are superfluous and can be omitted by clearing the "has
name" bit in the descriptor. In this case, a default name is used,
depending on the descriptor type. Not all descriptor types allow
omitting the name.

Every resource name used should be unique within the group it is in
(including within the implicit nameless group), so the group name
together with the descriptor name can be used to identify the resource
on the scout. Furthermore, each group must have a name that is unique
among all groups.

These strings are always encoded using ASCII, no fancy characters are
allowed. Even more, it is recommended to keep these identifiers simple
and use only (lowercase) letters, numbers, periods and underscores to allow
them to be used as bitlash identifiers.

Every character in the string is stored in its own byte. Since ASCII is
only a 7-bit encoding, the most significant bit of each byte is used to
indicate the end of the string: If the MSB is 0, there are more
characters, if the MSB is 1 this is the last character. This means it is
not possible to indicate an empty string using this mechanism.

.. admonition:: Rationale: Naming resources

        Giving a name to a resource mostly serves two purposes:

        * Provide guidance to a user that looks at a resource overview
          or wants to talk to a backpack manually.
        * Allow a library to talk to a backpack without requiring
          explicit configuration. By using names, it can identify
          resources even when multiple of the same type are present,
          without having to resort to fragile methods like "the first
          I²C address is always the temperature sensor".

---------------
Descriptor list
---------------
Below, all the currently defined descriptor types are defined.

Group
"""""
This descriptor describes a part of the backpack or otherwise groups all
subsequent descriptors, up to excluding the next group descriptor.
Nested groups are not supported. This is mostly informational, but is
functionally relevant for the power mode descriptor as well.

Furthermore, descriptors names are only required to be unique inside a
group.

A name must be specified for this descriptor, there is no default.

.. table:: Group descriptor layout
        :class: align-center

        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | offset   | 7          | 6          | 5          | 4          | 3          | 2          | 1          | 0          |
        +==========+============+============+============+============+============+============+============+============+
        | 0        | Descriptor type                                                                                       |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        || 1       || last?     || Resource name                                                                           |
        || |vdots| || |vdots|   || |vdots|                                                                                 |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+

.. admonition:: Open Question: Group types / metadata

        Does this descriptor need some kind of group type (physical
        section / IC / logical section / ...) field or other metadata?

I²C slave
"""""""""
This resource indicates an I²C slave is present that uses pins 21 as SCL
and pin 22 as SDA.

If not specfied, the name of this descriptor defaults to "i2c".

.. table:: I²C slave descriptor layout
        :class: align-center

        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | offset   | 7          | 6          | 5          | 4          | 3          | 2          | 1          | 0          |
        +==========+============+============+============+============+============+============+============+============+
        | 0        | Descriptor type                                                                                       |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 1        | has name   | I²C address                                                                              |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 2        | *reserved*                                                                  | Maximum speed           |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        || 3       || last?     || Resource name                                                                           |
        || |vdots| || |vdots|   || |vdots|                                                                                 |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+

The I²C address is the 7-bit address, without the R/W bit.

.. table:: Maximum Speed values

        =====   ===============
        Value   Meaning
        =====   ===============
        0       Standard-mode (100 kbit/s)
        1       Fast-mode (400 kbit/s)
        2       Fast-mode plus (1 Mbit/s)
        3       High-speed mode (3.4 Mbit/s)
        =====   ===============

.. admonition:: Rationale: Speed values

        The speed values listed come from the I²C specification. In
        theory, devices could have different maximum speeds as well, but
        this seems uncommon. If non-standard speeds are encountered on
        devices, additional values can be added in the reserved bits.
        Alternatively, a descriptor can just specify a slower speed than
        really supported.

        Another alternative would have been to allow specifying an
        arbitrary speed, instead of picking one from a list. However, to
        get the same range of speeds, this would require more bits in
        the descriptor, without much obvious gain.

SPI Slave
"""""""""
This resource indicates an SPI slave is present that uses pin 3 as SCK,
pin 4 as MISO and pin 5 as MOSI. The SS pin used is indicated by the
descriptor.

If not specfied, the name of this descriptor defaults to "spi".

.. table:: SPI Slave descriptor layout
        :class: align-center

        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | offset   | 7          | 6          | 5          | 4          | 3          | 2          | 1          | 0          |
        +==========+============+============+============+============+============+============+============+============+
        | 0        | Descriptor type                                                                                       |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 1        |  has name  | *reserved* | Slave select pin number                                                     |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 3        | Maximum speed exponent                            | Maximum speed significand                         |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        || 4       || last?     || Resource name                                                                           |
        || |vdots| || |vdots|   || |vdots|                                                                                 |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+

The SPI slave device is assumed to send and receive bytes with the
most-significant bit first, and use "SPI mode 0" (CPOL = 0 and CPHA =
0).

The CPOL and CPHA bits represent the clock polarity and phase. CPOL
represents the idle state of the clock, and CPHA indicates where in the
clock cycle the data is captured and shifted. These terms have been
defined in the `SPI Block Guide`_ by Freescale Semiconductor.

.. _SPI Block Guide: http://www.ee.nmt.edu/~teare/ee308l/datasheets/S12SPIV3.pdf

.. admonition:: No CPOL, CPHA and lsb first fields

        An earlier draft of this spec included fields for these
        properties. Most devices seem to use MSB first, CPOL = 0 and
        CPHA = 0, but it makes sense to allow specifying other settings.

        However, for the first version of this layout these fields were
        removed to save a bit of space, so the wifi backpack descriptors
        would fit in the EEPROM available. Future backpacks will
        probably have a slightly bigger chip.

The SPI speed uses a minifloat format that expresses the speed in Mhz.

:sign bit: no
:significand: 4 bits
:exponent: 4 bits
:exponent bias: 6 (*i.e.,* exponent value of 1 means ×2\ :sup:`−5`)
:significands: 1.0000\ :sub:`2` to 1.1111\ :sub:`2` (normal), 0.0000\ :sub:`2` to 0.1111\ :sub:`2` (denormal)
:exponents: −5 to 9 (normal), −5 (denormal)

Note that there are no special values like NaN and infinity, so the
maximum exponent value is not treated specially. The value 0 means the
speed is unknown or otherwise cannot be defined.

Speed values should be rounded *down* to the nearest available
value.

.. admonition:: Example: Decoding speed values

        Normal numbers (*e ≠ 0*) are decoded with an implicit leading
        "1.":

        .. math::

                byte = 0x56 \\
                e = 5 \\
                s = 0x6 = 0110_2 \\
                exponent = e + e_bias = 5 − 6 = −1 \\
                significand = 1.0110_2 \\
                \\
                value = significand × 2^{exponent} = 0.0110_2 × 2^{−1} \\
                value = 0.11011_2 ≈ 0.688\ Mhz = 688\ kHz

        Denormal numbers (*e = 0*) are decoded with an implicit leading
        "0.", with the same exponent as values with *e = 1*):

        .. math::

                byte = 0x0a \\
                e = 0 \\
                s = 0xa = 1010_2 \\
                exponent = 1 + e_bias = 1 − 6 = -5 \\
                significand = 0.1010_2 \\
                \\
                value = significand × 2^{exponent} = 0.1010_2 × 2^{5} \\
                value ≈ 0.0195\ Mhz = 19.5\ kHz

.. table:: SPI speed values
        :class: align-right

        =====  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========
        |es|           0          1          2          3          4          5          6          7          8          9          a          b          c          d          e          f
        =====  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========
        **0**    Unknown    1.95kHz    3.91kHz    5.86kHz    7.81kHz    9.77kHz    11.7kHz    13.7kHz    15.6kHz    17.6kHz    19.5kHz    21.5kHz    23.4kHz    25.4kHz    27.3kHz    29.3kHz
        **1**    31.2kHz    33.2kHz    35.2kHz    37.1kHz    39.1kHz      41kHz      43kHz    44.9kHz    46.9kHz    48.8kHz    50.8kHz    52.7kHz    54.7kHz    56.6kHz    58.6kHz    60.5kHz
        **2**    62.5kHz    66.4kHz    70.3kHz    74.2kHz    78.1kHz      82kHz    85.9kHz    89.8kHz    93.8kHz    97.7kHz     102kHz     105kHz     109kHz     113kHz     117kHz     121kHz
        **3**     125kHz     133kHz     141kHz     148kHz     156kHz     164kHz     172kHz     180kHz     188kHz     195kHz     203kHz     211kHz     219kHz     227kHz     234kHz     242kHz
        **4**     250kHz     266kHz     281kHz     297kHz     312kHz     328kHz     344kHz     359kHz     375kHz     391kHz     406kHz     422kHz     438kHz     453kHz     469kHz     484kHz
        **5**     500kHz     531kHz     562kHz     594kHz     625kHz     656kHz     688kHz     719kHz     750kHz     781kHz     812kHz     844kHz     875kHz     906kHz     938kHz     969kHz
        **6**       1MHz    1.06MHz    1.12MHz    1.19MHz    1.25MHz    1.31MHz    1.38MHz    1.44MHz     1.5MHz    1.56MHz    1.62MHz    1.69MHz    1.75MHz    1.81MHz    1.88MHz    1.94MHz
        **7**       2MHz    2.12MHz    2.25MHz    2.38MHz     2.5MHz    2.62MHz    2.75MHz    2.88MHz       3MHz    3.12MHz    3.25MHz    3.38MHz     3.5MHz    3.62MHz    3.75MHz    3.88MHz
        **8**       4MHz    4.25MHz     4.5MHz    4.75MHz       5MHz    5.25MHz     5.5MHz    5.75MHz       6MHz    6.25MHz     6.5MHz    6.75MHz       7MHz    7.25MHz     7.5MHz    7.75MHz
        **9**       8MHz     8.5MHz       9MHz     9.5MHz      10MHz    10.5MHz      11MHz    11.5MHz      12MHz    12.5MHz      13MHz    13.5MHz      14MHz    14.5MHz      15MHz    15.5MHz
        **a**      16MHz      17MHz      18MHz      19MHz      20MHz      21MHz      22MHz      23MHz      24MHz      25MHz      26MHz      27MHz      28MHz      29MHz      30MHz      31MHz
        **b**      32MHz      34MHz      36MHz      38MHz      40MHz      42MHz      44MHz      46MHz      48MHz      50MHz      52MHz      54MHz      56MHz      58MHz      60MHz      62MHz
        **c**      64MHz      68MHz      72MHz      76MHz      80MHz      84MHz      88MHz      92MHz      96MHz     100MHz     104MHz     108MHz     112MHz     116MHz     120MHz     124MHz
        **d**     128MHz     136MHz     144MHz     152MHz     160MHz     168MHz     176MHz     184MHz     192MHz     200MHz     208MHz     216MHz     224MHz     232MHz     240MHz     248MHz
        **e**     256MHz     272MHz     288MHz     304MHz     320MHz     336MHz     352MHz     368MHz     384MHz     400MHz     416MHz     432MHz     448MHz     464MHz     480MHz     496MHz
        **f**     512MHz     544MHz     576MHz     608MHz     640MHz     672MHz     704MHz     736MHz     768MHz     800MHz     832MHz     864MHz     896MHz     928MHz     960MHz     992MHz
        =====  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========

.. admonition:: Rationale: Speed format

        Every SPI device has a particular maximum supported SPI speed,
        there are no standard speeds. Because of this, it makes sense to
        support a wide range of values.

        Looking at the SPI implementation on AVR, the clock speed is
        derived from the system clock using a prescaler. This means that
        it does not support arbitrary speeds and the SPI hardware can
        often not run at the maximum supported speed (which is
        unavoidable). However, when the speeds supported by the EEPROM
        layout do not match the speeds supported by the hardware, it
        could happen that the speed is "rounded down" twice (once to fit
        in the EEPROM and once to configure the hardware). In some
        cases, this means that the speed used is not the optimal speed.

        To prevent this, we should make sure that the EEPROM speeds
        match the hardware speeds as much as possible. An obvious way is
        to just store the clock divider value to use, so the EEPROM is
        limited to the values 8Mhz, 4Mhz, 2Mhz, etc. However, if in the
        future a Scout version is introduced that runs on a different
        speed (say 20Mhz), or perhaps an ARM version that runs at higher
        speeds, we'd again have sub-optimal speeds.

        By using this minifloat format, we can support a wide range of
        values, with reasonable granularity. This allows specifying the
        maximum SPI speed as accurate as possible, without relying on
        the implementation details of the current scout design.

        However, by using the Mhz unit for the values, we do ensure that
        the SPI speeds for a 16Mhz AVR are included, making sure that
        for the current scout design, we will at least get optimal
        speeds. But as you can see other common speeds like 20Mhz are
        also included.

Single I/O pin
""""""""""""""
This describes a single I/O pin used by the backpack.

A name must be specified for this descriptor, there is no default.

.. table:: I/O pin descriptor layout
        :class: align-center

        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | offset   | 7          | 6          | 5          | 4          | 3          | 2          | 1          | 0          |
        +==========+============+============+============+============+============+============+============+============+
        | 0        | Descriptor type                                                                                       |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 1        | *reserved*              | Pin number                                                                  |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        || 2       || last?     || Resource name                                                                           |
        || |vdots| || |vdots|   || |vdots|                                                                                 |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+

Any pins that are specified by other resources (e.g., MISO or the CS pin
in an SPI resource) do not also need to be explicitly specified as an
I/O pin resource.

Power pins, including GND do not need to be explicitly specified either.

.. admonition:: Open Question: Usage field and metadata

        In the original discussion, a "pin usage" field was proposed.
        However, it's not quite clear what kind of values this should
        contain. I originally wrote:


                The usage field describes the way the pin is to be used.
                This is mostly informative, but it can be used to
                distinguish pins by a generic driver or to potentially
                allow resource-sharing (e.g., when two backpacks both
                use the same pin as an open-collector interrupt pin).

        And suggested some potential usage types:

                - Open-collector/push-pull interrupt active high/low, to
                  set up interrupt handling automatically.
                - LED, to allow turning it on and off through bitlash
                - General digital input, general digital output, to set
                  up pinMode automatically. Perhaps also have general
                  input with pullup?
                - PWM output
                - Analog input
                - Reset (active high/low), to have the backpack
                  automatically reset when the Pinoccio resets?

        Does any of this actually make sense? Or is this overengineering
        and is it sufficient to just list that a pin is used (to detect
        pin conflicts) and assign it a name (to allow libraries to work
        without hardcoded pin numbers)?

        Perhaps it makes sense to split up these usages into multiple
        subfields (input/output, digital/analog, etc?).

        For now, it seems sensible to just leave out this field and add
        it a later layout version, when the scout-side code is further
        along as well.

.. admonition:: Rationale: Single I/O pins only

        It seems overly verbose to use a complete descriptor for every
        new pin. When declaring a lot of pins, chunking them together in
        a descriptor seems useful to reduce overhead.

        However, in practice, most of the pins will be indepenent and
        thus need their own name and (once we add them) usage flags and
        other metadata. This means that stacking together pins could
        save the descriptor type byte for each pin, but we'll still need
        the pin number and name, so the gain would be rather small. This
        would also mean multiple resources (and names) are declared in
        the same descriptor, which might make the parsing code more
        complicated.

        If at some point a backpack is produced that uses a bus of pins
        (e.g., 4 or 8 pins who are identical except for the bit they
        transfer and could also share a common name), introducing a new
        descriptor for that makes sense.

UART
""""
If not specfied, the name of this descriptor defaults to "uart".

.. table:: UART descriptor layout
        :class: align-center

        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        + offset   | 7          | 6          | 5          | 4          | 3          | 2          | 1          | 0          |
        +==========+============+============+============+============+============+============+============+============+
        | 0        | Descriptor type                                                                                       |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 1        | *reserved*              | TX pin number (from backpack point of view)                                 |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 2        | *reserved*              | RX pin number (from backpack point of view)                                 |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 3        | has name   | *reserved*                           | Speed                                             |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        || 4       || last?     || Resource name                                                                           |
        || |vdots| || |vdots|   || |vdots|                                                                                 |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+

The TX and RX pins are specified from the backpack point of view, so the
pin in the TX field should correspond to an RX pin on the scout and vice
versa.

.. table:: UART Speed values

        =====   ===============
        Value   Meaning
        =====   ===============
        0       Unspecified
        1       300 bps
        2       600 bps
        3       1200 bps
        4       2400 bps
        5       4800 bps
        6       9600 bps
        7       19200 bps
        8       38400 bps
        9       57600 bps
        10      115200 bps
        =====   ===============

Power usage
"""""""""""
This describes the power usage of (a part of) the backpack, as drawn
from a particular power pin.

A backpack should declare a power usage descriptor for every power line
it draws from. Within a group, there must not be more than one power
usage descriptor for a given pin.

If this descriptor appears as part of a group, it is assumed to describe
the power usage of that particular part of the backpack. If the
descriptor is in the default group, it is taken to mean the power usage
of the entire backpack, excluding any groups that have their own power
usage desriptors.

This means that the total power usage of the backpack must be the sum of
all power usage descriptors in the EEPROM.

This descriptor does not have a name.

.. table:: Power usage descriptor
        :class: align-center

        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        + offset   | 7          | 6          | 5          | 4          | 3          | 2          | 1          | 0          |
        +==========+============+============+============+============+============+============+============+============+
        | 0        | Descriptor type                                                                                       |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 1        | *reserved*              | Power pin number                                                            |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 2        | Minimum power usage exponent         | Minimum power usage signifcand                                 |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 3        | Typical power usage exponent         | Typical power usage signifcand                                 |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 4        | Maximum power usage exponent         | Maximum power usage signifcand                                 |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+

The power usage fields use a minifloat format that expresses the speed
in MHz.

:sign bit: no
:significand: 4 bits
:exponent: 4 bits
:exponent bias: −4 (*i.e.,* exponent value of 1 means ×2\ :sup:`5`)
:significands: 1.0000\ :sub:`2` to 1.1111\ :sub:`2` (normal), 0.0000\ :sub:`2` to 0.1111\ :sub:`2` (denormal)
:exponents: −5 to 9 (normal), −5 (denormal)

Note that there are no special values like NaN and infinity, so the
maximum exponent value is not treated specially. The value 0 means the
speed is unknown or otherwise cannot be defined.

Power usage values should be rounded *up* to the nearest available
value.

.. admonition:: Example: Decoding power usage values

        Normal numbers (*e ≠ 0*) are decoded with an implicit leading
        "1.":

        .. math::

                byte = 0x56 \\
                e = 5 \\
                s = 0x6 = 0110_2 \\
                exponent = e + e_bias = 5 − (−4) = 9 \\
                significand = 1.0110_2 \\
                \\
                value = significand × 2^{exponent} = 0.0110_2 × 2^{9} \\
                value = 101100000_2 = 704μA

        Denormal numbers (*e = 0*) are decoded with an implicit leading
        "0.", with the same exponent as values with *e = 1*):

        .. math::

                byte = 0x0a \\
                e = 0 \\
                s = 0xa = 1010_2 \\
                exponent = 1 + e_bias = 1 − (−4) = 5 \\
                significand = 0.1010_2 \\
                \\
                value = significand × 2^{exponent} = 0.1010_2 × 2^{5} \\
                value = 10100_2 = 20 μA


.. table:: Power usage values
        :class: align-right


        =====  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========
        |es|           0          1          2          3          4          5          6          7          8          9          a          b          c          d          e          f
        =====  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========
        **0**    Unknown       2μA        4μA        6μA        8μA       10μA       12μA       14μA       16μA       18μA       20μA       22μA       24μA       26μA       28μA       30μA 
        **1**      32μA       34μA       36μA       38μA       40μA       42μA       44μA       46μA       48μA       50μA       52μA       54μA       56μA       58μA       60μA       62μA 
        **2**      64μA       68μA       72μA       76μA       80μA       84μA       88μA       92μA       96μA      100μA      104μA      108μA      112μA      116μA      120μA      124μA 
        **3**     128μA      136μA      144μA      152μA      160μA      168μA      176μA      184μA      192μA      200μA      208μA      216μA      224μA      232μA      240μA      248μA 
        **4**     256μA      272μA      288μA      304μA      320μA      336μA      352μA      368μA      384μA      400μA      416μA      432μA      448μA      464μA      480μA      496μA 
        **5**     512μA      544μA      576μA      608μA      640μA      672μA      704μA      736μA      768μA      800μA      832μA      864μA      896μA      928μA      960μA      992μA 
        **6**    1.02mA     1.09mA     1.15mA     1.22mA     1.28mA     1.34mA     1.41mA     1.47mA     1.54mA      1.6mA     1.66mA     1.73mA     1.79mA     1.86mA     1.92mA     1.98mA 
        **7**    2.05mA     2.18mA      2.3mA     2.43mA     2.56mA     2.69mA     2.82mA     2.94mA     3.07mA      3.2mA     3.33mA     3.46mA     3.58mA     3.71mA     3.84mA     3.97mA 
        **8**     4.1mA     4.35mA     4.61mA     4.86mA     5.12mA     5.38mA     5.63mA     5.89mA     6.14mA      6.4mA     6.66mA     6.91mA     7.17mA     7.42mA     7.68mA     7.94mA 
        **9**    8.19mA      8.7mA     9.22mA     9.73mA     10.2mA     10.8mA     11.3mA     11.8mA     12.3mA     12.8mA     13.3mA     13.8mA     14.3mA     14.8mA     15.4mA     15.9mA 
        **a**    16.4mA     17.4mA     18.4mA     19.5mA     20.5mA     21.5mA     22.5mA     23.6mA     24.6mA     25.6mA     26.6mA     27.6mA     28.7mA     29.7mA     30.7mA     31.7mA 
        **b**    32.8mA     34.8mA     36.9mA     38.9mA       41mA       43mA     45.1mA     47.1mA     49.2mA     51.2mA     53.2mA     55.3mA     57.3mA     59.4mA     61.4mA     63.5mA 
        **c**    65.5mA     69.6mA     73.7mA     77.8mA     81.9mA       86mA     90.1mA     94.2mA     98.3mA      102mA      106mA      111mA      115mA      119mA      123mA      127mA 
        **d**     131mA      139mA      147mA      156mA      164mA      172mA      180mA      188mA      197mA      205mA      213mA      221mA      229mA      238mA      246mA      254mA 
        **e**     262mA      279mA      295mA      311mA      328mA      344mA      360mA      377mA      393mA      410mA      426mA      442mA      459mA      475mA      492mA      508mA 
        **f**     524mA      557mA      590mA      623mA      655mA      688mA      721mA      754mA      786mA      819mA      852mA      885mA      918mA      950mA      983mA     1.02A  
        =====  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========  =========

Data
""""
This is a descriptor type that is not added during manufacturing, but
can be added by the scout to store arbitrary information. The structure
of this data is not defined at all, it is up to the scout to interpret
this.

Data descriptors are not considered part of any group and are
recommended to be used only at the end of the EEPROM, just before the
checksum.

If not specfied, the name of this descriptor defaults to "data".

.. table:: Data descriptor layout
        :class: align-center

        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        + offset   | 7          | 6          | 5          | 4          | 3          | 2          | 1          | 0          |
        +==========+============+============+============+============+============+============+============+============+
        | 0        | Descriptor type                                                                                       |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 1        | has name   | Data length                                                                              |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        || 2       || Data                                                                                                 |
        || |vdots| || |vdots|                                                                                              |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        ||         || last?     || Resource name                                                                           |
        || |vdots| || |vdots|   || |vdots|                                                                                 |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+

The data length indicates how many bytes of data are present, excluding
the header bytes and name bytes.

.. admonition:: Rationale:: Custom data

        This descriptor could be used by the scout to store arbitrary
        data, such as calibration or configuration settings.

        It is expected that this data can be used by a backpack-specific
        library to store things. No attempt is made to uniquely label
        the data for a given purpose: it is expected that the code
        running on the scout for a given backpack will know how to read
        and write this data and that it will be the same code that
        accesses the data every time.

.. admonition:: Open Question:: Multiple data types

        Would it make sense to have a dozen or so data types, so a
        library can store different kinds of data without having to add
        another "subtype" byte?

        Or would it perhaps make sense to give this descriptor a name as
        well and use that to identify subtypes?

Empty
"""""
This descriptor does not contain any data. Instead, it just repeats the
descriptor type byte an arbitrary amount. The end of the descriptor is
the first different byte, which is the start of the next descriptor.

.. admonition:: Rationale: Empty descriptor

        This descriptor is intended to allow removal of an existing
        descriptor, without having to move all of the subsequent
        descriptors.

Checksum
""""""""
This descriptor contains a checksum, calculated over all previous bytes
(including the descriptor type byte of this descriptor).

The checksum descriptor is always the last descriptor, no other
descriptors are allowed after this one, nor can the checksum descriptor
be omitted.

The checksum descriptor does not have a name and is not considered to be
part of a group.

.. table:: Checksum layout
        :class: align-center

        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        + offset   | 7          | 6          | 5          | 4          | 3          | 2          | 1          | 0          |
        +==========+============+============+============+============+============+============+============+============+
        | 0        | Descriptor type                                                                                       |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 1        | High checksum byte                                                                                    |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 2        | Low checksum byte                                                                                     |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+

The checksum value is calculated using the CRC algorithm over all bytes
in the EEPROM up to the checksum. The CRC variant used is a non-standard
one, as proposed by P. Koopman in the paper `CRC Selection for Embedded
Network Messages`_. The parameters for this CRC variant are below,
expressed in terms of the Rocksoft model (see `A PAINLESS GUIDE TO CRC
ERROR DETECTION ALGORITHMS`_).

:Width:         16 bits
:Polynomial:    x\ :sup:`16` + x\ :sup:`15` + x\ :sup:`13` + x\ :sup:`10` + x :sup:`9` + x :sup:`8` + x :sup:`7` + x :sup:`6` + x :sup:`4` + x :sup:`1` + 1
:Poly in hex:   0xa7d3 (Rocksoft) / 0xd3e9 (Koopman)
:Initial value: 0x0
:Reflect in:    No
:Reflect out:   No
:Xor out:       0x0
:Check:         0x3f29

.. admonition:: Rationale:: Checksum algorithm

        See the Backpack bus specification for some more background on
        checksum algorithm selection.

        Looking at the paper `CRC Selection for Embedded Network
        Messages`_, none of the 16-bit CRCs selected there come close to
        the performance bound for 512-bit messages (64 bytes, e.g., a
        full EEPROM). However, the 0xbaad and 0xd3e9 polynomials are
        near the bound for messages sizes above 1270 bits (0xbaad is not
        within 1%, but closer inspection of the raw data shows that it
        is still within a few %). For sizes above about 250 bits, these
        still stick within 2x the bound, which is still good.

        Given that our inital EEPROM is 512 bits, but it seems unlikely
        that it will ever be less than half full, both of these
        polynomials seem promising. The fact that they scale well into
        bigger EEPROM sizes is useful for future expansion.

        Looking at the raw data for both CRCs shows that 0xbaad is a lot
        better (but still far from the bound) for small data sizes (<
        100 bits), but 0xd3e9 is better for any data size > 350 bits, so
        that seems to be the best one for this application.

.. _CRC Selection for Embedded Network Messages: http://www.ece.cmu.edu/~koopman/crc/
.. _A PAINLESS GUIDE TO CRC ERROR DETECTION ALGORITHMS: http://www.csm.ornl.gov/~dunigan/crc.html

=============================
Modifying the EEPROM contents
=============================
The backpack bus slave microcontroller only needs very little knowledge
about the EEPROM layout used. It is expected that implementations will
simply hardcode some offsets, to prevent having to completely parse the
complete EEPROM to find the relevant info.

In the current version of the layout, the slave will only need to access
its unique identifier. This means that, in theory, the scout could
change the EEPROM contents, includig changing to a different layout
version, as long as the unique identifier doesn't move to a different
place.

In the future, the slave might read more data from specific spots in the
EEPROM (*e.g.*, an I²C address configured by the scout) to configure the
backpack. Then, the same constraint applies: the scout could change the
EEPROM layout, as long as that configuration data does not move.

In general, however, it is recommended to always keep the EEPROM layout
the same, and just change the value of specific bytes. This should be
sufficient for any automatic configuration that might happen in the
future.

=================================
Future versions and compatibility
=================================
The first byte contains the EEPROM layout version, in order to allow new
revisions of this layout to be specified in the future.

For compatibility, we only account for backward compatibility on the
scout side. This means the scout needs to be able to read older EEPROM
layout versions, but a newer EEPROM layout does not need to be readable
by an older scout firmware. If a scout encounters a newer EEPROM layout
than its firmware supports, it will simply skip the entire backpack and
flag an error to the user (suggesting to upgrade the scout's firmware).

Something similar holds for individual descriptors: If the scout
encounters a descriptor type it does not know about, it will skip the
entire backpack as well and flag an error to the user. It would seem
obvious to only skip the unknown descriptor, but that descriptor could
be essential to the backpack operation, so the user will have to upgrade
the scout's firmware anyway). Also, the descriptors do not explicitely
store their length, so a scout cannot actually skip a descriptor if it
does not understand it.

Finally, if the scout encounters an invalid value in a field (e.g., a
UART speed it does not know about), it should also skip the entire
backpack, since the layout will have a newer minor version than the
scout supports.

---------------
Future versions
---------------
There is technically no need for a future version to resemble older
versions at all, other than that it must have a version number as the
first byte. However, since the firmware running on the scout needs to
support all previously released EEPROM layouts, it makes sense to keep
the same general structure and mostly add new fields and data in order
to keep the parsing code simpler.

To support this, we split the EEPROM layout version into a major and
minor version (e.g., 1.0). Only the major version number is stored into
the EEPROM and it is raised on incompatible changes. For some changes,
only raising the minor version should be sufficient.

If a previously defined field is no longer valid, it should be marked as
deprecated, but not removed, to prevent all other fields from shifting
position. This needs a bump of the major version. Deprecated fields
should always contain all zeroes.

In the descriptors, dropping an old field entirely might also make sense
sometimes, to prevent it takiing up too much space.

If a new field needs to be added, it can be added in place of an older
deprecated field, or at the end if there is no old field. This needs a
major version bump, except when the conditions below are satisfied:

* There are bits available for this field which were previously marked
  as "reserved" or "deprecated".
* Scouts that do not support the new field and simply ignore it should
  not cause problems.
* Backpacks that do not support the new field and thus have all zeroes
  as the field content should not cause problems (e.g., all zeroes
  should be a sane default).

Furthermore, a new descriptor type can be added when only bumping the
minor version (since a scout that encounters an unknown descriptor type
will also flag an "unsupported EEPROM layout" error).

For the same reason, adding new values to an enumeration field (*e.g.*,
adding a new UART speed) can also happen with just a mnior version bump.

.. admonition:: Open Question: Configurable parameters

        In the future, we'd like to use the tiny to configure some
        parameters as well. Obvious usecase is to set an I²C address
        through some backpack bus command and have the tiny toggle the
        right pins on some chip.

        The question arises of how to describe in the EEPROM what
        toggles are available and how they affect the resources used. A
        single I²C address seems simple enough (just add an "address
        configurable" flag in the I²C descriptors), but things can get
        complicated when:

         - Not all bits of the I²C address are configurable (which will
           be so in practice).
         - A since configuration toggle will change (possibly different)
           bits in the addresses of two different devices (which seems
           reasonable, since the attiny only has a few pins to work
           with).

        For these reasons, it seems like a good idea to define
        "configuration" descriptors that define what other descriptor
        they change (possibly through their index, since repeating names
        is too verbose) and what part of that descriptor they change?
        This might get complicated real quick, though. An advantage of
        this is that we can just add these configuration descriptors
        later, though we might need to consider now what the effect on
        the other descriptors should be...
