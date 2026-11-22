// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Black-box tests driven only by fixtures and the public decoder contract.
#include "ampbeam/core/capture.hpp"
#include "ampbeam/core/decoder.hpp"
#include "ampbeam/core/hex.hpp"
#include "ampbeam/core/measurement.hpp"
#include "ampbeam/core/meter.hpp"
#include "ampbeam/core/profile.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::uint8_t> bytes(std::initializer_list<int> values) {
  std::vector<std::uint8_t> out;
  out.reserve(values.size());
  for (const int value : values) {
    out.push_back(static_cast<std::uint8_t>(value));
  }
  return out;
}

QString text(std::string_view value) {
  return QString::fromLatin1(value.data(), static_cast<int>(value.size()));
}

std::vector<ampbeam::DecodedFrame> loadDecodedFixture(const QString& name) {
  const QString directory = QStringLiteral(AMPBEAM_FIXTURE_DIR);
  QFile file(directory + QLatin1Char('/') + name);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
  const QJsonArray frames = document.object().value(QStringLiteral("frames")).toArray();
  const ampbeam::MeterDecoder decoder;
  std::vector<ampbeam::DecodedFrame> decoded;
  for (const auto& value : frames) {
    const auto parsed =
        ampbeam::fromHex(value.toObject().value(QStringLiteral("hex")).toString().toStdString());
    if (!parsed.has_value()) {
      continue;
    }
    decoded.push_back(decoder.decode(*parsed));
  }
  return decoded;
}

} // namespace

class CoreTest final : public QObject {
  Q_OBJECT

private slots:
  void hexEncodesUppercase();
  void hexEncodesWithSeparator();
  void hexParsesCommonFormats();
  void hexRejectsMalformedInput();
  void unitSymbolsAndQuantities();
  void numberFormatting();
  void measurementFormatting();
  void rawDecoderReturnsPayload();
  void meterDecoderDoesNotInventValues();
  void meterDecodesLongFrame();
  void meterDecodesSiPrefix();
  void meterDecodesNegativeReading();
  void meterOverloadHasNoNumericValue();
  void meterFrontPanelFlags();
  void meterDecodesAcVoltage();
  void meterDecodesSecondaryValue();
  void meterFramerSplitsConcatenatedFrames();
  void meterRejectsIncompleteFrames();
  void meterKeyAndDeviceInfo();
  void meterFixturesDecode();
  void meterRangeSweepFixture();
  void meterScaleSweepFixture();
  void meterMinMaxFixture();
  void meterMinMaxAcFixture();
  void meterFrequencyVoltageFixture();
  void meterRelativeFixture();
  void profileContainsExpectedUuids();
  void captureParsesCliOutput();
  void captureParsesHexAndEpochPrefix();
  void captureReportsJunkLines();
  void fixtureJsonEscapesAndParses();
};

void CoreTest::hexEncodesUppercase() {
  const auto data = bytes({0x00, 0x1A, 0x2B, 0xFF});
  QCOMPARE(QString::fromStdString(ampbeam::toHex(data)), QStringLiteral("001A2BFF"));
}

void CoreTest::hexEncodesWithSeparator() {
  const auto data = bytes({0x00, 0x1A, 0x2B});
  QCOMPARE(QString::fromStdString(ampbeam::toHex(data, ':')), QStringLiteral("00:1A:2B"));
  QCOMPARE(QString::fromStdString(ampbeam::toHex(data, ' ', false)), QStringLiteral("00 1a 2b"));
}

void CoreTest::hexParsesCommonFormats() {
  const auto expected = bytes({0x00, 0x1A, 0x2B, 0xFF});

  for (const char* text : {"001A2BFF", "00:1A:2B:FF", "00-1a-2b-ff", "00 1a 2b ff",
                           "0x00 0x1A 0x2B 0xFF", " 00:1a:2b:ff\n"}) {
    const auto parsed = ampbeam::fromHex(text);
    QVERIFY2(parsed.has_value(), text);
    QCOMPARE(*parsed, expected);
  }
}

void CoreTest::hexRejectsMalformedInput() {
  QVERIFY(!ampbeam::fromHex("0").has_value());
  QVERIFY(!ampbeam::fromHex("0:0").has_value());
  QVERIFY(!ampbeam::fromHex("zz").has_value());
  QVERIFY(!ampbeam::fromHex("00:1A:2B:F").has_value());
}

void CoreTest::unitSymbolsAndQuantities() {
  QCOMPARE(QString::fromUtf8(ampbeam::unitSymbol(ampbeam::Unit::Volt)), QStringLiteral("V"));
  QCOMPARE(QString::fromUtf8(ampbeam::unitSymbol(ampbeam::Unit::Nanofarad)), QStringLiteral("nF"));
  QCOMPARE(ampbeam::quantityOf(ampbeam::Unit::Millivolt), ampbeam::Quantity::Voltage);
  QCOMPARE(ampbeam::quantityOf(ampbeam::Unit::Megaohm), ampbeam::Quantity::Resistance);
  QCOMPARE(ampbeam::quantityOf(ampbeam::Unit::Percent), ampbeam::Quantity::DutyCycle);
  QCOMPARE(ampbeam::quantityOf(ampbeam::Unit::None), ampbeam::Quantity::Unknown);
}

void CoreTest::numberFormatting() {
  QCOMPARE(QString::fromStdString(ampbeam::formatNumber(4.998)), QStringLiteral("4.998"));
  QCOMPARE(QString::fromStdString(ampbeam::formatNumber(0.0)), QStringLiteral("0"));
  QCOMPARE(QString::fromStdString(ampbeam::formatNumber(-0.0)), QStringLiteral("0"));
  QCOMPARE(QString::fromStdString(ampbeam::formatNumber(1234567.0)), QStringLiteral("1.2346e+06"));
  QCOMPARE(QString::fromStdString(ampbeam::formatNumber(std::numeric_limits<double>::quiet_NaN())),
           QStringLiteral("nan"));
}

