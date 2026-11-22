// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// White-box tests for ampbeam_core.
//
// These tests are built from the structure of the implementation: every gear
// code, prefix, decimal point, value status, frame size, framer state and
// formatting flag is exercised explicitly. The companion
// `tst_core_blackbox.cpp` suite only uses captured fixtures and the public
// decoder contract. Keeping the two apart means a regression shows up either as
// "a branch lost its test" or as "hardware bytes decode differently", never as
// both at once.

#include "ampbeam/core/capture.hpp"
#include "ampbeam/core/decoder.hpp"
#include "ampbeam/core/hex.hpp"
#include "ampbeam/core/measurement.hpp"
#include "ampbeam/core/meter.hpp"
#include "ampbeam/core/selection.hpp"

#include <QTest>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using ampbeam::CurrentType;
using ampbeam::MeterGear;
using ampbeam::MeterPrefix;
using ampbeam::MeterValueStatus;
using ampbeam::Quantity;
using ampbeam::Unit;

Bytes bytes(std::initializer_list<int> values) {
  Bytes out;
  out.reserve(values.size());
  for (const int value : values) {
    out.push_back(static_cast<std::uint8_t>(value));
  }
  return out;
}

/// Gear word of a 15 byte frame: dp in bits 0..2, prefix in 3..5, gear code in
/// 6..10, second-value flag in bit 12.
std::uint32_t gearWord(std::uint8_t decimalPoint, MeterPrefix prefix, std::uint8_t code,
                       bool hasSecondValue = false) {
  return static_cast<std::uint32_t>(decimalPoint) | (static_cast<std::uint32_t>(prefix) << 3U) |
         (static_cast<std::uint32_t>(code) << 6U) | (hasSecondValue ? 1U << 12U : 0U);
}

/// Value word: magnitude in bits 0..18, status in 20..22, sign in bit 23.
std::uint32_t valueWord(std::uint32_t magnitude, std::uint8_t status = 0, bool negative = false) {
  std::uint32_t word = magnitude & 0x7FFFFU;
  word |= (status & 0x7U) << 20U;
  if (negative) {
    word |= 1U << 23U;
  }
  return word;
}

void appendLe24(Bytes& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
}

Bytes longFrame(std::uint32_t gear, std::uint32_t value, std::uint32_t secondGear = 0,
                std::uint32_t secondValue = 0, std::uint32_t state = 0) {
  Bytes out;
  out.reserve(15);
  appendLe24(out, gear);
  appendLe24(out, value);
  appendLe24(out, secondGear);
  appendLe24(out, secondValue);
  appendLe24(out, state);
  return out;
}

QString q(std::string_view value) {
  return QString::fromLatin1(value.data(), static_cast<int>(value.size()));
}

QString utf8(const char* value) {
  return QString::fromUtf8(value);
}

bool nearlyEqual(double actual, double expected) {
  return std::abs(actual - expected) <= std::abs(expected) * 1e-9 + 1e-12;
}

} // namespace

class CoreWhiteBoxTest final : public QObject {
  Q_OBJECT

private slots:
  void unitSymbolsAndQuantities();
  void quantityNames();
  void numberFormatting();
  void measurementFormatting();
  void meterDecodesEveryValueStatus();
  void meterDecodesEveryDecimalPoint();
  void meterUnitAndPrefixMatrix();
  void meterCurrentTypeMatrix();
  void secondaryPairValidation();
  void minMaxStateExposesSecondReading();
  void frequencyWithVoltageSecondary();
  void relativeStateSetsFlag();
  void meterStateAndNameHelpers();
  void framerStateMachine();
  void deviceInfoAndCommands();
  void captureEdgeCases();
  void fixtureJsonEscapesEveryCharacter();
  void deviceSelection();
  void decodersReportTheirName();
};

void CoreWhiteBoxTest::unitSymbolsAndQuantities() {
  struct Case {
    Unit unit;
    const char* symbol;
    Quantity quantity;
  };
  const std::array<Case, 24> cases{{
      {Unit::None, "", Quantity::Unknown},
      {Unit::Volt, "V", Quantity::Voltage},
      {Unit::Millivolt, "mV", Quantity::Voltage},
      {Unit::Microvolt, "µV", Quantity::Voltage},
      {Unit::Kilovolt, "kV", Quantity::Voltage},
      {Unit::Ampere, "A", Quantity::Current},
      {Unit::Milliampere, "mA", Quantity::Current},
      {Unit::Microampere, "µA", Quantity::Current},
      {Unit::Nanoampere, "nA", Quantity::Current},
      {Unit::Ohm, "Ω", Quantity::Resistance},
      {Unit::Kiloohm, "kΩ", Quantity::Resistance},
      {Unit::Megaohm, "MΩ", Quantity::Resistance},
      {Unit::Gigaohm, "GΩ", Quantity::Resistance},
      {Unit::Microohm, "µΩ", Quantity::Resistance},
      {Unit::Farad, "F", Quantity::Capacitance},
      {Unit::Picofarad, "pF", Quantity::Capacitance},
      {Unit::Nanofarad, "nF", Quantity::Capacitance},
      {Unit::Microfarad, "µF", Quantity::Capacitance},
      {Unit::Millifarad, "mF", Quantity::Capacitance},
      {Unit::Hertz, "Hz", Quantity::Frequency},
      {Unit::Kilohertz, "kHz", Quantity::Frequency},
      {Unit::Megahertz, "MHz", Quantity::Frequency},
      {Unit::Gigahertz, "GHz", Quantity::Frequency},
      {Unit::Percent, "%", Quantity::DutyCycle},
  }};

  for (const auto& entry : cases) {
    QCOMPARE(utf8(ampbeam::unitSymbol(entry.unit)), utf8(entry.symbol));
    QCOMPARE(ampbeam::quantityOf(entry.unit), entry.quantity);
  }

  // Invalid enum values are not produced by the decoder, but they must stay
  // printable instead of indexing past a table.
  QCOMPARE(utf8(ampbeam::unitSymbol(static_cast<Unit>(200))), QString());
  QCOMPARE(ampbeam::quantityOf(static_cast<Unit>(200)), Quantity::Unknown);
}

