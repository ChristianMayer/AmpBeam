#!/usr/bin/env python3
# Copyright (C) 2026 Christian Mayer
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fails a test run when a gated source file is not fully covered.
"""Enforce the project's line-coverage invariant.

Runs ``gcov`` over every instrumented object in a ``--coverage`` build tree and
fails when a file listed in ``required.txt`` has an executable line that no test
executed. Files outside the list are reported for visibility but do not fail the
gate.

Usage:
    python3 tests/coverage/coverage_gate.py --build-dir build/linux-coverage

The script only needs the Python standard library and the ``gcov`` that belongs
to the GCC used for the build.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

LINE_RE = re.compile(r"^\s*([^:]*):\s*(\d+):(.*)$")
SOURCE_RE = re.compile(r"^\s*-\s*:\s*0:Source:(.*)$")


def parse_gcov_text(text: str) -> tuple[int, int, list[int]]:
    """Return (executed, executable, uncovered line numbers) for one .gcov file."""
    executed = 0
    executable = 0
    uncovered: list[int] = []
    for raw in text.splitlines():
        line = LINE_RE.match(raw)
        if line is None:
            continue
        count = line.group(1).strip()
        number = int(line.group(2))
        if count in ("-", "====="):
            # `-` is non-executable. GCC also prints `=====` for some closing
            # braces that carry no statement; counting those as misses would
            # make 100% coverage unreachable without weakening the gate.
            continue
        executable += 1
        if count == "#####":
            uncovered.append(number)
        else:
            executed += 1
    return executed, executable, uncovered


def source_of(gcov_text: str, fallback: Path) -> Path:
    for raw in gcov_text.splitlines():
        match = SOURCE_RE.match(raw)
        if match is not None:
            return Path(match.group(1))
    return fallback


def collect(build_dir: Path) -> dict[Path, tuple[int, int, list[int]]]:
    """Map absolute source paths to their coverage counters."""
    results: dict[Path, tuple[int, int, list[int]]] = {}
    for gcno in sorted(build_dir.rglob("*.gcno")):
        with tempfile.TemporaryDirectory() as work:
            subprocess.run(
                ["gcov", "-b", "-c", str(gcno)],
                cwd=work,
                capture_output=True,
                text=True,
                check=False,
            )
            for produced in sorted(Path(work).glob("*.gcov")):
                text = produced.read_text(encoding="utf-8", errors="replace")
                source = source_of(text, gcno)
                results[source] = parse_gcov_text(text)
    return results


def load_list(path: Path) -> list[str]:
    if not path.is_file():
        return []
    entries: list[str] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        entry = raw.split("#", 1)[0].strip()
        if entry:
            entries.append(entry)
    return entries


def relative_source(source: Path, source_root: Path) -> str | None:
    try:
        return source.resolve().relative_to(source_root.resolve()).as_posix()
    except ValueError:
        return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path,
                        help="configured --coverage build tree")
    parser.add_argument("--source-root", type=Path, default=None,
                        help="repository root (defaults to the build directory's grandparent)")
    parser.add_argument("--list-dir", type=Path, default=None,
                        help="directory holding required.txt (defaults to this script's directory)")
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    if not build_dir.is_dir():
        print(f"error: build directory not found: {build_dir}", file=sys.stderr)
        return 2

    source_root = args.source_root or build_dir.parent.parent
    list_dir = args.list_dir or Path(__file__).resolve().parent
    required = load_list(list_dir / "required.txt")

    try:
        coverage = collect(build_dir)
    except FileNotFoundError:
        print("error: gcov was not found on PATH; install GCC or run the gate "
              "inside the dev container", file=sys.stderr)
        return 2
    if not coverage:
        print("error: no instrumented sources found; run the tests first "
              "(ctest --preset linux-coverage)", file=sys.stderr)
        return 2

    build_prefix = relative_source(build_dir, source_root)
    rows: list[tuple[str, int, int, list[int], bool]] = []
    for source, (executed, executable, uncovered) in coverage.items():
        rel = relative_source(source, source_root)
        if rel is None:
            continue
        if build_prefix is not None and (rel == build_prefix or
                                         rel.startswith(build_prefix + "/")):
            continue
        if rel.startswith("tests/"):
            # Test sources are not production code; their coverage says nothing
            # about the product and would only add noise.
            continue
        rows.append((rel, executed, executable, uncovered, rel in required))
    rows.sort(key=lambda row: row[0])

    print(f"{'file':<44} {'lines':>12}  {'status':<8}")
    print("-" * 70)
    for rel, executed, executable, uncovered, is_required in rows:
        percent = 100.0 if executable == 0 else 100.0 * executed / executable
        status = "required" if is_required else "reported"
        print(f"{rel:<44} {executed:>5}/{executable:<6} {percent:>5.1f}%  {status}")

    missing: list[str] = []
    for rel in required:
        match = next((row for row in rows if row[0] == rel), None)
        if match is None:
            missing.append(f"{rel}: no coverage data (is the target instrumented and tested?)")
            continue
        _, executed, executable, uncovered, _ = match
        if executable == 0:
            missing.append(f"{rel}: no executable lines were recorded")
        elif uncovered:
            lines = ", ".join(str(number) for number in uncovered[:20])
            more = "" if len(uncovered) <= 20 else f", ... ({len(uncovered)} total)"
            missing.append(f"{rel}: {executable - executed} uncovered line(s): {lines}{more}")

    if missing:
        print("\ncoverage gate failed:", file=sys.stderr)
        for problem in missing:
            print(f"  {problem}", file=sys.stderr)
        print("\nAdd a test that executes the line, or remove the dead code. Hardware-bound "
              "code must not be listed in required.txt; see docs/testing.md.", file=sys.stderr)
        return 1

    print(f"\ncoverage gate passed: {len(required)} file(s) at 100% line coverage")
    return 0


if __name__ == "__main__":
    sys.exit(main())
