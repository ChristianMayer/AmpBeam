# Development guide

## Build environment

Required:

* CMake 3.21+ and Ninja
* A C++23 compiler (GCC 13 or Clang 17 tested)
* Qt 6.5+ with `Core`, `Gui`, `Bluetooth`, `Quick`, `QuickControls2`,
  `QuickDialogs2` and `Qml`
* Linux: BlueZ with a powered adapter
* Android (optional): Qt for Android arm64, SDK 34, NDK r26, min SDK 24,
  target 34

If Qt is not in a standard location, set `CMAKE_PREFIX_PATH` or `QT_HOST_PATH`
to the Qt 6 installation. The maintainer's local development container is
**not** part of this repository; `/.devcontainer/` and `/.vscode/` are ignored.

Verify the Bluetooth adapter before running the scanner:

```bash
sudo systemctl enable --now bluetooth
bluetoothctl show            # adapter must be powered
rfkill list bluetooth        # must not be blocked
./build/linux-debug/cli/ampbeam-cli --scan --scan-timeout 5000
```

The desktop application needs a display. For a headless smoke test:

```bash
xvfb-run -a timeout 6 ./build/linux-debug/app/ampbeam
```

## Build and test

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

The suites are split by layer; [`testing.md`](testing.md) explains which one a
change belongs in. The coverage gate is separate because it needs a GCC + gcov
build:

```bash
cmake --preset linux-coverage
cmake --build --preset linux-coverage
ctest --preset linux-coverage        # includes the 100% core coverage gate
```

## Run

```bash
./build/linux-debug/cli/ampbeam-cli            # default: discover and mirror the display
./build/linux-debug/cli/ampbeam-cli --scan
./build/linux-debug/cli/ampbeam-cli --device auto     # tracing mode; only if exactly one meter advertises the service
./build/linux-debug/cli/ampbeam-cli --device <address>
./build/linux-debug/app/ampbeam
```

The desktop application mirrors the CLI's live session with a GUI: it starts the
same automatic discovery/reconnect session on launch, **Devices...** allows an
explicit choice when several BLE devices are nearby, and the large meter face
shows the verified mode, reading, unit and indicators. The verified **Select**,
**Range** and **Hold** keys are available while streaming. The live log uses the
capture format and can be recorded to a file; **Export CSV...** writes the
decoded readings of the current session. The Device tab shows the series id and
firmware reported by the meter.

Qt Quick's `FileDialog` uses the desktop's native file chooser only when a
platform theme provides one. The CSV and capture choosers therefore call the
XDG file-chooser portal directly, which gives the KDE/GNOME dialog even when the
distribution's Qt lacks the `xdgdesktopportal` platform theme (as on Kubuntu).
Install and enable `xdg-desktop-portal` with its KDE or GTK backend; without a
reachable portal the Qt Quick chooser is used.

Without an action option the CLI is a live mirror: it scans, connects, renders
one self-overwriting line (mode, value, unit, indicators) and reconnects with
backoff after a failed attempt or a lost link. A quiet stream is not treated as
an error, because the meter legitimately sends few frames in some modes.
`--mirror` requests that mode explicitly, `--plain` prints one line per reading
instead of overwriting (automatic when stdout is not a terminal), `--verbose`
appends the decoded frame fields (`gear`, `dp`, `raw`, `states`), and
`--duration N` ends the session after N seconds. `--csv <file>` exports every
decoded reading to CSV and selects the meter decoder. The same renderer runs on a
fixture: `ampbeam-cli --replay <fixture> --mirror --plain`. See
[`../cli/README.md`](../cli/README.md) for the full option and CSV reference.

Values keep the decimal places the frame reports (`6.000 V`), and `[manual]`
appears when the AUTO bit is clear on a function with ranges. The frame carries
the decimal point and SI prefix but no full-scale range label, so this is all
the range information the protocol contains.

Each mirror scan attempt lasts 5 s unless `--scan-timeout` is given. `--scan`
prints `rssi`, name, address and advertised services. `--device auto` scans for
`scan-timeout` milliseconds, prefers a device that advertises the meter service,
and falls back to the only discovered device when service UUIDs are unavailable.
Add `--name <advertised name>` to filter by name: with a name the scan stops as
soon as that device appears. It refuses to guess when several devices are
possible, which is common when BlueZ hides service UUIDs and other BLE devices
are nearby; pass `--name` or `--device <address>` then.

`ampbeam-cli --help` lists all options, including GATT UUID overrides, `--import`
(capture log → JSON fixture) and `--replay`.

```bash
./build/linux-debug/cli/ampbeam-cli --import capture.log \
    --output tests/fixtures/my-meter.json --fixture-name my-meter
```