void CoreWhiteBoxTest::quantityNames() {
  struct Case {
    Quantity quantity;
    const char* name;
  };
  const std::array<Case, 9> cases{{
      {Quantity::Unknown, "unknown"},
      {Quantity::Voltage, "voltage"},
      {Quantity::Current, "current"},
      {Quantity::Resistance, "resistance"},
      {Quantity::Capacitance, "capacitance"},
      {Quantity::Frequency, "frequency"},
      {Quantity::DutyCycle, "duty cycle"},
      {Quantity::Continuity, "continuity"},
      {Quantity::Diode, "diode"},
  }};
  for (const auto& entry : cases) {
    QCOMPARE(q(ampbeam::quantityName(entry.quantity)), q(entry.name));
  }
  QCOMPARE(q(ampbeam::quantityName(static_cast<Quantity>(200))), QStringLiteral("unknown"));
}

void CoreWhiteBoxTest::numberFormatting() {
  QCOMPARE(q(ampbeam::formatNumber(std::numeric_limits<double>::quiet_NaN())),
           QStringLiteral("nan"));
  QCOMPARE(q(ampbeam::formatNumber(std::numeric_limits<double>::infinity())),
           QStringLiteral("inf"));
  QCOMPARE(q(ampbeam::formatNumber(-std::numeric_limits<double>::infinity())),
           QStringLiteral("-inf"));
  QCOMPARE(q(ampbeam::formatNumber(0.0)), QStringLiteral("0"));
  QCOMPARE(q(ampbeam::formatNumber(-0.0)), QStringLiteral("0"));
  QCOMPARE(q(ampbeam::formatNumber(4.998)), QStringLiteral("4.998"));
  QCOMPARE(q(ampbeam::formatNumber(1234567.0)), QStringLiteral("1.2346e+06"));
  QCOMPARE(q(ampbeam::formatNumber(1.0, 3)), QStringLiteral("1"));
}

void CoreWhiteBoxTest::measurementFormatting() {
  ampbeam::Measurement measurement;
  measurement.value = 1.5;
  measurement.unit = Unit::Volt;
  measurement.quantity = Quantity::Voltage;
  measurement.currentType = CurrentType::DC;
  measurement.flags.overload = true;
  measurement.flags.hold = true;
  measurement.flags.autoRange = true;
  measurement.flags.relative = true;
  measurement.secondary.present = true;
  measurement.secondary.value = 2.5;
  measurement.secondary.unit = Unit::Hertz;

  QCOMPARE(q(ampbeam::format(measurement)),
           QStringLiteral("1.5 V DC [overload] [hold] [auto] [rel] / 2.5 Hz"));

  measurement.flags = {};
  measurement.currentType = CurrentType::AC;
  measurement.secondary = {};
  QCOMPARE(q(ampbeam::format(measurement)), QStringLiteral("1.5 V AC"));

  measurement.currentType = CurrentType::None;
  measurement.unit = Unit::None;
  QCOMPARE(q(ampbeam::format(measurement)), QStringLiteral("1.5"));

  // A NaN secondary reading has to be printable rather than a raw nan.
  measurement.value = std::numeric_limits<double>::quiet_NaN();
  measurement.secondary.present = true;
  measurement.secondary.value = std::numeric_limits<double>::quiet_NaN();
  measurement.secondary.unit = Unit::None;
  QCOMPARE(q(ampbeam::format(measurement)), QStringLiteral("--- / ---"));
}

