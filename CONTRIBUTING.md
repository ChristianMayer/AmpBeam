# Contributing

Thanks for taking a look! This project is at an early stage, so the most useful
contributions are hardware captures and protocol findings.

## Most valuable: captures from real meters

1. Put the meter in Bluetooth mode and note the exact model.
2. Run `ampbeam-cli --scan`, then `ampbeam-cli --device <address> --duration 30 > capture.log`.
3. While it runs, change every setting on the meter (mode, range, hold, min/max,
   relative, peak, backlight, battery state).
4. Attach the log to an issue together with a description of what you did.

Do not attach proprietary packages, PDFs, fonts or images. Capture logs contain
only the bytes the meter sends.

## Development

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug

# coverage gate: 100% of core/ must be executed by a test
cmake --preset linux-coverage
cmake --build --preset linux-coverage
ctest --preset linux-coverage
```

The project is test-driven: write the failing test first, then the change. Put
the test in the suite that matches its layer — white box for branch matrices,
fixtures for hardware behaviour, `tst_cli` for command-line contracts. Adding a
line to `core/` that no test executes will fail CI. [`docs/testing.md`](docs/testing.md)
explains the layers and the gate.

Please run `clang-format` before opening a pull request. CI enforces it, along
with the two-space / no-tab indentation policy. [`AGENTS.md`](AGENTS.md) lists
the conventions and the pre-flight checklist.

## Protocol changes

Any change to frame decoding must come with a fixture in `tests/fixtures/` and a
test that fails first: fixture behaviour belongs in
`tests/tst_core_blackbox.cpp`, a branch matrix in `tests/tst_core_whitebox.cpp`.
Regenerate the fixture with `ampbeam-cli --import`; never hand edit it. A decoder
may only claim to have decoded a frame when the byte positions are backed by a
capture.

## Licensing

By contributing you agree that your contribution is licensed under
GPL-3.0-or-later. Do not copy code from other applications or proprietary SDKs;
describe the wire format instead.
