#!/usr/bin/env bash
# Copyright (C) 2026 Christian Mayer
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build the AmpBeam desktop AppImage from an existing Linux release build.
#
# The release preset must already be configured and built:
#
#   cmake --preset linux-release
#   cmake --build --preset linux-release
#   packaging/appimage/build-appimage.sh --version 0.1.0
#
# Qt must be discoverable through QT_HOST_PATH (or --qt-dir). The script
# downloads pinned linuxdeploy binaries into build/appimage-tools unless they
# are already present.
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "$script_dir/../.." && pwd)

build_dir="$repo_root/build/linux-release"
output_dir="$repo_root/dist"
tools_dir="$repo_root/build/appimage-tools"
work_dir="$repo_root/build/appimage-work"
version=""
qt_dir="${QT_HOST_PATH:-}"

# Pinned for reproducible packaging. Bump both the version and the checksum
# together; the checksums are for the x86_64 AppImages.
linuxdeploy_version="1-alpha-20251107-1"
linuxdeploy_sha256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
plugin_version="1-alpha-20250213-1"
plugin_sha256="15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724"

usage() {
  cat <<'EOF'
Usage: build-appimage.sh [options]

Options:
  --build-dir <dir>    Linux release build tree (default: build/linux-release)
  --output-dir <dir>   Directory for the finished AppImage (default: dist)
  --tools-dir <dir>    Cache for linuxdeploy binaries (default: build/appimage-tools)
  --qt-dir <dir>       Qt host installation (default: $QT_HOST_PATH)
  --version <text>     Version in the file name (default: project version)
  -h, --help           Show this help

Environment:
  QT_HOST_PATH         Qt 6 host installation, used when --qt-dir is absent
  APPIMAGE_EXTRACT_AND_RUN=1 is set by the script; FUSE is not required.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      build_dir=$2
      shift 2
      ;;
    --output-dir)
      output_dir=$2
      shift 2
      ;;
    --tools-dir)
      tools_dir=$2
      shift 2
      ;;
    --qt-dir)
      qt_dir=$2
      shift 2
      ;;
    --version)
      version=$2
      shift 2
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ -z "$version" ]]; then
  version=$(sed -n 's/^[[:space:]]*VERSION[[:space:]]\+\([0-9][0-9.]*\).*/\1/p' \
    "$repo_root/CMakeLists.txt" | head -n 1)
fi
if [[ -z "$version" ]]; then
  echo "error: could not determine the project version" >&2
  exit 1
fi

if [[ -z "$qt_dir" ]]; then
  if command -v qmake >/dev/null 2>&1; then
    qt_dir=$(qmake -query QT_INSTALL_PREFIX)
  fi
fi
if [[ -z "$qt_dir" || ! -x "$qt_dir/bin/qmake" ]]; then
  echo "error: set QT_HOST_PATH or pass --qt-dir to a Qt 6 installation" >&2
  exit 1
fi

if [[ ! -x "$build_dir/app/ampbeam" ]]; then
  echo "error: $build_dir/app/ampbeam not found" >&2
  echo "       configure and build the linux-release preset first" >&2
  exit 1
fi

icon_source="$repo_root/assets/branding/ampbeam-icon.png"
if [[ ! -f "$icon_source" ]]; then
  echo "error: $icon_source is missing" >&2
  exit 1
fi

mkdir -p "$tools_dir" "$output_dir"
rm -rf "$work_dir"
mkdir -p "$work_dir/AppDir" "$work_dir/icons"

fetch_tool() {
  local url=$1
  local destination=$2
  local checksum=$3

  if [[ -x "$destination" ]]; then
    return
  fi

  echo "Downloading $(basename "$destination")..."
  curl -fsSL --retry 3 -o "$destination.part" "$url"
  if ! echo "$checksum  $destination.part" | sha256sum -c - >/dev/null; then
    rm -f "$destination.part"
    echo "error: checksum mismatch for $url" >&2
    exit 1
  fi
  mv "$destination.part" "$destination"
  chmod +x "$destination"
}

fetch_tool \
  "https://github.com/linuxdeploy/linuxdeploy/releases/download/${linuxdeploy_version}/linuxdeploy-x86_64.AppImage" \
  "$tools_dir/linuxdeploy-x86_64.AppImage" \
  "$linuxdeploy_sha256"
fetch_tool \
  "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/${plugin_version}/linuxdeploy-plugin-qt-x86_64.AppImage" \
  "$tools_dir/linuxdeploy-plugin-qt-x86_64.AppImage" \
  "$plugin_sha256"

# Install the application into a private AppDir. The CMake install rules put
# the binary under usr/bin, the desktop entry under usr/share/applications and
# the icon under usr/share/icons, which is exactly the layout AppImage expects.
DESTDIR="$work_dir/AppDir" cmake --install "$build_dir" --prefix /usr

# Older appimagetool releases only recognise the legacy AppStream file name.
metainfo_dir="$work_dir/AppDir/usr/share/metainfo"
if [[ -f "$metainfo_dir/ampbeam.metainfo.xml" ]]; then
  cp "$metainfo_dir/ampbeam.metainfo.xml" "$metainfo_dir/ampbeam.appdata.xml"
fi

# appimagetool looks for a top-level PNG named after Icon= in the desktop file
# to create .DirIcon. linuxdeploy would otherwise only link the SVG.
cp "$icon_source" "$work_dir/icons/ampbeam.png"
cp "$icon_source" "$work_dir/AppDir/ampbeam.png"

export PATH="$tools_dir:$PATH"
export QMAKE="$qt_dir/bin/qmake"
export LD_LIBRARY_PATH="$qt_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# The QML files are embedded in the binary with RESOURCE_PREFIX /qt/qml; point
# the plugin at the sources so it can still deploy the Qt Quick modules the
# application imports.
export QML_SOURCES_PATHS="$repo_root/app"
export APPIMAGE_EXTRACT_AND_RUN=1
export ARCH=x86_64

(
  cd "$work_dir"
  linuxdeploy-x86_64.AppImage \
    --appdir "$work_dir/AppDir" \
    --desktop-file "$work_dir/AppDir/usr/share/applications/ampbeam.desktop" \
    --icon-file "$work_dir/icons/ampbeam.png" \
    --plugin qt \
    --output appimage
)

generated=$(find "$work_dir" -maxdepth 1 -name '*.AppImage' -print -quit)
if [[ -z "$generated" ]]; then
  echo "error: linuxdeploy did not create an AppImage" >&2
  exit 1
fi

target="$output_dir/AmpBeam-${version}-x86_64.AppImage"
mv "$generated" "$target"
chmod +x "$target"

echo "AppImage: $target"
