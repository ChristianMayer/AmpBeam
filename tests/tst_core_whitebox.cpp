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
#include "ampbeam/core/csv.hpp"
#include "ampbeam/core/decoder.hpp"
#include "ampbeam/core/hex.hpp"
#include "ampbeam/core/measurement.hpp"
#include "ampbeam/core/meter.hpp"
#include "ampbeam/core/monitor.hpp"
#include "ampbeam/core/selection.hpp"
#include "ampbeam/core/trace.hpp"

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

/// Realistic receive time for trace tests; epoch milliseconds are far larger
/// than every timebase, so the newest sample sits on the right edge.
constexpr std::uint64_t kTraceBaseMs = 1700000000000ULL;

} // namespace

class CoreWhiteBoxTest final : public QObject {
  Q_OBJECT

private slots:
  void unitSymbolsAndQuantities();
  void quantityNames();
  void numberFormatting();
  void measurementFormatting();
  void monitorModeNames();
  void monitorLineFormatting();
  void monitorDisplayParts();
  void traceHistorySamplesAreScaled();
  void traceHistoryFlatAndSingleSeries();
  void traceHistoryStartsANewSeries();
  void traceHistoryMarksGaps();
  void traceHistoryClampsCapacity();
  void traceHistoryTimeZoom();
  void traceHistoryValueZoom();
  void traceHistoryPlotsSecondaryReadings();
  void traceHistoryDropsSamplesOutsideTheTimebase();
  void csvFormatting();
  void reconnectPolicyBackoff();
  void decoderCarriesGearAndMinMax();
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
  void captureLineFormatting();
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

  // The LCD keeps its decimal places; the mirror reproduces them.
  QCOMPARE(q(ampbeam::formatFixedNumber(6.0, 3)), QStringLiteral("6.000"));
  QCOMPARE(q(ampbeam::formatFixedNumber(0.0, 4)), QStringLiteral("0.0000"));
  QCOMPARE(q(ampbeam::formatFixedNumber(-0.0, 2)), QStringLiteral("0.00"));
  QCOMPARE(q(ampbeam::formatFixedNumber(-2.5, 3)), QStringLiteral("-2.500"));
  QCOMPARE(q(ampbeam::formatFixedNumber(4.998, 3)), QStringLiteral("4.998"));
  QCOMPARE(q(ampbeam::formatFixedNumber(7.0, -1)), QStringLiteral("7"));
  QCOMPARE(q(ampbeam::formatFixedNumber(std::numeric_limits<double>::quiet_NaN(), 2)),
           QStringLiteral("nan"));
  QCOMPARE(q(ampbeam::formatFixedNumber(std::numeric_limits<double>::infinity(), 2)),
           QStringLiteral("inf"));
  QCOMPARE(q(ampbeam::formatFixedNumber(-std::numeric_limits<double>::infinity(), 2)),
           QStringLiteral("-inf"));
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
  measurement.flags.minMax = true;
  measurement.secondary.present = true;
  measurement.secondary.value = 2.5;
  measurement.secondary.unit = Unit::Hertz;

