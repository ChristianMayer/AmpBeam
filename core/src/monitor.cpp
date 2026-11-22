// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Live mirror line rendering and reconnect backoff.
#include "ampbeam/core/monitor.hpp"

#include "ampbeam/core/meter.hpp"

#include <cmath>
#include <cstdint>

namespace ampbeam {
namespace {

/// Number exactly as the LCD shows it, or `---` when there is none.
[[nodiscard]] std::string numericText(double value, int displayDecimals) {
  if (std::isnan(value)) {
    return "---";
  }
  if (displayDecimals >= 0) {
    return formatFixedNumber(value, displayDecimals);
  }
  return formatNumber(value, 6);
}

/// `numericText` followed by the unit symbol when there is one.
[[nodiscard]] std::string valueWithUnit(double value, Unit unit, int displayDecimals) {
  std::string out = numericText(value, displayDecimals);
  const char* symbol = unitSymbol(unit);
  if (symbol != nullptr && *symbol != '\0') {
    out += ' ';
    out += symbol;
  }
  return out;
}

} // namespace

std::string monitorModeName(const Measurement& measurement) {
  return meterGearName(measurement.gear);
}

std::string monitorValueText(const Measurement& measurement) {
  return numericText(measurement.value, measurement.displayDecimals);
}

std::string monitorUnitText(const Measurement& measurement) {
  const char* symbol = unitSymbol(measurement.unit);
  return symbol != nullptr ? std::string{symbol} : std::string{};
}

std::string monitorSecondaryText(const Measurement& measurement) {
  if (!measurement.secondary.present) {
    return {};
  }
  std::string out;
  // The second reading is the stored reference in REL mode and MAX in the
  // MinMax view; both are stated by docs/protocol.md. Everything else is shown
  // without a label rather than guessed.
  if (measurement.flags.relative) {
    out = "REF ";
  } else if (measurement.flags.minMax) {
    out = "MAX ";
  }
  out += valueWithUnit(measurement.secondary.value, measurement.secondary.unit,
                       measurement.secondary.displayDecimals);
  return out;
}

bool monitorGearHasRanges(MeterGear gear) {
  switch (gear) {
  case MeterGear::DcVoltage:
  case MeterGear::AcVoltage:
  case MeterGear::DcCurrent:
  case MeterGear::AcCurrent:
  case MeterGear::Resistance:
  case MeterGear::Capacitance:
  case MeterGear::Frequency:
    return true;
  case MeterGear::DutyCycle:
  case MeterGear::Diode:
  case MeterGear::Continuity:
  case MeterGear::Ncv:
  case MeterGear::Unknown:
    return false;
  }
  return false;
}

std::string monitorLine(const Measurement& measurement) {
  std::string out = monitorModeName(measurement);
  out += "  ";
  out += valueWithUnit(measurement.value, measurement.unit, measurement.displayDecimals);

  if (measurement.flags.overload) {
    out += "  [overload]";
  }
  if (measurement.flags.hold) {
    out += "  [hold]";
  }
  if (measurement.flags.autoRange) {
    out += "  [auto]";
  } else if (monitorGearHasRanges(measurement.gear)) {
    // Verified by the Range-key capture: pressing Range clears the AUTO bit.
    out += "  [manual]";
  }
  if (measurement.flags.relative) {
    out += "  [rel]";
  }
  if (measurement.flags.minMax) {
    out += "  [minmax]";
  }

  if (measurement.secondary.present) {
    out += "  |  ";
    out += monitorSecondaryText(measurement);
  }

  return out;
}

int ReconnectPolicy::nextDelayMs() {
  constexpr std::uint64_t kBaseMs = 1000;
  constexpr std::uint64_t kMaxMs = 30000;

  std::uint64_t delay = kBaseMs;
  for (unsigned attempt = 0; attempt < m_attempts && delay < kMaxMs; ++attempt) {
    delay *= 2;
  }
  if (delay > kMaxMs) {
    delay = kMaxMs;
  }
  ++m_attempts;
  return static_cast<int>(delay);
}

unsigned ReconnectPolicy::attempts() const {
  return m_attempts;
}

void ReconnectPolicy::reset() {
  m_attempts = 0;
}

} // namespace ampbeam
