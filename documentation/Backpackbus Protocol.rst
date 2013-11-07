==============================
Pinoccio Backpack Bus Protocol
==============================
This protocol is intended to allow a Pinoccio scout discover all
backpacks connected to it and to allow communicating with these
backpacks.

A central pillar in the protocol is the bus enumeration. Each slave has
a globally unique identifier, which is used for bus arbitration during
the bus enumeration part of the protocol. During enumeration, each slave
gets assigned a single-byte address which is used to address it during
normal communication (since always using the long unique identifier
results in too much overhead).

Another pillar of the protocol is the commands to read and write the
EEPROM embedded in the slave. This EEPROM will contain metadata about
the backpack, possibly contain some resource allocation settings and can
be used by the scout to store arbitrary settings if any free space
remains. The layout of the EEPROM contents is not defined by this
document, but is the subject of separate specification.

The bus is a strict master-slave bus, where the Pinoccio scout is the
master and all backpacks are slaves. All communication is initiated by
the master and even when the slaves are sending a bit, the master
iniates the start of every bit.

The bus is designed to typically transfer 1400 bits per second (though
it can be a bit slower, depending on exact clock rate of the slaves).
After subtracting parity, flow control and error reporting overhead,
this results in up to 116 bytes per second of throughput (8.6ms per
byte).

This document describes version 1.0-draft of the protocol. It is still a
draft and as such open for change.

========
Hardware
========
The bus is an open-collector bus. This means there is a pull-up resistor
that pulls the line high when idle and each device has its bus pin in
high-impedance / open collector mode. When a device wants to pull the
line low, it grounds the bus pin, making the the bus go low and causing
a small current to flow through the pull-up resistor.

The timings of the protocol have been chosen such that they will work
under the following conditions:

* Bus capacitance up to 400pF
* Pull-up resistor up to 125kΩ
* Slave clock inaccuracy up to 10%

============
Reset signal
============
To properly synchronize all devices on the bus, the master always start
a new transaction by sending a reset signal. To do so, the master pulls
the line low and keeps it low for a longer period. The slaves always
detect this signal, regardless of what they were doing before.

To detect this signal, a slave samples the line at a fixed amount of
time after every falling edge on the bus, unless another falling edge
occurs before the reset sample time. If the slave finds the line is low,
a reset has happened.

After a reset signal, a slave must read a single address byte from the
bus to see if it is addressed.

.. admonition:: Rationale: Reset on every transaction

        Making the master perform a reset signal that is easily
        distinguishable from any regular communication has two distinct
        advantages:

        * If a slave is somehow confused about the current bus state
          (perhaps because it was powered on halfway through a
          transaction), it will always resynchronize at the next reset
          signal.
        * If a slave is not addressed or does not support a particular
          command, it can simply stop paying attention to the bus until
          the next reset signal. The alternative would be to make every
          slave follow all communication so they could decide when the
          transaction is finished and a new one should begin, requiring
          extra overhead in the protocol or requiring every slave to
          always know about the complete protocol, even parts that are
          not relevant to it.

====================
Ending a transaction
====================
A transaction can be ended by leaving the bus high for more than a
specified amount of time. When a slave does not detect a falling edge on
the bus for more than this time, it becomes idle. When a slave is idle,
it responds only to a reset signal and nothing else.

To make sure that a transaction is not ended accidentally halfway, the
protocol defines a maximum time between to subsequent bit starts.
Furthermore, the timings have been chosen such that the slave can decide
the bus was idle for long enough at the same time it samples the bus for
a reset condition.

Alternatively, a transaction can also be ended by a reset signal, which
at the same time starts the next transaction. In this case, the slave is
still expecting to send or receive a next byte and will see the falling
edge as the start of the next bit. However, since this will at most
cause the slave to process one bit and never a full byte, this should
not cause any adverse effects.

.. admonition:: Rationale: Ending a transaction

        The protocol must some way to explicitely define the end of a
        transaction, to prevent the slave from staying in some other
        state for potentially a long time. Consider for example writing
        some data to the EEPROM. If the master would simply stop after
        receiving the last ack signal, the slave would still wait for
        the next byte to send. If now a glitches on the bus would
        occur, the slave could think it saw a falling edge and read a 1.
        If this happens 9 times, it would have read a full byte and
        write 0xff to the EEPROM.

        It seems this could be mitigated by just sending a reset signal
        at the end of every transaction, but this would cause the slave
        to remain in the "read address byte" state where it could
        eventually read a 0xff address byte.

        For this reason, the slave should fall back to the idle state
        after some time. A short glitch on the bus cannot accidentally
        trigger a reset signal, so this should be safe.

