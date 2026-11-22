# Distribution

AmpBeam ships two user-facing binaries:

| Artifact | Platform | Contents |
| --- | --- | --- |
| `AmpBeam-<version>-x86_64.AppImage` | Linux x86_64 | Qt Quick application, bundled Qt 6 runtime and Qt Quick modules, and the `ampbeam-cli` tool. |
| `AmpBeam-<version>-arm64-v8a.apk` | Android arm64-v8a | Qt Quick application for Android 7.0 (API 24) and newer. |

Both are attached to every tagged release. Every pull request gets a nightly
build of the same two artifacts as workflow artifacts.

## Where to download

### Releases

Every tag matching `v*` (for example `v0.1.0`) runs the
[`Release` workflow](../.github/workflows/release.yml). The GitHub release for
that tag contains:

```text
AmpBeam-0.1.0-x86_64.AppImage
AmpBeam-0.1.0-arm64-v8a.apk
SHA256SUMS
```

The release is created only after both packages have been built and smoke
tested. Download the files from the
[releases page](https://github.com/ChristianMayer/AmpBeam/releases).

Check the downloads before running them:

```bash
sha256sum -c SHA256SUMS
```

### Pull-request nightlies

Every pull request (and every push to `main`) runs the
[`Nightly` workflow](../.github/workflows/nightly.yml). The artifacts are kept
for 14 days on the workflow run:

1. Open the pull request and select a **Nightly** run under **Checks**.
2. Open the run summary and scroll to **Artifacts**.
3. Download `ampbeam-appimage-pr-<number>` or `ampbeam-apk-pr-<number>`.
4. Extract the ZIP. The files are named
   `AmpBeam-nightly-pr<number>-<commit>-x86_64.AppImage` and
   `AmpBeam-nightly-pr<number>-<commit>-arm64-v8a.apk`.

Nightlies are development snapshots. They are built from the pull request's
merge commit and are not published as GitHub releases.

## Linux AppImage

### Requirements

* x86_64 Linux with a glibc-based distribution (the AppImage is built on Ubuntu
  24.04; older glibc releases may refuse to start it).
* X11 or XWayland. The bundled Qt uses the `xcb` platform plugin; a Wayland-only
  session without XWayland is not supported by the AppImage.
* BlueZ with a powered Bluetooth adapter for live readings. The included CLI is
  useful to check the adapter:

  ```bash
  bluetoothctl show
  rfkill list bluetooth
  ```

No system Qt installation is needed. Qt, the Quick modules and the xcb plugins
are inside the AppImage.

### Run it

```bash
chmod +x AmpBeam-0.1.0-x86_64.AppImage
./AmpBeam-0.1.0-x86_64.AppImage
```

The application starts the same automatic discovery and reconnect session as
the CLI. Use **Devices...** to pick a meter explicitly when several BLE devices
are nearby.

If the system lacks FUSE 2 (`dlopen(): error loading libfuse.so.2`), install
`libfuse2`, or run the AppImage in extraction mode:

```bash
./AmpBeam-0.1.0-x86_64.AppImage --appimage-extract-and-run
```

To integrate the application into the desktop menu, use an AppImage integration
tool such as AppImageLauncher, or extract it and run `AppRun` from the extracted
directory. The AppImage contains the `.desktop` file and the icon, so
integration tools can register it.

The AppImage is a single file. "Uninstalling" it means deleting the file (and
any menu entry created by an integration tool).

### Use the bundled CLI

The AppImage also contains `ampbeam-cli`. Extract it when you want the
headless tools:

```bash
./AmpBeam-0.1.0-x86_64.AppImage --appimage-extract
./squashfs-root/usr/bin/ampbeam-cli --scan
./squashfs-root/usr/bin/ampbeam-cli --replay tests/fixtures/meter-hardware-dcv.json --mirror
```

See [`cli/README.md`](../cli/README.md) for every option.

### Troubleshooting

| Symptom | Fix |
| --- | --- |
| `dlopen(): error loading libfuse.so.2` | Install `libfuse2` or use `--appimage-extract-and-run`. |
| `Could not load the Qt platform plugin "xcb"` | The session has no X11 display. Start the app from a desktop session, or enable XWayland. |
| No devices are found | Check `bluetoothctl show` and `rfkill list bluetooth`; the adapter must be powered and unblocked. |
| The file does not start (`Permission denied`) | Run `chmod +x AmpBeam-*.AppImage`. |
| The window opens but no reading appears | The meter must be in Bluetooth mode and nearby. A quiet stream in HOLD, continuity or a steady reading is normal; see the raw tab. |

## Android APK

### Requirements

* Android 7.0 (API 24) or newer.
* An arm64-v8a device (`arm64` ABI). The APK contains no 32-bit code.
* Bluetooth Low Energy support, which the manifest requires.

The APK version name is the project version (`0.1.0`); the Android version code
is derived from it (`0.1.0` becomes `100`), so a newer release can be installed
over an older one.

### Install with adb

```bash
adb devices
adb install -r AmpBeam-0.1.0-arm64-v8a.apk
```

`adb install` reports `INSTALL_FAILED_UPDATE_INCOMPATIBLE` when the installed
copy was signed with a different key. Uninstall the old copy first:

```bash
adb uninstall org.ampbeam.app
adb install AmpBeam-0.1.0-arm64-v8a.apk
```

### Install from the device

1. Copy the APK to the device (USB, MTP, cloud storage).
2. Open it in the file manager.
3. Allow the file manager or browser to install unknown apps when Android asks.
4. Confirm the installation.

On Android 12 and newer, grant the nearby-devices permission on first launch.
If it was denied, enable it under **Settings → Apps → AmpBeam → Permissions**.

### Signing

The APK is signed for installation with `apksigner`:

* **Release builds** use the project keystore when the
  `ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`,
  `ANDROID_KEY_ALIAS` and `ANDROID_KEY_PASSWORD` repository secrets are
  configured.
* **Nightly builds** (and releases without those secrets) are signed with a
  throwaway key generated for that build. They install normally, but Android
  treats them as a different application identity. Uninstall a nightly before
  installing a release signed with another key, and vice versa.

The signing certificate fingerprint is printed by `apksigner verify --print-certs`
and by the CI job.

## How the artifacts are built

The CI pipeline is split into three workflows:

| Workflow | Trigger | Result |
| --- | --- | --- |
| [`ci.yml`](../.github/workflows/ci.yml) | push to `main`, pull request | Linux build, tests and the 100% `core/` coverage gate. |
| [`nightly.yml`](../.github/workflows/nightly.yml) | pull request, push to `main`, manual | AppImage and APK artifacts for the workflow run, kept 14 days. |
| [`release.yml`](../.github/workflows/release.yml) | `v*` tag | AppImage and APK uploaded to the GitHub release, plus `SHA256SUMS`. |

`nightly.yml` and `release.yml` both call the reusable
[`package.yml`](../.github/workflows/package.yml), so the two artifact types are
built by the same steps.

The local equivalents are:

```bash
# Linux AppImage (needs QT_HOST_PATH)
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
packaging/appimage/build-appimage.sh --version 0.1.0

# Android APK (needs QT_ANDROID_PATH, QT_HOST_PATH, ANDROID_SDK_ROOT and
# ANDROID_NDK_ROOT; see docs/development.md)
packaging/android/build-apk.sh --version 0.1.0
```

The finished files are written to `dist/`. The AppImage script downloads pinned
linuxdeploy binaries and a pinned type2 AppImage runtime, and verifies their
SHA-256 checksums before use. The APK script configures the `android-release`
preset, builds the unsigned APK, aligns and signs it, and verifies the result.

Both workflows run a smoke test before uploading:

* The AppImage is started under `Xvfb`; CI fails if the QML module does not
  load or the Qt platform plugin cannot start. The bundled CLI must replay a
  fixture.
* The APK must pass `apksigner verify`, report package `org.ampbeam.app`, and
  contain the `arm64-v8a` ABI.

### Release checklist

1. Update `project(AmpBeam VERSION ...)` in `CMakeLists.txt`, `CHANGELOG.md` and
   the `<releases>` entry in `app/ampbeam.metainfo.xml`.
2. Tag the commit: `git tag v0.1.0 && git push origin v0.1.0`.
3. Wait for the **Release** workflow. It builds, tests and packages both
   artifacts, then attaches them to the GitHub release.
4. Download one asset of each type and install it once on the target platform.
