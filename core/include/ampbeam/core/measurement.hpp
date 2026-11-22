// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Measurement model, units, quantities and display formatting.
#pragma once

#include <cstdint>
#include <string>

namespace ampbeam {

/// Physical quantity a measurement represents.
///
/// Only quantities observed on the tested meter are listed. Anything else is a
/// guess and belongs in `docs/protocol.md` as unknown, not in the model.
enum class Quantity : std::uint8_t {
  Unknown,
  Voltage,
  Current,
  Resistance,
  Capacitance,
  Frequency,
  DutyCycle,
  Continuity,
  Diode,
};

/// Display unit. Deliberately explicit instead of a prefix + base unit pair so
/// the decoder can report exactly what the meter shows.
///
/// Only units needed by the confirmed measurement functions are listed; every
/// SI magnitude of those functions is supported.
enum class Unit : std::uint8_t {
  None,
  Volt,
  Millivolt,
  Microvolt,
  Kilovolt,
  Ampere,
  Milliampere,
  Microampere,
  Nanoampere,
  Ohm,
  Kiloohm,
  Megaohm,
  Gigaohm,
  Microohm,
  Farad,
  Picofarad,
  Nanofarad,
  Microfarad,
  Millifarad,
  Hertz,
  Kilohertz,
  Megahertz,
  Gigahertz,
  Percent,
};

/// Meter indicators that a capture confirms. Unknown state bits are ignored.
struct Flags {
  bool hold = false;
  bool autoRange = false;
  bool overload = false;
  /// Relative measurement (REL): the displayed value is relative to a stored
  /// reference.
  bool relative = false;

  friend bool operator==(const Flags&, const Flags&) = default;
};

/// Current type, derived from the verified gear code (DCV/ACV/DCA/ACA).
enum class CurrentType : std::uint8_t {
  None,
  DC,
  AC,
};

/// A single decoded reading.
/// A second reading carried by the same frame. The meter displays it next to the
/// main value; a decoder that cannot interpret it leaves `present` false rather
/// than inventing a value.
struct SecondaryReading {
  bool present = false;
  double value = 0.0;
  Unit unit = Unit::None;
  Quantity quantity = Quantity::Unknown;

  friend bool operator==(const SecondaryReading&, const SecondaryReading&) = default;
};

struct Measurement {
  /// Numeric value, in `unit`. NaN means "no numeric value" (blank display
  /// or overload).
  double value = 0.0;
  Unit unit = Unit::None;
  Quantity quantity = Quantity::Unknown;
  CurrentType currentType = CurrentType::None;
  Flags flags{};
  /// Second reading a frame can carry alongside the main one, e.g. the duty
  /// cycle shown next to a frequency or the frequency shown under an AC voltage.
  SecondaryReading secondary{};
  /// Milliseconds since the UNIX epoch when the frame was received.
  std::uint64_t receivedAtMs = 0;

  friend bool operator==(const Measurement&, const Measurement&) = default;
};

/// UTF-8 symbol for a unit, e.g. "V", "mV", "Ω", "µF", "°C".
[[nodiscard]] const char* unitSymbol(Unit unit);

/// Quantity that a unit belongs to.
[[nodiscard]] Quantity quantityOf(Unit unit);

/// Stable, human readable quantity name (English).
[[nodiscard]] const char* quantityName(Quantity quantity);

/// Formats a number with at most `significantDigits` significant digits and no
/// trailing zeros. Uses the classic C locale ("." decimal separator).
[[nodiscard]] std::string formatNumber(double value, int significantDigits = 5);

/// Formats a reading for display, e.g. "4.998 V DC [hold]".
[[nodiscard]] std::string format(const Measurement& measurement);

} // namespace ampbeam
