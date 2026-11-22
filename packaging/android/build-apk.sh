#!/usr/bin/env bash
# Copyright (C) 2026 Christian Mayer
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Configure, build and sign the arm64-v8a Android release APK.
#
# Required environment:
#   QT_ANDROID_PATH    Qt for Android installation
#   QT_HOST_PATH       Qt host installation of the same version
#   ANDROID_SDK_ROOT   Android SDK 34+
#   ANDROID_NDK_ROOT   Android NDK r26
#
# Optional signing variables are documented in sign-apk.sh.
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "$script_dir/../.." && pwd)

build_dir="$repo_root/build/android-release"
output_dir="$repo_root/dist"
version=""

usage() {
  cat <<'EOF'
Usage: build-apk.sh [options]

Options:
  --build-dir <dir>    Android release build tree (default: build/android-release)
  --output-dir <dir>   Directory for the signed APK (default: dist)
  --version <text>     Version in the file name (default: project version)
  -h, --help           Show this help
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

: "${QT_ANDROID_PATH:?set QT_ANDROID_PATH to the Qt for Android installation}"
: "${QT_HOST_PATH:?set QT_HOST_PATH to the Qt host installation}"
: "${ANDROID_SDK_ROOT:?set ANDROID_SDK_ROOT to the Android SDK}"
: "${ANDROID_NDK_ROOT:?set ANDROID_NDK_ROOT to the Android NDK}"
export QT_ANDROID_PATH QT_HOST_PATH ANDROID_SDK_ROOT ANDROID_NDK_ROOT

cmake --preset android-release

# Maven Central occasionally rate-limits shared CI runners with HTTP 429.
# Gradle caches completed downloads, so a short backoff and retry usually
# succeeds. Other build errors fail immediately.
build_log=$(mktemp)
trap 'rm -f "$build_log"' EXIT
build_ok=0
for attempt in 1 2 3; do
  if cmake --build --preset android-release 2>&1 | tee "$build_log"; then
    build_ok=1
    break
  fi
  if ! grep -q 'status code 429' "$build_log"; then
    break
  fi
  if [[ "$attempt" -lt 3 ]]; then
    echo "Maven Central rate limit; retrying the Android build in $((attempt * 30)) s" >&2
    sleep $((attempt * 30))
  fi
done
if [[ "$build_ok" -ne 1 ]]; then
  echo "error: Android build failed" >&2
  exit 1
fi

apk_dir="$build_dir/app/android-build/build/outputs/apk/release"
unsigned=$(find "$apk_dir" -maxdepth 1 -name '*unsigned.apk' -print -quit 2>/dev/null || true)
if [[ -z "$unsigned" ]]; then
  unsigned=$(find "$apk_dir" -maxdepth 1 -name '*.apk' -print -quit 2>/dev/null || true)
fi
if [[ -z "$unsigned" ]]; then
  echo "error: no release APK was produced under $build_dir" >&2
  exit 1
fi

mkdir -p "$output_dir"
target="$output_dir/AmpBeam-${version}-arm64-v8a.apk"
"$script_dir/sign-apk.sh" "$unsigned" "$target"