=========================
Transmitting a single bit
=========================
The sending and receiving of a single bit works by pulling the line low
either for a short or long period.

Every bit starts with the master pulling the line low.

When the master wants to send a 0, it keeps the line low for a long
period. When the master wants to send a 1, it releases the line after a
short period.

The slave devices will see the falling edge on the bus, wait for a fixed
amount of time and then sample the bus. If the bus is still low, it
receives a 0, if the bus is high again it's a 1.


When it's the slave's turn to send a bit, the master also pulls the line
low for a short while.

The slave sees the falling edge and, if it wants to send a 0, pulls the
line low and keeps it low for a long period. If it wants to send a 1, it
simply does nothing.

The master then waits a fixed amount of time and samples the bus after
that: If the bus is still low, it receives a 0, if the bus is high again
it receives a 1.

.. admonition:: Rationale: Synchronizing every bit

        The protocol makes the master synchronize the start of every bit
        by letting the line float high after every bit and pulling it
        low at the start of every bit. This is done to make the timing
        constraints on the slave very relaxed, so it does not a very
        precise system clock.

        The most obvious alternative, a regular UART-style serial
        protocol with a start bit and then just fixed-width databits is
        likely to cause desynchronisation by the time the last bit is
        being transmitted if the slave's clock is imprecise.

        The initial implementation of this protocol uses an Attiny chip
        with an internal oscillator, which can have a clock inaccuracy
        of as much as +/- 10%, preventing a regular UART style protocol
        from working properly.

-----------
Bus timings
-----------
The exact timings for the protocol are defined below.

===================  ========  ========  ========
Duration             minimum   typical   maximum
===================  ========  ========  ========
Master reset         1800μs    2000μs    2200μs
Slave sample reset   1200μs    1450μs    1700μs

Master send 1        50μs      100μs     200μs
Master send 0        600μs     650μs     700μs
Slave sample data    250μs     350μs     450μs

Slave send 0         500μs     650μs     800μs
Master sample data   300μs     350μs     400μs

Next bit start       700μs               1100μs
Bus idle time        50μs
===================  ========  ========  ========

All time values indicate a duration from the bit start (the falling edge
on the bus), except the "Bus idle time", which indicates the minimum
time the bus should be high between bits (which can cause the "Next bit
start" to become more than its minimal value if the slave or master
exceeds its "send 0" time).

Implementations should make sure that, under nominal circumstances, the
durations are implemented like shown in the typical column.
Additionally, under extreme circumstances (*e.g.*, oscillator
inacurracy, environmental temperatures, etc.) the values should be
guaranteed to lie within the minimum and maximum.

.. admonition:: Rationale: Timings

        When choosing the timings for the bus, the master is assumed to
        have an accurate crystal, with negligable deviations from the
        nominal frequency. The master timings simply allow +/- 50μs, so
        the exact software implementation does not need to jump through
        hoops to get very exact timings. The reset duration has a bit
        more allowance, simply because the actual duration doesn't
        matter much.

        For the slave, the minimum and maximum are more relaxed, to
        allow slaves to use a less accurate RC oscillator for their
        clock.

        Finally, care is taken to guarantee at least 50μs between every
        bus change and sample moment, to allow for bus rise time (125kΩ
        · 400pF is about 50μs).

        Detailed timing calculations that formed the basis of these
        calculations are available as `a separate spreadsheet`_.

.. _a separate spreadsheet: https://docs.google.com/spreadsheet/ccc?key=0AkzdEQpvWpTbdGU2RHAzN2NUTXB1Y25wdXJFelZqb3c&usp=sharing

===================
Transmitting a byte
===================
Transmitting a byte happens by transmitting each of the bits in turn,
most significant bit first. A parity bit is added for some basic error
detection. Furthermore, a number of handshaking bits are added to
prevent overwhelming a slow slave and to allow the slave to signal
errors.

.. admonition:: Rationale: MSB-first

        A byte is transmitted MSB-first (unlike most serial protocols,
        which transmit LSB-first) so that the bus enumeration happens in
        order of increasing unique identifier. Also, reading a trace of
        the bus is easier during debugging.

------
Parity
------
The parity used is odd, meaning that the total number of ones in the
data bits plus parity bit should be odd.

