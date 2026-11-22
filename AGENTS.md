# AGENTS.md

Operating manual for AI agents (and humans in a hurry) working on **AmpBeam**.

Read this before touching code. When something here conflicts with a guess,
this file wins. Deeper background lives in [`docs/`](docs/): architecture,
protocol notes and the development guide.

## What this project is

AmpBeam is a Linux + Android application and reusable C++ library for Bluetooth
Low Energy multimeters that speak the frame protocol documented in
`docs/protocol.md`. It is a Qt 6 / C++23 project built with CMake + Ninja.

**Status: early, verified on one physical meter.** The BLE transport, capture
tooling and app shell work, and the 15 byte frame format has been confirmed end
to end against a physical meter with series id 61 and firmware 1.4.0 (DCV, ACV,
DCA, ACA, resistance, continuity, diode, capacitance, frequency/duty, NCV, HOLD,
auto-range, oscilloscope mode, keys 1-3 and rename). `docs/protocol.md`
describes only verified behaviour; anything not established is **unknown** and
is not implemented.

**No vendor or model names in the implementation.** Device names are guesses
unless a capture or a hardware hint proves them. Use neutral identifiers
(`MeterDecoder`, `meterBleProfile`, `meter-hardware-*.json`); model names belong
only in the explicitly unverified compatibility note in `README.md`.

## Non-negotiable invariants

1. **Never invent measurements.** `ampbeam::MeterDecoder` reports what the
   extracted tables prove and returns `Kind::Unknown` for anything else. A
   plausible-but-wrong reading is worse than no reading. Do not "improve" this
   without a capture, a fixture and a test.
2. **No third-party material in the repo.** Do not extract or commit artwork,
   fonts, app packages or PDFs that belong to someone else. Capture logs are
   fine; they contain only what the meter sent.
3. **`ampbeam_core` has no Qt, no I/O, no globals.** Everything protocol-related
   belongs there so the GUI, the CLI and the tests share one implementation.
4. **Two-space indentation, never tabs** (see *Formatting*).
5. **The name is AmpBeam** — `ampbeam`, `ampbeam_*`, `ampbeam::`, `AmpBeam`.
   No `mm`, no "Bluetooth Multimeter", no abbreviations, anywhere.
6. **`assets/branding/` is the only copy of the logo.** Never duplicate
   artwork; reference the paths.
7. **Do not edit `build/`.** It is generated; reconfigure or delete it.
8. **Test first, and leave no line behind.** Write the failing test before the
   production change. Everything a test can reach must be reached: `core/` is
   gated at 100% line coverage, and the offline CLI paths, transport state and
   controller pipeline each have a suite. Code that needs a Bluetooth adapter
   or a display must contain no logic and is verified by the bench suite and the
   smoke run. The `linux-coverage` preset enforces the gate; see
   [`docs/testing.md`](docs/testing.md).

## Repository map

| Path | Contents |
| --- | --- |
| `core/` (`ampbeam_core`) | Dependency-free C++23: `Measurement`, `Unit`, `Flags`, `GattProfile`, hex helpers, capture/fixture handling, `FrameDecoder` + `RawDecoder` / `MeterDecoder`. Strictly warning-clean. |
| `core/include/ampbeam/core/` | Public headers (`.hpp`). |
| `ble/` (`ampbeam_ble`) | Qt Bluetooth transport: `DeviceScanner`, `MultimeterClient`, permission helper. Headers use `.h`. |
| `app/` (`ampbeam`) | Qt Quick application (`Main.qml`, module URI `AmpBeam`) with the reusable `MeterDisplay`, `IndicatorChip`, `AppButton` and `DeviceDialog` QML components and `AppController` (registered via `QML_ELEMENT`), plus `PortalFileDialog` for the XDG portal file chooser. Also the Android manifest overlay and the desktop entry. |
| `cli/` (`ampbeam-cli`) | Headless mirror / CSV export / scan / capture / import / replay tool. Usage: `cli/README.md`. |
| `tests/` | White-box (`tst_core_whitebox`), fixture black box (`tst_core_blackbox`), CLI black box (`tst_cli`), transport (`tst_ble`) and controller (`tst_app`) suites, plus the hardware bench and the coverage gate. |
| `assets/branding/` | Logo source of truth + Android icon derivation script. |
| `packaging/` | `appimage/` and `android/` release packaging and signing scripts used by CI; see `docs/distribution.md`. |
| `cmake/CompilerWarnings.cmake` | `ampbeam_set_warnings()` / `ampbeam_set_strict_warnings()`. |
| `docs/` | `architecture.md`, `protocol.md`, `development.md`, `testing.md`, `distribution.md`. |
| `CHANGELOG.md` | Release history and known limitations. |
| `.user/` | Local scratch space, ignored by git via `/.user/`. **Never commit it.** |

## Environment

The maintainer develops in a local container whose definition is **not shipped**
with the repository; `/.devcontainer/` is ignored. A normal development machine
needs the versions below. Build trees are local too (`build/` is ignored).

