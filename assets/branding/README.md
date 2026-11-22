# AmpBeam branding assets

This directory is the **single source of truth** for the official AmpBeam logo.
Every other place that shows a logo — window icon, Linux icon theme, Android
launcher, README — is generated from a file in here or references it directly.

## Files present

| File | Format | Consumed by |
| --- | --- | --- |
| `ampbeam-icon.svg` | SVG, 512×512 squircle with the mark | Window icon, freedesktop icon theme |
| `ampbeam-icon.png` | PNG, 512×512 RGBA | Window icon fallback, Android rasters |
| `ampbeam-banner.svg` | SVG, 640×160 horizontal lockup | README header and documentation |
| `derive-android-icons.py` | Python 3, stdlib only | Regenerates the Android launcher icons |

## Build integration

`app/CMakeLists.txt` guards every branding hook with `EXISTS()`, so the project
builds with or without artwork. At runtime `app/main.cpp` picks the first icon
that loads from `ampbeam-icon-flat.svg` → `ampbeam-icon.svg` →
`ampbeam-icon.png`. `ampbeam-icon.svg` contains a glow filter that Qt's SVG
renderer ignores; add a filter-free `ampbeam-icon-flat.svg` if that matters. On
Linux, `cmake --install` installs the scalable icon and the desktop entry.

## Android launcher icons

The rasters are generated once and committed under `app/android/res/`:

```text
app/android/res/mipmap-mdpi/ic_launcher.png        48 x 48
app/android/res/mipmap-hdpi/ic_launcher.png        72 x 72
app/android/res/mipmap-xhdpi/ic_launcher.png       96 x 96
app/android/res/mipmap-xxhdpi/ic_launcher.png     144 x 144
app/android/res/mipmap-xxxhdpi/ic_launcher.png    192 x 192
```

After changing `ampbeam-icon.png`, regenerate them with:

```bash
python3 assets/branding/derive-android-icons.py
```

The script uses only the Python standard library. `AndroidManifest.xml`
references the result through `android:icon="@mipmap/ic_launcher"`.

## Rules

* Keep `ampbeam-icon.svg` square with a transparent surround; it is reused at
  16 px and masked by launchers.
* Do not bake a background rectangle into a mark that is meant to be
  transparent.
* Do not copy artwork elsewhere in the tree — reference these paths.
* Do not extract artwork from any third-party application or package. The
  AmpBeam logo must be original work; see `CONTRIBUTING.md`.


