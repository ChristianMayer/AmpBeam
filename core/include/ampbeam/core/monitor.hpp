// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Rendering and retry policy for a live mirror of the meter display.
#pragma once

#include <string>

#include "measurement.hpp"

namespace ampbeam {

/// Short name of the meter mode a reading came from: "DCV", "ACV", "DCA",
/// "ACA", "RES", "CAP", "FREQ", "DUTY", "DIODE", "CONT", "NCV", or "?" when the
/// frame did not establish one.
[[nodiscard]] std::string monitorModeName(const Measurement& measurement);

/// Numeric part of a reading as the meter displays it, e.g. `4.998`, or `---`
/// when there is no number (blank display or overload). No unit.
///
/// This is the same text `monitorLine` prints, split out for a GUI that draws
/// the number and the unit with different sizes.
[[nodiscard]] std::string monitorValueText(const Measurement& measurement);

/// Unit symbol of the main reading (`V`, `mV`, `Ω`, ...); empty when the
/// reading has no unit (NCV field level, duty cycle is handled by its unit).
[[nodiscard]] std::string monitorUnitText(const Measurement& measurement);

/// Secondary reading with its confirmed role label (`MAX 9.371 mV`,
/// `REF -2.432 mV`), or an empty string when the frame carries none.
[[nodiscard]] std::string monitorSecondaryText(const Measurement& measurement);

/// `true` for the gears whose ranges can be selected (DCV, ACV, DCA, ACA,
/// resistance, capacitance, frequency). Continuity, diode, duty, NCV and unknown
/// gears return `false`, so an absent AUTO bit is not reported as "manual" for
/// them. See docs/protocol.md §5.1.
[[nodiscard]] bool monitorGearHasRanges(MeterGear gear);

/// One self-contained line for a live terminal mirror of the meter display,
/// e.g. `DCV  4.998 V  [auto]` or
/// `DCV  0.107 mV  [manual]  [minmax]  |  MAX 9.371 mV`.
///
/// Values are printed with the meter's own decimal places when the frame
/// carries them, and `[manual]` is shown when AUTO is clear on a function that
/// has selectable ranges. The line contains only what the measurement proves. A
/// reading with no number (blank display, overload) prints `---`. A frame the
/// decoder rejected is not a measurement; the caller renders that case itself.
[[nodiscard]] std::string monitorLine(const Measurement& measurement);

/// Backoff for reconnecting a mirror session: 1, 2, 4, 8, 16 seconds and then
/// 30 seconds. One instance per session; `reset()` after the first successful
/// connection so a later drop reconnects quickly.
class ReconnectPolicy {
public:
  /// Delay for the next attempt, in milliseconds. Records the attempt.
  [[nodiscard]] int nextDelayMs();

  /// Delays handed out since the last `reset()`.
  [[nodiscard]] unsigned attempts() const;

  void reset();

private:
  unsigned m_attempts = 0;
};

} // namespace ampbeam