| Item | Required |
| --- | --- |
| CMake / Ninja | 3.21+ / any recent |
| Qt | 6.5+ with `Core`, `Gui`, `Bluetooth`, `Quick`, `QuickControls2`, `QuickDialogs2`, `Qml` |
| Compiler | C++23 (`GCC 13` or `Clang 17` tested) |
| Android (optional) | Qt for Android arm64, SDK 34, NDK r26, min SDK 24, target 34 |
| BLE on Linux | BlueZ with a powered adapter |

If Qt cannot be found, set `CMAKE_PREFIX_PATH` or `QT_HOST_PATH` to the Qt 6
installation.

## Commands

```bash
# Linux desktop
cmake --preset linux-debug          # or linux-release
cmake --build --preset linux-debug
ctest --preset linux-debug
./build/linux-debug/app/ampbeam
./build/linux-debug/cli/ampbeam-cli --scan

# Coverage gate (GCC + gcov): 100% of core/, see docs/testing.md
cmake --preset linux-coverage
cmake --build --preset linux-coverage
ctest --preset linux-coverage

# Android
cmake --preset android-arm64
cmake --build --preset android-arm64
adb install -r build/android-arm64/app/android-build/build/outputs/apk/debug/android-build-debug.apk

# Distribution artifacts (see docs/distribution.md)
packaging/appimage/build-appimage.sh --version 0.1.0
packaging/android/build-apk.sh --version 0.1.0

# Headless GUI smoke test (no display needed)
xvfb-run -a timeout 6 ./build/linux-debug/app/ampbeam
```

Build trees live in `build/<preset>/`; presets export
`compile_commands.json`. When in doubt about a stale tree, delete
`build/<preset>` and reconfigure — never patch generated files.

## `ampbeam-cli`

```
ampbeam-cli                                             # default: discover + mirror
ampbeam-cli [--mirror] [--device <address|auto>] [--name X] [--duration s] [--plain] [--verbose]
ampbeam-cli [--mirror] [--device <address|auto>] [--csv <file>] [--duration s]
ampbeam-cli --scan [--scan-timeout ms]
ampbeam-cli --device <address|auto> [--name X] [--duration s] [--send <hex>]... [--challenge]
ampbeam-cli --device <address> [--key <code>]... [--key-delay ms] [--key-release]
ampbeam-cli --import capture.log --output tests/fixtures/<name>.json --fixture-name <name>
ampbeam-cli --replay tests/fixtures/<name>.json [--decoder raw|meter] [--mirror] [--plain]
ampbeam-cli --service-uuid <uuid> --notify-uuid <uuid> --command-uuid <uuid> --key-uuid <uuid>
```

The default action mirrors the meter display on one self-overwriting line and
reconnects automatically; it is the computer-multimeter mode. `--mirror` forces
it, `--plain` prints one line per reading, `--verbose` appends the decoded
fields, `--csv <file>` exports every decoded reading, and `--mirror` with
`--replay` renders a fixture offline. Mirror mode always uses `MeterDecoder`;
`--csv` selects it as well. Mirror scan attempts last 5 s unless
`--scan-timeout` is set; `--name` ends the scan as soon as the named device
appears.

Capture lines are `<epochMs>\t<length>\t<hex>\t<detail>`. The importer also
accepts `<epochMs> <hex>` and bare `<hex>` lines, `#` comments and blank lines.

Fixtures are JSON: `{ name, createdAt, source?, frames[] }` where each frame is
`{ receivedMs?, hex }`. Regenerating a fixture is always preferable to hand
editing one.

## Testing rules

* **Red, green, refactor.** No production change without a test that fails
  first. This includes bug fixes: reproduce the bug in a test before fixing it.
* **100% line coverage of `core/`.** Enforced by
  `ctest --preset linux-coverage`; `tests/coverage/required.txt` lists the gated
  files. Do not add a hardware-bound file to that list.
* **Two black-box layers.** `tst_core_blackbox` uses only captured fixtures and
  the public decoder; `tst_cli` runs the real binary and checks its documented
  output and exit codes. Neither may include implementation headers.
* **White-box tests describe behaviour.** `tst_core_whitebox` may assume the wire
  layout and enumerator values, but asserts decoded values, units, details and
  side effects, never private members.
* **Logic lives in `core`.** A decision in `ble/`, `app/` or the CLI must be
  moved into `core` and tested there. Those layers keep only type translation.
  Their headless parts are covered by `tst_ble`/`tst_app`; the rest needs an
  adapter or a display and is covered by the bench suite and the smoke run.
* **No skipped or disabled tests, no coverage exemptions.** Delete dead code
  instead of excluding it. A `QSKIP` or a commented-out assertion is a review
  finding.

## Architecture rules

* `core` knows nothing about Qt or transports; `ble` owns Qt and all platform
  behaviour; `app` and `cli` are thin front-ends.
* `FrameDecoder::decode` is `const` and stateless — keep it that way, it is the
  seam for future worker-thread decoding.
* All BLE objects live on the GUI thread. Use signals; never block the event
  loop waiting for a connection or notification.
