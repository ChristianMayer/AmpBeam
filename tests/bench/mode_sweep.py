#!/usr/bin/env python3
# Copyright (C) 2026 Christian Mayer
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Drives a real meter through its modes; not run by CI.
"""Bench sweep for the BLE multimeter: press keys, report the measurement mode.

Each key press runs in its own connection because the meter keeps the selected
function across connections, which makes the mapping from press to result
unambiguous. Nothing needs to be connected to the meter's probes: with open
inputs the readings are zero and only the mode (the gear word) carries
information, which is exactly what a mode-coverage check should compare.

Linux only. Connections go through the host's bluetoothd over D-Bus, so the
environment has to be the one described in docs/development.md.

Examples
--------
Enumerate the measurement functions on a bare meter:

    tests/bench/mode_sweep.py --device <address> --codes 1 1 1 1 1 1

Probe which key steps the range, keeping a fixture for the test suite:

    tests/bench/mode_sweep.py --device <address> --codes 2 2 \
        --json tests/fixtures/meter-hardware-key-range.json

The exit status is non-zero when a press produced no frames, so the script can
guard a release build on a bench with the meter attached.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

FRAME_SIZE = 15
DEFAULT_CHALLENGE_VERSION = "5.72"


@dataclass
class Reading:
    """One decoded frame line, reduced to what a mode sweep cares about."""

    gear_word: int
    mode: str

    @property
    def decimal_point(self) -> int:
        return self.gear_word & 0x7

    @property
    def prefix(self) -> int:
        return (self.gear_word >> 3) & 0x7

    @property
    def gear(self) -> int:
        return (self.gear_word >> 6) & 0x1F

    @property
    def second_value(self) -> bool:
        return bool((self.gear_word >> 12) & 0x1)


def parse_capture(text: str) -> list[Reading]:
    """Reads `ampbeam-cli` TSV output and keeps one Reading per long frame."""
    readings: list[Reading] = []
    for line in text.splitlines():
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        raw = parts[2].split()
        if len(raw) < FRAME_SIZE:
            continue
        # Gear word: three little-endian bytes at offset 0.
        gear_word = int(raw[0], 16) | (int(raw[1], 16) << 8) | (int(raw[2], 16) << 16)
        detail = parts[3] if len(parts) > 3 else ""
        mode = detail.split()[1] if detail.startswith("meter: ") else "?"
        readings.append(Reading(gear_word=gear_word, mode=mode))
    return readings


def scan_briefly() -> None:
    """BlueZ drops a device object a few seconds after the last scan."""
    if shutil.which("bluetoothctl") is None:
        return
    subprocess.run(
        ["timeout", "12", "bluetoothctl", "--timeout", "9", "scan", "on"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )


def press(cli: Path, device: str, code: int, duration: int) -> str | None:
    """Connects, presses `code` once and returns the capture text, or None."""
    env = dict(os.environ)
    env.setdefault("BLUETOOTH_FORCE_DBUS_LE_VERSION", DEFAULT_CHALLENGE_VERSION)
    for _ in range(3):
        scan_briefly()
        try:
            completed = subprocess.run(
                [
                    str(cli),
                    "--device",
                    device,
                    "--decoder",
                    "meter",
                    "--duration",
                    str(duration),
                    "--key",
                    str(code),
                ],
                capture_output=True,
                text=True,
                env=env,
                timeout=duration + 25,
                check=False,
            )
        except subprocess.TimeoutExpired:
            continue
        if "\t15\t" in completed.stdout:
            return completed.stdout
    return None


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--device", required=True, help="meter address")
    parser.add_argument(
        "--codes",
        nargs="+",
        type=int,
        default=[1] * 8,
        help="key codes pressed in order, one connection per press (default: eight Select presses)",
    )
    parser.add_argument("--duration", type=int, default=3, help="capture window per press, in seconds")
    parser.add_argument("--out", type=Path, help="append the raw captures to this log")
    parser.add_argument("--json", type=Path, help="also write the frames as a JSON fixture")
    parser.add_argument(
        "--groups",
        type=int,
        default=1,
        help="number of function groups to sweep; between groups the script waits for the "
        "meter's Tab key to be pressed (default 1)",
    )
    parser.add_argument(
        "--expect-modes",
        default="",
        help="comma separated gear codes that must all appear; a key press is occasionally "
        "dropped by the meter, so coverage is checked rather than press-by-press order",
    )
    parser.add_argument(
        "--cli",
        type=Path,
        default=repo_root / "build/linux-debug/cli/ampbeam-cli",
        help="ampbeam-cli binary",
    )
    args = parser.parse_args()

    if not args.cli.is_file():
        print(f"ampbeam-cli not found at {args.cli} (build it first)", file=sys.stderr)
        return 2

    print(f"{'press':>5}  {'code':>4}  {'gear':>4}  {'dp':>2}  {'prefix':>6}  mode")
    failures = 0
    seen: dict[tuple[int, int, int], str] = {}
    captured: list[str] = []
    index = 0

    for group in range(1, args.groups + 1):
        if group > 1:
            input("switch the meter to the next function group (Tab key), then press Enter: ")

        for code in args.codes:
            index += 1
            text = press(args.cli, args.device, code, args.duration)
            if text is None:
                print(f"{index:>5}  {code:>4}  {'--':>4}  {'--':>2}  {'--':>6}  no frames")
                failures += 1
                continue

            captured.append(text)
            readings = parse_capture(text)
            if not readings:
                print(f"{index:>5}  {code:>4}  {'--':>4}  {'--':>2}  {'--':>6}  undecodable")
                failures += 1
                continue

            last = readings[-1]
            print(
                f"{index:>5}  {code:>4}  {last.gear:>4}  {last.decimal_point:>2}  "
                f"{last.prefix:>6}  {last.mode}"
            )
            seen[(last.gear, last.decimal_point, last.prefix)] = last.mode

    print("\ndistinct modes seen (gear, decimal point, prefix -> mode):")
    for (gear, dp, prefix), mode in sorted(seen.items()):
        print(f"  gear {gear:>2}  dp {dp}  prefix {prefix}  {mode}")

    missing: list[int] = []
    if args.expect_modes:
        expected = [int(value, 0) for value in args.expect_modes.split(",") if value.strip()]
        present = {gear for gear, _, _ in seen}
        missing = [gear for gear in expected if gear not in present]
        if missing:
            print(
                "\nmissing gear codes: " + ", ".join(str(gear) for gear in missing),
                file=sys.stderr,
            )

    if args.out is not None:
        with args.out.open("a", encoding="utf-8") as handle:
            handle.writelines(captured)
        print(f"\nraw captures appended to {args.out}")

    if args.json is not None:
        import tempfile

        with tempfile.NamedTemporaryFile("w", suffix=".log", delete=False, encoding="utf-8") as handle:
            # Only frame lines: the importer warns about any other text.
            handle.writelines(
                line + "\n"
                for text in captured
                for line in text.splitlines()
                if "\t15\t" in line
            )
            log_path = handle.name
        subprocess.run(
            [
                str(args.cli),
                "--import",
                log_path,
                "--output",
                str(args.json),
                "--fixture-name",
                args.json.stem,
                "--source",
                "bench sweep: keys "
                + " ".join(str(code) for code in args.codes)
                + " pressed in order on a meter with open inputs",
            ],
            check=False,
        )
        os.unlink(log_path)

    if failures:
        print(f"\n{failures} press(es) produced no usable frames", file=sys.stderr)
        return 1
    if missing:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