void CoreWhiteBoxTest::meterDecodesEveryValueStatus() {
  const ampbeam::MeterDecoder decoder;

  const Bytes normal =
      longFrame(gearWord(2, MeterPrefix::None, static_cast<std::uint8_t>(MeterGear::DcVoltage)),
                valueWord(1234, 0));
  const auto normalResult = decoder.decode(normal);
  QCOMPARE(normalResult.kind, ampbeam::DecodedFrame::Kind::Measurement);
  QVERIFY(nearlyEqual(normalResult.measurement.value, 12.34));
  QVERIFY(!normalResult.measurement.flags.overload);

  const Bytes overload = longFrame(gearWord(2, MeterPrefix::None, 0), valueWord(1234, 1));
  const auto overloadResult = decoder.decode(overload);
  QCOMPARE(overloadResult.kind, ampbeam::DecodedFrame::Kind::Measurement);
  QVERIFY(std::isnan(overloadResult.measurement.value));
  QVERIFY2(overloadResult.detail.find("OL") != std::string::npos, overloadResult.detail.c_str());
  QVERIFY(overloadResult.measurement.flags.overload);

  // Status codes other than normal and overload are not mapped to a reading.
  for (std::uint8_t status = 2; status <= 7; ++status) {
    const Bytes frame = longFrame(gearWord(2, MeterPrefix::None, 0), valueWord(1234, status));
    const auto decoded = decoder.decode(frame);
    QCOMPARE(decoded.kind, ampbeam::DecodedFrame::Kind::Unknown);
  }
}

void CoreWhiteBoxTest::meterDecodesEveryDecimalPoint() {
  const ampbeam::MeterDecoder decoder;
  for (std::uint8_t dp = 0; dp <= 4; ++dp) {
    const Bytes frame = longFrame(gearWord(dp, MeterPrefix::None, 0), valueWord(1234, 0));
    const auto decoded = decoder.decode(frame);
    QCOMPARE(decoded.kind, ampbeam::DecodedFrame::Kind::Measurement);
    const double expected = 1234.0 * std::pow(10.0, -static_cast<double>(dp));
    QVERIFY2(nearlyEqual(decoded.measurement.value, expected), decoded.detail.c_str());
  }

  for (const std::uint8_t dp : {std::uint8_t{5}, std::uint8_t{6}, std::uint8_t{7}}) {
    const Bytes frame = longFrame(gearWord(dp, MeterPrefix::None, 0), valueWord(1234, 0));
    QCOMPARE(decoder.decode(frame).kind, ampbeam::DecodedFrame::Kind::Unknown);
  }
}

