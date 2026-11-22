# Protocol — BLE multimeter frames

Wire protocol observed from a Bluetooth Low Energy multimeter. Device names are
not part of the protocol: the only unit tested in this project has series id 61
and firmware 1.4.0. Only behaviour reproduced on that unit is described here;
anything not established is **unknown** and is not implemented.

## Confidence markers

| Marker | Meaning |
| --- | --- |
| **verified** | reproduced on hardware: a fixture in `tests/fixtures/` or a replay test pins it |
| **unknown** | not established. Not implemented, never guessed, never emitted |

| Unit | Series id | Firmware | Frame size |
| --- | --- | --- | --- |
| Tested unit | 61 | 1.4.0 | 15 byte |

Other series ids may be parsed for diagnostics, but neither their frame size nor
their field meanings are claimed.

---

## 1. Transport

The meter is a GATT peripheral advertising the primary service
`0000fff0-0000-1000-8000-00805f9b34fb`; clients are expected to filter scans on
that service.

**Verified:** one connection at a time; the meter refuses new connections briefly
after a previous client disconnects; the central sets the MTU (517 observed, the
meter never requests one); the address type is public.

BLE mode is not permanent. It is switched on from the device (hold the link key
until the Bluetooth indicator appears) and while it is on the meter's automatic
power-off is suspended. When the mode is left, the meter stops advertising.

### GATT characteristics

| Short | Role | Properties | Payload |
| --- | --- | --- | --- |
| `fff1` | command channel | `read`, `write`, `write-without-response` | 16 bytes |
| `fff2` | device identity | `read` | 16 bytes |
| `fff3` | front-panel keys | `write` (with response) | 2 bytes |
| `fff4` | measurement stream | `notify`, CCCD `2902` | 15 bytes |
| `fff5` | unused | `read` | 1 byte, reads `00` |

Keys and general commands use **different** characteristics. A key payload
written to `fff1` is accepted and ignored (**verified**).

## 2. Connection sequence

1. Connect and discover the `fff0` service.
2. Read `fff2`; the payload determines the series id and firmware (§3).
3. Subscribe to `fff4` by writing `01 00` to its CCCD.
4. Notifications start within a few hundred milliseconds; frames arrive at
   roughly 5 Hz (**verified**).
5. Commands may be written at any later point; no handshake is required (§7.4).

## 3. Device identity (`fff2`)

16 bytes, read only:

| Offset | Meaning |
| --- | --- |
| 0 | series id |
| 1 | reserved on the tested unit, reads `00` |
| 2..4 | firmware version, three decimal components |
| 5 | configuration byte, meaning **unknown** |
| 6..15 | zero |

Example (**verified**): `3D 00 01 04 00 00 00 00 00 00 00 00 00 00 00 00` — series
61, firmware 1.4.0.

Only series 61 is verified. The decoder implements only the 15 byte layout
below; any other frame size is reported as unknown.

## 4. Frames on `fff4`

### 4.1 Layout

15 byte frame (**verified**):

| Offset | Width | Field |
| --- | --- | --- |
| 0..2 | 24 bit LE | gear word |
| 3..5 | 24 bit LE | value word |
| 6..8 | 24 bit LE | gear word 2 |
| 9..11 | 24 bit LE | value word 2 |
| 12..14 | 24 bit LE | state word |

Notifications may be concatenated or split by the stack; a client must buffer and
emit 15 byte records.

### 4.2 Gear word

| Bits | Meaning |
| --- | --- |
| 0..2 | decimal point code |
| 3..5 | SI prefix code |
| 6..10 | gear code |
| 12 | the second gear/value pair is meaningful |

Decimal point codes (**verified**):

| Code | Scale |
| --- | --- |
| 0 | 1 |
| 1 | 0.1 |
| 2 | 0.01 |
| 3 | 0.001 |
| 4 | 0.0001 |

Codes 0..4 are **verified**; other decimal point codes are **unknown** and a
frame carrying one is not decoded.

SI prefix codes:

