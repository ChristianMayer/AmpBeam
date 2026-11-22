// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Headless tests for the Qt Quick controller.
//
// The controller is deliberately thin, but the notification pipeline (raw hex,
// log ring buffer, framing and value formatting) is logic that must not regress.
// `handleNotification` is a private slot so the tests can drive it through the
// meta-object without widening the public API.

#include "app_controller.h"

#include "ampbeam/core/hex.hpp"

#include <QFile>
#include <QMetaObject>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes frame15(std::uint8_t decimalPoint, std::uint8_t gear, std::uint32_t magnitude) {
  Bytes out;
  const std::uint32_t gearWord = static_cast<std::uint32_t>(decimalPoint) |
                                 (4U << 3U) // prefix None
                                 | (static_cast<std::uint32_t>(gear) << 6U);
  const std::uint32_t valueWord = magnitude & 0x7FFFFU;
  const std::array<std::uint32_t, 5> words{gearWord, valueWord, 0, 0, 0};
  for (const std::uint32_t word : words) {
    out.push_back(static_cast<std::uint8_t>(word & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((word >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((word >> 16U) & 0xFFU));
  }
  return out;
}

QByteArray toByteArray(const Bytes& bytes) {
  return QByteArray(reinterpret_cast<const char*>(bytes.data()),
                    static_cast<qsizetype>(bytes.size()));
}

bool deliver(AppController& controller, const Bytes& bytes) {
  return QMetaObject::invokeMethod(&controller, "handleNotification", Qt::DirectConnection,
                                   Q_ARG(QByteArray, toByteArray(bytes)));
}

} // namespace

class AppControllerTest final : public QObject {
  Q_OBJECT

private slots:
  void startsIdle();
  void clearLogEmits();
  void rejectedConnectionIsReported();
  void disconnectReturnsToDisconnected();
  void notificationUpdatesValueAndRawHex();
  void splitFramesAreBufferedUntilComplete();
  void nonMeasurementFramesKeepTheLastValue();
  void identicalPayloadsDoNotRepeatNotifications();
  void logRingBufferIsBounded();
  void displayFieldsDescribeTheReading();
  void indicatorsDescribeVerifiedFlags();
  void csvExportWritesOnlyDecodedReadings();
  void captureRecordsFramedCaptureLines();
  void keyPressIsRejectedWhenIdleOrUnverified();
};

void AppControllerTest::startsIdle() {
  AppController controller;
  QCOMPARE(controller.status(), QStringLiteral("Idle"));
  QCOMPARE(controller.valueText(), QStringLiteral("—"));
  QVERIFY(controller.rawHex().isEmpty());
  QVERIFY(controller.logText().isEmpty());
  QVERIFY(!controller.scanning());
  QVERIFY(!controller.connected());
  QVERIFY(!controller.streaming());
  QVERIFY(controller.devices().isEmpty());
  QVERIFY(!controller.hasReading());
  QCOMPARE(controller.numberText(), QStringLiteral("—"));
  QVERIFY(controller.modeText().isEmpty());
  QVERIFY(controller.unitText().isEmpty());
  QVERIFY(controller.secondaryText().isEmpty());
  QVERIFY(controller.indicators().isEmpty());
  QVERIFY(controller.lastUpdateText().isEmpty());
  QVERIFY(controller.detailText().isEmpty());
  QCOMPARE(controller.readingCount(), 0);
  QVERIFY(!controller.capturing());
  QCOMPARE(controller.capturedFrames(), 0);
  QVERIFY(!controller.autoConnecting());
  QVERIFY(!controller.deviceSelectionRequired());
}

void AppControllerTest::clearLogEmits() {
  AppController controller;
  QSignalSpy spy(&controller, &AppController::logTextChanged);
  controller.clearLog();
  QCOMPARE(spy.count(), 1);
  QVERIFY(controller.logText().isEmpty());
  controller.stopScan(); // no-op while idle
}

void AppControllerTest::rejectedConnectionIsReported() {
  AppController controller;
  QSignalSpy connectedSpy(&controller, &AppController::connectedChanged);
  QSignalSpy statusSpy(&controller, &AppController::statusChanged);

  controller.connectToDevice(QString(), QStringLiteral("meter"));

  // The state transition emits "Failed" first; the detailed error message then
  // replaces it, which is what the log and status line show.
  QCOMPARE(controller.status(), QStringLiteral("Empty device address"));
  QVERIFY(!controller.connected());
  QVERIFY(connectedSpy.count() >= 1);
  QVERIFY(statusSpy.count() >= 1);
  QVERIFY(controller.logText().contains(QStringLiteral("connect:  (meter)")));
  QVERIFY(controller.logText().contains(QStringLiteral("error: Empty device address")));
}

void AppControllerTest::disconnectReturnsToDisconnected() {
  AppController controller;
  controller.connectToDevice(QString(), QString());
  QCOMPARE(controller.status(), QStringLiteral("Empty device address"));

  controller.disconnectDevice();
  QCOMPARE(controller.status(), QStringLiteral("Disconnected"));
  QVERIFY(!controller.connected());
}

void AppControllerTest::notificationUpdatesValueAndRawHex() {
  AppController controller;
  QSignalSpy valueSpy(&controller, &AppController::valueTextChanged);
  QSignalSpy rawSpy(&controller, &AppController::rawHexChanged);
  QSignalSpy logSpy(&controller, &AppController::logTextChanged);

  // Gear 0 (DCV), decimal point 3, magnitude 4998 -> 4.998 V DC.
  QVERIFY(deliver(controller, frame15(3, 0, 4998)));

  QCOMPARE(controller.valueText(), QStringLiteral("4.998 V DC"));
  QCOMPARE(controller.rawHex(), QStringLiteral("23 00 00 86 13 00 00 00 00 00 00 00 00 00 00"));
  QCOMPARE(valueSpy.count(), 1);
  QCOMPARE(rawSpy.count(), 1);
  QCOMPARE(logSpy.count(), 1);
  const QStringList parts = controller.logText().split(QLatin1Char('\t'));
  QCOMPARE(parts.size(), 4);
  QCOMPARE(parts.at(1), QStringLiteral("15"));
  QCOMPARE(parts.at(2), controller.rawHex());
  QVERIFY(parts.at(3).contains(QStringLiteral("dp=3")));
}

void AppControllerTest::splitFramesAreBufferedUntilComplete() {
  AppController controller;
  const Bytes frame = frame15(2, 0, 1234);
  const Bytes head(frame.begin(), frame.begin() + 10);
  const Bytes tail(frame.begin() + 10, frame.end());

  QVERIFY(deliver(controller, head));
  QCOMPARE(controller.valueText(), QStringLiteral("—"));

  QVERIFY(deliver(controller, tail));
  QCOMPARE(controller.valueText(), QStringLiteral("12.34 V DC"));
}

void AppControllerTest::nonMeasurementFramesKeepTheLastValue() {
  AppController controller;
  QVERIFY(deliver(controller, frame15(2, 0, 1234)));
  QCOMPARE(controller.valueText(), QStringLiteral("12.34 V DC"));

  // Oscilloscope mode (state bit 18) carries no measurement.
  Bytes frame = frame15(2, 0, 9);
  frame[12] = 0x00;
  frame[13] = 0x00;
  frame[14] = 0x04; // bit 18
  QVERIFY(deliver(controller, frame));
  QCOMPARE(controller.valueText(), QStringLiteral("12.34 V DC"));
}

void AppControllerTest::identicalPayloadsDoNotRepeatNotifications() {
  AppController controller;
  const Bytes frame = frame15(2, 0, 1234);
  QVERIFY(deliver(controller, frame));
  QSignalSpy valueSpy(&controller, &AppController::valueTextChanged);
  QSignalSpy rawSpy(&controller, &AppController::rawHexChanged);
  QVERIFY(deliver(controller, frame));
  QCOMPARE(valueSpy.count(), 0);
  QCOMPARE(rawSpy.count(), 0);
}

void AppControllerTest::logRingBufferIsBounded() {
  AppController controller;
  const Bytes frame = frame15(2, 0, 1234);
  for (int index = 0; index < 505; ++index) {
    QVERIFY(deliver(controller, frame));
  }
  const QStringList lines = controller.logText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  QCOMPARE(lines.size(), 500);
}

void AppControllerTest::displayFieldsDescribeTheReading() {
  AppController controller;
  QSignalSpy readingSpy(&controller, &AppController::readingChanged);

  // Gear 0 (DCV), decimal point 3, magnitude 4998 -> 4.998 V DC.
  QVERIFY(deliver(controller, frame15(3, 0, 4998)));

  QVERIFY(controller.hasReading());
  QCOMPARE(controller.modeText(), QStringLiteral("DCV"));
  QCOMPARE(controller.numberText(), QStringLiteral("4.998"));
  QCOMPARE(controller.unitText(), QStringLiteral("V"));
  QVERIFY(controller.secondaryText().isEmpty());
  QCOMPARE(controller.valueText(), QStringLiteral("4.998 V DC"));
  QVERIFY(!controller.lastUpdateText().isEmpty());
  QVERIFY(controller.detailText().contains(QStringLiteral("dp=3")));
  QCOMPARE(controller.readingCount(), 1);
  QVERIFY(readingSpy.count() >= 1);

  // The split pieces reassemble the single-line mirror text.
  QCOMPARE(controller.modeText() + QStringLiteral("  ") + controller.numberText() +
               QStringLiteral(" ") + controller.unitText(),
           QStringLiteral("DCV  4.998 V"));
}

void AppControllerTest::indicatorsDescribeVerifiedFlags() {
  AppController controller;

  // State word 0 with a rangeable gear means manual ranging.
  QVERIFY(deliver(controller, frame15(2, 0, 1234)));
  auto labels = [&controller]() {
    QStringList out;
    for (const QVariant& chip : controller.indicators()) {
      out.append(chip.toMap().value(QStringLiteral("label")).toString());
    }
    return out;
  };
  QCOMPARE(labels(), QStringList{QStringLiteral("MANUAL")});

  // HOLD (bit 0), REL (bit 1), AUTO (bit 2) and MinMax (bit 5).
  Bytes flagged = frame15(2, 0, 1234);
  flagged[12] = 0x27; // HOLD | REL | AUTO | MINMAX
  QVERIFY(deliver(controller, flagged));
  const QStringList flaggedLabels = labels();
  QVERIFY(flaggedLabels.contains(QStringLiteral("HOLD")));
  QVERIFY(flaggedLabels.contains(QStringLiteral("REL")));
  QVERIFY(flaggedLabels.contains(QStringLiteral("MINMAX")));
  QVERIFY(flaggedLabels.contains(QStringLiteral("AUTO")));
  QVERIFY(!flaggedLabels.contains(QStringLiteral("MANUAL")));

  // Value status 1 is the confirmed overload state.
  Bytes overloaded = frame15(2, 0, 1234);
  overloaded[5] = 0x10; // status bits 20..22 = 1
  QVERIFY(deliver(controller, overloaded));
  QCOMPARE(controller.numberText(), QStringLiteral("---"));
  QVERIFY(labels().contains(QStringLiteral("OVERLOAD")));
}

void AppControllerTest::csvExportWritesOnlyDecodedReadings() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("readings.csv"));

  AppController controller;
  QVERIFY(deliver(controller, frame15(3, 0, 4998)));

  // An oscilloscope frame carries no measurement and must not be exported.
  Bytes oscilloscope = frame15(2, 0, 9);
  oscilloscope[12] = 0x00;
  oscilloscope[13] = 0x00;
  oscilloscope[14] = 0x04; // bit 18
  QVERIFY(deliver(controller, oscilloscope));

  QCOMPARE(controller.readingCount(), 1);
  QVERIFY(controller.exportCsv(path));

  QFile file(path);
  QVERIFY(file.open(QIODevice::ReadOnly));
  const QString content = QString::fromUtf8(file.readAll());
  const QStringList lines = content.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  QCOMPARE(lines.size(), 2);
  QVERIFY(lines.at(0).startsWith(QStringLiteral("received_ms,time,mode,value,unit")));
  QVERIFY(lines.at(1).contains(QStringLiteral(",DCV,4.998,V,")));
  QVERIFY(!content.contains(QStringLiteral("?")));
}