  QCOMPARE(q(ampbeam::format(measurement)),
           QStringLiteral("1.5 V DC [overload] [hold] [auto] [rel] [minmax] / 2.5 Hz"));

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

void CoreWhiteBoxTest::monitorModeNames() {
  struct Case {
    MeterGear gear;
    const char* mode;
  };
  const std::array<Case, 12> cases{{
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
  }};
  for (const auto& entry : cases) {
    ampbeam::Measurement measurement;
    measurement.gear = entry.gear;
    QCOMPARE(q(ampbeam::monitorModeName(measurement)), q(entry.mode));
  }
}

void CoreWhiteBoxTest::monitorLineFormatting() {
  ampbeam::Measurement measurement;
  measurement.gear = MeterGear::DcVoltage;
  measurement.value = 4.998;
  measurement.unit = Unit::Volt;
  measurement.quantity = Quantity::Voltage;
  measurement.currentType = CurrentType::DC;
  measurement.displayDecimals = 3;
  measurement.flags.autoRange = true;
  QCOMPARE(q(ampbeam::monitorLine(measurement)), QStringLiteral("DCV  4.998 V  [auto]"));

  // The LCD resolution is preserved: 6 V on a 3-decimal range is 6.000 V.
  measurement.value = 6.0;
  QCOMPARE(q(ampbeam::monitorLine(measurement)), QStringLiteral("DCV  6.000 V  [auto]"));

  // AUTO clear on a rangeable function is the verified manual-range state.
  measurement.flags = {};
  measurement.value = 4.769;
  measurement.unit = Unit::Millivolt;
  measurement.flags.hold = true;
  QCOMPARE(q(ampbeam::monitorLine(measurement)), QStringLiteral("DCV  4.769 mV  [hold]  [manual]"));

  // Continuity has no ranges, so an absent AUTO bit is not "manual".
  measurement = {};
  measurement.gear = MeterGear::Continuity;
  measurement.value = 0.0;
  measurement.unit = Unit::Ohm;
  measurement.displayDecimals = 1;
  QCOMPARE(QString::fromStdString(ampbeam::monitorLine(measurement)),
           QStringLiteral("CONT  0.0 Ω"));

  // MinMax view: the second reading is the confirmed MAX value.
  measurement = {};
  measurement.gear = MeterGear::DcVoltage;
  measurement.value = 0.107;
  measurement.unit = Unit::Millivolt;
  measurement.displayDecimals = 3;
  measurement.flags.minMax = true;
  measurement.secondary.present = true;
  measurement.secondary.value = 9.371;
  measurement.secondary.unit = Unit::Millivolt;
  measurement.secondary.displayDecimals = 3;
  QCOMPARE(q(ampbeam::monitorLine(measurement)),
           QStringLiteral("DCV  0.107 mV  [manual]  [minmax]  |  MAX 9.371 mV"));

  // REL mode: the second reading is the stored reference.
  measurement.flags = {};
  measurement.flags.relative = true;
  measurement.value = 2.584;
  measurement.secondary.value = -2.432;
  QCOMPARE(q(ampbeam::monitorLine(measurement)),
           QStringLiteral("DCV  2.584 mV  [manual]  [rel]  |  REF -2.432 mV"));

  // A secondary reading without a REL/MinMax meaning is shown unlabelled.
  measurement.flags = {};
  measurement.value = 49.98;
  measurement.unit = Unit::Hertz;
  measurement.gear = MeterGear::Frequency;
  measurement.displayDecimals = 2;
  measurement.secondary.value = 32.8;
  measurement.secondary.displayDecimals = 2;
  QCOMPARE(q(ampbeam::monitorLine(measurement)),
           QStringLiteral("FREQ  49.98 Hz  [manual]  |  32.80 mV"));

  // Overload / blank display and a NaN secondary carry no number.
  measurement = {};
  measurement.gear = MeterGear::DcVoltage;
  measurement.value = std::numeric_limits<double>::quiet_NaN();
  measurement.unit = Unit::Volt;
  measurement.flags.overload = true;
  QCOMPARE(q(ampbeam::monitorLine(measurement)),
           QStringLiteral("DCV  --- V  [overload]  [manual]"));

  // NCV is a field level, not a decimal reading: no fixed decimals, no range.
  measurement = {};
  measurement.gear = MeterGear::Ncv;
  measurement.value = 1.0;
  measurement.secondary.present = true;
  measurement.secondary.value = std::numeric_limits<double>::quiet_NaN();
  QCOMPARE(q(ampbeam::monitorLine(measurement)), QStringLiteral("NCV  1  |  ---"));

  measurement = {};
  measurement.gear = MeterGear::DutyCycle;
  measurement.value = 50.0;
  measurement.unit = Unit::Percent;
  measurement.flags.hold = true;
  measurement.flags.autoRange = true;
  QCOMPARE(q(ampbeam::monitorLine(measurement)), QStringLiteral("DUTY  50 %  [hold]  [auto]"));

  // Duty has no verified selectable range, so the absent AUTO bit stays silent.
  measurement.flags = {};
  QCOMPARE(q(ampbeam::monitorLine(measurement)), QStringLiteral("DUTY  50 %"));

  // An unknown gear value carries no range knowledge either.
  measurement = {};
  measurement.gear = static_cast<MeterGear>(200);
  measurement.value = 1.0;
  QCOMPARE(q(ampbeam::monitorLine(measurement)), QStringLiteral("?  1"));

  QVERIFY(ampbeam::monitorGearHasRanges(MeterGear::DcVoltage));
  QVERIFY(ampbeam::monitorGearHasRanges(MeterGear::Frequency));
  QVERIFY(!ampbeam::monitorGearHasRanges(MeterGear::DutyCycle));
  QVERIFY(!ampbeam::monitorGearHasRanges(MeterGear::Diode));
  QVERIFY(!ampbeam::monitorGearHasRanges(MeterGear::Continuity));
  QVERIFY(!ampbeam::monitorGearHasRanges(MeterGear::Ncv));
  QVERIFY(!ampbeam::monitorGearHasRanges(MeterGear::Unknown));
  QVERIFY(!ampbeam::monitorGearHasRanges(static_cast<MeterGear>(200)));
}

void CoreWhiteBoxTest::monitorDisplayParts() {
  // The GUI shows mode, number and unit separately; the pieces must match the
  // single-line mirror exactly.
  ampbeam::Measurement measurement;
  measurement.gear = MeterGear::DcVoltage;
  measurement.value = 4.998;
  measurement.unit = Unit::Volt;
  measurement.displayDecimals = 3;
  QCOMPARE(q(ampbeam::monitorValueText(measurement)), QStringLiteral("4.998"));
  QCOMPARE(q(ampbeam::monitorUnitText(measurement)), QStringLiteral("V"));
  QVERIFY(ampbeam::monitorSecondaryText(measurement).empty());

  // A blank or overloaded display has no number.
  measurement.value = std::numeric_limits<double>::quiet_NaN();
  QCOMPARE(q(ampbeam::monitorValueText(measurement)), QStringLiteral("---"));

  // Without LCD decimals (NCV field levels, synthetic values) the number is
  // formatted without trailing zeros.
  measurement.value = 2.0;
  measurement.displayDecimals = -1;
  measurement.unit = Unit::None;
  QCOMPARE(q(ampbeam::monitorValueText(measurement)), QStringLiteral("2"));
  QVERIFY(ampbeam::monitorUnitText(measurement).empty());

  // The secondary reading keeps the confirmed REL/MinMax label.
  measurement = {};
  measurement.secondary.present = true;
  measurement.secondary.value = 9.371;
  measurement.secondary.unit = Unit::Millivolt;
  measurement.secondary.displayDecimals = 3;
  QCOMPARE(q(ampbeam::monitorSecondaryText(measurement)), QStringLiteral("9.371 mV"));

  measurement.flags.minMax = true;
  QCOMPARE(q(ampbeam::monitorSecondaryText(measurement)), QStringLiteral("MAX 9.371 mV"));

  measurement.flags = {};
  measurement.flags.relative = true;
  QCOMPARE(q(ampbeam::monitorSecondaryText(measurement)), QStringLiteral("REF 9.371 mV"));

  // A NaN secondary is still printable.
  measurement.flags = {};
  measurement.secondary.value = std::numeric_limits<double>::quiet_NaN();
  measurement.secondary.unit = Unit::None;
  QCOMPARE(q(ampbeam::monitorSecondaryText(measurement)), QStringLiteral("---"));
}

void CoreWhiteBoxTest::csvFormatting() {
  QCOMPARE(q(ampbeam::csvHeader()),
           QStringLiteral("received_ms,time,mode,value,unit,current,decimals,overload,hold,"
                          "relative,auto,manual,minmax,secondary_role,secondary_value,"
                          "secondary_unit,secondary_decimals"));

  ampbeam::Measurement measurement;
  measurement.receivedAtMs = 1732180000123;
  measurement.gear = MeterGear::DcVoltage;
  measurement.value = 4.998;
  measurement.unit = Unit::Volt;
  measurement.displayDecimals = 3;
  measurement.currentType = CurrentType::DC;
  measurement.flags.autoRange = true;
  QCOMPARE(q(ampbeam::csvRow(measurement, "2024-11-21T07:06:40.123Z")),
           QStringLiteral("1732180000123,2024-11-21T07:06:40.123Z,DCV,4.998,V,DC,3,0,0,0,1,0,0,"
                          ",,,"));

  // Manual MinMax with a MAX secondary; an overload has no numeric value.
  measurement.flags = {};
  measurement.flags.minMax = true;
  measurement.value = std::numeric_limits<double>::quiet_NaN();
  measurement.secondary.present = true;
  measurement.secondary.value = 9.371;
  measurement.secondary.unit = Unit::Millivolt;
  measurement.secondary.displayDecimals = 3;
  QCOMPARE(q(ampbeam::csvRow(measurement, "t")),
           QStringLiteral("1732180000123,t,DCV,,V,DC,3,0,0,0,0,1,1,MAX,9.371,mV,3"));

  // REL labels the secondary as REF.
  measurement.flags = {};
  measurement.flags.relative = true;
  measurement.value = 2.584;
  measurement.secondary.value = -2.432;
  QCOMPARE(q(ampbeam::csvRow(measurement, "t")),
           QStringLiteral("1732180000123,t,DCV,2.584,V,DC,3,0,0,1,0,1,0,REF,-2.432,mV,3"));

  // NCV has no decimal places, and its unit is empty.
  measurement = {};
  measurement.gear = MeterGear::Ncv;
  measurement.value = 2.0;
  measurement.flags.autoRange = true;
  QCOMPARE(q(ampbeam::csvRow(measurement, "t")), QStringLiteral("0,t,NCV,2,,,,0,0,0,1,0,0,,,,"));

  // AC current and a quoted timestamp exercise the remaining fields.
  measurement = {};
  measurement.gear = MeterGear::AcCurrent;
  measurement.value = 0.123;
  measurement.unit = Unit::Milliampere;
  measurement.displayDecimals = 3;
  measurement.currentType = CurrentType::AC;
  measurement.flags.autoRange = true;
  QCOMPARE(q(ampbeam::csvRow(measurement, "a,\"b\"")),
           QStringLiteral("0,\"a,\"\"b\"\"\",ACA,0.123,mA,AC,3,0,0,0,1,0,0,,,,"));
}

void CoreWhiteBoxTest::reconnectPolicyBackoff() {
  ampbeam::ReconnectPolicy policy;
  QCOMPARE(policy.attempts(), 0U);
  QCOMPARE(policy.nextDelayMs(), 1000);
  QCOMPARE(policy.nextDelayMs(), 2000);
  QCOMPARE(policy.nextDelayMs(), 4000);
  QCOMPARE(policy.nextDelayMs(), 8000);
  QCOMPARE(policy.nextDelayMs(), 16000);
  QCOMPARE(policy.nextDelayMs(), 30000);
  QCOMPARE(policy.nextDelayMs(), 30000);
  QCOMPARE(policy.attempts(), 7U);

  policy.reset();
  QCOMPARE(policy.attempts(), 0U);
  QCOMPARE(policy.nextDelayMs(), 1000);
}

void CoreWhiteBoxTest::decoderCarriesGearAndMinMax() {
  const ampbeam::MeterDecoder decoder;

  // NCV has no decimal quantity, so the gear is what names the mode.
  const Bytes ncv = longFrame(gearWord(2, MeterPrefix::Nano, 13), valueWord(2));
  const auto ncvDecoded = decoder.decode(ncv);
  QCOMPARE(ncvDecoded.kind, ampbeam::DecodedFrame::Kind::Measurement);
  QCOMPARE(ncvDecoded.measurement.gear, MeterGear::Ncv);
  QCOMPARE(ncvDecoded.measurement.displayDecimals, -1);
  QVERIFY(!ncvDecoded.measurement.flags.minMax);
  QCOMPARE(q(ampbeam::monitorLine(ncvDecoded.measurement)), QStringLiteral("NCV  2"));

  // State bit 5 toggles the verified MinMax flag.
  const Bytes minMax = longFrame(gearWord(3, MeterPrefix::Milli, 0, true), valueWord(107),
                                 gearWord(3, MeterPrefix::Milli, 0, true), valueWord(9371), 0x20U);
  const auto minMaxDecoded = decoder.decode(minMax);
  QCOMPARE(minMaxDecoded.kind, ampbeam::DecodedFrame::Kind::Measurement);
  QCOMPARE(minMaxDecoded.measurement.gear, MeterGear::DcVoltage);
  QCOMPARE(minMaxDecoded.measurement.displayDecimals, 3);
  QCOMPARE(minMaxDecoded.measurement.secondary.displayDecimals, 3);
  QVERIFY(minMaxDecoded.measurement.flags.minMax);
  QCOMPARE(q(ampbeam::monitorLine(minMaxDecoded.measurement)),
           QStringLiteral("DCV  0.107 mV  [manual]  [minmax]  |  MAX 9.371 mV"));
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

void CoreWhiteBoxTest::captureLineFormatting() {
  // The CLI and the GUI share this exact line, so the log, a recorded capture
  // and the importer agree byte for byte.
  const Bytes frame = bytes({0x01, 0xAB, 0xFF});
  QCOMPARE(q(ampbeam::captureLine(42, frame, "meter: DCV 4.998 V")),
           QStringLiteral("42\t3\t01 AB FF\tmeter: DCV 4.998 V"));

  const Bytes empty;
  QCOMPARE(q(ampbeam::captureLine(0, empty, "")), QStringLiteral("0\t0\t\t"));
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

void CoreWhiteBoxTest::traceHistorySamplesAreScaled() {
  ampbeam::TraceHistory history(4);
  QVERIFY(history.empty());
  QCOMPARE(history.size(), std::size_t{0});
  QCOMPARE(history.capacity(), std::size_t{4});
  QVERIFY(!history.minimum().has_value());
  QVERIFY(!history.maximum().has_value());
  QVERIFY(!history.visibleMinimum().has_value());
  QVERIFY(!history.visibleMaximum().has_value());
  QVERIFY(history.samples().empty());
  QCOMPARE(history.gear(), MeterGear::Unknown);
  QCOMPARE(history.unit(), Unit::None);
  QCOMPARE(history.timeSpanMs(), std::uint64_t{10000});
  QCOMPARE(history.newestTimeMs(), std::uint64_t{0});

  ampbeam::Measurement measurement;
  measurement.gear = MeterGear::DcVoltage;
  measurement.unit = Unit::Volt;
  for (int index = 0; index < 3; ++index) {
    measurement.value = static_cast<double>(index + 1);
    measurement.receivedAtMs = kTraceBaseMs + static_cast<std::uint64_t>(index) * 1000U;
    history.push(measurement);
  }

  // The x axis is the fixed 10 s timebase, not the number of samples: the
  // newest reading sits on the right edge and older ones keep their distance.
  QCOMPARE(history.size(), std::size_t{3});
  QVERIFY(!history.empty());
  QCOMPARE(history.gear(), MeterGear::DcVoltage);
  QCOMPARE(history.unit(), Unit::Volt);
  QCOMPARE(history.newestTimeMs(), kTraceBaseMs + 2000U);
  QVERIFY(nearlyEqual(history.minimum().value_or(0.0), 1.0));
  QVERIFY(nearlyEqual(history.maximum().value_or(0.0), 3.0));

  auto samples = history.samples();
  QCOMPARE(samples.size(), std::size_t{3});
  QVERIFY(nearlyEqual(samples.at(0).x, 0.8));
  QVERIFY(nearlyEqual(samples.at(1).x, 0.9));
  QVERIFY(nearlyEqual(samples.at(2).x, 1.0));
  QVERIFY(nearlyEqual(samples.at(0).y, 0.0));
  QVERIFY(nearlyEqual(samples.at(1).y, 0.5));
  QVERIFY(nearlyEqual(samples.at(2).y, 1.0));
  QVERIFY(samples.at(0).valid);
  QVERIFY(samples.at(2).valid);

  // A later reading scrolls the first sample left while the timebase stays
  // constant.
  measurement.value = 4.0;
  measurement.receivedAtMs = kTraceBaseMs + 3000U;
  history.push(measurement);
  samples = history.samples();
  QCOMPARE(samples.size(), std::size_t{4});
  QVERIFY(nearlyEqual(samples.at(0).x, 0.7));
  QVERIFY(nearlyEqual(samples.at(3).x, 1.0));

  // Capacity overflow drops the oldest reading, not the right edge.
  measurement.value = 5.0;
  measurement.receivedAtMs = kTraceBaseMs + 4000U;
  history.push(measurement);
  QCOMPARE(history.size(), std::size_t{4});
  QCOMPARE(history.newestTimeMs(), kTraceBaseMs + 4000U);
  QVERIFY(nearlyEqual(history.minimum().value_or(0.0), 2.0));
  QVERIFY(nearlyEqual(history.maximum().value_or(0.0), 5.0));
  samples = history.samples();
  QCOMPARE(samples.size(), std::size_t{4});
  QVERIFY(nearlyEqual(samples.at(0).x, 0.7));
  QVERIFY(nearlyEqual(samples.at(0).y, 0.0));
  QVERIFY(nearlyEqual(samples.at(3).y, 1.0));
}

void CoreWhiteBoxTest::traceHistoryFlatAndSingleSeries() {
  ampbeam::TraceHistory history(4);

  ampbeam::Measurement measurement;
  measurement.gear = MeterGear::DcVoltage;
  measurement.unit = Unit::Volt;
  measurement.value = 5.0;
  measurement.receivedAtMs = kTraceBaseMs + 2000U;
  history.push(measurement);

  // A single reading is on the right edge ("now") and on the middle line.
  auto samples = history.samples();
  QCOMPARE(samples.size(), std::size_t{1});
  QVERIFY(nearlyEqual(samples.at(0).x, 1.0));
  QVERIFY(nearlyEqual(samples.at(0).y, 0.5));
  QVERIFY(samples.at(0).valid);

  measurement.receivedAtMs = kTraceBaseMs + 3000U;
  history.push(measurement);
  samples = history.samples();
  QCOMPARE(samples.size(), std::size_t{2});
  QVERIFY(nearlyEqual(samples.at(0).x, 0.9));
  QVERIFY(nearlyEqual(samples.at(1).x, 1.0));
  QVERIFY(nearlyEqual(samples.at(0).y, 0.5));
  QVERIFY(nearlyEqual(samples.at(1).y, 0.5));
  QVERIFY(nearlyEqual(history.minimum().value_or(0.0), 5.0));
  QVERIFY(nearlyEqual(history.maximum().value_or(0.0), 5.0));
}

void CoreWhiteBoxTest::traceHistoryStartsANewSeries() {
  ampbeam::TraceHistory history(8);
  std::uint64_t time = kTraceBaseMs;

  ampbeam::Measurement reading;
  reading.value = 1.0;
  reading.unit = Unit::Volt;
  reading.gear = MeterGear::DcVoltage;
  reading.secondary.present = true;
  reading.secondary.value = 10.0;
  reading.secondary.unit = Unit::Percent;
  reading.receivedAtMs = time;
  history.push(reading);
  time += 1000U;
  reading.receivedAtMs = time;
  history.push(reading);
  QCOMPARE(history.size(), std::size_t{2});
  QVERIFY(history.hasSecondary());
  QCOMPARE(history.secondaryUnit(), Unit::Percent);

  // The same unit on another function is a different series, and its
  // secondary reading starts over as well.
  time += 1000U;
  reading.gear = MeterGear::AcVoltage;
  reading.secondary = {};
  reading.receivedAtMs = time;
  history.push(reading);
  QCOMPARE(history.size(), std::size_t{1});
  QCOMPARE(history.gear(), MeterGear::AcVoltage);
  QVERIFY(!history.hasSecondary());
  QCOMPARE(history.secondaryUnit(), Unit::None);

  // A range change switches the unit, which also starts over.
  time += 1000U;
  reading.unit = Unit::Millivolt;
  reading.receivedAtMs = time;
  history.push(reading);
  QCOMPARE(history.size(), std::size_t{1});
  QCOMPARE(history.unit(), Unit::Millivolt);

  history.clear();
  QVERIFY(history.empty());
  QVERIFY(!history.minimum().has_value());
  QVERIFY(!history.maximum().has_value());
  QCOMPARE(history.gear(), MeterGear::Unknown);
  QCOMPARE(history.unit(), Unit::None);

  // A cleared history starts a new series without comparing against the old
  // one.
  time += 1000U;
  reading.unit = Unit::Volt;
  reading.gear = MeterGear::DcVoltage;
  reading.receivedAtMs = time;
  history.push(reading);
  QCOMPARE(history.size(), std::size_t{1});
}

void CoreWhiteBoxTest::traceHistoryMarksGaps() {
  ampbeam::TraceHistory history(4);

  ampbeam::Measurement overload;
  overload.gear = MeterGear::DcVoltage;
  overload.unit = Unit::Volt;
  overload.value = std::numeric_limits<double>::quiet_NaN();
  overload.flags.overload = true;
  overload.receivedAtMs = kTraceBaseMs;
  history.push(overload);

  QCOMPARE(history.size(), std::size_t{1});
  QVERIFY(!history.minimum().has_value());
  QVERIFY(!history.maximum().has_value());
  auto samples = history.samples();
  QCOMPARE(samples.size(), std::size_t{1});
  QVERIFY(!samples.at(0).valid);
  QVERIFY(nearlyEqual(samples.at(0).y, 0.0));

  // A gap between finite readings keeps its place in the timebase.
  ampbeam::Measurement reading = overload;
  reading.flags = {};
  reading.value = 2.0;
  reading.receivedAtMs = kTraceBaseMs + 1000U;
  history.push(reading);
  reading.value = 1.0;
  reading.receivedAtMs = kTraceBaseMs + 2000U;
  history.push(reading);
  overload.receivedAtMs = kTraceBaseMs + 3000U;
  history.push(overload);
  QCOMPARE(history.size(), std::size_t{4});
  samples = history.samples();
  QCOMPARE(samples.size(), std::size_t{4});
  QVERIFY(!samples.at(0).valid);
  QVERIFY(samples.at(1).valid);
  QVERIFY(samples.at(2).valid);
  QVERIFY(!samples.at(3).valid);
  QVERIFY(nearlyEqual(samples.at(1).y, 1.0));
  QVERIFY(nearlyEqual(samples.at(2).y, 0.0));
  QVERIFY(nearlyEqual(history.minimum().value_or(0.0), 1.0));
  QVERIFY(nearlyEqual(history.maximum().value_or(0.0), 2.0));

  // An infinite value is a gap as well and never becomes an extremum.
  reading.value = std::numeric_limits<double>::infinity();
  reading.receivedAtMs = kTraceBaseMs + 4000U;
  history.push(reading);
  samples = history.samples();
  QCOMPARE(samples.size(), std::size_t{4});
  QVERIFY(!samples.at(2).valid);
  QVERIFY(!samples.at(3).valid);
  QVERIFY(nearlyEqual(history.maximum().value_or(0.0), 2.0));
}

void CoreWhiteBoxTest::traceHistoryClampsCapacity() {
  // A zero capacity would break the stored window; it becomes one slot.
  ampbeam::TraceHistory history(0);
  QCOMPARE(history.capacity(), std::size_t{1});
  QCOMPARE(history.timeSpanMs(), std::uint64_t{10000});
  QVERIFY(history.canZoomTimeIn());
  QVERIFY(history.canZoomTimeOut());
  QCOMPARE(history.zoomTimeIn(), std::uint64_t{5000});
  QCOMPARE(history.zoomTimeOut(), std::uint64_t{10000});
  QCOMPARE(history.valueZoom(), 1.0);
  QVERIFY(history.canZoomValueIn());
  QVERIFY(!history.canZoomValueOut());

  // NCV has no unit but is still a valid series.
  ampbeam::Measurement reading;
  reading.gear = MeterGear::Ncv;
  reading.value = 1.0;
  reading.receivedAtMs = kTraceBaseMs;
  history.push(reading);
  reading.value = 2.0;
  reading.receivedAtMs = kTraceBaseMs + 1000U;
  history.push(reading);
  QCOMPARE(history.size(), std::size_t{1});
  QCOMPARE(history.unit(), Unit::None);
  QVERIFY(nearlyEqual(history.samples().at(0).y, 0.5));
}

void CoreWhiteBoxTest::traceHistoryTimeZoom() {
  ampbeam::TraceHistory history(200);
  QCOMPARE(history.timeSpanMs(), std::uint64_t{10000});
  QVERIFY(history.canZoomTimeIn());
  QVERIFY(history.canZoomTimeOut());

  // One reading per second for 40 s.
  ampbeam::Measurement measurement;
  measurement.gear = MeterGear::DcVoltage;
  measurement.unit = Unit::Volt;
  for (int index = 0; index < 40; ++index) {
    measurement.value = static_cast<double>(index + 1);
    measurement.receivedAtMs = kTraceBaseMs + static_cast<std::uint64_t>(index) * 1000U;
    history.push(measurement);
  }
  QCOMPARE(history.size(), std::size_t{40});
  QCOMPARE(history.newestTimeMs(), kTraceBaseMs + 39000U);

  // The 10 s timebase shows only the newest 11 readings.
  auto samples = history.samples();
  QCOMPARE(samples.size(), std::size_t{11});
  QVERIFY(nearlyEqual(samples.at(0).x, 0.0));
  QVERIFY(nearlyEqual(samples.at(10).x, 1.0));
  QVERIFY(nearlyEqual(history.minimum().value_or(0.0), 30.0));
  QVERIFY(nearlyEqual(history.maximum().value_or(0.0), 40.0));

  // Zooming the timebase out brings older readings back.
  QCOMPARE(history.zoomTimeOut(), std::uint64_t{30000});
  QCOMPARE(history.samples().size(), std::size_t{31});
  QVERIFY(nearlyEqual(history.minimum().value_or(0.0), 10.0));

  QCOMPARE(history.zoomTimeOut(), std::uint64_t{60000});
  QCOMPARE(history.samples().size(), std::size_t{40});
  QVERIFY(nearlyEqual(history.minimum().value_or(0.0), 1.0));
  QVERIFY(!history.canZoomTimeOut());
  QCOMPARE(history.zoomTimeOut(), std::uint64_t{60000});

  // Zooming in down to the floor narrows the window to the newest readings.
  QCOMPARE(history.zoomTimeIn(), std::uint64_t{30000});
  QCOMPARE(history.zoomTimeIn(), std::uint64_t{10000});
  QCOMPARE(history.samples().size(), std::size_t{11});
  QCOMPARE(history.zoomTimeIn(), std::uint64_t{5000});
  QCOMPARE(history.samples().size(), std::size_t{6});
  QCOMPARE(history.zoomTimeIn(), std::uint64_t{2000});
  QCOMPARE(history.samples().size(), std::size_t{3});
  QVERIFY(!history.canZoomTimeIn());
  QCOMPARE(history.zoomTimeIn(), std::uint64_t{2000});
}

void CoreWhiteBoxTest::traceHistoryValueZoom() {
  ampbeam::TraceHistory history(10);
  ampbeam::Measurement measurement;
  measurement.gear = MeterGear::DcVoltage;
  measurement.unit = Unit::Volt;
  for (int index = 0; index < 3; ++index) {
    measurement.value = static_cast<double>(index + 1);
    measurement.receivedAtMs = kTraceBaseMs + static_cast<std::uint64_t>(index) * 1000U;
    history.push(measurement);
  }

  // Fit shows the full range; a value zoom magnifies around the middle.
  QCOMPARE(history.valueZoom(), 1.0);
  QVERIFY(history.canZoomValueIn());
  QVERIFY(!history.canZoomValueOut());
  QVERIFY(nearlyEqual(history.visibleMinimum().value_or(0.0), 1.0));
  QVERIFY(nearlyEqual(history.visibleMaximum().value_or(0.0), 3.0));
  QCOMPARE(history.zoomValueIn(), 2.0);
  QVERIFY(nearlyEqual(history.visibleMinimum().value_or(0.0), 1.5));
  QVERIFY(nearlyEqual(history.visibleMaximum().value_or(0.0), 2.5));
  auto samples = history.samples();
  QCOMPARE(samples.size(), std::size_t{3});
  QVERIFY(nearlyEqual(samples.at(0).y, -0.5));
  QVERIFY(nearlyEqual(samples.at(1).y, 0.5));
  QVERIFY(nearlyEqual(samples.at(2).y, 1.5));

  QCOMPARE(history.zoomValueIn(), 4.0);
  QCOMPARE(history.zoomValueIn(), 8.0);
  QCOMPARE(history.zoomValueIn(), 16.0);
  QVERIFY(!history.canZoomValueIn());
  QCOMPARE(history.zoomValueIn(), 16.0);

  QCOMPARE(history.zoomValueOut(), 8.0);
  QCOMPARE(history.zoomValueOut(), 4.0);
  QCOMPARE(history.zoomValueOut(), 2.0);
  QCOMPARE(history.zoomValueOut(), 1.0);
  QVERIFY(!history.canZoomValueOut());
  QCOMPARE(history.zoomValueOut(), 1.0);
  QVERIFY(nearlyEqual(history.visibleMinimum().value_or(0.0), 1.0));
  QVERIFY(nearlyEqual(history.visibleMaximum().value_or(0.0), 3.0));
  samples = history.samples();
  QVERIFY(nearlyEqual(samples.at(0).y, 0.0));
  QVERIFY(nearlyEqual(samples.at(1).y, 0.5));
  QVERIFY(nearlyEqual(samples.at(2).y, 1.0));
}

void CoreWhiteBoxTest::traceHistoryPlotsSecondaryReadings() {
  ampbeam::TraceHistory history(10);
  QVERIFY(!history.hasSecondary());
  QCOMPARE(history.secondaryUnit(), Unit::None);

  // A frequency primary with the duty cycle as the second reading, like the
  // verified frequency+duty frame.
  ampbeam::Measurement measurement;
  measurement.gear = MeterGear::Frequency;
  measurement.unit = Unit::Hertz;
  measurement.secondary.present = true;
  measurement.secondary.unit = Unit::Percent;
  for (int index = 0; index < 3; ++index) {
    measurement.value = 50.0 + static_cast<double>(index) * 10.0;
    measurement.secondary.value = 10.0 + static_cast<double>(index) * 10.0;
    measurement.receivedAtMs = kTraceBaseMs + static_cast<std::uint64_t>(index) * 1000U;
    history.push(measurement);
  }

  QVERIFY(history.hasSecondary());
  QCOMPARE(history.secondaryUnit(), Unit::Percent);
  QVERIFY(nearlyEqual(history.minimum().value_or(0.0), 50.0));
  QVERIFY(nearlyEqual(history.maximum().value_or(0.0), 70.0));
  QVERIFY(nearlyEqual(history.secondaryMinimum().value_or(0.0), 10.0));
  QVERIFY(nearlyEqual(history.secondaryMaximum().value_or(0.0), 30.0));
  QVERIFY(nearlyEqual(history.secondaryVisibleMinimum().value_or(0.0), 10.0));
  QVERIFY(nearlyEqual(history.secondaryVisibleMaximum().value_or(0.0), 30.0));

  const auto samples = history.samples();
  QCOMPARE(samples.size(), std::size_t{3});
  QVERIFY(samples.at(0).secondaryValid);
  QVERIFY(nearlyEqual(samples.at(0).secondaryY, 0.0));
  QVERIFY(nearlyEqual(samples.at(1).secondaryY, 0.5));
  QVERIFY(nearlyEqual(samples.at(2).secondaryY, 1.0));

  // A frame without a second reading leaves a gap in that channel only.
  measurement.secondary = {};
  measurement.value = 80.0;
  measurement.receivedAtMs = kTraceBaseMs + 3000U;
  history.push(measurement);
  const auto later = history.samples();
  QCOMPARE(later.size(), std::size_t{4});
  QVERIFY(later.at(3).valid);
  QVERIFY(!later.at(3).secondaryValid);
  QVERIFY(nearlyEqual(history.secondaryMinimum().value_or(0.0), 10.0));
  QVERIFY(nearlyEqual(history.secondaryMaximum().value_or(0.0), 30.0));
}

void CoreWhiteBoxTest::traceHistoryDropsSamplesOutsideTheTimebase() {
  ampbeam::TraceHistory history(10);
  ampbeam::Measurement measurement;
  measurement.gear = MeterGear::DcVoltage;
  measurement.unit = Unit::Volt;
  measurement.value = 1.0;
  measurement.receivedAtMs = kTraceBaseMs;
  history.push(measurement);
  measurement.value = 2.0;
  measurement.receivedAtMs = kTraceBaseMs + 1000U;
  history.push(measurement);
  measurement.value = 3.0;
  measurement.receivedAtMs = kTraceBaseMs + 12000U;
  history.push(measurement);

  // The two oldest readings are older than the 10 s window and are not drawn,
  // but they stay stored in case the user zooms the timebase out.
  QCOMPARE(history.size(), std::size_t{3});
  auto samples = history.samples();
  QCOMPARE(samples.size(), std::size_t{1});
  QVERIFY(nearlyEqual(samples.at(0).x, 1.0));
  QVERIFY(nearlyEqual(history.minimum().value_or(0.0), 3.0));
  QVERIFY(nearlyEqual(history.maximum().value_or(0.0), 3.0));

  // A reading in the window appears just left of the newest one.
  measurement.value = 4.0;
  measurement.receivedAtMs = kTraceBaseMs + 13000U;
  history.push(measurement);
  samples = history.samples();
  QCOMPARE(samples.size(), std::size_t{2});
  QVERIFY(nearlyEqual(samples.at(0).x, 0.9));
  QVERIFY(nearlyEqual(samples.at(1).x, 1.0));
  QVERIFY(nearlyEqual(samples.at(0).y, 0.0));
  QVERIFY(nearlyEqual(samples.at(1).y, 1.0));

  // Timestamps below the timebase clamp the window start to zero.
  ampbeam::TraceHistory early(4);
  measurement.value = 1.0;
  measurement.receivedAtMs = 500;
  early.push(measurement);
  QVERIFY(nearlyEqual(early.samples().at(0).x, 0.05));
}

void CoreWhiteBoxTest::decodersReportTheirName() {
  const ampbeam::MeterDecoder meter;
  QCOMPARE(q(meter.name()), QStringLiteral("meter"));
  const ampbeam::RawDecoder raw;
  QCOMPARE(q(raw.name()), QStringLiteral("raw"));
}

QTEST_GUILESS_MAIN(CoreWhiteBoxTest)

#include "tst_core_whitebox.moc"