| Code | Symbol | Factor |
| --- | --- | --- |
| 0 | p | 1e-12 |
| 1 | n | 1e-9 |
| 2 | µ | 1e-6 |
| 3 | m | 1e-3 |
| 4 | (none) | 1 |
| 5 | k | 1e3 |
| 6 | M | 1e6 |
| 7 | G | 1e9 |

All eight values are mapped to the standard SI magnitude of the confirmed
measurement function. Verified on hardware: n (capacitance), m, none, k and M.
The other magnitudes are implemented from the SI definition and are
**suspected**; capture a frame on that range to promote them.

Gear codes (**verified**):

| Code | Gear | Code | Gear |
| --- | --- | --- | --- |
| 0 | DCV | 7 | duty cycle |
| 1 | ACV | 10 | diode |
| 2 | DCA | 11 | continuity |
| 3 | ACA | 13 | NCV |
| 4 | resistance | | |
| 5 | capacitance | | |
| 6 | frequency | | |

Every other gear code is **unknown** and is not decoded.

### 4.3 Observed ranges

The fixtures exercise these (decimal point, prefix) combinations. Other SI
magnitudes of the same functions are mapped from the prefix field; see §4.2.
Other functions and decimal point codes are **unknown** and are not decoded:

| Function | Observed (dp, prefix) |
| --- | --- |
| DC voltage | (4, none), (3, none), (2, none), (1, none), (3, milli), (2, milli) |
| AC voltage | (4, none) |
| DC current | (0, none), (1, none), (2, none) |
| AC current | (2, none) |
| Resistance | (2, none), (2, kilo), (2, mega), (3, kilo), (3, mega), (4, kilo), (4, mega) |
| Capacitance | (1, nano), (2, nano), (3, nano) |
| Frequency | (2, none), (2, kilo), (3, none), (3, kilo), (3, mega), (4, kilo), (4, mega) |
| Duty cycle | (1, none) |
| Diode | (4, none) |
| Continuity | (1, none) |
| NCV | (1, none) |

### 4.4 Value word

24 bit (**verified**):

| Bits | Meaning |
| --- | --- |
| 0..18 | magnitude |
| 19 | unused |
| 20..22 | status: 0 normal, 1 overload |
| 23 | sign, negative when set |

Status codes 2..7 are **unknown**; a frame carrying one is not decoded.

The displayed value is `magnitude × decimalPointScale`. The supported units
already carry their SI prefix, so no separate prefix factor is applied.

### 4.5 State word

Only these indicators are confirmed:

| Bit | Indicator |
| --- | --- |
| 0 | HOLD |
| 1 | REL (relative measurement) |
| 2 | AUTO |
| 5 | Min/Max/Avg view |
| 18 | oscilloscope |

Every other bit is **unknown**. The decoder ignores unknown state bits instead
of naming them. Bit 5 (`0x20`) is the Min/Max/Avg view (**verified**): it was set
in three captures whose display showed that view and clear in every other
capture. Bit 1 (`0x02`) is REL (**verified**): it was set in a DC mV capture
whose display showed the relative value with a stored reference, and clear in
every other capture.

### 4.6 Second gear/value pair

Bit 12 of the first gear word decides whether the second pair carries a second
reading (**verified** in both directions):

* **bit 12 set** — the pair is a second measurement. Frequency mode sends the
duty cycle next to the frequency:

  ```
  A3 11 F0 | 00 00 00 | E1 11 F0 | 00 00 00 | 04 00 00
  ```

  The primary gear word `0xF011A3` is decimal point 3, prefix none, gear 6
  (frequency), bit 12 set. The second gear word `0xF011E1` is decimal point 1,
  prefix none, gear 7 (duty cycle). The display showed `0 Hz` and `0 %`.

  The Hz/V view uses the same layout for two different quantities: frequency as
  the primary value and AC voltage in the second pair (**verified**):

  ```
  A2 11 F0 | 86 13 00 | 5A 10 F0 | D0 0C 00 | 04 00 00
  ```

  Primary gear word `0xF011A2` is decimal point 2, prefix none, gear 6
  (frequency), bit 12 set; the second gear word `0xF0105A` is decimal point 2,
  prefix 3 (milli), gear 1 (ACV). The display showed `49.98 Hz` and
  `32.80 mV AC`, with AUTO as the only state bit.

