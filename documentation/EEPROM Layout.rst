.. |vdots| unicode:: U+22EE

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
        | a        || Firmware version                                                                                     |
        +----------+| (2 bytes)                                                                                            +
        | b        |                                                                                                       |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+

.. admonition:: Open Question: Endianness

        What endianness should data in the EEPROM have? Big endian seems
        to be the data transfer / network order (and also makes more
        sense in my head), but GCC on AVR seems to do little endian.
        Perhaps just Big endian and make sure that conversion from
        EEPROM byte data to 16-bit integers just happens manual
        bitshifting instead of by pointer casting?

The backpack unique id contains a few subfields. See the Pinoccio
Backpack Bus Protocol specification for how the id is composed.

The Backpack unique id is read-only, meaning it cannot be changed
through the Backpack Bus protocol.

The firmware version listed is about the code running inside the
microcontroller in the backpack, that listens to the backpack bus and
allows accessing the EEPROM.

.. admonition:: Open Question: Firmware version

        What should we put inside the firmware version field? A
        particular challenge could be that the firmware development
        might branch for different types of backpacks. Initial simple
        backpacks will probably all use the tiny13 with an identical
        firmware version, but future backpacks might allow more things
        such as setting I²C addresses or toggling pins. It's entirely
        possible to have backpack-specific commands as well, which would
        also lead to backpack-specific firmwares.

        Perhaps this field should only identify firmwares released for a
        specific backpack (or rather, used during the production of a
        backpack, since we don't actually expect users to upgrade the
        tiny firmware)? Then one byte is probably sufficient.

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

Pins are numbered 0-31. Pin 0 is the pin top left, pin 16 is the pin top
right. On the v1.0 pinoccio boards, the top is where the USB connector
is.

All pin numbers are stored in a 5-bit field, which is exactly big enough
to adress all pins. However, in general, a few bits above every pin
number should be kept as reserved for future expansion.

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

----------------
Descriptor names
----------------
All descriptors can contain a string, which defines a short name for the
resource. This can be used by the user to easily access different pins
using a short name, as well as by library code running on the scout to
distinguish different resources.

Sometimes names are superfluous and can be omitted by setting its length
at 0. In this case, a default name is used, depending on the descriptor
type. Not all descriptor types allow omitting the name.

Every resource name used should be unique within the group it is in
(including within the implicit nameless group), so the group name
together with the descriptor name can be used to identify the resource
on the scout. Furthermore, each group must have a name that is unique
among all groups.

.. admonition:: Open Question: Name encoding

        What should be the encoding of these names? UTF-8 is the way of
        the future, but complete overkill here. Plain ASCII is obvious,
        since they are not intended to encode complicated text. Seems a
        pity to waste a bit there, though. Using 8859-1 could also work,
        but we really don't need anything other than letters, numbers
        and some other stuff. Perhaps try to fit in 6 or even 5 bits and
        squash together the characters to save space?

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
        | 1        | Name length (*namelen*)                           | *reserved*                                        |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        || 2       || Resource name                                                                                        |
        || |vdots| || (*namelen* bytes)                                                                                    |
        |          || |vdots|                                                                                              |
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
        | 1        | *reserved* | I²C address                                                                              |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 2        | Name length (*namelen*)                           | *reserved*              | Maximum speed           |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        || 3       || Resource name                                                                                        |
        || |vdots| || (*namelen* bytes)                                                                                    |
        |          || |vdots|                                                                                              |
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
        | 1        | *reserved*                           | Slave select pin number                                        |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 2        | Name length (*namelen*)                           | *reserved*                                        |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        || 3       || Resource name                                                                                        |
        || |vdots| || (*namelen* bytes)                                                                                    |
        |          || |vdots|                                                                                              |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+

.. admonition:: Open Question: SPI speed

        Since SPI does not have a well-defined specification and no
        standard speed grades, using a simple list of fixed maximum
        speeds like with I²C does not seem feasible. SPI devices usually
        have a maximum speed defined in their datasheet, which according
        to Wikipedia is commonly between 10kHz and 100Mhz.

        To cover the whole range in 1kHz granularity, you'd need a 27
        number, which is way to much for in a descriptor.

        Something more coarse should be found, probably something with a
        base and exponent value so the granularity drops when the value
        increases.

        The Pinoccio scout can only support speeds that are a power-of-2
        fraction of 8Mhz, so only specifying those would be efficient
        (the backpack can just select the fastest mode it can support).
        However, this is not very future-proof, perhaps a future Scout
        runs at 20Mhz (and then is forced to use 5Mhz for a slave that
        supports 10Mhz but had to specify 8Mhz in the descriptor) or a
        future scout might have even more flexibility in SPI speeds...

        I have't been able to find a satisfying solution so far...

