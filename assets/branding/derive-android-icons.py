#!/usr/bin/env python3
# Copyright (C) 2026 Christian Mayer
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Generates the Android launcher rasters from the canonical PNG.
"""Derive the Android launcher icons from assets/branding/ampbeam-icon.png.

Android needs raster launcher icons, which cannot be produced by CMake from the
vector source, so they are generated once and committed. Re-run this script
whenever the artwork changes:

    python3 assets/branding/derive-android-icons.py

Only the Python standard library is used (no Pillow, no ImageMagick), so this
works in the dev container and in CI without extra packages.
"""

from __future__ import annotations

import pathlib
import struct
import sys
import zlib

SRC = pathlib.Path(__file__).resolve().with_name("ampbeam-icon.png")
RES = pathlib.Path(__file__).resolve().parents[2] / "app" / "android" / "res"

# Legacy launcher icon sizes, per Android density bucket.
DENSITIES = {
    "mdpi": 48,
    "hdpi": 72,
    "xhdpi": 96,
    "xxhdpi": 144,
    "xxxhdpi": 192,
}


def read_png(path: pathlib.Path) -> tuple[int, int, bytearray]:
    """Decode an 8-bit non-interlaced RGB/RGBA PNG into raw RGBA bytes."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG file")

    pos = 8
    idat = bytearray()
    width = height = colour_type = bit_depth = interlace = None
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        kind = data[pos + 4 : pos + 8]
        payload = data[pos + 8 : pos + 8 + length]
        if kind == b"IHDR":
            width, height, bit_depth, colour_type, _, _, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
        elif kind == b"IDAT":
            idat += payload
        elif kind == b"IEND":
            break
        pos += 12 + length

    if bit_depth != 8 or interlace != 0 or colour_type not in (2, 6):
        raise ValueError(
            f"{path}: unsupported PNG (need 8-bit, non-interlaced, RGB or RGBA)"
        )

    channels = 4 if colour_type == 6 else 3
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    previous = bytearray(stride)
    pixels = bytearray(width * height * 4)

    offset = 0
    for y in range(height):
        filter_type = raw[offset]
        offset += 1
        line = bytearray(raw[offset : offset + stride])
        offset += stride

        if filter_type == 1:  # Sub
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif filter_type == 2:  # Up
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif filter_type == 3:  # Average
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif filter_type == 4:  # Paeth
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                up = previous[i]
                upleft = previous[i - channels] if i >= channels else 0
                estimate = left + up - upleft
                pa, pb, pc = (
                    abs(estimate - left),
                    abs(estimate - up),
                    abs(estimate - upleft),
                )
                predictor = left if (pa <= pb and pa <= pc) else (up if pb <= pc else upleft)
                line[i] = (line[i] + predictor) & 0xFF
        elif filter_type != 0:
            raise ValueError(f"{path}: unknown PNG filter {filter_type}")

        base = y * width * 4
        for x in range(width):
            src = x * channels
            dst = base + x * 4
            pixels[dst] = line[src]
            pixels[dst + 1] = line[src + 1]
            pixels[dst + 2] = line[src + 2]
            pixels[dst + 3] = line[src + 3] if channels == 4 else 0xFF

        previous = line

    return width, height, pixels


def resize(width: int, height: int, pixels: bytearray, size: int) -> bytearray:
    """Box-filter downscale in premultiplied alpha space."""
    out = bytearray(size * size * 4)
    ratio = width / size

    for y in range(size):
        y0 = int(y * ratio)
        y1 = max(y0 + 1, min(height, round((y + 1) * ratio)))
        for x in range(size):
            x0 = int(x * ratio)
            x1 = max(x0 + 1, min(width, round((x + 1) * ratio)))
            count = (y1 - y0) * (x1 - x0)
            red = green = blue = alpha = 0
            for sy in range(y0, y1):
                row = sy * width * 4
                for sx in range(x0, x1):
                    index = row + sx * 4
                    a = pixels[index + 3]
                    red += pixels[index] * a
                    green += pixels[index + 1] * a
                    blue += pixels[index + 2] * a
                    alpha += a
            dst = (y * size + x) * 4
            if alpha:
                out[dst] = min(255, round(red / alpha))
                out[dst + 1] = min(255, round(green / alpha))
                out[dst + 2] = min(255, round(blue / alpha))
            out[dst + 3] = round(alpha / count)
    return out


def write_png(path: pathlib.Path, size: int, pixels: bytearray) -> None:
    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + kind
            + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
        )

    raw = bytearray()
    stride = size * 4
    for y in range(size):
        raw.append(0)  # filter type None
        raw += pixels[y * stride : (y + 1) * stride]

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )


def main() -> int:
    if not SRC.exists():
        print(f"missing artwork: {SRC}", file=sys.stderr)
        return 1

    width, height, pixels = read_png(SRC)
    for density, size in DENSITIES.items():
        target = RES / f"mipmap-{density}" / "ic_launcher.png"
        write_png(target, size, resize(width, height, pixels, size))
        print(f"{target.relative_to(RES.parents[2])}: {size}x{size}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
