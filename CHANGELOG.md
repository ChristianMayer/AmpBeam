# Changelog

All notable changes to AmpBeam are documented in this file. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Nothing has been
released yet; the first release will be tagged from the current state.

## [Unreleased]

### Added

- `ampbeam_core`: dependency-free C++23 measurement model (`Measurement`, `Unit`,
  `Flags`, `SecondaryReading`), hex helpers, capture-log parsing, JSON fixture
  import/export, unique-device selection and the `FrameDecoder` interface.
- `MeterDecoder` for the 15 byte frame layout: DCV, ACV, DCA, ACA, resistance,
  continuity, diode, capacitance, frequency with duty cycle or AC voltage, NCV
  field levels, MinMax MAX, HOLD, auto-range, overload, oscilloscope-mode
  detection and the confirmed decimal point codes 0..4.
- `ampbeam_ble`: Qt 6 transport with device discovery, connection state machine,
  notification subscription, front-panel key writes, command writes and the
  device-info read.
- `ampbeam-cli`: scan, capture to TSV, front-panel key injection, command
  injection, rename, challenge probe, fixture import and offline replay, plus
  GATT UUID overrides for protocol exploration.
- `ampbeam`: Qt Quick desktop and Android application with automatic meter
  discovery and reconnect with backoff, an explicit device picker, a split
  meter face (mode, value, unit, verified indicators and secondary reading),
  the verified Select/Range/Hold keys, device identity, a signal trace of the
  recent readings, an optional raw-frame log, session CSV export and capture
  recording.
- The GUI opens on a normal multimeter view, with the **Trace** tab plotting the
  proven readings on a fixed, scrollable timebase (10 s by default, 2 s to
  60 s). The newest reading is pinned to the right edge, older readings scroll
  left, the timebase and the value range can be zoomed separately, and both the
  primary and the secondary reading of a frame are drawn together. The tab also
  has pause/continue and clear controls. The raw frame log is extended
  information in its own **Raw** tab and is never the default view; the
  **Device** tab shows the device identity.
- `ampbeam_core`: `TraceHistory` is time-based: it keeps a bounded window of
  proven readings with their receive times, drops readings outside the selected
  timebase, scales the primary and secondary values independently, and reports
  the zoomed value range so axis labels match the clipped curve. A gap marks an
  overload (or an infinite value), and a different mode or unit starts a new
  series, so no curve joins two unrelated functions.
- `ampbeam_core`: `monitorValueText`, `monitorUnitText` and
  `monitorSecondaryText` expose the mirror's number, unit and secondary reading
  separately so the GUI renders the same verified text as the CLI.
- `ampbeam_core`: `captureLine` formats the capture log line shared by the CLI
  output and the GUI log/recording, so the importer and both front-ends agree.
- `ampbeam_ble`: `selectMeterFromDevices` applies the core selection policy to
the scanner model, replacing the duplicated QVariant-to-candidate translation
in the CLI and the GUI.
- The device picker now shows the currently connected meter and explains that a
  connected meter may stop advertising; it does not start a scan while
  connected, so the last visible device is not wiped by a pointless scan.
- The GUI's CSV and capture file choosers use the XDG desktop portal directly,
  so the desktop's native dialog works even when the distribution's Qt lacks the
  `xdgdesktopportal` platform theme (as on Kubuntu); the Qt Quick dialog remains
  the fallback when no portal is reachable.
- The GUI starts the same automatic discovery/reconnect session as the CLI on
  launch and prefers the desktop portal's native file chooser when a platform
  theme is available. GTK desktops get `gtk3`, other desktops use
  `xdgdesktopportal`; `QT_QPA_PLATFORMTHEME` overrides the choice.
- Both front-ends hide Qt's informational
  `qt.bluetooth.bluez: Missing CAP_NET_ADMIN permission...` message unless
  `QT_LOGGING_RULES` explicitly configures the `qt.bluetooth.bluez` category;
  the BLE transport itself is unaffected.
- `ampbeam-cli --device auto` prefers the only device that advertises the
  meter service, falls back to the only discovered device when service UUIDs
  are unavailable, and accepts `--name` as a filter. It refuses to guess when
  several devices are possible.
- `ampbeam-cli` default mirror mode: without an action it discovers the meter,
  renders one self-overwriting line with the mode, value, unit and indicators
  (`--plain` for one line per reading), and reconnects with backoff after a
  failed attempt or a lost connection. `--mirror` forces the mode and works with
  `--replay` for offline rendering.
- `Measurement` now carries the verified `MeterGear`, and `Flags` carries the
  verified MinMax view (`[minmax]`), so a display can name the mode even for NCV,
  which has no decimal quantity.
- The mirror reproduces the LCD resolution (`Measurement::displayDecimals`, so
  `6 V` prints as `6.000 V`), marks a clear AUTO bit as `[manual]` on functions
  with ranges, and `--verbose` appends the decoded `gear`/`dp`/`raw`/`states`.
  Mirror scans use a 5 s window per attempt and a `--name` scan stops as soon as
  the named meter appears.
- `ampbeam-cli --csv <file>` exports every decoded reading (timestamp, mode,
  value/unit, range state, indicators and secondary reading) as UTF-8 CSV, in
  mirror, replay and tracing modes. Only decoded readings are written; raw or
  oscilloscope frames are skipped. `cli/README.md` documents the columns.
- MinMax view decoding: state bit 5 is named `MINMAX`, the second pair is
  documented as MAX, and hardware fixtures cover changing DC mV and AC mV
  readings. MIN and AVG are not present in the frame.
- REL mode: state bit 1 is named `REL`, `Measurement::flags.relative` is set and
  the second pair is documented as the stored reference. Covered by a hardware
  fixture.
- The Hz/V view is decoded: frequency is the primary value and AC voltage
  (millivolt range) is the second measurement. Covered by a hardware fixture.
- Linux: the transport holds a short BlueZ LE discovery open while connecting
  and waits for the `org.bluez.Device1` object, so BlueZ 5.72 does not fail with
  `Remote device cannot be found`. Other platforms use a no-op stub.
- Capture-derived fixtures for DCV, high-voltage DCV, HOLD/millivolt, AC current,
  frequency with duty cycle or AC voltage, resistance overload, continuity, NCV,
  oscilloscope mode, MinMax and the full function/range sweeps. Every fixture is
  a real capture from the tested meter.
- Test suites: white-box core, fixture black-box, CLI black-box, transport and
  controller tests, plus the hardware bench sweep.
- 100% line coverage of `core/`, enforced by `ctest --preset linux-coverage`.

### Known limitations

- Only series 61 with firmware 1.4.0 has been tested. Other series ids and
  other frame formats are not implemented.
- The model and decoder contain only confirmed functions, gears, statuses, state
  bits and decimal point codes. Unsupported functions (temperature, hFE, power,
  4~20 mA, motor), their units and unconfirmed flags are not implemented. Every
  SI magnitude of the confirmed gears is mapped, but not every magnitude has
  been captured on hardware yet.
- Front-panel keys 1 (Select), 2 (Range) and 3 (Hold) are verified. No other
  remote function has a confirmed code.
- The application has no charting or settings persistence. CSV export,
  capture recording and reconnect handling are implemented.
- Only a debug Android APK is produced; release signing, an adaptive launcher
  icon and store packaging are not configured.
- Packaging for Linux (`.deb`, AppImage) is not implemented.