* **bit 12 clear** — the pair holds left-over data that must be ignored. An AC
  current frame carried gear 6 with a plausible but meaningless magnitude there
  while the display showed a single value, and the NCV and capacitance frames
  carry gear 7 in that slot with bit 12 clear.

Not every populated pair is a different quantity. During the Min/Max/Avg view
(state bit 5, §4.5) the second pair carries the **MAX** value of the displayed
function (**verified** in DC mV and AC mV). In DC mV the live reading moved
between 0.075 mV and 0.130 mV while the second pair stayed at 9.371 mV, and the
operator confirmed MAX showed 9.371 mV. An earlier DC current block in the same
view carried the live pair twice because MAX equalled the live reading.

In AC mV the display read MIN 23.44 mV, MAX 45.00 mV and AVG 27.93 mV. Over 73
frames the second pair rose from 32.14 mV to 45.00 mV without ever decreasing,
and the operator confirmed MAX. Neither 23.44 mV (MIN) nor 27.93 mV (AVG)
appeared in any field, and the fixed layout has no slot left for them: this
frame does **not** carry MIN or AVG. Whether another channel can read them is
**unknown**.

In REL mode the second pair carries the stored reference (**verified** in DC
mV): the primary value is the displayed relative reading and the second value
stayed at magnitude 2432 while the display showed a reference of 2.432 mV and a
relative value around 2.58 mV. On the wire that second value has the sign bit
set, so the decoder reports -2.432 mV. The capture does not establish whether
the sign bit is the displayed sign or a marker for the stored offset, so a
consumer should treat the second value as the reference rather than as an
independent negative measurement.

Layout of the second pair mirrors the first: gear word then value word.
`Measurement::secondary` carries it; decoders must leave it absent when bit 12 is
clear rather than reporting the leftovers.

## 5. Operating modes

* **Metering** — one measurement per frame, decoded as in §4.
* **Relative measurement** — signalled by state bit 1 (**verified**). The primary
  value is the relative reading and the second pair is the stored reference
  (§4.6).
* **Min/Max/Avg view** — an overlay on a live measurement, not a separate mode.
  State bit 5 is set and the second pair carries MAX (§4.6). MIN and AVG are
  **unknown**.
* **Oscilloscope** — signalled by state bit 18 (**verified**). The meter keeps
  sending a frame whose gear and value words do not track the display; the
  on-screen statistics (RMS, frequency, max/min/avg, Vpp) never appear in the
  payload and no waveform channel exists. Frames in this mode carry no
  measurement and decoders must report them as unknown (§6).

### 5.1 Measurement function groups

The device selects a measurement function locally. On the tested model remote
code 1 (Select) steps to the next function inside the current group, and the
device's Tab key changes the group. Sweeping all groups produced these
functions (**verified**):

| Group | Functions, as (gear, decimal point, prefix) |
| --- | --- |
| resistance | resistance (4, 2, none) and (4, 2, mega), continuity (11, 1, none), diode (10, 4, none) |
| frequency | frequency (6, 3, none) with duty cycle in the second pair, frequency (6, 2, none) with AC voltage (1, 2, milli) in the Hz/V view, NCV (13, 1, none), capacitance (5, 3, nano) |
| current | DC current (2, 2, none), AC current (3, 2, none) |
| voltage | DC voltage (0, 4, none), (0, 3, milli), (0, 1, none); AC voltage (1, 4, none), (1, 2, milli) |

Each step changes exactly one reading's gear word, and the sequence repeats
within the group. In the frequency group the observed order is capacitance →
frequency → NCV → capacitance. Which function a group starts on depends on where
the meter was before, so a sweep enumerates a group rather than returning to a
fixed first member.

