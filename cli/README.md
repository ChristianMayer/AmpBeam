# ampbeam-cli

Headless companion for AmpBeam. It mirrors a supported BLE multimeter, scans for
devices, records captures, exports readings to CSV, turns captures into fixtures
and replays fixtures offline. The wire format is documented in
[`../docs/protocol.md`](../docs/protocol.md); the GUI is described in
[`../README.md`](../README.md).

## Build

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
./build/linux-debug/cli/ampbeam-cli --help
```

The binary needs Qt 6 Bluetooth and, on Linux, BlueZ with a powered adapter. The
meter must have its BLE mode switched on (hold the link key until the Bluetooth
indicator appears).

## Default: mirror the meter display

Run it without an action option:

```bash
./build/linux-debug/cli/ampbeam-cli
```

It scans, connects, and keeps one self-overwriting line in sync with the meter
(about five frames per second):

```
DCV  0.0001 V  [manual]
DCV  0.5692 V  [auto]
DCV  0.107 mV  [manual]  [minmax]  |  MAX 9.371 mV
NCV  2  [auto]
```

The line shows:

* the mode: `DCV`, `ACV`, `DCA`, `ACA`, `RES`, `CAP`, `FREQ`, `DUTY`, `DIODE`,
  `CONT` or `NCV`;
* the value with the meter's decimal places and unit (`6.000 V`, not `6 V`);
* `[auto]` / `[manual]` range state (manual is shown only for functions with
  selectable ranges; continuity and diode have none);
* `[hold]`, `[rel]`, `[minmax]`, `[overload]`;
* the second reading as `REF …` (relative), `MAX …` (MinMax view) or unlabelled
  (duty cycle next to frequency, AC voltage next to frequency);
* NCV field levels as a number without a unit.

The frame does **not** carry a full-scale range label or MIN/AVG; the decimal
point and SI prefix are the only range information the protocol contains. A
frame the decoder cannot prove is shown as `? …` with its bytes instead of a
reading.

Options:

```bash
ampbeam-cli --name <advertised name>   # scan filter; stops scanning as soon as it appears
ampbeam-cli --device <address> --mirror
ampbeam-cli --plain                    # one line per reading (automatic when piped)
ampbeam-cli --verbose                  # append gear/dp/raw/state fields
ampbeam-cli --duration 60              # stop after a minute (0 = until Ctrl+C)
```

The mirror reconnects with backoff (1, 2, 4 … 30 s) after a failed connection or
a lost link. It never exits on its own; stop it with Ctrl+C. A quiet stream is
normal in some modes (HOLD, continuity, steady readings), so silence does not
trigger a reconnect.

Each scan attempt lasts 5 s (`--scan-timeout` overrides it).

## Export readings to CSV

`--csv <file>` writes every decoded reading to a CSV file while the CLI does its
normal output. It selects the meter decoder, so it works in mirror mode, with
`--replay`, and in tracing mode.

```bash
# live mirror plus CSV
ampbeam-cli --name <advertised name> --csv readings.csv

# offline export from a captured fixture
ampbeam-cli --replay tests/fixtures/meter-hardware-minmax-millivolt.json \
    --csv readings.csv

