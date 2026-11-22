# Architecture

## Goals

* One protocol implementation for Linux and Android.
* A reusable library (not just an app) that others can link against.
* Testability without hardware: every decoder decision must be reproducible
  from a recorded frame.

## Layers

```
+------------------------------------------------------------+
| app/  Qt Quick UI (QML) + AppController                    |
| cli/  ampbeam-cli: mirror, CSV, scan, trace, import, replay   |
+---------------------------^--------------------------------+
                            | raw QByteArray notifications
+---------------------------+--------------------------------+
| ble/  ampbeam_ble: Qt Bluetooth transport                  |
|       DeviceScanner, MultimeterClient                      |
+---------------------------^--------------------------------+
                            | std::span<const uint8_t>
+---------------------------+--------------------------------+
| core/ ampbeam_core: Measurement, Unit, Flags, GattProfile, |
|       FrameDecoder (RawDecoder / MeterDecoder)              |
+------------------------------------------------------------+
```

`core` is plain C++23 with no Qt, no I/O and no globals. `ble` owns Qt and all
platform behaviour. `app` and `cli` are thin front-ends.

## Shared front-end building blocks

Both front-ends link `ampbeam_core` and share every protocol decision: decoding,
framing, device selection (`selectMeterDevice` plus the `ble` scanner-model
wrapper `selectMeterFromDevices`), reconnect backoff (`ReconnectPolicy`),
mirror/display text (`monitorLine` and its split parts), CSV columns (`csvRow`)
and capture-log lines (`captureLine`). The transport (`DeviceScanner`,
`MultimeterClient`) is shared as well.

What stays in a front-end is only the driver of that state machine and its I/O:
`ampbeam-cli` uses a blocking `QEventLoop` for its mirror/trace actions, while
the GUI is signal-driven and owns its files and widgets. File writing for CSV
and capture recording is therefore repeated, but the row/line formatting is not.
The GUI's file choosers call `org.freedesktop.portal.FileChooser` over QtDBus
when the desktop provides it, because several Qt distributions (for example
Kubuntu's) do not ship the `xdgdesktopportal` platform theme. Protocol-exploration
switches (`--send`, `--challenge`, GATT UUID overrides) and offline fixture
replay/import remain CLI-only.

## Tooling without a second language

Capture parsing and fixture generation live in `ampbeam_core` and `ampbeam-cli`
rather than in a separate scripting toolchain. `ampbeam-cli --import` reads the
same log formats that a capture produces and writes the JSON fixture schema that
`--replay` consumes. This keeps one build system, one test-suite family and one
language, without a second scripting toolchain in development or CI.

## Why C++ and Qt

* **C++** was requested for compiled code and gives direct access to the
  platform Bluetooth APIs.
* **Qt 6** is the standard cross-platform C++ application framework for Linux
  and Android. Its `Qt Bluetooth` module provides `QLowEnergyController`, a
  central-role BLE API backed by BlueZ on Linux (D-Bus) and by the Android
  Bluetooth stack on Android. The same transport code runs on both targets.
  On Linux, `BluezLeDiscovery` holds a short LE discovery open while connecting
  because BlueZ 5.72 forgets unpaired device objects once discovery stops; other
  targets compile a no-op stub.
* **Qt Quick / QML** keeps the UI declarative and small, and scales from a
  desktop window to a phone screen.

### Alternatives considered

| Option | Why not |
| --- | --- |
| Flutter + `flutter_blue_plus` | `flutter_blue_plus` 2.x requires a paid commercial licence for for-profit use, and the stack would force Dart for the core. |
| Flutter + `bluetooth_low_energy` | MIT and Linux-capable, but the core would be Dart rather than C++. |
| Electron + C++ addon | No first-class Android target. |
| Kotlin (Android) + GTK/C++ (Linux) | Two implementations of the protocol and two UIs. |

## Qt version

The code requires Qt 6.5 or newer; CI and the release builds use **Qt 6.7.3**,
the newest release that a packaging tool can fetch for both the desktop and the
Android `arm64-v8a` targets. Bump the version in `CMakeLists.txt` (minimum) and
in the CI workflow together when a newer release is adopted.

## Threading

`QLowEnergyController` is asynchronous and event-loop driven. All BLE objects
live on the GUI thread; signals carry raw payloads to the decoder. There is no
blocking wait anywhere. A future high-rate capture mode can move decoding to a
worker thread without changing the interfaces, because `FrameDecoder::decode`
is stateless and `const`.

## Decoder policy

`ampbeam::MeterDecoder` implements the verified 15 byte layout described in
[`protocol.md`](protocol.md), but it never guesses:

* Only complete 15 byte records are decoded, and only when the gear and status
  are confirmed and the decimal point code is 0..4. Every SI prefix is mapped
  for a confirmed gear. Anything else is reported as `Kind::Unknown` with a hex
  dump, as are frames from oscilloscope mode.
* The status bits, decimal point, sign and SI prefix are decoded explicitly;
  overload reports `NaN` and the overload flag.
* A frame may carry a second reading (frequency plus duty cycle).
  `Measurement::secondary` holds it and is left absent when the gear word says
  the pair is meaningless, so leftovers never surface as a measurement.
* The algorithm is pinned by the white-box branch matrix and by the hardware
  captures in `tests/fixtures/`. See [`testing.md`](testing.md) for the layers
  and the coverage gate.
* Only measurements confirmed on hardware are implemented. Unknown gear and
  decimal point codes and other command payloads are not generated; the SI
  prefix is a standard magnitude of the confirmed function.

## GATT profile

The UUIDs in `ampbeam::meterBleProfile` are measured on the tested unit (series
61). The roles are distinct — keys and commands travel on different
characteristics:

| Role | UUID | Access |
| --- | --- | --- |
| Service | `0000fff0-0000-1000-8000-00805f9b34fb` | discovery |
| Commands + challenge | `0000fff1-0000-1000-8000-00805f9b34fb` | write, 16 byte rename payloads |
| Info (read) | `0000fff2-0000-1000-8000-00805f9b34fb` | read |
| Keys | `0000fff3-0000-1000-8000-00805f9b34fb` | write with response, 2 bytes |
| Data (notify) | `0000fff4-0000-1000-8000-00805f9b34fb` | subscribe |

`ampbeam-cli` can override them (`--service-uuid`, `--data-uuid`/`--notify-uuid`,
`--command-uuid`/`--write-uuid`, `--key-uuid`, `--info-uuid`) so other devices
can be explored without recompiling. The client reads `fff2` after discovery and
emits `infoReceived`; `ampbeam-cli` prints the series id and the verified frame
format (or `auto` when the series is unknown). `MultimeterClient::sendCommand` targets the command characteristic
and `sendKey` the key characteristic; both emit a `*Written` signal so an ATT
acknowledgement can be told apart from the meter acting on the payload.

## Android specifics

* Runtime permission `BLUETOOTH_SCAN`/`BLUETOOTH_CONNECT` is requested through
  `QPermissions` before scanning or connecting.
* The manifest keeps Qt's `%%INSERT_*%%` placeholders; Qt merges it during
  deployment.
* Minimum SDK 24, target SDK 34.