* GATT profile (confirmed on hardware): service `0000fff0-…`, data/notify
  `0000fff4-…`, front-panel keys `0000fff3-…`, command channel + challenge
  `0000fff1-…`, info read `0000fff2-…`. Keys and commands go to *different*
  characteristics — one write per keystroke on `fff3`, 16 byte rename payloads on
  `fff1`. Commands on `fff1` act immediately; a **rename only becomes visible in
  the advertisement after the meter accepts another connection**, so verify it
  with a second connection, never with an immediate scan. Override UUIDs via CLI
  flags, not by editing defaults per device.

## Code conventions

* C++23, `CMAKE_CXX_EXTENSIONS OFF`. No GNU extensions.
* Namespace `ampbeam` (`ampbeam::ble` in the transport). Nothing in the global
  namespace except `main`.
* Includes: `#include "ampbeam/core/hex.hpp"` for the core, `#include
  "ble/device_scanner.h"` for the transport. Angle brackets only for system and
  Qt headers. Include blocks are preserved (no auto re-sorting across blocks).
* Warnings are the specification: `core` builds with `-Wconversion
  -Wsign-conversion -Wuseless-cast` on top of the shared set. Fix the code, do
  not disable the flag.
* Prefer `[[nodiscard]]`, `std::optional` over sentinels, `std::span` over
  pointer+length, `qsizetype`/`static_cast` for Qt/C++ boundary conversions.
* Qt: parent-owned objects on the heap, `QStringLiteral` for literals,
  `qsTr()` for user-visible QML strings, `Q_PROPERTY` + `NOTIFY` for anything
  QML reads.
* New QML types need `QML_ELEMENT` (and the header listed in
  `qt_add_qml_module`), not imperative registration.
* Version is single-sourced: `project(AmpBeam VERSION …)` →
  `AMPBEAM_VERSION` compile definition.

## Formatting

Two spaces, no tab characters. `.editorconfig` (editors), `.clang-format`
(C++), CI (verification). Deliberate exceptions: `Makefile` recipes need a real
tab; `*.py` uses four spaces per PEP 8; `.user/**` is never edited.

```bash
find core ble cli app tests -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
  -print0 | xargs -0 clang-format --dry-run --Werror
grep -rnP '^\t' --include='*.cpp' --include='*.h' --include='*.hpp' --include='*.qml' \
  --include='*.txt' --include='*.cmake' --include='*.json' --include='*.yml' \
  --include='*.sh' --include='*.py' core ble cli app tests docs cmake \
  CMakeLists.txt CMakePresets.json
```

`clang-format` 18 has no `c++23` value for `Standard`, so the config uses
`Latest`.

## Branding

* Source of truth: `assets/branding/`; `assets/branding/README.md` lists the
  current files and the icon regeneration steps.
* New artwork must survive Qt's `QSvgRenderer` (SVG 1.2 Tiny subset): no
  filters, no `<style>`, no `currentColor`, no `<text>`, no embedded rasters.
  The existing icon contains a glow filter, which Qt ignores; the window icon
  tries `ampbeam-icon-flat.svg` → `ampbeam-icon.svg` → `ampbeam-icon.png`.
* `app/CMakeLists.txt` guards every branding hook with `EXISTS()`; a checkout
  without artwork must still configure and build. Keep that property.
* Android launcher rasters are generated, never hand-edited:
  `python3 assets/branding/derive-android-icons.py` (stdlib only).

## Changing protocols or fixtures

Any decoder change needs, in the same commit: a capture log entry (or a
description of the hardware bytes), a fixture under `tests/fixtures/`, and a
test in `tests/tst_core_blackbox.cpp` (fixture behaviour) or
`tests/tst_core_whitebox.cpp` (branch matrix) that fails without the change.
The fixture is regenerated with `ampbeam-cli --import`, never hand edited.
Document the finding in `docs/protocol.md`.

## Definition of done

Before you report work as finished:

1. The change was driven by a test that failed first; the test is in the suite
   that matches its layer (white box, fixture black box, CLI black box, unit).
2. `cmake --build --preset linux-debug` succeeds with no new warnings.
3. `ctest --preset linux-debug` passes.
4. `ctest --preset linux-coverage` passes, including the coverage gate, with
   `core/` at 100.0% and no new uncovered lines.
5. `cmake --build --preset android-arm64` succeeds if you touched shared code,
   the manifest, resources or CMake.
6. `clang-format --dry-run --Werror` is clean and no tabs were introduced.
7. Docs touched by the change are updated (`README.md`, `docs/*`, and
   `assets/branding/README.md` for artwork).

## Rejected by review

* Renaming things back to `mm*` or reintroducing "Bluetooth Multimeter".
* Guessing protocol bytes, or a decoder that returns a measurement it did not
  prove.
* Committing `.user/`, `.devcontainer/`, `.vscode/`, capture logs, third-party
  binaries, built APKs or `build/` contents.
* Adding a dependency (library, image tool, formatter) without updating CI and
  the build documentation.
* Introducing a vendor or model name as if it were a hardware fact.
* Silencing a warning, disabling a test, or editing generated files.
* Production code without a test that failed first, or a `core/` line that no
  test executes.
* Adding a hardware-bound file to `tests/coverage/required.txt`, or excluding a
  reachable line from coverage instead of testing or deleting it.