void AppControllerTest::captureRecordsFramedCaptureLines() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("capture.log"));

  AppController controller;
  QVERIFY(!controller.capturing());
  QVERIFY(controller.startCapture(path));
  QVERIFY(controller.capturing());
  QCOMPARE(controller.capturePath(), path);

  // Two frames in one notification must produce two capture lines.
  Bytes twoFrames = frame15(3, 0, 4998);
  const Bytes second = frame15(2, 0, 1234);
  twoFrames.insert(twoFrames.end(), second.begin(), second.end());
  QVERIFY(deliver(controller, twoFrames));

  controller.stopCapture();
  QVERIFY(!controller.capturing());
  QCOMPARE(controller.capturedFrames(), 2);

  QFile file(path);
  QVERIFY(file.open(QIODevice::ReadOnly));
  const QStringList lines =
      QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  QCOMPARE(lines.size(), 2);
  for (const QString& line : lines) {
    const QStringList parts = line.split(QLatin1Char('\t'));
    QCOMPARE(parts.size(), 4);
    QCOMPARE(parts.at(1), QStringLiteral("15"));
    QVERIFY(parts.at(3).contains(QStringLiteral("dp=")));
  }
}

void AppControllerTest::keyPressIsRejectedWhenIdleOrUnverified() {
  AppController controller;

  // No connection means no writable key characteristic; this must be reported,
  // not silently dropped.
  QVERIFY(!controller.pressKey(2));
  QVERIFY(controller.logText().contains(QStringLiteral("key")));

  // Only codes 1..3 have a verified effect on the tested meter.
  QVERIFY(!controller.pressKey(9));
  QVERIFY(controller.logText().contains(QStringLiteral("code 9")));
}

QTEST_GUILESS_MAIN(AppControllerTest)

#include "tst_app.moc"
