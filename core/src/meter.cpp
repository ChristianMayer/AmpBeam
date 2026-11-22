// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Decodes 6 and 15 byte meter frames and splits notification streams.
#include "ampbeam/core/meter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace ampbeam {
namespace {

constexpr std::size_t kFrameSize = 15;

struct StateName {
  MeterState state;
  const char* name;
};

constexpr std::array<StateName, 5> kStateNames{{
    {MeterState::Hold, "HOLD"},
    {MeterState::Relative, "REL"},
    {MeterState::AutoRange, "AUTO"},
    {MeterState::MinMax, "MINMAX"},
    {MeterState::Oscilloscope, "OSC"},
}};

[[nodiscard]] std::uint32_t readLe24(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U);
}

/// Decimal point code (bits 0..2 of the gear word) to scale factor.
///
/// Codes 0..4 were observed on the tested meter. Anything else is not mapped;
/// the frame is treated as unknown instead of being scaled by a guess.
[[nodiscard]] std::optional<double> decimalPointScale(std::uint8_t code) {
  switch (code) {
  case 0:
    return 1.0;
  case 1:
    return 0.1;
  case 2:
    return 0.01;
  case 3:
    return 0.001;
  case 4:
    return 0.0001;
  default:
    return std::nullopt;
  }
}

[[nodiscard]] MeterPrefix prefixFromCode(std::uint8_t code) {
  return static_cast<MeterPrefix>(code & 0x7U);
}

[[nodiscard]] MeterGear gearFromCode(std::uint8_t code) {
  switch (code) {
  case 0:
    return MeterGear::DcVoltage;
  case 1:
    return MeterGear::AcVoltage;
  case 2:
    return MeterGear::DcCurrent;
  case 3:
    return MeterGear::AcCurrent;
  case 4:
    return MeterGear::Resistance;
  case 5:
    return MeterGear::Capacitance;
  case 6:
    return MeterGear::Frequency;
  case 7:
    return MeterGear::DutyCycle;
  case 10:
    return MeterGear::Diode;
  case 11:
    return MeterGear::Continuity;
  case 13:
    return MeterGear::Ncv;
  default:
    return MeterGear::Unknown;
  }
}

[[nodiscard]] double prefixFactor(MeterPrefix prefix) {
  constexpr std::array<double, 8> kFactors{1e-12, 1e-9, 1e-6, 1e-3, 1.0, 1e3, 1e6, 1e9};
  return kFactors[static_cast<std::size_t>(prefix)];
}

[[nodiscard]] Unit baseUnit(MeterGear gear) {
  switch (gear) {
  case MeterGear::DcVoltage:
  case MeterGear::AcVoltage:
  case MeterGear::Diode:
    return Unit::Volt;
  case MeterGear::DcCurrent:
  case MeterGear::AcCurrent:
    return Unit::Ampere;
  case MeterGear::Resistance:
  case MeterGear::Continuity:
    return Unit::Ohm;
  case MeterGear::Capacitance:
    return Unit::Farad;
  case MeterGear::Frequency:
    return Unit::Hertz;
  default:
    return Unit::None;
  }
}

[[nodiscard]] Quantity quantityFor(MeterGear gear) {
  switch (gear) {
  case MeterGear::DcVoltage:
  case MeterGear::AcVoltage:
    return Quantity::Voltage;
  case MeterGear::Diode:
    return Quantity::Diode;
  case MeterGear::DcCurrent:
  case MeterGear::AcCurrent:
    return Quantity::Current;
  case MeterGear::Resistance:
    return Quantity::Resistance;
  case MeterGear::Continuity:
    return Quantity::Continuity;
  case MeterGear::Capacitance:
    return Quantity::Capacitance;
  case MeterGear::Frequency:
    return Quantity::Frequency;
  default:
    return Quantity::Unknown;
  }
}

/// Unit for the (gear, prefix) pair. `absorbed` is true when the returned unit
/// already contains the prefix; otherwise the caller folds the prefix into the
/// value.
struct UnitChoice {
  Unit unit = Unit::None;
  Quantity quantity = Quantity::Unknown;
  bool absorbed = true;
};

