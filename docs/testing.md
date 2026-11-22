# Testing

This project is test-driven. A change starts with a failing test, then the
smallest production change that makes it pass, then a cleanup that keeps the
suite green. Tests are not something added after a feature works.

Hardware captures are the one documented exception: the capture comes first, but
it becomes a fixture plus a failing test *before* the decoder changes (see
[Changing a protocol](protocol.md) and the fixture workflow below).

## The coverage invariant

**Every line in `core/` is executed by at least one test.** That is enforced by
`ctest --preset linux-coverage`, which runs the tests under gcov and then runs
`tests/coverage/coverage_gate.py`. The gate reads
`tests/coverage/required.txt`; a file in that list must report 100.0% line
coverage or CTest fails.

Why `core` is the gated set: `ampbeam_core` has no Qt, no I/O, no globals and no
event loop, so every line is reachable from a deterministic test. The remaining
layers fall into two groups:

| Layer | How it is verified | Coverage |
| --- | --- | --- |
| `core/` | White-box and black-box suites below | Gated at 100% |
| `cli/main.cpp` | Black-box `tst_cli`: the real binary, real files, real exit codes | Offline paths covered; BLE paths need an adapter |
| `ble/` | `tst_ble` for state, guards and permission handling; bench suite for connections | Headless paths covered; discovery needs hardware |
| `app/` | `tst_app` for the controller pipeline; headless smoke run | Controller logic covered; QML shell needs a display |

The rule that keeps this honest: **logic belongs in `core`**. If a transport or
controller change adds a decision, put the decision in `core` and test it there.
The remaining code should only translate Qt types into `core` calls.

The gate deliberately does not offer a "skip this line" comment. A line that
cannot be reached is dead code and must be removed; a line that is reachable only
with an adapter or a display must live outside `core` and outside
`required.txt`. Do not add a hardware-bound file to `required.txt`.

`ctest --preset linux-coverage` prints the current per-file table and either
`coverage gate passed` or the exact uncovered lines. Do not copy that table into
documentation: it changes with the first line of code and only a real run
answers it.

## Test layers

### White box — `tests/tst_core_whitebox.cpp`

Built from knowledge of the implementation. Every verified gear code, SI prefix,
decimal point, value status, frame size, framer state, unit, quantity and
formatting flag is exercised explicitly, including the defensive fallbacks for
invalid enum values. These tests answer "did a branch lose its test?".

They are allowed to assume the wire layout. They must still assert observable
behaviour (decoded value, unit, quantity, detail text), never private state.

### Black box — `tests/tst_core_blackbox.cpp`

Driven only by captured fixtures in `tests/fixtures/` and the public
`FrameDecoder` contract. These tests answer "do the hardware bytes still decode
the way the meter showed them?".

A fixture is a capture, not a test input invented to make a branch green. Add or
regenerate fixtures with:

```bash
./build/linux-debug/cli/ampbeam-cli --import capture.log \
    --output tests/fixtures/<name>.json --fixture-name <name>
```

Hand editing a fixture is a review finding; regenerate it instead.

### Black box — `tests/tst_cli.cpp`

Runs the real `ampbeam-cli` executable as a subprocess with `QProcess`. It only
asserts the documented contract: arguments, stdout TSV, stderr diagnostics, exit
codes and the JSON files the tool writes. Nothing is linked from the CLI, so a
refactor cannot make these tests pass accidentally.

Every offline command path is covered: help/version, the default mirror's
duration-bounded shutdown, import validation and conversion, replay with both
decoders, offline mirror rendering (including an undecoded frame), CSV export
(rows, header, decoder forcing and argument/file validation), malformed frames,
missing files and broken JSON. Connection, scan and key-injection paths need an
adapter and are left to the bench suite.

### Unit — `tests/tst_ble.cpp`

Headless checks for the transport's deterministic surface: initial state, state
names, empty-address rejection, write guards without characteristics,
disconnect idempotence, profile overrides and the permission helper. The
discovery/notification state machine is exercised against a real meter.

### Unit — `tests/tst_app.cpp`

Drives `AppController::handleNotification` through the meta-object and checks
raw hex, the bounded log, framing of split notifications, value formatting and
"no measurement" frames. It also covers the rejected-connection path.

### Hardware — `tests/bench/mode_sweep.py`

Registered only when `AMPBEAM_BENCH_DEVICE` is set at configure time. It drives a
real meter through its modes and checks that every frame decodes. CI never runs
it.

### Smoke — application shell

```bash
xvfb-run -a timeout 6 ./build/linux-debug/app/ampbeam
```

Confirms that QML loads, the controller is registered and the window is created.
It is a smoke test, not coverage.

## Running the suites

```bash
# normal development
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug

# coverage gate (GCC + gcov)
cmake --preset linux-coverage
cmake --build --preset linux-coverage
ctest --preset linux-coverage

# hardware bench
AMPBEAM_BENCH_DEVICE=<address> cmake --preset linux-debug
ctest --preset linux-debug -L bench
```

`ctest --preset linux-coverage` prints a per-file table and then either
`coverage gate passed` or the exact uncovered line numbers. Re-run a single
suite with:

```bash
./build/linux-coverage/tests/tst_core_whitebox
./build/linux-coverage/tests/tst_cli -v1
```

The gate treats gcov's `#####` marker as "not executed" and ignores `-` and
`=====` (non-executable and GCC's un-attributed closing braces). It never
treats a line as covered just because it was compiled.

## Writing a new test

1. Write the failing test first. Run it; confirm it fails for the expected
   reason.
2. Make the smallest change that passes.
3. Run the whole preset, not only the new test.
4. If the change is protocol-related, add the capture, the fixture and the test
   in the same commit (see `AGENTS.md`).
5. If a new source file is fully reachable without hardware, add it to
   `tests/coverage/required.txt`.

Naming: test slots describe the behaviour, not the function
(`meterDecodesEveryValueStatus`, not `testDecode2`). One slot per behaviour;
table-driven loops are preferred when the behaviour differs only by input.