### Probing a meter on the bench

`ampbeam-cli` doubles as the hardware probe. It prints `write acknowledged: <hex>`
on stderr for every command the ATT layer accepted, which separates "the write
never left the stack" from "the meter got it and ignored it".

```bash
# One keystroke per press; repeatable and staggered, so a single capture can
# probe several codes. The reaction is visible in the frame stream.
ampbeam-cli --device <addr> --decoder meter --duration 30 \
  --key 2 --key 3 --key 4 --key-delay 2500
```

Commands on the command characteristic take effect immediately. The rename
command is the exception to how it *looks*: it only shows up in the advertisement
after the meter accepts another connection, so verify it with a second short
connection instead of scanning straight away.

```bash
# rename the meter (marker 0x40, then the ASCII name, zero padded to 16 bytes)
ampbeam-cli --device <addr> --duration 6 \
  --send "40 41 4D 50 42 45 41 4D 00 00 00 00 00 00 00 00"
ampbeam-cli --device <addr> --duration 3   # connection that applies the rename
ampbeam-cli --scan --scan-timeout 10000    # now the new name shows up
```

`--challenge` writes a 16 byte nonce to the command characteristic and logs
whatever the meter returns. On the tested unit it changes nothing — no reply
comes back and commands work without it — so it exists for protocol exploration,
not as a prerequisite.

Two practical notes from real sessions:

* Connections are occasionally refused outright (no frames at all) when the
  meter was just disconnected from another client. Retry after a few seconds
  before drawing conclusions from an empty capture.
* Only state that the frames prove counts. `write acknowledged` does not mean
  the meter acted on the payload, so compare the gear/state words before and
  after a command instead of trusting the acknowledgement.

### Bench sweep script

`tests/bench/mode_sweep.py` drives a real meter: it connects, presses a key, waits
for frames and reports the measurement mode that results. Nothing needs to be
connected to the probes — with open inputs only the mode (gear word) carries
information, which is what the script compares.

```bash
# cycle the measurement functions with the Select key
tests/bench/mode_sweep.py --device <address> --codes 1 1 1 1 1 1

# cover every function group: between groups the script asks for the meter's Tab key
tests/bench/mode_sweep.py --device <address> --codes 1 1 1 1 --groups 3

# keep a fixture and require that all three modes were reached
tests/bench/mode_sweep.py --device <address> --codes 1 1 1 1 1 1 \
    --expect-modes 13,5,6 --json tests/fixtures/meter-hardware-mode-sweep.json
```

It exits non-zero when a press produced no frames or when an expected mode was
missing. Expect roughly 20 seconds per press: each one gets its own connection
because the meter keeps the selected function across connections, which is what
makes the press-to-result mapping unambiguous.

The meter occasionally drops a key write, so the script and the test suite check
*coverage* (all modes reached) rather than a strict press-by-press order.

To run it as part of CTest, configure with the device address. Without the
variable the test is not registered at all, so CI never needs hardware:

```bash
AMPBEAM_BENCH_DEVICE=<address> cmake --preset linux-debug
ctest --preset linux-debug -L bench
```

### Fixture map

`docs/protocol.md` marks each field as verified or unknown; this table says
which capture backs which part of it.

| Fixture | Covers |
| --- | --- |
| `meter-hardware-dcv.json` | 15 byte layout, gear 0 (DCV), decimal point 4, no prefix, AUTO, magnitude with a measured value |
| `meter-hardware-dcv-1000v.json` | decimal point 1 (0.1 V resolution), a completely clear state word |
| `meter-hardware-dcv-hold-millivolt.json` | prefix 3 (milli), decimal point 3, HOLD bit set, AUTO cleared by manual ranging |
| `meter-hardware-resistance-ol.json` | gear 4, prefix 6 (mega), value status 1 (overload) |
| `meter-hardware-ac-current.json` | gear 3 (ACA), decimal point 2, second value pair discarded via gear-word bit 12 |
| `meter-hardware-key-range.json` | key payload semantics: one write per step, AUTO cleared on the first Range press |
| `meter-hardware-freq-duty.json` | gear 6 and 7, and a populated second gear/value pair (bit 12 set) |
| `meter-hardware-freq-ac-millivolt.json` | the Hz/V view: frequency primary, AC millivolt secondary, AUTO state |
| `meter-hardware-mode-sweep.json` | every function group: DCV/ACV, DCA/ACA, resistance/continuity/diode, frequency/NCV/capacitance, with the left-over second pairs and the auto-range behaviour per function |
| `meter-hardware-resistance-continuity.json` | resistance overload switching to continuity in one session; both report overload on open probes |
| `meter-hardware-range-sweep.json` | resistance Ω/kΩ/MΩ, capacitance nF and frequency with duty cycle across several ranges |
| `meter-hardware-scale-sweep.json` | frequency Hz/kHz/MHz, DC current with decimal point 0/1/2 and sign, DC voltage mV/V, NCV levels 0..4 and the MinMax state bit |
| `meter-hardware-minmax-millivolt.json` | state bit 5 set for the whole capture and a second measurement carrying MAX while the live DC mV reading changes |
| `meter-hardware-minmax-ac-millivolt.json` | the same view in AC mV: MAX rises with the input, MIN and AVG never appear in the frame |
| `meter-hardware-relative-millivolt.json` | state bit 1 (REL), the relative reading and the stored reference in the second pair |
| `meter-hardware-osc-mode.json` | state bit 18 and the "no measurement" decision |

