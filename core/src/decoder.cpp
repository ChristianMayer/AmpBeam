// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Implements the raw and meter frame decoders.
#include "ampbeam/core/decoder.hpp"

#include "ampbeam/core/hex.hpp"
#include "ampbeam/core/meter.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace ampbeam {
namespace {

[[nodiscard]] CurrentType currentTypeFor(MeterGear gear) {
  switch (gear) {
  case MeterGear::AcVoltage:
  case MeterGear::AcCurrent:
    return CurrentType::AC;
  case MeterGear::DcVoltage:
  case MeterGear::DcCurrent:
    return CurrentType::DC;
  default:
    return CurrentType::None;
  }
}

[[nodiscard]] Flags flagsFor(const MeterFrame& frame) {
  Flags flags{};
  flags.hold = meterHasState(frame.state, MeterState::Hold);
  flags.relative = meterHasState(frame.state, MeterState::Relative);
  flags.autoRange = meterHasState(frame.state, MeterState::AutoRange);
  flags.overload = frame.primary.status == MeterValueStatus::Overload;
  return flags;
}

/// Numeric value for the reading, NaN when the display carries no number.
[[nodiscard]] double numericValue(const MeterReading& reading) {
  switch (reading.status) {
  case MeterValueStatus::Normal:
    return reading.value;
  default:
    return std::numeric_limits<double>::quiet_NaN();
  }
}

[[nodiscard]] const char* statusText(MeterValueStatus status) {
  return status == MeterValueStatus::Overload ? "OL" : "";
}

[[nodiscard]] std::string describeReading(const MeterReading& reading) {
  std::string out = meterGearName(reading.range.gear);
  out += ' ';

  const double value = numericValue(reading);
  if (std::isnan(value)) {
    out += statusText(reading.status);
  } else if (reading.range.gear == MeterGear::Ncv) {
    // NCV reports a field level, see the comment in MeterDecoder::decode.
    out += "level=";
    out += std::to_string(reading.magnitude);
  } else {
    out += formatNumber(value, 6);
    if (const char* symbol = unitSymbol(reading.unit); symbol != nullptr && *symbol != '\0') {
      out += ' ';
      out += symbol;
    }
  }

  out += " dp=";
  out += std::to_string(static_cast<unsigned>(reading.range.decimalPoint));
  out += " raw=";
  out += std::to_string(reading.magnitude);
  return out;
}

[[nodiscard]] std::string describe(const MeterFrame& frame) {
  std::string out = describeReading(frame.primary);
  out += " states=";
  bool first = true;
  for (std::uint32_t bit = 0; bit < 24U; ++bit) {
    const auto stateBit = static_cast<MeterState>(bit);
    if (!meterHasState(frame.state, stateBit)) {
      continue;
    }
    if (const char* name = meterStateName(stateBit); name != nullptr) {
      if (!first) {
        out += ',';
      }
      out += name;
      first = false;
    }
  }
  if (first) {
    out += '-';
  }
  return out;
}

} // namespace

DecodedFrame RawDecoder::decode(std::span<const std::uint8_t> frame) const {
  return DecodedFrame{
      .kind = DecodedFrame::Kind::Unknown,
      .measurement = {},
      .detail = toHex(frame, ':'),
  };
}

std::string_view RawDecoder::name() const {
  return "raw";
}

DecodedFrame MeterDecoder::decode(std::span<const std::uint8_t> frame) const {
  const std::optional<MeterFrame> decoded = meterDecodeFrame(frame);
  if (!decoded.has_value()) {
    // Never guess. A frame that is not a complete 15 byte record, or that uses
    // a gear/prefix/status combination no capture proves, is surfaced verbatim.
    return DecodedFrame{
        .kind = DecodedFrame::Kind::Unknown,
        .measurement = {},
        .detail = "meter: undecoded frame [" + toHex(frame, ':') + ']',
    };
  }

  const MeterFrame& parsed = *decoded;

  // Oscilloscope mode does not stream measurements. The meter keeps sending a
  // constant frame whose gear/value words do not track anything on its screen
  // (verified on hardware: 384 frames identical while the operator changed the
  // statistic, the pause state, the vertical range and the time base). Reporting
  // those bytes as a reading would invent a measurement, so surface the mode
  // instead. See docs/protocol.md.
  if (meterHasState(parsed.state, MeterState::Oscilloscope)) {
    return DecodedFrame{
        .kind = DecodedFrame::Kind::Unknown,
        .measurement = {},
        .detail = "meter: oscilloscope mode, no measurement [" + toHex(frame, ':') + ']',
    };
  }

  Measurement measurement{};
  measurement.value = numericValue(parsed.primary);
  measurement.unit = parsed.primary.unit;
  measurement.quantity = parsed.primary.quantity;
  measurement.currentType = currentTypeFor(parsed.primary.range.gear);
  measurement.flags = flagsFor(parsed);

  // NCV is not a decimal measurement: the frame's magnitude counts the field
  // segments the display shows (0 = no field, 1 = first segment and the beeper).
  // Scaling it by the decimal point would turn a level into a fake reading, so
  // the raw magnitude is reported instead. See docs/protocol.md.
  if (parsed.primary.range.gear == MeterGear::Ncv && !std::isnan(measurement.value)) {
    measurement.value = static_cast<double>(parsed.primary.magnitude);
  }

  // A frame may carry a second reading next to the main one, for example the
  // duty cycle shown beside a frequency. It is only reported when the frame says
  // the pair is meaningful.
  if (parsed.secondary.has_value()) {
    measurement.secondary.present = true;
    measurement.secondary.value = numericValue(*parsed.secondary);
    measurement.secondary.unit = parsed.secondary->unit;
    measurement.secondary.quantity = parsed.secondary->quantity;
  }

  std::string detail = "meter: ";
  detail += describe(parsed);
  if (parsed.secondary.has_value()) {
    detail += " | ";
    detail += describeReading(*parsed.secondary);
  }

  return DecodedFrame{
      .kind = DecodedFrame::Kind::Measurement,
      .measurement = measurement,
      .detail = std::move(detail),
  };
}

std::string_view MeterDecoder::name() const {
  return "meter";
}

} // namespace ampbeam