When a slave receives a byte with an incorrect parity value, it should
complete send the stall and ready bits as normal and then send a nack
bit and the "Parity error" error code (see below).

.. admonition:: Rationale: Odd parity

        The parity bit is chosen such that when no slaves are participating on
        the bus and a byte is read (returning all ones), the parity bit will be
        ok. This prevents parity errors on the last round of the bus
        enumeration.

---------------
Stall and Ready
---------------
After the parity bit, any number of stall bits can be sent by the slave
to indicate it is still processing the previous byte of data, or
preparing to send or receive the next byte of data. When the slave is
done processing, it sends a ready bit.

A stall bit is sent as a 0 and a ready bit is sent as a 1, so the master
can just receive a bit and then tell if the slave sent a stall or ready
bit. A slave can send any number of stall bits (including none), always
followed by exactly one ready bit. After the ready bit, the slave can no
longer send stall bits, so it should make sure it is prepared well
enough to handle the next sequence of bits without delay.

When a slave sends a ready bit, it should also sample the bus to see if
any other slave is still sending a stall bit. If so, it should keep
trying to send its ready bit until no conflict occurs (meaning all
slaves are sending a ready bit) and then continue with the ack or nack.

.. admonition:: Rationale: Stall and ready bits

        Adding stall and ready bits allows a slave to take its time
        processing a command. For the currently defined commands and the
        first implementation, all commands should be processed fast
        enough to be processed in the idle time between bits, but adding
        these bits allows other implementations to be slower, or future
        commands to have a variable processing time.

        The stall bit is sent as a 0, so that during bus enumeration the
        master and any ready slaves can detect if *any* slave is sending
        a stall bit. This ensures that the transmission is stalled as
        long as any slaves are still processing and only continues when
        all slaves are ready.

------------
Ack and Nack
------------
After the ready bit, the slave either sends an ack or a nack.

A slave sends an ack by sending a 0 followed by a 1. A nack is the
reverse, a 1 followed by a 0.

Under normal circumstances, the slave sends an ack and the devices
continue with the next byte. However, when some error condition occurs,
the slave can send a nack. This can happen when for example:

* A parity erorr occured
* The previous received byte did not make sense to the slave (e.g., unknown
  command, invalid address, etc.)
* There was an error processing the previous byte (e.g. EEPROM write
  error)
* There was an error preparing the next byte (e.g. EEPROM read error)

After a nack was sent, the slave sends one more byte, which contains an
error code. The error code byte should be followed by the regular
handshaking bits, except that the slave may not send a nack bit for it.

If the master receives a nack for an error code anyway, it must not
continue to read *another* error code, it should instead end the
transaction.

After the slave completed sending the error code byte, including the
handshaking bits, it drops off the bus. The master should end the
transaction (and possibly try again).

The error code sent can be a generic error code, which has the same
meaning no matter what state the slave is in. There are also
command-specific error codes, which are only valid during the execution
of a particular command (including when a nack is sent in response to
the command byte itself).

Generic error codes are numbered from 1 upwards, while command-specific
error codes are numbered from 255 downwards.

.. admonition:: Rationale: Error code numbering

        By splitting the error codes and counting from the outside in,
        we're sure to never run out of room for either of the
        categories, at least not until all 255 error codes are taken.

        Furthermore, keeping error code 0 reserved allows
        implementations to use that code internally to mean "no error".

.. table:: Generic error codes

        ======  =================
        Code    Meaning
        ======  =================
        0x0     Reserved
        0x1     Other error
        0x2     Other protocol error
        0x3     Parity error
        0x4     Unknown command
        ======  =================

If a master receives a nack when multiple slaves are still participating
(e.g., after sending the address byte, or during bus enumeration), it
should not try to read an error code but end the transaction
immediately.


.. admonition:: Rationale: Two bits for ack/nack

        Making the ack and nack two bits instead of one allows the master to
        distinguish four different cases:

        1. All participating slaves are sending an ack
        2. All participating slaves are sending a nack
        3. No slaves are participating
        4. Some slaves are sending an ack, some slaves are sending a nack

        Usually only one slave will be participating, making only case 1 - 3
        meaningful. However, during bus enumeration, multiple slaves will
        participate and case 4 allows the master to detect when *any* device is
        sending a nack.

To summarize, sending a byte from the master to a slave works by sending
these bits in the following order:

====  =========  =========
Bits  Direction  Purpose
====  =========  =========
8     M → S      Data
1     M → S      Parity
0+    S → M      Stall
1     S → M      Ready
2     S → M      Ack or Nack
====  =========  =========