Every fixture is a capture from the tested meter; there is no synthetic frame
data in the repository.

## Android

```bash
cmake --preset android-arm64
cmake --build --preset android-arm64
```

The APK is written to:

```
build/android-arm64/app/android-build/build/outputs/apk/debug/android-build-debug.apk
```

Install it on a connected device:

```bash
adb devices
adb install -r build/android-arm64/app/android-build/build/outputs/apk/debug/android-build-debug.apk
```

Notes:

* The first Android build downloads Gradle; network access is required.
* `android-arm64` produces a debug APK. The `android-release` preset produces an
  unsigned release APK; `packaging/android/build-apk.sh` signs it for
  distribution (see below).
* A USB-connected device needs the usual host udev rules. If `adb devices`
  reports `no permissions`, fix the udev rule or use wireless debugging
  (`adb pair`, `adb connect`), which needs no extra configuration:

  ```bash
  adb pair 192.0.2.10:37123
  adb connect 192.0.2.10:5555
  ```

* On Android 12+ the app asks for `BLUETOOTH_SCAN` and `BLUETOOTH_CONNECT` at
  runtime. If you denied them once, re-enable them in the app settings.

## Packaging

The release and nightly pipelines build two artifacts. User-facing install
instructions live in [`distribution.md`](distribution.md); this section covers
the local build steps.

### Linux AppImage

The AppImage is built from the `linux-release` preset. No system Qt is needed
on the target machine, and the packaged application includes the CLI.

```bash
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release

QT_HOST_PATH=/path/to/Qt/6.7.3/gcc_64 \
  packaging/appimage/build-appimage.sh --version 0.1.0
```

The script installs into a private `AppDir` with
`cmake --install`, downloads pinned `linuxdeploy` and `linuxdeploy-plugin-qt`
AppImages into `build/appimage-tools/`, verifies their checksums and writes
`dist/AmpBeam-<version>-x86_64.AppImage`.

`QML_SOURCES_PATHS` points the Qt plugin at `app/`, so the Qt Quick modules the
application imports are bundled. The application's own QML module is compiled
into the binary with `RESOURCE_PREFIX "/qt/qml"`; it therefore loads
`AmpBeam.Main` from `qrc:/qt/qml/AmpBeam/qmldir` instead of relying on the build
directory's `AmpBeam/` folder.

A headless check of the result:

```bash
xvfb-run -a timeout 8 ./dist/AmpBeam-0.1.0-x86_64.AppImage \
  --appimage-extract-and-run
./dist/AmpBeam-0.1.0-x86_64.AppImage --appimage-extract
./squashfs-root/usr/bin/ampbeam-cli --replay tests/fixtures/meter-hardware-dcv.json \
  --mirror --plain
```

The exit status `124` from `timeout` means the GUI was still running, which is
the expected result.

### Android release APK

The `android-release` preset builds the same application with
`CMAKE_BUILD_TYPE=Release` and without host tests. Qt's Android deployment
produces an unsigned APK; `packaging/android/build-apk.sh` aligns and signs it.

```bash
export QT_ANDROID_PATH=/path/to/Qt/6.7.3/android_arm64_v8a
export QT_HOST_PATH=/path/to/Qt/6.7.3/gcc_64
export ANDROID_SDK_ROOT=/path/to/android-sdk
export ANDROID_NDK_ROOT=/path/to/android-sdk/ndk/26.1.10909125

packaging/android/build-apk.sh --version 0.1.0
```

The result is `dist/AmpBeam-<version>-arm64-v8a.apk`. The script uses
`zipalign` and `apksigner` from the newest Android build-tools and verifies the
signature afterwards. The Android `versionCode` is derived from the project
version (`0.1.0` becomes `100`), so a release can be installed over the previous
one.

Release signing is configured through environment variables:

