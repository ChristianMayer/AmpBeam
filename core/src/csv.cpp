// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// CSV export of decoded readings.
#include "ampbeam/core/csv.hpp"

#include "ampbeam/core/meter.hpp"
#include "ampbeam/core/monitor.hpp"

#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ampbeam {
namespace {

std::string escape(std::string_view field) {
  if (field.find_first_of(",\"\r\n") == std::string_view::npos) {
    return std::string(field);
  }
  std::string out{"\""};
  for (const char value : field) {
    if (value == '"') {
      out += '"';
    }
    out += value;
  }
  out += '"';
  return out;
}

std::string numberField(double value, int decimals) {
  if (std::isnan(value)) {
    return {};
  }
  if (decimals >= 0) {
    return formatFixedNumber(value, decimals);
  }
  return formatNumber(value, 15);
}

std::string boolField(bool value) {
  return value ? "1" : "0";
}

std::string currentField(CurrentType currentType) {
  switch (currentType) {
  case CurrentType::DC:
    return "DC";
  case CurrentType::AC:
    return "AC";
  case CurrentType::None:
    break;
  }
  return {};
}

} // namespace

std::string csvHeader() {
  return "received_ms,time,mode,value,unit,current,decimals,overload,hold,relative,auto,"
         "manual,minmax,secondary_role,secondary_value,secondary_unit,secondary_decimals";
}

std::string csvRow(const Measurement& measurement, std::string_view receivedAtIso) {
  std::vector<std::string> fields;
  fields.reserve(17);
  fields.push_back(std::to_string(measurement.receivedAtMs));
  fields.push_back(std::string(receivedAtIso));
  fields.push_back(meterGearName(measurement.gear));
  fields.push_back(numberField(measurement.value, measurement.displayDecimals));
  fields.push_back(unitSymbol(measurement.unit));
  fields.push_back(currentField(measurement.currentType));
  fields.push_back(measurement.displayDecimals >= 0 ? std::to_string(measurement.displayDecimals)
                                                    : std::string{});
  fields.push_back(boolField(measurement.flags.overload));
  fields.push_back(boolField(measurement.flags.hold));
  fields.push_back(boolField(measurement.flags.relative));
  fields.push_back(boolField(measurement.flags.autoRange));
  fields.push_back(
      boolField(!measurement.flags.autoRange && monitorGearHasRanges(measurement.gear)));
  fields.push_back(boolField(measurement.flags.minMax));

  const char* role = "";
  if (measurement.secondary.present) {
    if (measurement.flags.relative) {
      role = "REF";
    } else if (measurement.flags.minMax) {
      role = "MAX";
    }
  }
  fields.emplace_back(role);
  fields.push_back(
      measurement.secondary.present
          ? numberField(measurement.secondary.value, measurement.secondary.displayDecimals)
          : std::string{});
  fields.push_back(measurement.secondary.present ? unitSymbol(measurement.secondary.unit)
                                                 : std::string{});
  fields.push_back(measurement.secondary.present && measurement.secondary.displayDecimals >= 0
                       ? std::to_string(measurement.secondary.displayDecimals)
                       : std::string{});

  std::string out;
  for (std::size_t index = 0; index < fields.size(); ++index) {
    if (index != 0U) {
      out += ',';
    }
    out += escape(fields[index]);
  }
  return out;
}

} // namespace ampbeam