Sending a byte from a slave to the master is the same, except the
direction of the data and parity bits is reversed (all the handshaking
bits are always slave-to-master.

====  =========  =========
Bits  Direction  Purpose
====  =========  =========
8     S → M      Data
1     S → M      Parity
0+    S → M      Stall
1     S → M      Ready
2     S → M      Ack or Nack
====  =========  =========


================
Slave addressing
================

After every reset signal, the master starts by transmitting the
single-byte address of the slave it wants to talk to. The slave whose
address was sent keeps paying attention, all other slaves drop off the
bus until the next reset signal.

If the master sends the special address 255 (0xff) all slaves will forget
their current address (if any) and switch into bus enumeration mode to
get a new address.

Valid slave addresses are 1 to 127 (0x7f). Addresses 128 (0x80) to
255 (0xff) are reserved for broadcast commands and potentially other
future uses.

Slaves can assume that the master will never enumerate more than 128
devices, so they do not need to check if their address would become
invalid.

When a slave receives an unknown broadcast command, it should drop off
the bus and not send any handshaking bits.

.. admonition:: Rationale: Number of slaves

        This approach allows 128 slaves to be connected to the bus, which should
        be plenty. Also, it allows checking bit 7 to distinguish between address
        and broadcast command, which might be useful at some point. Having 128
        possible broadcast commands available is probably more then ever needed,
        though.

        Address 0 is reserved so there is at least one value that is
        never a valid address, which might be useful for
        implementations.

===========  =====================
Adress       Meaning
===========  =====================
0            Reserved
1 - 127      Slave addresses
128 - 254    Reserved
255          Start enumeration
===========  =====================

=================
Unique identifier
=================
Every slave has a fixed and globally unique identifier, which is 8 bytes
long. This identifier should be different for every slave device
produced and allows doing bus enumeration in a deterministic way.

The unique identifier consists of the following parts:

=====  =======================
Bytes  Meaning
=====  =======================
0      Protocol major version number
1-2    Model identifier
3      Hardware revision
4-6    Serial number
7      A checksum
=====  =======================

When writing down unique identifiers, the convention is to make byte 0
the most-significant byte. Any subfields are written in big-endian
order. For example, the unique ID 0x01abcd0300000159 expands to:

=========  =====================
Value      Meaning
=========  =====================
0x01       Protocol major version number
0xabcd     Model identifier
0x03       Hardware revision
0x000001   Serial number
0x59       A checksum
=========  =====================

Model identifiers are assigned by the Pinoccio company on request. A
list of identifiers in use will be published somewhere. The hardware
revision field should start at 01 and be incremented whenever a
significant change in the hardware is made. The serial number should
start at 000001 whenever the hardware revision is incremented and is
unique for a given model and revision.

The checksum byte is calculated using the CRC algorithm with the 7
preceding bytes as input data. The CRC variant used is a non-standard
one, as proposed by P. Koopman in the paper `CRC Selection for Embedded
Network Messages`_. The parameters for this CRC variant are below,
expressed in terms of the Rocksoft model (see `A PAINLESS GUIDE TO CRC
ERROR DETECTION ALGORITHMS`_).

==============  ========
Parameter       Value
==============  ========
Width           8 bits
Polynomial      x⁸+ x⁵ + x³ +x² + x + 1
Poly in hex     0x2f (Rocksoft) / 0x97 (Koopman)
Initial value   0x0
Reflect in      No
Reflect out     No
Xor out         0x0
Check           0x3e
==============  ========

.. admonition:: Hint: Pycrc options

        For reference, pycrc is a python library and program that can be
        used to calculate arbitrary CRC values and generate C code for
        them. The commands used to generate the example checksum and
        check value above are::

                pycrc --width=8 --poly=0x2f --xor-in=0 --reflect-in=false \
                      --reflect-out=false --xor-out=0 \
                      --check-hexstring 01abcd03000001
                pycrc --width=8 --poly=0x2f --xor-in=0 --reflect-in=false \
                      --reflect-out=false --xor-out=0 \
                      --check-string "123456789"

.. admonition:: Rationale: Checksum algorithm

        The choice of checksum algorithm was made based on a few
        sources. The paper `The Effectiveness of Checksums for Embedded
        Networks`_ compares CRC checksums with simple XOR and addition
        checksum schemes and shows that CRC is dramatically more
        effective at the expense of more computation (about four times
        for table-based implementations).

        Given that this CRC will only be used during bus enumeration, it
        should be ok to invest a bit more processing power. Looking at a
        typical non-table based implementation (in particular, the
        bit-by-bit-fast code generated by the pycrc tool), it should be
        able to run on an AVR at around 200 cycles per data byte. In
        terms of this protocol, that means that it can calculate the CRC
        of 60 bytes of data in the time a single bit is transmitted, so
        that should be more than fast enough.

        Regarding the actual CRC variant (polynomial) to use, the paper `CRC
        Selection for Embedded Network Messages`_ shows that the
        effectiveness of a given polynomial heavily depends on the data
        length.

        For the unique identifier, which has 56 data bits, the 0x97
        polynomial is suited, sine is optimal (according to some
        average measure, of course) for 10 to 119 databits.

        An alternative would be the 0x98 polynomial, which is optimal
        for 42 to 119 databits, used in the One-Wire protocol for the
        same purpose and for which an optimzed implementation is
        available in avr-libc.

        However, the entire EEPROM will also need a checksum. Since the
        EEPROM has a data length of 512 bits (64 bytes) and possibly
        more in the future, neither of these polynomials will suffice
        for that. Another one, like for example the 0xa6 polynomial,
        which is optimal for more than 210 bits of data, makes sense
        there.

        For this reason it also makes sense to include generic CRC
        implementation that works for an arbitrary polynomial in the
        scout's firmware instead of using the avr-libc implementation
        for the unique identifier and another implementation for the
        EEPROM checksum.

.. _The Effectiveness of Checksums for Embedded Networks: http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.72.4059&rep=rep1&type=pdf
.. _CRC Selection for Embedded Network Messages: http://www.ece.cmu.edu/~koopman/crc/
.. _A PAINLESS GUIDE TO CRC ERROR DETECTION ALGORITHMS: http://www.csm.ornl.gov/~dunigan/crc.html

===============
Bus enumeration
===============
The bus enumeration happens in rounds. In each round, all unenumerated
slaves will transmit their unique id to the master.  At the end of the
round there will be always exactly one slave that transmitted its
address without conflicts, which will (implicitly) claim the next
address. The rest of the slaves participate in the next round. This
continues until all slaves have claimed an address.

During a round all unenumerated slaves will transmit their unique id to
the master. As long as all slaves transmit the same bit values, this
happens without conflicts. Eventually, this will result in a conflict on
the bus, where some slaves try to transmit a 0, while others transmit a
1. The physical design of the bus will make sure that if this happens,
the 0 will "win". In other words, if at least one slave is
transmitting a 0, the bus will be pulled low and read as a 0.

To handle these conflicts, a slave that wants to transmit a 1 reads the
bus to see if anyone else is transmitting a 0. If so, it will stop
transmitting their unique id for this round, allowing the slaves that
transmitted a 0 to continue. Since all slaves will have a different id,
there will eventually be exactly one slave that finishes transmitting
its address. Furthmore, this slave will know that it completely sent
its address without conflicts. This slave assigns itself the next
address, drops off the bus and is now considered enumerated.

The addresses are assigned in order: The slave that completes the first
round gets address 1, the slave that completes the second round gets
address 2, etc.

The master will continue reading unique ids from the bus, until it reads
a first byte of 0xff for which it receives neither an ack nor a nack.
This means every slave has dropped off the bus and it's only reading the
pullup values, so enumeration is now complete.

After enumeration:

* The master knows the unique ids of all attached devices.
* Every device now has a short address that is unique on the bus.

Note that it will always be the slave with the lowest unique id that
"wins" a round, so the slaves are always enumerated in order of
increasing id. This is pretty much the same way I²C also handles
arbitration of the bus on regular transmissions.

Transmitting the unique identifier transmits the bytes of the identifier
in order, starting with byte 0 (the protocol version number).

========
Commands
========
When the master sends a regular address byte (< 128), the addressed slave will
read another byte from the bus to find out what it is supposed to do.

If the addressed slave reads a command that it does not understand, it
will send a nack and the "Unknown command" address byte.

Initially, only two commands are defined:

====   =======
Byte   Command
====   =======
0x00   Reserved
0x01   READ_EEPROM
0x02   WRITE_EEPROM
====   =======

.. admonition:: Rationale: Supported commands

        Initially, the backpack bus will be used only to retrieve
        metadata about the backpack, so reading and writing the EEPROM
        should be enough. It would seem that writing isn't even needed,
        but when a user makes a hardware modification to a backpack (for
        example to reroute a pin), he will need to update the EEPROM
        contents to reflect this.

        Future protocol versions are expected to add commands which
        allow the attiny to actively do things as well, such as
        controlling I²C address pins or multiplexers to reroute pins.

-----------
READ_EEPROM
-----------
The slave reads a one-byte EEPROM address from the bus
and then starts to send EEPROM contents starting from that address.
After the first byte, it continues to send subsequent bytes as long as
the master keeps reading data.

When the address byte sent is beyond the end of the EEPROM, a nack is
sent with an "Invalid address" error code.

When the last byte of the EEPROM is read, that byte is sent as normal,
followed by a nack and the "end of EEPROM" error code. In this case, the
byte itself is valid, but no further bytes can be read.

.. admonition:: Open Question: End of EEPROM error?

        Should this work like described, or should the last byte be
        acked as normal and the next byte send dummy data and a nack?
        The latter seems to make some more sense, but is probably harder
        to implement. Also, the meaning of the nak bit was also to
        indicate an error with processing the previous byte or preparing
        the next byte, and the error code clarifies which of the two
        cases is actually happening...

        Same thing applies to EEPROM write.

=====  =========  =========
Bytes  Direction  Purpose
=====  =========  =========
1      M → S      Slave address
1      M → S      EEPROM address
0+     S → M      EEPROM data
=====  =========  =========

.. table:: Command-specific error codes

        ======  =================
        Code    Meaning
        ======  =================
        0xff    Invalid address
        0xfe    End of EEPROM
        ======  =================

------------
WRITE_EEPROM
------------
The slave reads a one-byte EEPROM address from the bus and then
starts to read data from the bus. This data is written to the
EEPROM, starting at the given address and upwards as long as the
master continues to transmit bytes.

When the address byte sent is beyond the end of the EEPROM, a nack is
sent with an "Invalid address" error code.

When the last byte of the EEPROM is written, that byte is received and
written as normal, followed by a nack and the "end of EEPROM" error
code. In this case, the byte was succesfully written, but no further
bytes can be read.

Some bytes in the EEPROM might be read-only and cannot be written.
Typically, the bytes storing unique id cannot be changed through this
command, but which bytes this concerns exactly is defined by the EEPROM
layout specification

When the WRITE_EEPROM command is used to write a read-only byte and the
value to write is different from the current value, the slave sends a
nack and the "Read-only byte" error code. If the value is not changed,
the slave sends an ack just as if the byte was written succesfully.

If the byte cannot be written for any other reason, the "Write failed"
error code is returned.

=====  =========  =========
Bytes  Direction  Purpose
=====  =========  =========
1      M → S      Slave address
1      M → S      EEPROM address
0+     M → S      EEPROM data
=====  =========  =========

.. table:: Command-specific error codes

        ======  =================
        Code    Meaning
        ======  =================
        0xff    Invalid address
        0xfe    End of EEPROM
        0xfd    Read only byte
        0xfc    Write failed
        ======  =================

=================================
Future versions and compatibility
=================================
Because it is expected that this protocol will be extended and changed,
a version number was introduced. The protocol version consists of a
major and a minor version number.

A slave advertises only the major part of the protocol version it
supports, so every potentially incompatible change in the protocol needs
to raise the major version number. The minor version number can be used
for clarifications and small changes to the protocol that do not cause
problems with slaves still running an older version of the protocol.

With regard to compatibility, a slave is required to implement only one
specific version of this protocol, as advertised in its unique
identifier. The master, on the other hand, is expected to know about all
previous versions as well, so it can also talk to slaves running an
older version of the protocol.

With regards to future versions of the protocol, the most basic
requirement is of course that a collection of slaves with any
combination of protocol version should still work. In particular, that
means that all future versions of the protocol should use the same:

* reset signal
* addressing mechanism
* bus enumeration mechanism

Technically, anything beyond that can be changed in future protocol
version. In the extreme, the byte framing and even bit meanings could be
changed once a single slave has been addressed, since all other slaves
will have stopped listening to the bus (as long as the reset signal is
not re-used).

In practice however, the changes made in future protocol versions
should not be so invasive. In particular, future versions are expected
to mostly add new commands, while possibly deprecating old commands.
Replacing or changing a commands is also possible, but should be done
with care to prevent confusion and an overly complicated implementation
at the master side.
