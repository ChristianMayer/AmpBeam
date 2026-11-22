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

void appendValue(std::string& out, double value, Unit unit, int displayDecimals) {
  if (std::isnan(value)) {
    out += "---";
  } else if (displayDecimals >= 0) {
    out += formatFixedNumber(value, displayDecimals);
  } else {
    out += formatNumber(value, 6);
  }
  const char* symbol = unitSymbol(unit);
  if (symbol != nullptr && *symbol != '\0') {
    out += ' ';
    out += symbol;
  }
}

} // namespace

std::string monitorModeName(const Measurement& measurement) {
  return meterGearName(measurement.gear);
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
  appendValue(out, measurement.value, measurement.unit, measurement.displayDecimals);

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
    // The second reading is the stored reference in REL mode and MAX in the
    // MinMax view; both are stated by docs/protocol.md. Everything else is
    // shown without a label rather than guessed.
    if (measurement.flags.relative) {
      out += "REF ";
    } else if (measurement.flags.minMax) {
      out += "MAX ";
    }
    appendValue(out, measurement.secondary.value, measurement.secondary.unit,
                measurement.secondary.displayDecimals);
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
