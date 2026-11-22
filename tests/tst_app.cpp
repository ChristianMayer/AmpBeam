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

#include <QMetaObject>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>

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
  QCOMPARE(parts.size(), 3);
  QCOMPARE(parts.at(1), QStringLiteral("15"));
  QCOMPARE(parts.at(2), controller.rawHex());
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

QTEST_GUILESS_MAIN(AppControllerTest)

#include "tst_app.moc"