void CoreTest::measurementFormatting() {
  ampbeam::Measurement measurement;
  measurement.value = 4.998;
  measurement.unit = ampbeam::Unit::Volt;
  measurement.quantity = ampbeam::Quantity::Voltage;
  measurement.currentType = ampbeam::CurrentType::DC;
  measurement.flags.hold = true;

  QCOMPARE(QString::fromStdString(ampbeam::format(measurement)),
           QStringLiteral("4.998 V DC [hold]"));

  measurement.value = std::numeric_limits<double>::quiet_NaN();
  measurement.unit = ampbeam::Unit::None;
  measurement.currentType = ampbeam::CurrentType::None;
  measurement.flags = {};
  QCOMPARE(QString::fromStdString(ampbeam::format(measurement)), QStringLiteral("---"));
}

void CoreTest::rawDecoderReturnsPayload() {
  const ampbeam::RawDecoder decoder;
  const auto data = bytes({0x01, 0x02, 0x03});
  const auto decoded = decoder.decode(data);

  QCOMPARE(decoded.kind, ampbeam::DecodedFrame::Kind::Unknown);
  QCOMPARE(text(decoded.detail), QStringLiteral("01:02:03"));
  QCOMPARE(text(decoder.name()), QStringLiteral("raw"));
}

void CoreTest::meterDecoderDoesNotInventValues() {
  const ampbeam::MeterDecoder decoder;
  const auto data = bytes({0xDE, 0xAD, 0xBE, 0xEF});
  const auto decoded = decoder.decode(data);

  // A frame length that is neither 6 nor 15 bytes cannot be a reading; a wrong
  // value would be worse than no value.
  QCOMPARE(decoded.kind, ampbeam::DecodedFrame::Kind::Unknown);
  QCOMPARE(decoded.measurement.value, 0.0);
  QVERIFY(decoded.detail.find("DE:AD:BE:EF") != std::string::npos);
}

void CoreTest::meterDecodesLongFrame() {
  // 15 byte frame: gear 24 bit, value 24 bit, second gear/value, state 24 bit.
  // gear = dp 3 (0.001) | prefix 4 (none) | gear 0 (DCV) = 0x000023
  // value = 4998 (0x001386), status 0, positive -> 86 13 00
  const ampbeam::MeterDecoder decoder;
  const auto data = bytes(
      {0x23, 0x00, 0x00, 0x86, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00});
  const auto decoded = decoder.decode(data);

  QCOMPARE(decoded.kind, ampbeam::DecodedFrame::Kind::Measurement);
  QCOMPARE(decoded.measurement.value, 4.998);
  QCOMPARE(decoded.measurement.unit, ampbeam::Unit::Volt);
  QCOMPARE(decoded.measurement.quantity, ampbeam::Quantity::Voltage);
  QCOMPARE(decoded.measurement.currentType, ampbeam::CurrentType::DC);
  QVERIFY(decoded.measurement.flags.hold);
  QVERIFY(decoded.measurement.flags.autoRange);
  QVERIFY(!decoded.measurement.flags.overload);
}

