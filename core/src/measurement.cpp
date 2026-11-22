// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit, quantity and measurement formatting helpers.
#include "ampbeam/core/measurement.hpp"

#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace ampbeam {

const char* unitSymbol(Unit unit) {
  switch (unit) {
  case Unit::None:
    return "";
  case Unit::Volt:
    return "V";
  case Unit::Millivolt:
    return "mV";
  case Unit::Microvolt:
    return "µV";
  case Unit::Kilovolt:
    return "kV";
  case Unit::Ampere:
    return "A";
  case Unit::Milliampere:
    return "mA";
  case Unit::Microampere:
    return "µA";
  case Unit::Nanoampere:
    return "nA";
  case Unit::Ohm:
    return "Ω";
  case Unit::Kiloohm:
    return "kΩ";
  case Unit::Megaohm:
    return "MΩ";
  case Unit::Gigaohm:
    return "GΩ";
  case Unit::Microohm:
    return "µΩ";
  case Unit::Farad:
    return "F";
  case Unit::Picofarad:
    return "pF";
  case Unit::Nanofarad:
    return "nF";
  case Unit::Microfarad:
    return "µF";
  case Unit::Millifarad:
    return "mF";
  case Unit::Hertz:
    return "Hz";
  case Unit::Kilohertz:
    return "kHz";
  case Unit::Megahertz:
    return "MHz";
  case Unit::Gigahertz:
    return "GHz";
  case Unit::Percent:
    return "%";
  }
  return "";
}

Quantity quantityOf(Unit unit) {
  switch (unit) {
  case Unit::Volt:
  case Unit::Millivolt:
  case Unit::Microvolt:
  case Unit::Kilovolt:
    return Quantity::Voltage;
  case Unit::Ampere:
  case Unit::Milliampere:
  case Unit::Microampere:
  case Unit::Nanoampere:
    return Quantity::Current;
  case Unit::Ohm:
  case Unit::Kiloohm:
  case Unit::Megaohm:
  case Unit::Gigaohm:
  case Unit::Microohm:
    return Quantity::Resistance;
  case Unit::Farad:
  case Unit::Picofarad:
  case Unit::Nanofarad:
  case Unit::Microfarad:
  case Unit::Millifarad:
    return Quantity::Capacitance;
  case Unit::Hertz:
  case Unit::Kilohertz:
  case Unit::Megahertz:
  case Unit::Gigahertz:
    return Quantity::Frequency;
  case Unit::Percent:
    return Quantity::DutyCycle;
  case Unit::None:
    return Quantity::Unknown;
  }
  return Quantity::Unknown;
}

const char* quantityName(Quantity quantity) {
  switch (quantity) {
  case Quantity::Unknown:
    return "unknown";
  case Quantity::Voltage:
    return "voltage";
  case Quantity::Current:
    return "current";
  case Quantity::Resistance:
    return "resistance";
  case Quantity::Capacitance:
    return "capacitance";
  case Quantity::Frequency:
    return "frequency";
  case Quantity::DutyCycle:
    return "duty cycle";
  case Quantity::Continuity:
    return "continuity";
  case Quantity::Diode:
    return "diode";
  }
  return "unknown";
}

std::string formatNumber(double value, int significantDigits) {
  if (std::isnan(value)) {
    return "nan";
  }
  if (std::isinf(value)) {
    return value < 0.0 ? "-inf" : "inf";
  }
  if (value == 0.0) {
    return "0";
  }

  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(significantDigits) << std::defaultfloat << value;
  return stream.str();
}

std::string format(const Measurement& measurement) {
  std::string out =
      std::isnan(measurement.value) ? std::string{"---"} : formatNumber(measurement.value);

  const char* symbol = unitSymbol(measurement.unit);
  if (symbol != nullptr && *symbol != '\0') {
    out += ' ';
    out += symbol;
  }

  switch (measurement.currentType) {
  case CurrentType::DC:
    out += " DC";
    break;
  case CurrentType::AC:
    out += " AC";
    break;
  case CurrentType::None:
    break;
  }

  if (measurement.flags.overload) {
    out += " [overload]";
  }
  if (measurement.flags.hold) {
    out += " [hold]";
  }
  if (measurement.flags.autoRange) {
    out += " [auto]";
  }
  if (measurement.flags.relative) {
    out += " [rel]";
  }

  if (measurement.secondary.present) {
    out += " / ";
    out += std::isnan(measurement.secondary.value) ? std::string{"---"}
                                                   : formatNumber(measurement.secondary.value);
    const char* secondarySymbol = unitSymbol(measurement.secondary.unit);
    if (secondarySymbol != nullptr && *secondarySymbol != '\0') {
      out += ' ';
      out += secondarySymbol;
    }
  }

  return out;
}

} // namespace ampbeam