.. admonition:: Open Question: Clock polarity, phase and data order

        Another distinguishing characteristic of SPI implementations is
        apparently the clock polarity and phase, commonly called CPOL
        and CPHA according to `wikipedia`__. Both values are binary, so
        just 2 bits would be sufficient to store these. They should
        probably be added, after the speed value has been decided on.

        __ http://en.wikipedia.org/wiki/Serial_Peripheral_Interface_Bus#Clock_polarity_and_phase

        Data order is a third characteristic, MSB first or LSB first.


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
        | 1        | *reserved*                           | Pin number                                                     |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 2        | Name length (*namelen*)                           | *reserved*                                        |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        || 3       || Resource name                                                                                        |
        || |vdots| || (*namelen* bytes)                                                                                    |
        |          || |vdots|                                                                                              |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+

Any pins that are specified by other resources (e.g., MISO or the CS pin
in an SPI resource) do not also need to be explicitly specified as an
I/O pin resource.

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

.. admonition:: Open Question:: Multiple I/O pins

        When multiple pins are used, having to specify them each
        individually seems cumbersome. Grouping them together seems
        sensible. Possible options are:

         - Add a four-byte bitmask field that can include any of the
           pins. This has the downside that there is no explicit
           ordering.
         - Add a pin count field and then add that number of 5-bit pin
           number fields.

        This seems to mostly make sense when all pins belong to a
        logical group, like a data or address bus and share a common
        name and probably also usage info (otherwise, having to
        duplicate all that in a single descriptor isn't much more
        efficient than having multiple descriptors).

        For now, it seems we don't need this yet, but the e-ink backpack
        seems like it might benefit from this with 11 pins (none of them
        seem to be a logical bus, though).

.. admonition:: Open Question: Default pin name

        Should this descriptor get a default name? It seems there is no
        sane default, just "pin" does not make sense when listing
        resources or talking to the pin (unlike the defaults for spi and
        i2c, for example). Seems sane to just require a name?

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
        | 1        | *reserved*                           | TX pin number (from backpack point of view)                    |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 2        | *reserved*                           | RX pin number (from backpack point of view)                    |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        | 3        | Name length (*namelen*)                           | Speed                                             |
        +----------+------------+------------+------------+------------+------------+------------+------------+------------+
        || 4       || Resource name                                                                                        |
        || |vdots| || (*namelen* bytes)                                                                                    |
        |          || |vdots|                                                                                              |
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

.. admonition:: Open Question: Control signals

        Should there be any way to specify serial control signals?  It
        seems any actual handshaking signals are seldomly used in these
        kinds of systems, the HardwareSerial code doesn't even know
        them. Just leave this for a future version if we need it?

Power mode
""""""""""

.. admonition:: Open Question:: How should this work?

        This seems to be a complicated part of the spec still. Below was
        the initial proposal for a descriptor, that lists one of a few
        power modes that can be enabled, but I'm not sure yet how this
        stuff should work or be used...

This describes the power usage of (a part of) the backpack in a particular mode.

========  =======  ===========================
Byte(s)   Bits     Meaning
========  =======  ===========================
0                  Descriptor type
1         0-4      Power pin number
1         5-7      Reserved
2                  Minimum power usage
3                  Typical power usage
4                  Maximum power usage
5-...              Mode name
========  =======  ===========================

For most backpacks, these descriptors will be mostly informative for the
user or firmware (i.e., the wifi backpack can see if it there is enough
power available before starting transmission).

All the power modes within the same group are mutually exclusive, only
one of them can be active at the same time.

For some backpacks, some power modes might need to be explicitely
enabled through the backpack bus (i.e., the attiny needs to disable a
voltage regulator or IC for them). This could use bit 1.7 as a flag to
indicate this need (but it would also need its own command in the
protocol, so this is left as a TODO).

Data
""""
This is a descriptor type that is not added during manufacturing, but
can be added by the scout to store arbitrary information. The structure
of this data is not defined at all, it is up to the scout to interpret
this.

TODO: layout (data length and arbitrary data).

.. admonition:: Rationale:: Custom data

        This descriptor could be used by the scout to store arbitrary
        data, such as calibration or configuration settings.

        It is expected that this data can be used by a backpack-specific
        library to store things. No attempt is made to uniquely label
        the data for a given purpose: it is expected that the code
        running on the scout for a given backpack will know how to read
        and write this data and that it will be the same code that
        access the data every time.

.. admonition:: Open Question:: Multiple data types

        Would it make sense to have a dozen or so data types, so a
        library can store different kinds of data without having to add
        another "subtype" byte?

        Or would it perhaps make sense to give this descriptor a name as
        well and use that to identify subtypes?

Checksum
""""""""
The last descriptor in the EEPROM is always of this type and contains a
checksum, calculated over all previous bytes.

TODO: Document layout and pick CRC poly

.. admonition:: Rationale:: Checksum algorithm

        See the Backpack bus specification for some more background on
        checksum algorithm selection.

.. admonition:: Open Question: CRC length

        Is a single byte CRC enough? Probably two bytes (16 bit CRC)
        provides some more protection, without being out of proportion
        (two bytes out of 64 bytes of total EEPROM on the initial attiny
        used).

        It also seems that sharing a CRC implementation with the 8 bit
        CRC inside the unique identifier is still feasible.

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

TODO: Define somewhere that the scout should error out on unknown
descriptor types or field values.

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