[[nodiscard]] std::optional<UnitChoice> prefixedUnit(MeterGear gear, MeterPrefix prefix) {
  // These two display their unit directly; the prefix field does not scale it.
  if (gear == MeterGear::DutyCycle) {
    return UnitChoice{.unit = Unit::Percent, .quantity = Quantity::DutyCycle, .absorbed = true};
  }
  if (gear == MeterGear::Ncv) {
    return UnitChoice{.unit = Unit::None, .quantity = Quantity::Unknown, .absorbed = true};
  }

  const Quantity quantity = quantityFor(gear);
  const Unit base = baseUnit(gear);
  if (quantity == Quantity::Unknown || base == Unit::None) {
    return std::nullopt;
  }

  Unit direct = Unit::None;
  if (base == Unit::Volt) {
    if (prefix == MeterPrefix::Micro) {
      direct = Unit::Microvolt;
    } else if (prefix == MeterPrefix::Milli) {
      direct = Unit::Millivolt;
    } else if (prefix == MeterPrefix::Kilo) {
      direct = Unit::Kilovolt;
    }
  } else if (base == Unit::Ampere) {
    if (prefix == MeterPrefix::Nano) {
      direct = Unit::Nanoampere;
    } else if (prefix == MeterPrefix::Micro) {
      direct = Unit::Microampere;
    } else if (prefix == MeterPrefix::Milli) {
      direct = Unit::Milliampere;
    }
  } else if (base == Unit::Ohm) {
    if (prefix == MeterPrefix::Micro) {
      direct = Unit::Microohm;
    } else if (prefix == MeterPrefix::Kilo) {
      direct = Unit::Kiloohm;
    } else if (prefix == MeterPrefix::Mega) {
      direct = Unit::Megaohm;
    } else if (prefix == MeterPrefix::Giga) {
      direct = Unit::Gigaohm;
    }
  } else if (base == Unit::Farad) {
    if (prefix == MeterPrefix::Pico) {
      direct = Unit::Picofarad;
    } else if (prefix == MeterPrefix::Nano) {
      direct = Unit::Nanofarad;
    } else if (prefix == MeterPrefix::Micro) {
      direct = Unit::Microfarad;
    } else if (prefix == MeterPrefix::Milli) {
      direct = Unit::Millifarad;
    }
  } else if (base == Unit::Hertz) {
    if (prefix == MeterPrefix::Kilo) {
      direct = Unit::Kilohertz;
    } else if (prefix == MeterPrefix::Mega) {
      direct = Unit::Megahertz;
    } else if (prefix == MeterPrefix::Giga) {
      direct = Unit::Gigahertz;
    }
  }

  if (direct != Unit::None) {
    return UnitChoice{.unit = direct, .quantity = quantity, .absorbed = true};
  }
  return UnitChoice{.unit = base, .quantity = quantity, .absorbed = false};
}

[[nodiscard]] std::optional<MeterRange> decodeRange(std::uint32_t word) {
  MeterRange range{};
  range.decimalPoint = static_cast<std::uint8_t>(word & 0x7U);
  const std::optional<double> scale = decimalPointScale(range.decimalPoint);
  if (!scale.has_value()) {
    return std::nullopt;
  }
  range.scale = *scale;
  range.prefix = prefixFromCode(static_cast<std::uint8_t>((word >> 3U) & 0x7U));
  const auto code = static_cast<std::uint8_t>((word >> 6U) & 0x1FU);
  range.code = code;
  range.gear = gearFromCode(code);
  range.hasSecondValue = ((word >> 12U) & 0x1U) != 0U;
  return range;
}

[[nodiscard]] MeterReading decodeValue(std::uint32_t word, MeterRange range) {
  MeterReading reading{};
  reading.present = true;
  reading.range = range;
  reading.negative = ((word >> 23U) & 0x1U) != 0U;
  reading.magnitude = word & 0x7FFFFU;

  switch (static_cast<std::uint8_t>((word >> 20U) & 0x7U)) {
  case 0:
    reading.status = MeterValueStatus::Normal;
    break;
  case 1:
    reading.status = MeterValueStatus::Overload;
    break;
  default:
    reading.status = MeterValueStatus::Unknown;
    break;
  }
  return reading;
}

[[nodiscard]] bool finishReading(MeterReading& reading) {
  const std::optional<UnitChoice> choice = prefixedUnit(reading.range.gear, reading.range.prefix);
  if (!choice.has_value()) {
    return false;
  }
  reading.unit = choice->unit;
  reading.quantity = choice->quantity;

  double value = static_cast<double>(reading.magnitude) * reading.range.scale;
  if (!choice->absorbed) {
    value *= prefixFactor(reading.range.prefix);
  }
  if (reading.negative) {
    value = -value;
  }
  reading.value = value;
  return true;
}

[[nodiscard]] std::string describeReading(const MeterReading& reading) {
  std::string out = "gear=";
  out += std::to_string(static_cast<unsigned>(reading.range.code));
  out += " value=";
  out += std::to_string(reading.magnitude);
  out += " dp=";
  out += std::to_string(static_cast<unsigned>(reading.range.decimalPoint));
  if (reading.negative) {
    out += " neg";
  }
  if (reading.status == MeterValueStatus::Overload) {
    out += " OL";
  }
  return out;
}

} // namespace

bool meterHasState(std::uint32_t state, MeterState stateBit) {
  const auto bit = static_cast<std::uint32_t>(stateBit);
  if (bit >= 32U) {
    return false;
  }
  return (state & (std::uint32_t{1} << bit)) != 0U;
}

const char* meterStateName(MeterState stateBit) {
  for (const auto& entry : kStateNames) {
    if (entry.state == stateBit) {
      return entry.name;
    }
  }
  return nullptr;
}