void CoreWhiteBoxTest::meterUnitAndPrefixMatrix() {
  struct Case {
    std::uint8_t gear;
    MeterPrefix prefix;
    Unit unit;
    Quantity quantity;
    double factor;
  };
  // `factor` is what the SI prefix contributes when the unit does not already
  // carry it. Entries with a direct prefixed unit use 1.0.
  const std::array<Case, 48> cases{{
      {0, MeterPrefix::None, Unit::Volt, Quantity::Voltage, 1.0},
      {0, MeterPrefix::Micro, Unit::Microvolt, Quantity::Voltage, 1.0},
      {0, MeterPrefix::Milli, Unit::Millivolt, Quantity::Voltage, 1.0},
      {0, MeterPrefix::Kilo, Unit::Kilovolt, Quantity::Voltage, 1.0},
      {0, MeterPrefix::Pico, Unit::Volt, Quantity::Voltage, 1e-12},
      {0, MeterPrefix::Nano, Unit::Volt, Quantity::Voltage, 1e-9},
      {0, MeterPrefix::Mega, Unit::Volt, Quantity::Voltage, 1e6},
      {0, MeterPrefix::Giga, Unit::Volt, Quantity::Voltage, 1e9},
      {1, MeterPrefix::None, Unit::Volt, Quantity::Voltage, 1.0},
      {2, MeterPrefix::None, Unit::Ampere, Quantity::Current, 1.0},
      {2, MeterPrefix::Nano, Unit::Nanoampere, Quantity::Current, 1.0},
      {2, MeterPrefix::Micro, Unit::Microampere, Quantity::Current, 1.0},
      {2, MeterPrefix::Milli, Unit::Milliampere, Quantity::Current, 1.0},
      {2, MeterPrefix::Pico, Unit::Ampere, Quantity::Current, 1e-12},
      {2, MeterPrefix::Kilo, Unit::Ampere, Quantity::Current, 1e3},
      {2, MeterPrefix::Mega, Unit::Ampere, Quantity::Current, 1e6},
      {2, MeterPrefix::Giga, Unit::Ampere, Quantity::Current, 1e9},
      {3, MeterPrefix::None, Unit::Ampere, Quantity::Current, 1.0},
      {4, MeterPrefix::None, Unit::Ohm, Quantity::Resistance, 1.0},
      {4, MeterPrefix::Micro, Unit::Microohm, Quantity::Resistance, 1.0},
      {4, MeterPrefix::Kilo, Unit::Kiloohm, Quantity::Resistance, 1.0},
      {4, MeterPrefix::Mega, Unit::Megaohm, Quantity::Resistance, 1.0},
      {4, MeterPrefix::Giga, Unit::Gigaohm, Quantity::Resistance, 1.0},
      {4, MeterPrefix::Pico, Unit::Ohm, Quantity::Resistance, 1e-12},
      {4, MeterPrefix::Nano, Unit::Ohm, Quantity::Resistance, 1e-9},
      {4, MeterPrefix::Milli, Unit::Ohm, Quantity::Resistance, 1e-3},
      {11, MeterPrefix::None, Unit::Ohm, Quantity::Continuity, 1.0},
      {11, MeterPrefix::Kilo, Unit::Kiloohm, Quantity::Continuity, 1.0},
      {10, MeterPrefix::None, Unit::Volt, Quantity::Diode, 1.0},
      {10, MeterPrefix::Milli, Unit::Millivolt, Quantity::Diode, 1.0},
      {5, MeterPrefix::None, Unit::Farad, Quantity::Capacitance, 1.0},
      {5, MeterPrefix::Pico, Unit::Picofarad, Quantity::Capacitance, 1.0},
      {5, MeterPrefix::Nano, Unit::Nanofarad, Quantity::Capacitance, 1.0},
      {5, MeterPrefix::Micro, Unit::Microfarad, Quantity::Capacitance, 1.0},
      {5, MeterPrefix::Milli, Unit::Millifarad, Quantity::Capacitance, 1.0},
      {5, MeterPrefix::Kilo, Unit::Farad, Quantity::Capacitance, 1e3},
      {5, MeterPrefix::Mega, Unit::Farad, Quantity::Capacitance, 1e6},
      {5, MeterPrefix::Giga, Unit::Farad, Quantity::Capacitance, 1e9},
      {6, MeterPrefix::None, Unit::Hertz, Quantity::Frequency, 1.0},
      {6, MeterPrefix::Kilo, Unit::Kilohertz, Quantity::Frequency, 1.0},
      {6, MeterPrefix::Mega, Unit::Megahertz, Quantity::Frequency, 1.0},
      {6, MeterPrefix::Giga, Unit::Gigahertz, Quantity::Frequency, 1.0},
      {6, MeterPrefix::Pico, Unit::Hertz, Quantity::Frequency, 1e-12},
      {6, MeterPrefix::Nano, Unit::Hertz, Quantity::Frequency, 1e-9},
      {6, MeterPrefix::Micro, Unit::Hertz, Quantity::Frequency, 1e-6},
      {6, MeterPrefix::Milli, Unit::Hertz, Quantity::Frequency, 1e-3},
      {7, MeterPrefix::None, Unit::Percent, Quantity::DutyCycle, 1.0},
      {7, MeterPrefix::Milli, Unit::Percent, Quantity::DutyCycle, 1.0},
  }};

  const ampbeam::MeterDecoder decoder;
  for (const auto& entry : cases) {
    const Bytes frame = longFrame(gearWord(2, entry.prefix, entry.gear), valueWord(1000, 0));
    const auto decoded = decoder.decode(frame);
    QCOMPARE(decoded.kind, ampbeam::DecodedFrame::Kind::Measurement);
    QVERIFY2(decoded.measurement.unit == entry.unit, decoded.detail.c_str());
    QVERIFY2(decoded.measurement.quantity == entry.quantity, decoded.detail.c_str());
    QVERIFY2(nearlyEqual(decoded.measurement.value, 10.0 * entry.factor), decoded.detail.c_str());
  }

  // NCV reports its raw field level and ignores the prefix field.
  const Bytes ncv = longFrame(gearWord(2, MeterPrefix::Nano, 13), valueWord(1000, 0));
  const auto ncvDecoded = decoder.decode(ncv);
  QCOMPARE(ncvDecoded.kind, ampbeam::DecodedFrame::Kind::Measurement);
  QCOMPARE(ncvDecoded.measurement.unit, Unit::None);
  QCOMPARE(ncvDecoded.measurement.quantity, Quantity::Unknown);
  QVERIFY(nearlyEqual(ncvDecoded.measurement.value, 1000.0));

  // Gears that no capture proves must not be mapped to a reading.
  for (const std::uint8_t gear : {std::uint8_t{8}, std::uint8_t{14}, std::uint8_t{25}}) {
    const Bytes frame = longFrame(gearWord(2, MeterPrefix::None, gear), valueWord(1000, 0));
    QCOMPARE(decoder.decode(frame).kind, ampbeam::DecodedFrame::Kind::Unknown);
  }
}

void CoreWhiteBoxTest::meterCurrentTypeMatrix() {
  struct Case {
    std::uint8_t gear;
    CurrentType expected;
  };
  const std::array<Case, 6> cases{{
      {0, CurrentType::DC},
      {2, CurrentType::DC},
      {1, CurrentType::AC},
      {3, CurrentType::AC},
      {4, CurrentType::None},
      {10, CurrentType::None},
  }};

  const ampbeam::MeterDecoder decoder;
  for (const auto& entry : cases) {
    const Bytes frame = longFrame(gearWord(2, MeterPrefix::None, entry.gear), valueWord(1000, 0));
    const auto decoded = decoder.decode(frame);
    QCOMPARE(decoded.kind, ampbeam::DecodedFrame::Kind::Measurement);
    QVERIFY2(decoded.measurement.currentType == entry.expected, decoded.detail.c_str());
  }
}