# tracing session with CSV
ampbeam-cli --device <address> --csv readings.csv --duration 60
```

The file is truncated on every run, gets a header immediately, and each row is
flushed so `tail -f readings.csv` follows a live session. Only frames the decoder
proves become rows: undecoded frames and oscilloscope mode are skipped instead of
being exported as invented readings. `--csv` and `--decoder raw` are mutually
exclusive because a raw byte dump is not a reading.

### Columns

| Column | Meaning |
| --- | --- |
| `received_ms` | UNIX epoch milliseconds when the frame was received. |
| `time` | Same instant as ISO 8601 UTC (`2026-11-22T14:53:59.095Z`). |
| `mode` | Gear name: `DCV`, `ACV`, `DCA`, `ACA`, `RES`, `CAP`, `FREQ`, `DUTY`, `DIODE`, `CONT`, `NCV`, or `?`. |
| `value` | Displayed value with the meter's decimals; empty for a blank/overload display. |
| `unit` | Unit symbol (`V`, `mV`, `Ω`, `µF`, `%`, …); empty when the meter reports none (NCV). |
| `current` | `DC`, `AC`, or empty. |
| `decimals` | Decimal places the meter displays; empty for NCV levels. |
| `overload`, `hold`, `relative`, `auto`, `manual`, `minmax` | `1`/`0` indicators. |
| `secondary_role` | `REF` (relative mode), `MAX` (MinMax view), or empty. |
| `secondary_value`, `secondary_unit`, `secondary_decimals` | Second reading, empty when the frame carries none. |

Example:

```csv
received_ms,time,mode,value,unit,current,decimals,overload,hold,relative,auto,manual,minmax,secondary_role,secondary_value,secondary_unit,secondary_decimals
1795359239095,2026-11-22T14:53:59.095Z,DCV,0.107,mV,DC,3,0,0,0,0,1,1,MAX,9.371,mV,3
```

## Scan and select a device

```bash
ampbeam-cli --scan [--scan-timeout ms]
ampbeam-cli --device auto [--name <advertised name>]
ampbeam-cli --device <address>
```

`--scan` prints RSSI, name, address and advertised services. `--device auto`
prefers the only device that advertises the meter service and falls back to the
only discovered device when BlueZ hides service UUIDs. It refuses to guess when
several devices are possible; pass `--name` or the address from `--scan` then.
With `--name`, the scan stops as soon as a matching device appears.

## Tracing mode and capture logs

Without `--mirror`, `--device <address>` is a raw tracer. It prints one TSV line
per notification:

```
<epochMs>\t<length>\t<hex bytes>\t<detail>
```

```bash
ampbeam-cli --device <address> --decoder meter --duration 60 > capture.log
```

`--decoder meter` splits notifications into 15 byte frames and fills `detail`
with the decoded reading. `--csv` selects the meter decoder automatically.

## Fixtures: import and replay

```bash
ampbeam-cli --import capture.log --output tests/fixtures/my-meter.json \
    --fixture-name my-meter
ampbeam-cli --replay tests/fixtures/my-meter.json --decoder meter
ampbeam-cli --replay tests/fixtures/my-meter.json --mirror --plain
```

`--import` accepts the TSV capture format, `<epochMs> <hex>` and bare `<hex>`
lines, `#` comments and blank lines. Fixtures are JSON:
`{ name, createdAt, source?, frames[] }` with `{ receivedMs?, hex }` per frame.
Regenerate a fixture with `--import` instead of editing it by hand.

## Front-panel keys and commands

Keys go to `fff3`, every other command to `fff1`; they are not interchangeable.

```bash
# one keystroke per press, staggered by --key-delay (default 1500 ms)
ampbeam-cli --device <address> --decoder meter --duration 30 \
    --key 2 --key 3 --key 4 --key-delay 2500

# raw 16 byte command payloads (repeatable)
ampbeam-cli --device <address> --send "40 41 4D 50 42 45 41 4D 00 00 00 00 00 00 00 00"

# command-channel challenge probe
ampbeam-cli --device <address> --challenge --duration 6
```

`write acknowledged` on stderr means the ATT layer accepted a write, not that the
meter acted on it. A rename only becomes visible in the advertisement after the
meter accepts another connection, so verify it with a second connection.

## GATT overrides

For protocol exploration with another device:

```bash
ampbeam-cli --service-uuid <uuid> --notify-uuid <uuid> --command-uuid <uuid> \
    --key-uuid <uuid> --info-uuid <uuid>
```

Override UUIDs per run; do not edit the defaults per device.

## Output, diagnostics and exit codes

* stdout carries mirror lines or TSV capture lines; diagnostics and connection
  state go to stderr, so redirections stay clean.
* `0`: success (including a duration-bounded mirror or replay).
* `1`: usage error, unknown decoder, or a device that could not be selected
  unambiguously.
* `2`: an input/output error (unreadable capture log or fixture, unwritable
  fixture/CSV path).

## Related documentation

* [`../docs/protocol.md`](../docs/protocol.md) — verified frame layout and modes.
* [`../docs/development.md`](../docs/development.md) — build, bench and
  Bluetooth troubleshooting.
* [`../docs/testing.md`](../docs/testing.md) — test layers and the coverage gate.