Function changes the reported flags as well: **continuity and diode leave
automatic ranging** (the AUTO bit stays clear) while every other function keeps
it. Resistance, continuity and diode report the overload status with an `OL`
display when the probes are open.

NCV does not carry a measurement. The **magnitude is a field level**, and the
decimal point and prefix do not scale it:

| Magnitude | Display |
| --- | --- |
| 0 | `EF` (no field) |
| 1 | one segment, the meter beeps |
| 2 | two segments |
| 3 | observed; display not recorded |
| 4 | observed; display not recorded |

Levels 0..4 were observed; the state word stayed at AUTO throughout. The level
counts the displayed segments, but the exact display for levels 3 and 4 has not
been recorded, and levels above 4 are unmeasured.
`ampbeam::MeterDecoder` reports the raw magnitude as the value with no unit instead
of applying the decimal point, because scaling a level would invent a reading.

## 6. No measurement in oscilloscope mode

In oscilloscope mode the payload reads, for example,
`9B 03 F0 | D7 04 80 | A0 01 F0 | 00 00 00 | 00 00 04`. The gear and value words
correspond to nothing on the display, and the frame stays invariant while the
displayed statistic, pause state, vertical range and time base change
(**verified**).

`ampbeam::MeterDecoder` reports such frames as `Kind::Unknown` with an
`oscilloscope mode, no measurement` detail.

## 7. Commands

### 7.1 Front-panel keys (`fff3`)

Payload: two bytes `[code, 01]` (**verified**).

* One write performs one keystroke. Two consecutive writes step twice.
* A following `[code, 00]` write does **not** release a held key; it cancels the
  effect of the preceding press, so a press/release pair leaves the meter
  unchanged. Send a single write per keystroke.
* The first key press during auto ranging switches to manual ranging and clears
  the AUTO state bit.
* Keys have no effect in oscilloscope mode.
* A key write sent the instant notifications begin is occasionally dropped; a
  short settle delay before the first write avoids it.
* Only codes 1 (Select, §5.1), 2 (Range) and 3 (Hold) have a verified effect.
  Codes 4 to 11 were written and changed nothing (**verified**).
* Other front-panel functions have **no verified remote equivalent**.
* Do not import code assignments from another product or from vendor material.
  A code that has not been observed on hardware is **unknown**.

### 7.2 Command channel (`fff1`)

All payloads are exactly 16 bytes and zero padded after the meaningful content.
The only verified command is rename:

| Command | Bytes | Meaning |
| --- | --- | --- |
| rename | `40` + name + zeros | set the advertised device name |

The rename marker is `0x40`; the name is printable ASCII, up to 15 bytes. An
empty name or one longer than 15 bytes is rejected.
`ampbeam::meterRenameCommand` builds this payload.

No other command payload has a confirmed effect. Do not send or document one
without a capture that shows the meter acting on it.

### 7.3 Rename visibility

A rename takes effect in the advertisement only after the meter accepts **another
connection** (**verified**). A scan issued immediately after the write still
returns the previous name, so a rename can only be confirmed by reconnecting and
scanning again.

### 7.4 Compatibility challenge (`fff1`)

`fff1` accepts a 16 byte nonce and is readable afterwards. On the tested unit
the exchange has no observable effect: reading the characteristic returns
sixteen zero bytes and commands work without it (**verified**). The purpose and
any expected response are **unknown**; a client may skip the exchange.

## 8. Not defined by this document

The following are **not implemented** because no capture proves them:

* any frame size other than 15 bytes and any other series id;
* gear codes other than DCV, ACV, DCA, ACA, resistance, capacitance, frequency,
  duty cycle, diode, continuity and NCV;
* decimal point codes other than 0..4;
* state bits other than HOLD, REL, AUTO, MinMax and oscilloscope;
* MIN and AVG values, which the measurement frame does not carry;
* command payloads other than rename;
* any oscilloscope data path or waveform channel.

Such frames are reported as `Kind::Unknown`; they are never guessed at.