void CoreWhiteBoxTest::secondaryPairValidation() {
  const ampbeam::MeterDecoder decoder;
  const std::uint32_t primaryGear = gearWord(2, MeterPrefix::None, 0, true);

  // An invalid decimal point in the second pair rejects the whole frame.
  const Bytes invalidDp = longFrame(primaryGear, valueWord(1000, 0),
                                    gearWord(5, MeterPrefix::None, 0), valueWord(1000, 0));
  QCOMPARE(decoder.decode(invalidDp).kind, ampbeam::DecodedFrame::Kind::Unknown);

  // An unknown status in the second value word rejects the whole frame.
  const Bytes badStatus = longFrame(primaryGear, valueWord(1000, 0),
                                    gearWord(2, MeterPrefix::None, 0), valueWord(1000, 2));
  QCOMPARE(decoder.decode(badStatus).kind, ampbeam::DecodedFrame::Kind::Unknown);

  // An unsupported gear in the second pair rejects the whole frame.
  const Bytes badGear = longFrame(primaryGear, valueWord(1000, 0),
                                  gearWord(2, MeterPrefix::None, 8), valueWord(1000, 0));
  QCOMPARE(decoder.decode(badGear).kind, ampbeam::DecodedFrame::Kind::Unknown);
}

void CoreWhiteBoxTest::minMaxStateExposesSecondReading() {
  // Verified on hardware: in MinMax view the frame carries state bit 5 and a
  // second measurement. The display showed 0.107 mV live and 9.371 mV as MAX.
  const ampbeam::MeterDecoder decoder;
  const Bytes frame = longFrame(gearWord(3, MeterPrefix::Milli, 0, true), valueWord(107),
                                gearWord(3, MeterPrefix::Milli, 0, true), valueWord(9371), 0x20U);
  const auto decoded = decoder.decode(frame);
  QCOMPARE(decoded.kind, ampbeam::DecodedFrame::Kind::Measurement);
  QVERIFY(qFuzzyCompare(decoded.measurement.value, 0.107));
  QCOMPARE(decoded.measurement.unit, Unit::Millivolt);
  QVERIFY(decoded.measurement.secondary.present);
  QVERIFY(qFuzzyCompare(decoded.measurement.secondary.value, 9.371));
  QCOMPARE(decoded.measurement.secondary.unit, Unit::Millivolt);
  QVERIFY(decoded.detail.find("states=MINMAX") != std::string::npos);
}

void CoreWhiteBoxTest::frequencyWithVoltageSecondary() {
  // Real capture: the Hz/V view sends frequency as the primary value and the
  // AC voltage (millivolt range) in the second pair. State 0x04 is AUTO.
  const ampbeam::MeterDecoder decoder;
  const Bytes frame = longFrame(gearWord(2, MeterPrefix::None, 6, true), valueWord(4998),
                                gearWord(2, MeterPrefix::Milli, 1, true), valueWord(3280), 0x04U);
  const auto decoded = decoder.decode(frame);
  QCOMPARE(decoded.kind, ampbeam::DecodedFrame::Kind::Measurement);
  QCOMPARE(decoded.measurement.quantity, Quantity::Frequency);
  QCOMPARE(decoded.measurement.unit, Unit::Hertz);
  QVERIFY(nearlyEqual(decoded.measurement.value, 49.98));
  QVERIFY(decoded.measurement.flags.autoRange);
  QVERIFY(decoded.measurement.secondary.present);
  QCOMPARE(decoded.measurement.secondary.quantity, Quantity::Voltage);
  QCOMPARE(decoded.measurement.secondary.unit, Unit::Millivolt);
  QVERIFY(nearlyEqual(decoded.measurement.secondary.value, 32.8));
  QVERIFY(decoded.detail.find("FREQ") != std::string::npos);
  QVERIFY(decoded.detail.find("ACV") != std::string::npos);
}

void CoreWhiteBoxTest::relativeStateSetsFlag() {
  // Real capture: REL mode in the DC mV range. The primary value is the
  // displayed relative reading; the second pair is the stored reference and
  // carries the negative sign bit on the wire.
  const ampbeam::MeterDecoder decoder;
  const Bytes frame =
      longFrame(gearWord(3, MeterPrefix::Milli, 0, true), valueWord(2584),
                gearWord(3, MeterPrefix::Milli, 0, true), valueWord(2432, 0, true), 0x02U);
  const auto decoded = decoder.decode(frame);
  QCOMPARE(decoded.kind, ampbeam::DecodedFrame::Kind::Measurement);
  QVERIFY(decoded.measurement.flags.relative);
  QVERIFY(!decoded.measurement.flags.autoRange);
  QVERIFY(nearlyEqual(decoded.measurement.value, 2.584));
  QVERIFY(decoded.measurement.secondary.present);
  QVERIFY(nearlyEqual(decoded.measurement.secondary.value, -2.432));
  QVERIFY(decoded.detail.find("states=REL") != std::string::npos);
  QVERIFY(q(ampbeam::format(decoded.measurement)).contains(QStringLiteral("[rel]")));
}

