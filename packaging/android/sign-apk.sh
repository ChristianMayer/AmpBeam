#!/usr/bin/env bash
# Copyright (C) 2026 Christian Mayer
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Align and sign an Android APK produced by the android-release preset.
#
#   packaging/android/sign-apk.sh build/android-release/...-unsigned.apk \
#     dist/AmpBeam-0.1.0-arm64-v8a.apk
#
# Release signing uses a project keystore when one is available:
#
#   AMPBEAM_ANDROID_KEYSTORE           path to a PKCS#12/JKS keystore
#   AMPBEAM_ANDROID_KEYSTORE_BASE64    base64-encoded keystore (CI secret)
#   AMPBEAM_ANDROID_KEYSTORE_PASSWORD  keystore password
#   AMPBEAM_ANDROID_KEY_ALIAS          key alias (default: ampbeam)
#   AMPBEAM_ANDROID_KEY_PASSWORD       key password (default: keystore password)
#
# Without a keystore the script creates a throwaway one for this build. The APK
# is installable, but its signature differs from every other build, so an
# installed copy must be uninstalled before installing this one.
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: sign-apk.sh <unsigned.apk> <output.apk>

Environment:
  ANDROID_SDK_ROOT / ANDROID_HOME     Android SDK with build-tools
  AMPBEAM_ANDROID_KEYSTORE            Project keystore (optional)
  AMPBEAM_ANDROID_KEYSTORE_BASE64     Base64 project keystore (optional)
  AMPBEAM_ANDROID_KEYSTORE_PASSWORD   Keystore password
  AMPBEAM_ANDROID_KEY_ALIAS           Key alias (default: ampbeam)
  AMPBEAM_ANDROID_KEY_PASSWORD        Key password (default: store password)
EOF
}

if [[ $# -ne 2 ]]; then
  usage >&2
  exit 2
fi

input=$1
output=$2

if [[ ! -f "$input" ]]; then
  echo "error: APK not found: $input" >&2
  exit 1
fi

sdk_root="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
if [[ -z "$sdk_root" ]]; then
  echo "error: set ANDROID_SDK_ROOT or ANDROID_HOME" >&2
  exit 1
fi

build_tools_version=$(ls -1 "$sdk_root/build-tools" 2>/dev/null | sort -V | tail -n 1)
if [[ -z "$build_tools_version" ]]; then
  echo "error: no Android build-tools found under $sdk_root/build-tools" >&2
  exit 1
fi
build_tools="$sdk_root/build-tools/$build_tools_version"
zipalign="$build_tools/zipalign"
apksigner="$build_tools/apksigner"
if [[ ! -x "$zipalign" || ! -x "$apksigner" ]]; then
  echo "error: zipalign/apksigner missing in $build_tools" >&2
  exit 1
fi

alias_name="${AMPBEAM_ANDROID_KEY_ALIAS:-ampbeam}"
store_password="${AMPBEAM_ANDROID_KEYSTORE_PASSWORD:-}"
key_password="${AMPBEAM_ANDROID_KEY_PASSWORD:-$store_password}"

work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT

keystore="${AMPBEAM_ANDROID_KEYSTORE:-}"
if [[ -n "${AMPBEAM_ANDROID_KEYSTORE_BASE64:-}" ]]; then
  keystore="$work_dir/release.keystore"
  printf '%s' "$AMPBEAM_ANDROID_KEYSTORE_BASE64" | base64 -d > "$keystore"
fi

if [[ -n "$keystore" ]]; then
  if [[ ! -f "$keystore" ]]; then
    echo "error: keystore not found: $keystore" >&2
    exit 1
  fi
  if [[ -z "$store_password" ]]; then
    echo "error: AMPBEAM_ANDROID_KEYSTORE_PASSWORD is required" >&2
    exit 1
  fi
  echo "Signing with the project keystore."
else
  echo "warning: no project keystore configured; signing with a throwaway key." >&2
  echo "warning: the APK installs, but its signature differs from other builds." >&2
  keystore="$work_dir/ampbeam-ci.keystore"
  store_password="ampbeam-ci"
  key_password="$store_password"
  keytool -genkeypair \
    -keystore "$keystore" \
    -storetype PKCS12 \
    -alias "$alias_name" \
    -keyalg RSA \
    -keysize 2048 \
    -validity 10000 \
    -storepass "$store_password" \
    -keypass "$key_password" \
    -dname "CN=AmpBeam CI, O=AmpBeam, C=DE" >/dev/null 2>&1
fi

mkdir -p "$(dirname "$output")"
aligned="$work_dir/aligned.apk"

"$zipalign" -p -f 4 "$input" "$aligned"
"$apksigner" sign \
  --ks "$keystore" \
  --ks-key-alias "$alias_name" \
  --ks-pass "pass:$store_password" \
  --key-pass "pass:$key_password" \
  --v4-signing-enabled false \
  --out "$output" \
  "$aligned"

"$apksigner" verify --print-certs "$output"
"$zipalign" -c -v 4 "$output" >/dev/null

echo "APK: $output"