| Variable | Purpose |
| --- | --- |
| `AMPBEAM_ANDROID_KEYSTORE` | Path to a PKCS#12/JKS keystore. |
| `AMPBEAM_ANDROID_KEYSTORE_BASE64` | Base64 keystore, used by CI secrets. |
| `AMPBEAM_ANDROID_KEYSTORE_PASSWORD` | Keystore password. |
| `AMPBEAM_ANDROID_KEY_ALIAS` | Key alias, default `ampbeam`. |
| `AMPBEAM_ANDROID_KEY_PASSWORD` | Key password, default the store password. |

Without a keystore the script generates a throwaway key. That APK installs but
has a different signature from every other build, so an installed copy must be
uninstalled before switching builds.

The checks are mirrored in CI: `package.yml` starts the AppImage headlessly and
verifies the APK with `apksigner` and `aapt`. The full release/nightly flow is
described in [`distribution.md`](distribution.md).

## Troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| `qt.qpa.plugin: Could not load the Qt platform plugin "xcb"` | No display. Use a desktop session or `xvfb-run`. |
| `qt.bluetooth.bluez: Missing CAP_NET_ADMIN permission...` | Informational: Qt cannot classify random/public LE addresses because the optional BlueZ management socket needs `CAP_NET_ADMIN`. AmpBeam uses BlueZ over D-Bus, so scanning and connecting still work. The GUI and CLI hide this info message unless `QT_LOGGING_RULES` explicitly configures the `qt.bluetooth.bluez` category. |
| File dialog is the Qt Quick chooser, not the desktop's dialog | The CSV/capture choosers use the XDG portal directly. Install and enable `xdg-desktop-portal` plus a KDE or GTK backend; without it the app falls back to the Qt Quick chooser. On GTK desktops the app also selects the `gtk3` platform theme; `QT_QPA_PLATFORMTHEME` overrides the theme choice. |
| `Bluetooth adapter not available` / no devices found | `bluetoothd` not running or the adapter is blocked. Check `bluetoothctl show` and `rfkill list bluetooth`. |
| `Service 0000fff0-… not found` | The device is not in BLE mode or uses a different GATT profile. Use a GATT dump. |
| Android: `Bluetooth permission was not granted` | Grant nearby-devices permission in Android settings. |
| CMake cannot find Qt | `CMAKE_PREFIX_PATH` / `QT_HOST_PATH` is not set to the Qt 6 installation. |
| `Cannot start QML language server. not found` | `qmlls` is missing `libQt6LanguageServer.so.6` / `libQt6JsonRpc.so.6` (the `qtlanguageserver` Qt module), or no Qt installation is registered. |
| Android build fails at Gradle download | The first build needs internet access. |
| `ampbeam-cli` prints no frames | Notifications not enabled; check characteristic properties with `bluetoothctl`. |
| `Remote device cannot be found` on Linux | BlueZ 5.72 removes the `org.bluez.Device1` object of an unpaired device when discovery stops. The client now starts a short LE discovery and waits for the object before connecting; if it still fails, scan once with `ampbeam-cli --scan` or reconnect the meter. |

## Style

### Indentation

Two spaces. **No tab characters anywhere in the sources.** `.editorconfig`
carries the rule so editors apply it automatically; `.clang-format` enforces it
for C++; CI rejects tabs and unformatted C++.

Two deliberate exceptions, because the language mandates something else:

| Scope | Rule | Why |
| --- | --- | --- |
| `Makefile` recipe lines | tab | The make format requires a tab; a two-space recipe is a syntax error. |
| `*.py` | four spaces | PEP 8 prescribes four; following the language standard beats our house rule. |
| `.user/**` | untouched | Local scratch material; never edited, never staged. |

Qt's own coding conventions recommend four-space indentation for C++ and QML.
This project deliberately uses two, so new code must match the code around it
rather than Qt's upstream style.

Applies to C++, QML, CMake, JSON, YAML, shell and Markdown; table and list
continuations in Markdown stay aligned to their parent item.

Confirm before pushing:

```bash
# tabs
grep -rnP '^\t' --include='*.cpp' --include='*.h' --include='*.hpp' \
  --include='*.qml' --include='*.txt' --include='*.cmake' --include='*.json' \
  --include='*.yml' --include='*.sh' --include='*.py' \
  core ble cli app tests docs cmake CMakeLists.txt CMakePresets.json

# C++ formatting
find core ble cli app tests -type f \( -name '*.cpp' -o -name '*.h' \
  -o -name '*.hpp' \) -print0 | xargs -0 clang-format --dry-run --Werror
```

### Other

* C++: `clang-format` (config at repository root). CI enforces it.
* Commits: commit from your own environment; do not commit generated build
  trees, logs or local scratch files.