void CoreWhiteBoxTest::meterStateAndNameHelpers() {
  QVERIFY(!ampbeam::meterHasState(0, ampbeam::MeterState::Hold));
  QVERIFY(ampbeam::meterHasState(1U, ampbeam::MeterState::Hold));
  QVERIFY(!ampbeam::meterHasState(0xFFFFFFFFU, static_cast<ampbeam::MeterState>(32)));
  QVERIFY(!ampbeam::meterHasState(0xFFFFFFFFU, static_cast<ampbeam::MeterState>(40)));

  struct StateCase {
    ampbeam::MeterState state;
    const char* name;
  };
  const std::array<StateCase, 5> states{{
      {ampbeam::MeterState::Hold, "HOLD"},
      {ampbeam::MeterState::Relative, "REL"},
      {ampbeam::MeterState::AutoRange, "AUTO"},
      {ampbeam::MeterState::Oscilloscope, "OSC"},
      {ampbeam::MeterState::MinMax, "MINMAX"},
  }};
  for (const auto& entry : states) {
    QCOMPARE(q(ampbeam::meterStateName(entry.state)), q(entry.name));
  }
  QCOMPARE(ampbeam::meterStateName(static_cast<ampbeam::MeterState>(40)), nullptr);

  struct GearCase {
    MeterGear gear;
    const char* name;
  };
  const std::array<GearCase, 13> gears{{
      {MeterGear::DcVoltage, "DCV"},
      {MeterGear::AcVoltage, "ACV"},
      {MeterGear::DcCurrent, "DCA"},
      {MeterGear::AcCurrent, "ACA"},
      {MeterGear::Resistance, "RES"},
      {MeterGear::Capacitance, "CAP"},
      {MeterGear::Frequency, "FREQ"},
      {MeterGear::DutyCycle, "DUTY"},
      {MeterGear::Diode, "DIODE"},
      {MeterGear::Continuity, "CONT"},
      {MeterGear::Ncv, "NCV"},
      {MeterGear::Unknown, "?"},
      {static_cast<MeterGear>(99), "?"},
  }};
  for (const auto& entry : gears) {
    QCOMPARE(q(ampbeam::meterGearName(entry.gear)), q(entry.name));
  }

  struct PrefixCase {
    MeterPrefix prefix;
    const char* symbol;
  };
  const std::array<PrefixCase, 8> prefixes{{
      {MeterPrefix::Pico, "p"},
      {MeterPrefix::Nano, "n"},
      {MeterPrefix::Micro, "µ"},
      {MeterPrefix::Milli, "m"},
      {MeterPrefix::None, ""},
      {MeterPrefix::Kilo, "k"},
      {MeterPrefix::Mega, "M"},
      {MeterPrefix::Giga, "G"},
  }};
  for (const auto& entry : prefixes) {
    QCOMPARE(utf8(ampbeam::meterPrefixSymbol(entry.prefix)), utf8(entry.symbol));
  }
  QCOMPARE(utf8(ampbeam::meterPrefixSymbol(static_cast<MeterPrefix>(99))), QStringLiteral("?"));
}

void CoreWhiteBoxTest::framerStateMachine() {
  const Bytes frame = longFrame(gearWord(2, MeterPrefix::None, 0), valueWord(1234, 0));

  // A partial frame is buffered until the missing bytes arrive.
  ampbeam::MeterFramer framer;
  QVERIFY(framer.push(std::span<const std::uint8_t>(frame).first(3)).empty());
  const auto completed = framer.push(std::span<const std::uint8_t>(frame).subspan(3));
  QCOMPARE(completed.size(), static_cast<std::size_t>(1));
  QVERIFY(completed[0] == frame);

  // Concatenated frames are split in one push.
  Bytes twoFrames = frame;
  twoFrames.insert(twoFrames.end(), frame.begin(), frame.end());
  const auto split = framer.push(twoFrames);
  QCOMPARE(split.size(), static_cast<std::size_t>(2));
  QVERIFY(split[0] == frame);
  QVERIFY(split[1] == frame);

  // reset() drops buffered bytes.
  framer.reset();
  QVERIFY(framer.push(std::span<const std::uint8_t>(frame).first(14)).empty());
  const auto last = framer.push(std::span<const std::uint8_t>(frame).last(1));
  QCOMPARE(last.size(), static_cast<std::size_t>(1));

  QVERIFY(ampbeam::meterIsFrameSize(15));
  QVERIFY(!ampbeam::meterIsFrameSize(6));
  QVERIFY(!ampbeam::meterIsFrameSize(0));
}