void CoreTest::meterDecodesSiPrefix() {
  // 12.34 mV DC: gear 0 (DCV), prefix 3 (milli), dp 3 -> 0x001B.
  const ampbeam::MeterDecoder decoder;
  const auto data = bytes(
      {0x1B, 0x00, 0x00, 0x34, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  const auto decoded = decoder.decode(data);

  QCOMPARE(decoded.kind, ampbeam::DecodedFrame::Kind::Measurement);
  QCOMPARE(decoded.measurement.value, 12.34);
  QCOMPARE(decoded.measurement.unit, ampbeam::Unit::Millivolt);
  QCOMPARE(decoded.measurement.quantity, ampbeam::Quantity::Voltage);
}

void CoreTest::meterOverloadHasNoNumericValue() {
  // value word with status code 1 (overload): (1 << 20) = 0x100000
  const ampbeam::MeterDecoder decoder;
  const auto data = bytes(
      {0x23, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  const auto decoded = decoder.decode(data);

  QCOMPARE(decoded.kind, ampbeam::DecodedFrame::Kind::Measurement);
  QVERIFY(std::isnan(decoded.measurement.value));
  QVERIFY(decoded.measurement.flags.overload);
  QCOMPARE(decoded.measurement.unit, ampbeam::Unit::Volt);
}

void CoreTest::meterFrontPanelFlags() {
  // Bit positions are part of the wire format; keep the verified ones pinned.
  static_assert(static_cast<unsigned>(ampbeam::MeterState::Hold) == 0);
  static_assert(static_cast<unsigned>(ampbeam::MeterState::AutoRange) == 2);
  static_assert(static_cast<unsigned>(ampbeam::MeterState::MinMax) == 5);
  static_assert(static_cast<unsigned>(ampbeam::MeterState::Oscilloscope) == 18);
  static_assert(static_cast<unsigned>(ampbeam::MeterGear::Ncv) == 13);
  static_assert(static_cast<unsigned>(ampbeam::MeterGear::Diode) == 10);

  // Unknown state bits are ignored, not named or reported as readings.
  const auto frame = bytes(
      {0x23, 0x00, 0x00, 0x86, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3B, 0x04, 0x00});
  const auto decoded = ampbeam::meterDecodeFrame(frame);
  QVERIFY(decoded.has_value());
  QVERIFY(ampbeam::meterHasState(decoded->state, ampbeam::MeterState::Hold));
  QVERIFY(!ampbeam::meterHasState(decoded->state, ampbeam::MeterState::AutoRange));
  QCOMPARE(QString::fromUtf8(ampbeam::meterStateName(ampbeam::MeterState::Hold)),
           QStringLiteral("HOLD"));
}

void CoreTest::meterDecodesNegativeReading() {
  // Real capture from hardware: DCV, 20 mV manual range, reading `-0.249 mV`.
  // gear 0xF0001B -> dp 3, prefix 3 (milli), gear 0; value 0x8000F9 -> bit 23
  // sign set, magnitude 0xF9 = 249; state 0x000004 -> AUTO.
  const ampbeam::MeterDecoder decoder;
  const auto frame = bytes(
      {0x1B, 0x00, 0xF0, 0xF9, 0x00, 0x80, 0xA0, 0x01, 0xF0, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00});
  const auto decoded = decoder.decode(frame);
  QCOMPARE(decoded.kind, ampbeam::DecodedFrame::Kind::Measurement);
  QCOMPARE(decoded.measurement.unit, ampbeam::Unit::Millivolt);
  QVERIFY(qFuzzyCompare(decoded.measurement.value, -0.249));
}

void CoreTest::meterDecodesAcVoltage() {
  // 23.00 V AC: gear 1 (ACV), prefix 4 (none), dp 2 -> 0x0062
  // value 2300 = 0x08FC, state = AC (bit 13) = 0x2000
  const ampbeam::MeterDecoder decoder;
  const auto data = bytes(
      {0x62, 0x00, 0x00, 0xFC, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00});
  const auto decoded = decoder.decode(data);

  QCOMPARE(decoded.kind, ampbeam::DecodedFrame::Kind::Measurement);
  QCOMPARE(decoded.measurement.value, 23.0);
  QCOMPARE(decoded.measurement.unit, ampbeam::Unit::Volt);
  QCOMPARE(decoded.measurement.currentType, ampbeam::CurrentType::AC);
}

void CoreTest::meterDecodesSecondaryValue() {
  // hasSecondValue = bit 12: gear 0x1023. Primary 4998 -> 4.998 V,
  // secondary 1234 -> 1.234 V.
  const auto frame = bytes(
      {0x23, 0x10, 0x00, 0x86, 0x13, 0x00, 0x23, 0x00, 0x00, 0xD2, 0x04, 0x00, 0x00, 0x00, 0x00});
  const auto decoded = ampbeam::meterDecodeFrame(frame);
  QVERIFY(decoded.has_value());
  QVERIFY(decoded->secondary.has_value());
  QCOMPARE(decoded->primary.value, 4.998);
  QCOMPARE(decoded->secondary->value, 1.234);
  QCOMPARE(decoded->secondary->unit, ampbeam::Unit::Volt);
}

void CoreTest::meterFramerSplitsConcatenatedFrames() {
  const auto first = bytes(
      {0x23, 0x00, 0x00, 0x86, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  const auto second = bytes(
      {0x23, 0x00, 0x00, 0xD2, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  std::vector<std::uint8_t> stream = first;
  stream.insert(stream.end(), second.begin(), second.end());

  ampbeam::MeterFramer framer;
  const auto frames = framer.push(stream);
  QCOMPARE(frames.size(), static_cast<std::size_t>(2));
  QCOMPARE(frames.at(0), first);
  QCOMPARE(frames.at(1), second);

  // A split frame is buffered until it is complete.
  framer.reset();
  QVERIFY(framer.push(std::span<const std::uint8_t>(first).first(10)).empty());
  const auto tail = framer.push(std::span<const std::uint8_t>(first).subspan(10));
  QCOMPARE(tail.size(), static_cast<std::size_t>(1));
  QCOMPARE(tail.front(), first);
}

void CoreTest::meterRejectsIncompleteFrames() {
  QVERIFY(!ampbeam::meterDecodeFrame(bytes({0x23, 0x00, 0x00})).has_value());
  QVERIFY(!ampbeam::meterDecodeFrame(bytes({})).has_value());
  QVERIFY(ampbeam::meterIsFrameSize(15));
  QVERIFY(!ampbeam::meterIsFrameSize(6));
  QVERIFY(!ampbeam::meterIsFrameSize(7));
}

void CoreTest::meterKeyAndDeviceInfo() {
  const auto pressed = ampbeam::meterKeyCommand(3, true);
  const std::array<std::uint8_t, 2> expectedPressed{3, 1};
  QCOMPARE(pressed, expectedPressed);
  const auto released = ampbeam::meterKeyCommand(3, false);
  const std::array<std::uint8_t, 2> expectedReleased{3, 0};
  QCOMPARE(released, expectedReleased);

  // Rename payload, measured on hardware: marker 0x40, then the name, then
  // zeros to 16 bytes.
  const auto rename = ampbeam::meterRenameCommand("AMPBEAM");
  QVERIFY(rename.has_value());
  QCOMPARE(rename->at(0), static_cast<std::uint8_t>(0x40));
  QCOMPARE(rename->at(1), static_cast<std::uint8_t>('A'));
  QCOMPARE(rename->at(7), static_cast<std::uint8_t>('M'));
  QCOMPARE(rename->at(8), static_cast<std::uint8_t>(0));
  QVERIFY(!ampbeam::meterRenameCommand(std::string_view{}).has_value());
  QVERIFY(!ampbeam::meterRenameCommand("0123456789ABCDEF").has_value());
  QVERIFY(ampbeam::meterRenameCommand("0123456789ABCDE").has_value());

  // Device identity payload of the tested meter: series 61, firmware 1.4.0.
  const auto measured =
      ampbeam::meterParseDeviceInfo(bytes({0x3D, 0x00, 0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));
  QVERIFY(measured.has_value());
  QCOMPARE(static_cast<unsigned>(measured->seriesId), 61U);
  QCOMPARE(QString::fromStdString(measured->version), QStringLiteral("1.4.0"));

  const auto info = ampbeam::meterParseDeviceInfo(bytes({0x59, 0x00, 0x01, 0x02, 0x03}));
  QVERIFY(info.has_value());
  QCOMPARE(static_cast<unsigned>(info->seriesId), 89U);
  QCOMPARE(QString::fromStdString(info->version), QStringLiteral("1.2.3"));
}

void CoreTest::meterFixturesDecode() {
  const QString directory = QStringLiteral(AMPBEAM_FIXTURE_DIR);

  const auto decodeFixture = [&directory](const QString& name) {
    QFile file(directory + QLatin1Char('/') + name);
    if (!file.open(QIODevice::ReadOnly)) {
      return std::vector<ampbeam::DecodedFrame>{};
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonArray frames = document.object().value(QStringLiteral("frames")).toArray();
    const ampbeam::MeterDecoder decoder;
    std::vector<ampbeam::DecodedFrame> decoded;
    for (const auto& value : frames) {
      const auto parsed =
          ampbeam::fromHex(value.toObject().value(QStringLiteral("hex")).toString().toStdString());
      if (!parsed.has_value()) {
        continue;
      }
      decoded.push_back(decoder.decode(*parsed));
    }
    return decoded;
  };

  // Real captures from hardware (series 61, firmware 1.4.0). Frames are
  // `24 00 F0 <magnitude>`: dp code 4, prefix code 4 (none), gear 0 (DCV),
  // state 0x000004 (auto range).
  const auto hardwareDcv = decodeFixture(QStringLiteral("meter-hardware-dcv.json"));
  QCOMPARE(hardwareDcv.size(), static_cast<std::size_t>(52));
  QCOMPARE(hardwareDcv.at(0).kind, ampbeam::DecodedFrame::Kind::Measurement);
  QCOMPARE(hardwareDcv.at(0).measurement.unit, ampbeam::Unit::Volt);
  QVERIFY(qFuzzyCompare(hardwareDcv.at(0).measurement.value, 0.5692));
  QVERIFY(hardwareDcv.at(0).measurement.flags.autoRange);
  QVERIFY(!hardwareDcv.at(0).measurement.flags.hold);
  QVERIFY(!hardwareDcv.at(0).measurement.flags.overload);
  for (const auto& frame : hardwareDcv) {
    // The LCD showed 0.56 V .. 0.58 V while the capture ran.
    QVERIFY(frame.measurement.value >= 0.56);
    QVERIFY(frame.measurement.value <= 0.58);
  }

  // Open probes in resistance mode: `32 01 F0 <magnitude>`, gear 4, prefix 6
  // (mega) and value status 1 (overload). The LCD showed "OL" and "MOhm".
  const auto hardwareOl = decodeFixture(QStringLiteral("meter-hardware-resistance-ol.json"));
  QCOMPARE(hardwareOl.size(), static_cast<std::size_t>(37));
  QCOMPARE(hardwareOl.at(0).kind, ampbeam::DecodedFrame::Kind::Measurement);
  QCOMPARE(hardwareOl.at(0).measurement.unit, ampbeam::Unit::Megaohm);
  QVERIFY(hardwareOl.at(0).measurement.flags.overload);
  QVERIFY(hardwareOl.at(0).measurement.flags.autoRange);
  QVERIFY(std::isnan(hardwareOl.at(0).measurement.value));
  for (const auto& frame : hardwareOl) {
    QVERIFY(frame.measurement.flags.overload);
    QVERIFY(std::isnan(frame.measurement.value));
  }

  // AC current, 20 A auto range, LCD steady at `00.00 A`. The second
  // gear/value slot carries garbage (`A0 01 F0` = frequency, `20 CC 07`) that the
  // LCD never shows, because bit 12 of the primary gear word is clear.
  const auto hardwareAca = decodeFixture(QStringLiteral("meter-hardware-ac-current.json"));
  QCOMPARE(hardwareAca.size(), static_cast<std::size_t>(32));
  for (const auto& frame : hardwareAca) {
    QCOMPARE(frame.kind, ampbeam::DecodedFrame::Kind::Measurement);
    QCOMPARE(frame.measurement.unit, ampbeam::Unit::Ampere);
    QCOMPARE(frame.measurement.value, 0.0);
    QVERIFY(frame.measurement.flags.autoRange);
    QVERIFY(!frame.measurement.flags.overload);
    QVERIFY(frame.detail.find("|") == std::string::npos);
  }

  // DCV on the manually selected 20 mV range, HOLD frozen by the operator at
  // `04.769 mV`. Magnitude `0x12A1` = 4769 matches the LCD digit for digit, and
  // AUTO is clear because the range was chosen by hand.
  const auto hardwareHold = decodeFixture(QStringLiteral("meter-hardware-dcv-hold-millivolt.json"));
  QCOMPARE(hardwareHold.size(), static_cast<std::size_t>(27));
  for (const auto& frame : hardwareHold) {
    QCOMPARE(frame.kind, ampbeam::DecodedFrame::Kind::Measurement);
    QCOMPARE(frame.measurement.unit, ampbeam::Unit::Millivolt);
    QVERIFY(qFuzzyCompare(frame.measurement.value, 4.769));
    QVERIFY(frame.measurement.flags.hold);
    QVERIFY(!frame.measurement.flags.autoRange);
    QVERIFY(!frame.measurement.flags.overload);
  }

  // Oscilloscope mode (state bit 18). The meter streams a constant frame whose
  // gear/value words do not track its screen, so nothing may be reported as a
  // measurement.
  const auto hardwareOsc = decodeFixture(QStringLiteral("meter-hardware-osc-mode.json"));
  QCOMPARE(hardwareOsc.size(), static_cast<std::size_t>(5));
  for (const auto& frame : hardwareOsc) {
    QCOMPARE(frame.kind, ampbeam::DecodedFrame::Kind::Unknown);
    QVERIFY(frame.detail.find("oscilloscope mode") != std::string::npos);
  }

  // Two Range key presses while streaming: one write steps the range once and
  // the first press leaves auto ranging. Backs the key semantics in the doc.
  const auto keyRange = decodeFixture(QStringLiteral("meter-hardware-key-range.json"));
  QCOMPARE(keyRange.size(), static_cast<std::size_t>(37));
  QVERIFY(keyRange.front().measurement.flags.autoRange);
  QVERIFY(!keyRange.back().measurement.flags.autoRange);
  QVERIFY(keyRange.front().detail.find("dp=3") != std::string::npos);
  bool sawSecondRange = false;
  for (const auto& frame : keyRange) {
    if (frame.detail.find("dp=2") != std::string::npos) {
      sawSecondRange = true;
    }
  }
  QVERIFY(sawSecondRange);

  // DCV on the 1000 V range: resolution 0.1 V, so gear word 0x21 -> decimal
  // point code 1, no prefix, gear 0. State is completely clear (manual range).
  const auto dcv1000 = decodeFixture(QStringLiteral("meter-hardware-dcv-1000v.json"));
  QCOMPARE(dcv1000.size(), static_cast<std::size_t>(33));
  bool sawTenth = false;
  for (const auto& frame : dcv1000) {
    QCOMPARE(frame.kind, ampbeam::DecodedFrame::Kind::Measurement);
    QCOMPARE(frame.measurement.unit, ampbeam::Unit::Volt);
    QVERIFY(frame.measurement.flags.autoRange == false);
    QVERIFY(frame.detail.find("dp=1") != std::string::npos);
    if (qFuzzyCompare(frame.measurement.value, 0.1)) {
      sawTenth = true;
    }
  }
  QVERIFY(sawTenth);

  // Frequency mode shows a duty cycle as a second reading. Gear word 0x11A3 has
  // bit 12 set, the second gear word 0x11E1 decodes to gear 7 (duty cycle), and
  // both values are zero because no signal was applied.
  const auto freqDuty = decodeFixture(QStringLiteral("meter-hardware-freq-duty.json"));
  QCOMPARE(freqDuty.size(), static_cast<std::size_t>(27));
  for (const auto& frame : freqDuty) {
    QCOMPARE(frame.kind, ampbeam::DecodedFrame::Kind::Measurement);
    QCOMPARE(frame.measurement.unit, ampbeam::Unit::Hertz);
    QCOMPARE(frame.measurement.quantity, ampbeam::Quantity::Frequency);
    QCOMPARE(frame.measurement.value, 0.0);
    QVERIFY(frame.measurement.secondary.present);
    QCOMPARE(frame.measurement.secondary.unit, ampbeam::Unit::Percent);
    QCOMPARE(frame.measurement.secondary.quantity, ampbeam::Quantity::DutyCycle);
    QCOMPARE(frame.measurement.secondary.value, 0.0);
    QVERIFY(frame.detail.find("| DUTY") != std::string::npos);
  }

  // Function sweep across four groups with the Select key on open inputs. The
  // meter occasionally drops a key write, so the fixture is checked for coverage
  // and per-function structure rather than for a press-by-press order.
  const auto sweep = decodeFixture(QStringLiteral("meter-hardware-mode-sweep.json"));
  QCOMPARE(sweep.size(), static_cast<std::size_t>(62));
  bool sawNcv = false;
  bool sawCapacitance = false;
  bool sawFrequency = false;
  bool sawCapacitanceNoise = false;
  bool sawDiode = false;
  bool sawContinuity = false;
  bool sawResistance = false;
  bool sawAcVoltage = false;
  bool sawDcVoltage = false;
  bool sawAcCurrent = false;
  bool sawDcCurrent = false;
  for (const auto& frame : sweep) {
    QCOMPARE(frame.kind, ampbeam::DecodedFrame::Kind::Measurement);
    const bool isFrequency = frame.detail.find(" FREQ ") != std::string::npos;
    const bool isCapacitance = frame.detail.find(" CAP ") != std::string::npos;
    const bool isNcv = frame.detail.find(" NCV ") != std::string::npos;
    const bool isDiode = frame.detail.find(" DIODE ") != std::string::npos;
    const bool isContinuity = frame.detail.find(" CONT ") != std::string::npos;
    const bool isResistance = frame.detail.find(" RES ") != std::string::npos;
    const bool isAcVoltage = frame.detail.find(" ACV ") != std::string::npos;
    const bool isDcVoltage = frame.detail.find(" DCV ") != std::string::npos;
    const bool isAcCurrent = frame.detail.find(" ACA ") != std::string::npos;
    const bool isDcCurrent = frame.detail.find(" DCA ") != std::string::npos;
    QVERIFY(isFrequency || isCapacitance || isNcv || isDiode || isContinuity || isResistance ||
            isAcVoltage || isDcVoltage || isAcCurrent || isDcCurrent);

    // Frequency is the only function that populates the second reading (duty cycle).
    if (isFrequency) {
      QVERIFY(frame.measurement.secondary.present);
      QCOMPARE(frame.measurement.secondary.unit, ampbeam::Unit::Percent);
    } else {
      QVERIFY(!frame.measurement.secondary.present);
    }

    // Continuity and diode leave automatic ranging; every other function keeps it.
    if (isContinuity || isDiode) {
      QVERIFY(!frame.measurement.flags.autoRange);
    } else {
      QVERIFY(frame.measurement.flags.autoRange);
    }

    if (isCapacitance) {
      QCOMPARE(frame.measurement.unit, ampbeam::Unit::Nanofarad);
      if (frame.measurement.value != 0.0) {
        // Open probes still read a fraction of a nanofarad.
        sawCapacitanceNoise = true;
      }
    }
    if (isResistance) {
      QVERIFY(frame.measurement.unit == ampbeam::Unit::Ohm ||
              frame.measurement.unit == ampbeam::Unit::Megaohm);
    }
    if (isAcVoltage || isDcVoltage) {
      QVERIFY(frame.measurement.unit == ampbeam::Unit::Volt ||
              frame.measurement.unit == ampbeam::Unit::Millivolt);
    }
    if (isAcCurrent || isDcCurrent) {
      QCOMPARE(frame.measurement.unit, ampbeam::Unit::Ampere);
    }

    sawNcv = sawNcv || isNcv;
    sawCapacitance = sawCapacitance || isCapacitance;
    sawFrequency = sawFrequency || isFrequency;
    sawDiode = sawDiode || isDiode;
    sawContinuity = sawContinuity || isContinuity;
    sawResistance = sawResistance || isResistance;
    sawAcVoltage = sawAcVoltage || isAcVoltage;
    sawDcVoltage = sawDcVoltage || isDcVoltage;
    sawAcCurrent = sawAcCurrent || isAcCurrent;
    sawDcCurrent = sawDcCurrent || isDcCurrent;
  }
  QVERIFY(sawNcv);
  QVERIFY(sawCapacitance);
  QVERIFY(sawFrequency);
  QVERIFY(sawCapacitanceNoise);
  QVERIFY(sawDiode);
  QVERIFY(sawContinuity);
  QVERIFY(sawResistance);
  QVERIFY(sawAcVoltage);
  QVERIFY(sawDcVoltage);
  QVERIFY(sawAcCurrent);
  QVERIFY(sawDcCurrent);

  // One session that changed function: resistance overload, then continuity on
  // the same open probes. Continuity reports the overload status as well and
  // clears the AUTO bit.
  const auto resistanceContinuity =
      decodeFixture(QStringLiteral("meter-hardware-resistance-continuity.json"));
  QCOMPARE(resistanceContinuity.size(), static_cast<std::size_t>(13));
  bool sawResistanceOverload = false;
  bool sawContinuityOverload = false;
  for (const auto& frame : resistanceContinuity) {
    QCOMPARE(frame.kind, ampbeam::DecodedFrame::Kind::Measurement);
    QVERIFY(frame.measurement.flags.overload);
    QVERIFY(std::isnan(frame.measurement.value));
    if (frame.detail.find(" RES ") != std::string::npos) {
      sawResistanceOverload = true;
      QCOMPARE(frame.measurement.unit, ampbeam::Unit::Megaohm);
      QVERIFY(frame.measurement.flags.autoRange);
    } else if (frame.detail.find(" CONT ") != std::string::npos) {
      sawContinuityOverload = true;
      QCOMPARE(frame.measurement.quantity, ampbeam::Quantity::Continuity);
      QVERIFY(!frame.measurement.flags.autoRange);
    }
  }
  QVERIFY(sawResistanceOverload);
  QVERIFY(sawContinuityOverload);

  // NCV next to a live conductor at two distances: the magnitude is a field
  // level, not a scaled reading. Level 0 (no field, display `EF`) comes from the
  // function sweep; this fixture holds levels 1 and 2.
  const auto ncvLevels = decodeFixture(QStringLiteral("meter-hardware-ncv-levels.json"));
  QCOMPARE(ncvLevels.size(), static_cast<std::size_t>(60));
  bool sawLevelOne = false;
  bool sawLevelTwo = false;
  for (const auto& frame : ncvLevels) {
    QCOMPARE(frame.kind, ampbeam::DecodedFrame::Kind::Measurement);
    QCOMPARE(frame.measurement.quantity, ampbeam::Quantity::Unknown);
    QCOMPARE(frame.measurement.unit, ampbeam::Unit::None);
    QVERIFY(!frame.measurement.secondary.present);
    QVERIFY(frame.detail.find(" NCV ") != std::string::npos);
    if (qFuzzyCompare(frame.measurement.value, 1.0)) {
      sawLevelOne = true;
    } else if (qFuzzyCompare(frame.measurement.value, 2.0)) {
      sawLevelTwo = true;
    } else {
      QFAIL("unexpected field level");
    }
  }
  QVERIFY(sawLevelOne);
  QVERIFY(sawLevelTwo);
}

void CoreTest::meterRangeSweepFixture() {
  const auto frames = loadDecodedFixture(QStringLiteral("meter-hardware-range-sweep.json"));
  QCOMPARE(frames.size(), static_cast<std::size_t>(394));

  bool sawOhm = false;
  bool sawKiloohm = false;
  bool sawMegaohm = false;
  bool sawNanofarad = false;
  bool sawHertz = false;
  bool sawOverload = false;
  bool sawSecondary = false;
  for (const auto& frame : frames) {
    QCOMPARE(frame.kind, ampbeam::DecodedFrame::Kind::Measurement);
    sawOhm = sawOhm || frame.measurement.unit == ampbeam::Unit::Ohm;
    sawKiloohm = sawKiloohm || frame.measurement.unit == ampbeam::Unit::Kiloohm;
    sawMegaohm = sawMegaohm || frame.measurement.unit == ampbeam::Unit::Megaohm;
    sawNanofarad = sawNanofarad || frame.measurement.unit == ampbeam::Unit::Nanofarad;
    sawHertz = sawHertz || frame.measurement.unit == ampbeam::Unit::Hertz;
    sawOverload = sawOverload || frame.measurement.flags.overload;
    sawSecondary = sawSecondary || frame.measurement.secondary.present;
  }
  QVERIFY(sawOhm);
  QVERIFY(sawKiloohm);
  QVERIFY(sawMegaohm);
  QVERIFY(sawNanofarad);
  QVERIFY(sawHertz);
  QVERIFY(sawOverload);
  QVERIFY(sawSecondary);
}

void CoreTest::meterScaleSweepFixture() {
  const auto frames = loadDecodedFixture(QStringLiteral("meter-hardware-scale-sweep.json"));
  QCOMPARE(frames.size(), static_cast<std::size_t>(530));

  bool sawDcCurrentDp0 = false;
  bool sawNegativeCurrent = false;
  bool sawKilohertz = false;
  bool sawMegahertz = false;
  bool sawMillivolt = false;
  bool sawAcCurrent = false;
  bool sawNcvLevel[5] = {false, false, false, false, false};
  for (const auto& frame : frames) {
    QCOMPARE(frame.kind, ampbeam::DecodedFrame::Kind::Measurement);
    const auto& m = frame.measurement;
    if (m.quantity == ampbeam::Quantity::Current && m.unit == ampbeam::Unit::Ampere &&
        m.currentType == ampbeam::CurrentType::DC &&
        frame.detail.find("dp=0") != std::string::npos) {
      sawDcCurrentDp0 = true;
    }
    sawNegativeCurrent =
        sawNegativeCurrent || (m.quantity == ampbeam::Quantity::Current && m.value < 0.0);
    sawKilohertz = sawKilohertz || m.unit == ampbeam::Unit::Kilohertz;
    sawMegahertz = sawMegahertz || m.unit == ampbeam::Unit::Megahertz;
    sawMillivolt = sawMillivolt || m.unit == ampbeam::Unit::Millivolt;
    sawAcCurrent = sawAcCurrent || m.currentType == ampbeam::CurrentType::AC;
    if (m.quantity == ampbeam::Quantity::Unknown && m.unit == ampbeam::Unit::None &&
        frame.detail.find(" NCV ") != std::string::npos) {
      const auto level = static_cast<int>(std::lround(m.value));
      if (level >= 0 && level <= 4) {
        sawNcvLevel[level] = true;
      }
    }
  }
  QVERIFY(sawDcCurrentDp0);
  QVERIFY(sawNegativeCurrent);
  QVERIFY(sawKilohertz);
  QVERIFY(sawMegahertz);
  QVERIFY(sawMillivolt);
  QVERIFY(sawAcCurrent);
  for (const bool level : sawNcvLevel) {
    QVERIFY(level);
  }

  // State bit 5 means MinMax view (confirmed by the MinMax fixture below);
  // this block was captured while that view was open.
  QFile file(QStringLiteral(AMPBEAM_FIXTURE_DIR) +
             QStringLiteral("/meter-hardware-scale-sweep.json"));
  QVERIFY(file.open(QIODevice::ReadOnly));
  const QJsonArray raw =
      QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("frames")).toArray();
  int minMaxFrames = 0;
  for (const auto& value : raw) {
    const auto parsed =
        ampbeam::fromHex(value.toObject().value(QStringLiteral("hex")).toString().toStdString());
    if (!parsed.has_value()) {
      continue;
    }
    const auto decoded = ampbeam::meterDecodeFrame(*parsed);
    QVERIFY(decoded.has_value());
    if (ampbeam::meterHasState(decoded->state, ampbeam::MeterState::MinMax)) {
      ++minMaxFrames;
    }
  }
  QCOMPARE(minMaxFrames, 18);
}

void CoreTest::meterMinMaxFixture() {
  // Real capture from hardware: MinMax view in the DC mV range. The live
  // reading moved between 0.075 and 0.130 mV while the meter reported
  // MAX = 9.371 mV; the operator confirmed that display value. Every frame
  // carries state bit 5 and a second, non-duplicated measurement.
  QFile file(QStringLiteral(AMPBEAM_FIXTURE_DIR) +
             QStringLiteral("/meter-hardware-minmax-millivolt.json"));
  QVERIFY(file.open(QIODevice::ReadOnly));
  const QJsonArray raw =
      QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("frames")).toArray();
  QCOMPARE(raw.size(), 94);

  const ampbeam::MeterDecoder decoder;
  bool sawLiveChange = false;
  double firstLive = 0.0;
  for (const auto& value : raw) {
    const auto parsed =
        ampbeam::fromHex(value.toObject().value(QStringLiteral("hex")).toString().toStdString());
    QVERIFY(parsed.has_value());
    const auto frame = ampbeam::meterDecodeFrame(*parsed);
    QVERIFY(frame.has_value());
    QCOMPARE(frame->state, 0x20U);
    QVERIFY(ampbeam::meterHasState(frame->state, ampbeam::MeterState::MinMax));
    QCOMPARE(frame->primary.unit, ampbeam::Unit::Millivolt);
    QVERIFY(frame->primary.value >= 0.075 && frame->primary.value <= 0.130);
    QVERIFY(frame->secondary.has_value());
    QCOMPARE(frame->secondary->unit, ampbeam::Unit::Millivolt);
    QVERIFY(qFuzzyCompare(frame->secondary->value, 9.371));
    if (firstLive == 0.0) {
      firstLive = frame->primary.value;
    } else if (frame->primary.value != firstLive) {
      sawLiveChange = true;
    }

    const auto decoded = decoder.decode(*parsed);
    QCOMPARE(decoded.kind, ampbeam::DecodedFrame::Kind::Measurement);
    QVERIFY(decoded.measurement.secondary.present);
    QVERIFY(qFuzzyCompare(decoded.measurement.secondary.value, 9.371));
    QVERIFY(decoded.detail.find("states=MINMAX") != std::string::npos);
  }
  QVERIFY(sawLiveChange);
}

void CoreTest::meterMinMaxAcFixture() {
  // Real capture: AC mV in the MinMax view. The display read MIN 23.44 mV,
  // MAX 45.00 mV and AVG 27.93 mV, and the operator confirmed MAX. The payload
  // only ever carried the live value and MAX: the second value rose from
  // 32.14 mV to 45.00 mV without decreasing, and neither MIN nor AVG appeared
  // in any field. The fixed 15 byte layout has no slot left for them.
  const auto frames = loadDecodedFixture(QStringLiteral("meter-hardware-minmax-ac-millivolt.json"));
  QCOMPARE(frames.size(), static_cast<std::size_t>(73));

  double previousSecondary = 0.0;
  for (const auto& frame : frames) {
    QCOMPARE(frame.kind, ampbeam::DecodedFrame::Kind::Measurement);
    const auto& measurement = frame.measurement;
    QCOMPARE(measurement.unit, ampbeam::Unit::Millivolt);
    QCOMPARE(measurement.quantity, ampbeam::Quantity::Voltage);
    QCOMPARE(measurement.currentType, ampbeam::CurrentType::AC);
    QVERIFY(frame.detail.find("states=AUTO,MINMAX") != std::string::npos);
    QVERIFY(measurement.value >= 31.0 && measurement.value <= 45.0);
    QVERIFY(measurement.secondary.present);
    QCOMPARE(measurement.secondary.unit, ampbeam::Unit::Millivolt);
    QVERIFY(measurement.secondary.value + 1e-9 >= previousSecondary);
    QVERIFY(!qFuzzyCompare(measurement.secondary.value, 23.44));
    QVERIFY(!qFuzzyCompare(measurement.secondary.value, 27.93));
    previousSecondary = measurement.secondary.value;
  }
  QVERIFY(qFuzzyCompare(previousSecondary, 45.0));
}

void CoreTest::meterFrequencyVoltageFixture() {
  // Real capture: the Hz/V view in AC mV. Frequency is the primary value and
  // the AC voltage sits in the second pair. The gear words stay 0xF011A2
  // (decimal point 2, frequency) and 0xF0105A (decimal point 2, milli, ACV),
  // and the state word is AUTO.
  const auto frames = loadDecodedFixture(QStringLiteral("meter-hardware-freq-ac-millivolt.json"));
  QCOMPARE(frames.size(), static_cast<std::size_t>(114));

  for (const auto& frame : frames) {
    QCOMPARE(frame.kind, ampbeam::DecodedFrame::Kind::Measurement);
    const auto& measurement = frame.measurement;
    QCOMPARE(measurement.quantity, ampbeam::Quantity::Frequency);
    QCOMPARE(measurement.unit, ampbeam::Unit::Hertz);
    QVERIFY(measurement.value >= 49.98 && measurement.value <= 49.99);
    QVERIFY(measurement.flags.autoRange);
    QVERIFY(!measurement.flags.hold);
    QVERIFY(measurement.secondary.present);
    QCOMPARE(measurement.secondary.quantity, ampbeam::Quantity::Voltage);
    QCOMPARE(measurement.secondary.unit, ampbeam::Unit::Millivolt);
    QVERIFY(measurement.secondary.value >= 32.78 && measurement.secondary.value <= 32.85);
    QVERIFY(frame.detail.find("FREQ") != std::string::npos);
    QVERIFY(frame.detail.find("ACV") != std::string::npos);
    QVERIFY(frame.detail.find("states=AUTO") != std::string::npos);
  }
}

void CoreTest::meterRelativeFixture() {
  // Real capture: REL mode in the DC mV range. The display showed the relative
  // value ≈2.58 mV and a stored reference of 2.432 mV. The frame carries state
  // bit 1 (REL) and the reference in the second pair; on the wire that reference
  // has the negative sign bit set, so the decoder reports -2.432 mV.
  const auto frames = loadDecodedFixture(QStringLiteral("meter-hardware-relative-millivolt.json"));
  QCOMPARE(frames.size(), static_cast<std::size_t>(96));

  for (const auto& frame : frames) {
    QCOMPARE(frame.kind, ampbeam::DecodedFrame::Kind::Measurement);
    const auto& measurement = frame.measurement;
    QCOMPARE(measurement.unit, ampbeam::Unit::Millivolt);
    QCOMPARE(measurement.quantity, ampbeam::Quantity::Voltage);
    QCOMPARE(measurement.currentType, ampbeam::CurrentType::DC);
    QVERIFY(measurement.flags.relative);
    QVERIFY(!measurement.flags.autoRange);
    QVERIFY(measurement.value >= 2.2 && measurement.value <= 2.7);
    QVERIFY(measurement.secondary.present);
    QCOMPARE(measurement.secondary.unit, ampbeam::Unit::Millivolt);
    QVERIFY(qFuzzyCompare(measurement.secondary.value, -2.432));
    QVERIFY(frame.detail.find("states=REL") != std::string::npos);
    QVERIFY(QString::fromStdString(ampbeam::format(measurement)).contains(QStringLiteral("[rel]")));
  }
}

void CoreTest::profileContainsExpectedUuids() {
  using namespace std::string_view_literals;

  QCOMPARE(text(ampbeam::meterBleProfile.serviceUuid),
           QStringLiteral("0000fff0-0000-1000-8000-00805f9b34fb"));
  QCOMPARE(text(ampbeam::meterBleProfile.dataCharacteristicUuid),
           QStringLiteral("0000fff4-0000-1000-8000-00805f9b34fb"));
  QCOMPARE(text(ampbeam::meterBleProfile.commandCharacteristicUuid),
           QStringLiteral("0000fff1-0000-1000-8000-00805f9b34fb"));
  QCOMPARE(text(ampbeam::meterBleProfile.keyCharacteristicUuid),
           QStringLiteral("0000fff3-0000-1000-8000-00805f9b34fb"));
  QCOMPARE(text(ampbeam::meterBleProfile.infoCharacteristicUuid),
           QStringLiteral("0000fff2-0000-1000-8000-00805f9b34fb"));
}

void CoreTest::captureParsesCliOutput() {
  const auto parsed = ampbeam::parseCaptureLog("1732180000123\t3\t00 1A 2B\traw\n"
                                               "# a comment\n"
                                               "\n"
                                               "1732180000223\t2\tFF EE\n");

  QVERIFY(parsed.warnings.empty());
  QCOMPARE(static_cast<int>(parsed.frames.size()), 2);
  QVERIFY(parsed.frames[0].receivedMs.has_value());
  QCOMPARE(*parsed.frames[0].receivedMs, std::uint64_t{1732180000123});
  QVERIFY(parsed.frames[0].data == bytes({0x00, 0x1A, 0x2B}));
  QVERIFY(parsed.frames[1].data == bytes({0xFF, 0xEE}));
}

void CoreTest::captureParsesHexAndEpochPrefix() {
  const auto parsed = ampbeam::parseCaptureLog("00:1A:2B\n1732180000123 0xFF 0xEE\n");

  QVERIFY(parsed.warnings.empty());
  QCOMPARE(static_cast<int>(parsed.frames.size()), 2);
  QVERIFY(!parsed.frames[0].receivedMs.has_value());
  QVERIFY(parsed.frames[0].data == bytes({0x00, 0x1A, 0x2B}));
  QVERIFY(parsed.frames[1].receivedMs.has_value());
  QVERIFY(parsed.frames[1].data == bytes({0xFF, 0xEE}));
}

void CoreTest::captureReportsJunkLines() {
  const auto parsed = ampbeam::parseCaptureLog("scanning for devices...\n00:1\n00 11 22 # note\n");

  QCOMPARE(static_cast<int>(parsed.frames.size()), 1);
  QCOMPARE(static_cast<int>(parsed.warnings.size()), 2);
  QVERIFY(parsed.frames[0].data == bytes({0x00, 0x11, 0x22}));
}

void CoreTest::fixtureJsonEscapesAndParses() {
  ampbeam::CaptureParseResult capture;
  capture.frames.push_back(ampbeam::CaptureFrame{std::uint64_t{42}, bytes({0x01, 0x02, 0x03})});
  capture.frames.push_back(ampbeam::CaptureFrame{std::nullopt, bytes({0xFF})});

  ampbeam::FixtureMetadata metadata;
  metadata.name = "unit \"test\"";
  metadata.createdAt = "2026-11-21T00:00:00.000Z";
  metadata.source = "line\nbreak";

  const std::string json = ampbeam::toFixtureJson(capture, metadata);
  QVERIFY(json.find("\"name\": \"unit \\\"test\\\"\"") != std::string::npos);
  QVERIFY(json.find("\"source\": \"line\\nbreak\"") != std::string::npos);
  QVERIFY(json.find("\"receivedMs\": 42") != std::string::npos);
  QVERIFY(json.find("\"hex\": \"01 02 03\"") != std::string::npos);

  const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json));
  QVERIFY(!document.isNull());
  const QJsonArray frames = document.object().value(QStringLiteral("frames")).toArray();
  QCOMPARE(static_cast<int>(frames.size()), 2);
  QCOMPARE(frames.at(0).toObject().value(QStringLiteral("hex")).toString(),
           QStringLiteral("01 02 03"));
  QVERIFY(!frames.at(1).toObject().contains(QStringLiteral("receivedMs")));
}

QTEST_GUILESS_MAIN(CoreTest)

#include "tst_core_blackbox.moc"