const char* meterGearName(MeterGear gear) {
  switch (gear) {
  case MeterGear::DcVoltage:
    return "DCV";
  case MeterGear::AcVoltage:
    return "ACV";
  case MeterGear::DcCurrent:
    return "DCA";
  case MeterGear::AcCurrent:
    return "ACA";
  case MeterGear::Resistance:
    return "RES";
  case MeterGear::Capacitance:
    return "CAP";
  case MeterGear::Frequency:
    return "FREQ";
  case MeterGear::DutyCycle:
    return "DUTY";
  case MeterGear::Diode:
    return "DIODE";
  case MeterGear::Continuity:
    return "CONT";
  case MeterGear::Ncv:
    return "NCV";
  case MeterGear::Unknown:
    break;
  }
  return "?";
}

const char* meterPrefixSymbol(MeterPrefix prefix) {
  switch (prefix) {
  case MeterPrefix::Pico:
    return "p";
  case MeterPrefix::Nano:
    return "n";
  case MeterPrefix::Micro:
    return "µ";
  case MeterPrefix::Milli:
    return "m";
  case MeterPrefix::None:
    return "";
  case MeterPrefix::Kilo:
    return "k";
  case MeterPrefix::Mega:
    return "M";
  case MeterPrefix::Giga:
    return "G";
  }
  return "?";
}

bool meterIsFrameSize(std::size_t size) {
  return size == kFrameSize;
}

std::optional<MeterFrame> meterDecodeFrame(std::span<const std::uint8_t> frame) {
  if (frame.size() != kFrameSize) {
    return std::nullopt;
  }

  MeterFrame decoded{};
  decoded.size = kFrameSize;
  decoded.state = readLe24(frame, 12);

  // Oscilloscope frames carry no measurement. Return the frame so the decoder
  // can report the mode instead of showing a raw hex dump.
  if (meterHasState(decoded.state, MeterState::Oscilloscope)) {
    decoded.detail = "oscilloscope mode";
    return decoded;
  }

  const std::optional<MeterRange> primaryRange = decodeRange(readLe24(frame, 0));
  if (!primaryRange.has_value()) {
    return std::nullopt;
  }

  decoded.primary = decodeValue(readLe24(frame, 3), *primaryRange);
  if (decoded.primary.status == MeterValueStatus::Unknown || !finishReading(decoded.primary)) {
    return std::nullopt;
  }

  if (primaryRange->hasSecondValue) {
    const std::optional<MeterRange> secondaryRange = decodeRange(readLe24(frame, 6));
    if (!secondaryRange.has_value()) {
      return std::nullopt;
    }
    MeterReading secondary = decodeValue(readLe24(frame, 9), *secondaryRange);
    if (secondary.status == MeterValueStatus::Unknown || !finishReading(secondary)) {
      return std::nullopt;
    }
    decoded.secondary = secondary;
  }

  decoded.detail = describeReading(decoded.primary);
  if (decoded.secondary.has_value()) {
    decoded.detail += " | ";
    decoded.detail += describeReading(*decoded.secondary);
  }
  return decoded;
}

void MeterFramer::reset() {
  m_buffer.clear();
}

std::vector<std::vector<std::uint8_t>> MeterFramer::push(std::span<const std::uint8_t> payload) {
  m_buffer.insert(m_buffer.end(), payload.begin(), payload.end());

  std::vector<std::vector<std::uint8_t>> frames;
  while (m_buffer.size() >= kFrameSize) {
    frames.emplace_back(m_buffer.begin(),
                        m_buffer.begin() + static_cast<std::ptrdiff_t>(kFrameSize));
    m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(kFrameSize));
  }
  return frames;
}

std::optional<MeterDeviceInfo> meterParseDeviceInfo(std::span<const std::uint8_t> payload) {
  if (payload.empty()) {
    return std::nullopt;
  }

  MeterDeviceInfo info{};
  info.seriesId = payload[0];
  if (payload.size() >= 5U) {
    info.version = std::to_string(static_cast<unsigned>(payload[2]));
    info.version += '.';
    info.version += std::to_string(static_cast<unsigned>(payload[3]));
    info.version += '.';
    info.version += std::to_string(static_cast<unsigned>(payload[4]));
  }
  info.raw.assign(payload.begin(), payload.end());
  return info;
}

std::array<std::uint8_t, 2> meterKeyCommand(std::uint8_t key, bool pressed) {
  return {key, static_cast<std::uint8_t>(pressed ? 1U : 0U)};
}

std::optional<std::array<std::uint8_t, 16>> meterRenameCommand(std::string_view name) {
  constexpr std::size_t kPayloadSize = 16;
  if (name.empty() || name.size() > kPayloadSize - 1) {
    return std::nullopt;
  }
  std::array<std::uint8_t, kPayloadSize> out{};
  out[0] = meterRenameMarker;
  for (std::size_t index = 0; index < name.size(); ++index) {
    const auto byte = static_cast<unsigned char>(name[index]);
    if (byte < 0x20U || byte > 0x7EU) {
      return std::nullopt; // the meter takes printable ASCII only
    }
    out[index + 1] = byte;
  }
  return out;
}

} // namespace ampbeam