void CoreWhiteBoxTest::deviceInfoAndCommands() {
  QVERIFY(!ampbeam::meterParseDeviceInfo(bytes({})).has_value());

  const auto shortInfo = ampbeam::meterParseDeviceInfo(bytes({0x3D, 0x00, 0x01, 0x04}));
  QVERIFY(shortInfo.has_value());
  QCOMPARE(static_cast<unsigned>(shortInfo->seriesId), 61U);
  QCOMPARE(q(shortInfo->version), QString());
  QCOMPARE(shortInfo->raw.size(), static_cast<std::size_t>(4));

  const auto fullInfo = ampbeam::meterParseDeviceInfo(bytes({0x3D, 0x00, 0x01, 0x04, 0x02}));
  QVERIFY(fullInfo.has_value());
  QCOMPARE(q(fullInfo->version), QStringLiteral("1.4.2"));

  QVERIFY(!ampbeam::meterRenameCommand("").has_value());
  QVERIFY(!ampbeam::meterRenameCommand("0123456789ABCDEF").has_value());
  QVERIFY(ampbeam::meterRenameCommand("0123456789ABCDE").has_value());
  QVERIFY(!ampbeam::meterRenameCommand(std::string_view("\x01"
                                                        "ABC",
                                                        4))
               .has_value());
  QVERIFY(!ampbeam::meterRenameCommand(std::string_view("\x7F", 1)).has_value());

  const auto keyDown = ampbeam::meterKeyCommand(7, true);
  QCOMPARE(keyDown[0], static_cast<std::uint8_t>(7));
  QCOMPARE(keyDown[1], static_cast<std::uint8_t>(1));
  const auto keyUp = ampbeam::meterKeyCommand(7, false);
  QCOMPARE(keyUp[1], static_cast<std::uint8_t>(0));
}

void CoreWhiteBoxTest::captureEdgeCases() {
  // Leading and trailing whitespace is trimmed on both ends.
  const auto padded = ampbeam::parseCaptureLog("   00 11 22   \n");
  QVERIFY(padded.warnings.empty());
  QCOMPARE(padded.frames.size(), static_cast<std::size_t>(1));
  QVERIFY(padded.frames[0].data == bytes({0x00, 0x11, 0x22}));

  // A timestamp field longer than 20 digits is refused without losing the frame.
  const auto tooLong = ampbeam::parseCaptureLog("999999999999999999999\t3\t00 11 22\n");
  QCOMPARE(tooLong.frames.size(), static_cast<std::size_t>(1));
  QVERIFY(!tooLong.frames[0].receivedMs.has_value());

  // A 20 digit value that overflows uint64 is refused as well.
  const auto overflow = ampbeam::parseCaptureLog("99999999999999999999\t3\t00 11 22\n");
  QCOMPARE(overflow.frames.size(), static_cast<std::size_t>(1));
  QVERIFY(!overflow.frames[0].receivedMs.has_value());

  // Canonical fields are used when they parse, and junk lines warn.
  const auto mixed = ampbeam::parseCaptureLog("42\t2\t01 02\nnot hex at all\n");
  QCOMPARE(mixed.frames.size(), static_cast<std::size_t>(1));
  QCOMPARE(mixed.warnings.size(), static_cast<std::size_t>(1));
  QVERIFY(mixed.warnings[0].find("line 2") != std::string::npos);

  // A plain hex line with a trailing comment keeps the frame.
  const auto comment = ampbeam::parseCaptureLog("01 02 03 # measured while idle\n");
  QCOMPARE(comment.frames.size(), static_cast<std::size_t>(1));
  QVERIFY(comment.frames[0].data == bytes({0x01, 0x02, 0x03}));

  // No trailing newline still yields the last line.
  const auto noNewline = ampbeam::parseCaptureLog("01 02");
  QCOMPARE(noNewline.frames.size(), static_cast<std::size_t>(1));

  // An empty log has neither frames nor warnings.
  const auto empty = ampbeam::parseCaptureLog("");
  QVERIFY(empty.frames.empty());
  QVERIFY(empty.warnings.empty());
}

void CoreWhiteBoxTest::fixtureJsonEscapesEveryCharacter() {
  ampbeam::CaptureParseResult capture;
  capture.frames.push_back(ampbeam::CaptureFrame{std::nullopt, bytes({0xAB})});

  ampbeam::FixtureMetadata metadata;
  metadata.name = "quote \" backslash \\ newline \n return \r tab \t control \x01";
  metadata.createdAt = "2026-11-21T00:00:00.000Z";
  metadata.source = "";

  const std::string json = ampbeam::toFixtureJson(capture, metadata);
  QVERIFY(json.find("quote \\\" backslash \\\\ newline \\n return \\r tab \\t control \\u0001") !=
          std::string::npos);
  QVERIFY(json.find("\"source\"") == std::string::npos);
  QVERIFY(json.find("\"hex\": \"AB\"") != std::string::npos);

  // An empty capture still produces a valid fixture document.
  ampbeam::CaptureParseResult empty;
  const std::string emptyJson = ampbeam::toFixtureJson(empty, metadata);
  QVERIFY(emptyJson.find("\"frames\": [\n  ]\n}") != std::string::npos);
}

void CoreWhiteBoxTest::deviceSelection() {
  using ampbeam::DeviceCandidate;
  using ampbeam::DeviceSelectionStatus;
  using namespace std::string_view_literals;

  constexpr std::string_view kService = "0000fff0-0000-1000-8000-00805f9b34fb";

  const std::array<DeviceCandidate, 0> none{};
  const auto empty = ampbeam::selectMeterDevice(none, kService);
  QCOMPARE(empty.status, DeviceSelectionStatus::None);
  QCOMPARE(empty.candidateCount, static_cast<std::size_t>(0));
  QCOMPARE(empty.discoveredCount, static_cast<std::size_t>(0));
  QVERIFY(!empty.matchedService);
  QVERIFY(empty.address.empty());

  const std::array<DeviceCandidate, 4> candidates{{
      {"AA:BB:CC:DD:EE:01", "no service", "0000180f-0000-1000-8000-00805f9b34fb"},
      {"AA:BB:CC:DD:EE:02", "meter",
       "0000180f-0000-1000-8000-00805f9b34fb, "
       "0000FFF0-0000-1000-8000-00805F9B34FB   "},
      {"AA:BB:CC:DD:EE:03", "empty", ""},
      {"AA:BB:CC:DD:EE:04", "short", "0000fff0"},
  }};
  const auto selected = ampbeam::selectMeterDevice(candidates, kService);
  QCOMPARE(selected.status, DeviceSelectionStatus::Selected);
  QCOMPARE(selected.candidateCount, static_cast<std::size_t>(1));
  QCOMPARE(selected.discoveredCount, static_cast<std::size_t>(4));
  QVERIFY(selected.matchedService);
  QCOMPARE(q(selected.address), QStringLiteral("AA:BB:CC:DD:EE:02"));
  QCOMPARE(q(selected.name), QStringLiteral("meter"));

  const std::array<DeviceCandidate, 2> twoMeters{{
      {"AA:BB:CC:DD:EE:05", "one", kService},
      {"AA:BB:CC:DD:EE:06", "two", kService},
  }};
  const auto multiple = ampbeam::selectMeterDevice(twoMeters, kService);
  QCOMPARE(multiple.status, DeviceSelectionStatus::Multiple);
  QCOMPARE(multiple.candidateCount, static_cast<std::size_t>(2));
  QCOMPARE(multiple.discoveredCount, static_cast<std::size_t>(2));
  QVERIFY(multiple.matchedService);
  QVERIFY(multiple.address.empty());
  QVERIFY(multiple.name.empty());

  // BlueZ may omit service UUIDs at scan time. With exactly one discovered
  // device the fallback selects it; with several it refuses to guess.
  const std::array<DeviceCandidate, 1> singleNoService{{
      {"AA:BB:CC:DD:EE:07", "solo", "0000180f-0000-1000-8000-00805f9b34fb"},
  }};
  const auto fallback = ampbeam::selectMeterDevice(singleNoService, kService);
  QCOMPARE(fallback.status, DeviceSelectionStatus::Selected);
  QCOMPARE(fallback.candidateCount, static_cast<std::size_t>(0));
  QCOMPARE(fallback.discoveredCount, static_cast<std::size_t>(1));
  QVERIFY(!fallback.matchedService);
  QCOMPARE(q(fallback.address), QStringLiteral("AA:BB:CC:DD:EE:07"));
  QCOMPARE(q(fallback.name), QStringLiteral("solo"));

  const std::array<DeviceCandidate, 2> twoNoService{{
      {"AA:BB:CC:DD:EE:08", "one", ""},
      {"AA:BB:CC:DD:EE:09", "two", ""},
  }};
  const auto ambiguous = ampbeam::selectMeterDevice(twoNoService, kService);
  QCOMPARE(ambiguous.status, DeviceSelectionStatus::Multiple);
  QCOMPARE(ambiguous.candidateCount, static_cast<std::size_t>(0));
  QCOMPARE(ambiguous.discoveredCount, static_cast<std::size_t>(2));
  QVERIFY(!ambiguous.matchedService);
  QVERIFY(ambiguous.address.empty());

  // A name filter disambiguates without relying on advertised services.
  const auto byName = ampbeam::selectMeterDevice(candidates, kService, "  METER ");
  QCOMPARE(byName.status, DeviceSelectionStatus::Selected);
  QCOMPARE(byName.candidateCount, static_cast<std::size_t>(1));
  QVERIFY(byName.matchedName);
  QVERIFY(!byName.matchedService);
  QCOMPARE(q(byName.address), QStringLiteral("AA:BB:CC:DD:EE:02"));

  const auto noName = ampbeam::selectMeterDevice(candidates, kService, "missing");
  QCOMPARE(noName.status, DeviceSelectionStatus::None);
  QCOMPARE(noName.candidateCount, static_cast<std::size_t>(0));
  QVERIFY(!noName.matchedName);

  const std::array<DeviceCandidate, 2> duplicateNames{{
      {"AA:BB:CC:DD:EE:10", "meter", ""},
      {"AA:BB:CC:DD:EE:11", "meter", ""},
  }};
  const auto duplicate = ampbeam::selectMeterDevice(duplicateNames, kService, "meter");
  QCOMPARE(duplicate.status, DeviceSelectionStatus::Multiple);
  QCOMPARE(duplicate.candidateCount, static_cast<std::size_t>(2));
  QVERIFY(duplicate.matchedName);
  QVERIFY(duplicate.address.empty());
}

void CoreWhiteBoxTest::decodersReportTheirName() {
  const ampbeam::MeterDecoder meter;
  QCOMPARE(q(meter.name()), QStringLiteral("meter"));
  const ampbeam::RawDecoder raw;
  QCOMPARE(q(raw.name()), QStringLiteral("raw"));
}

QTEST_GUILESS_MAIN(CoreWhiteBoxTest)

#include "tst_core_whitebox.moc"
