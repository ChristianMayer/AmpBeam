// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Headless tests for the parts of ampbeam_ble that do not need a Bluetooth
// adapter: initial state, configuration, failure reporting and the write
// guards. The connection/discovery state machine is exercised by the bench
// suite against a real meter (tests/bench/mode_sweep.py) because Qt's BLE
// backend has no injectable test double.

#include "ble/device_scanner.h"
#include "ble/multimeter_client.h"
#include "ble/permissions.h"

#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QVariantMap>

class BleTest final : public QObject {
  Q_OBJECT

private slots:
  void scannerStartsIdle();
  void scannerIgnoresUnknownAddresses();
  void selectsMeterFromScannerModel();
  void clientStartsDisconnected();
  void clientRejectsAnEmptyAddress();
  void clientReportsMissingCharacteristics();
  void clientDisconnectsCleanly();
  void clientAcceptsAProfileOverride();
  void permissionHelperRunsOnDesktop();
};

void BleTest::scannerStartsIdle() {
  ampbeam::ble::DeviceScanner scanner;
  QVERIFY(!scanner.isRunning());
  QVERIFY(scanner.devices().isEmpty());
  QVERIFY(scanner.nameForAddress(QStringLiteral("00:11:22:33:44:55")).isEmpty());

  // Stopping an idle scanner is a documented no-op.
  scanner.stop();
  QVERIFY(!scanner.isRunning());
}

void BleTest::scannerIgnoresUnknownAddresses() {
  ampbeam::ble::DeviceScanner scanner;
  QCOMPARE(scanner.nameForAddress(QString()), QString());
  QCOMPARE(scanner.nameForAddress(QStringLiteral("not-an-address")), QString());
}

void BleTest::selectsMeterFromScannerModel() {
  const QString service = QStringLiteral("0000fff0-0000-1000-8000-00805f9b34fb");
  const QString otherService = QStringLiteral("0000180f-0000-1000-8000-00805f9b34fb");

  const auto empty = ampbeam::ble::selectMeterFromDevices(QVariantList{}, service);
  QCOMPARE(empty.status, ampbeam::DeviceSelectionStatus::None);
  QCOMPARE(empty.discoveredCount, static_cast<std::size_t>(0));

  const QVariantMap meter{{"address", "AA:BB:CC:DD:EE:01"},
                          {"name", "meter"},
                          {"rssi", -40},
                          {"services", otherService + ", " + service}};
  const QVariantMap other{{"address", "AA:BB:CC:DD:EE:02"},
                          {"name", "other"},
                          {"rssi", -70},
                          {"services", otherService}};

  const auto selected = ampbeam::ble::selectMeterFromDevices(QVariantList{meter, other}, service);
  QCOMPARE(selected.status, ampbeam::DeviceSelectionStatus::Selected);
  QCOMPARE(selected.address, QStringLiteral("AA:BB:CC:DD:EE:01"));
  QCOMPARE(selected.name, QStringLiteral("meter"));
  QCOMPARE(selected.candidateCount, static_cast<std::size_t>(1));
  QCOMPARE(selected.discoveredCount, static_cast<std::size_t>(2));
  QVERIFY(selected.matchedService);

  const QVariantMap secondMeter{
      {"address", "AA:BB:CC:DD:EE:03"}, {"name", "meter-2"}, {"rssi", -50}, {"services", service}};
  const auto multiple =
      ampbeam::ble::selectMeterFromDevices(QVariantList{meter, secondMeter}, service);
  QCOMPARE(multiple.status, ampbeam::DeviceSelectionStatus::Multiple);
  QCOMPARE(multiple.candidateCount, static_cast<std::size_t>(2));

  const auto byName = ampbeam::ble::selectMeterFromDevices(QVariantList{meter, secondMeter},
                                                           service, QStringLiteral("meter-2"));
  QCOMPARE(byName.status, ampbeam::DeviceSelectionStatus::Selected);
  QCOMPARE(byName.address, QStringLiteral("AA:BB:CC:DD:EE:03"));
  QVERIFY(byName.matchedName);
}

void BleTest::clientStartsDisconnected() {
  ampbeam::ble::MultimeterClient client;
  QCOMPARE(client.state(), ampbeam::ble::MultimeterClient::State::Disconnected);
  QCOMPARE(client.stateName(), QStringLiteral("Disconnected"));
  QVERIFY(client.address().isEmpty());
  QVERIFY(client.name().isEmpty());
  QVERIFY(!client.isStreaming());
}

void BleTest::clientRejectsAnEmptyAddress() {
  ampbeam::ble::MultimeterClient client;
  QSignalSpy stateSpy(&client, &ampbeam::ble::MultimeterClient::stateChanged);
  QSignalSpy errorSpy(&client, &ampbeam::ble::MultimeterClient::errorOccurred);

  client.connectToDevice(QStringLiteral("  "), QStringLiteral("meter"));

  QCOMPARE(client.state(), ampbeam::ble::MultimeterClient::State::Failed);
  QCOMPARE(client.stateName(), QStringLiteral("Failed"));
  QCOMPARE(stateSpy.count(), 1);
  QCOMPARE(errorSpy.count(), 1);
  QCOMPARE(errorSpy.first().first().toString(), QStringLiteral("Empty device address"));
}

void BleTest::clientReportsMissingCharacteristics() {
  ampbeam::ble::MultimeterClient client;
  QSignalSpy writeSpy(&client, &ampbeam::ble::MultimeterClient::writeFailed);

  QVERIFY(!client.sendCommand(QByteArrayLiteral("\x01\x02")));
  QCOMPARE(writeSpy.count(), 1);
  QVERIFY(writeSpy.last().first().toString().contains(QStringLiteral("command")));

  QVERIFY(!client.sendKey(QByteArrayLiteral("\x03\x01")));
  QCOMPARE(writeSpy.count(), 2);
  QVERIFY(writeSpy.last().first().toString().contains(QStringLiteral("key")));

  QVERIFY(!client.readCommand());
  QVERIFY(!client.requestInfo());
}

void BleTest::clientDisconnectsCleanly() {
  ampbeam::ble::MultimeterClient client;
  QSignalSpy stateSpy(&client, &ampbeam::ble::MultimeterClient::stateChanged);

  client.connectToDevice(QString(), QString());
  QCOMPARE(client.state(), ampbeam::ble::MultimeterClient::State::Failed);

  client.disconnectFromDevice();
  QCOMPARE(client.state(), ampbeam::ble::MultimeterClient::State::Disconnected);
  QVERIFY(stateSpy.count() >= 2);

  // Disconnecting twice does not emit another transition.
  const int transitions = stateSpy.count();
  client.disconnectFromDevice();
  QCOMPARE(stateSpy.count(), transitions);
}

void BleTest::clientAcceptsAProfileOverride() {
  ampbeam::ble::MultimeterClient client;
  client.setGattProfile(QStringLiteral("service"), QStringLiteral("data"),
                        QStringLiteral("command"), QString(), QString());
  client.setGattProfile(QStringLiteral("service"), QStringLiteral("data"),
                        QStringLiteral("command"), QStringLiteral("key"), QStringLiteral("info"));
  // Nothing observable changes until a controller exists, but the call must
  // remain side-effect free and keep the client usable.
  QCOMPARE(client.state(), ampbeam::ble::MultimeterClient::State::Disconnected);
}

void BleTest::permissionHelperRunsOnDesktop() {
  // Non-Android platforms grant synchronously; the guard for an empty callback
  // must not crash or invoke anything.
  bool called = false;
  bool granted = false;
  ampbeam::ble::ensureBluetoothPermission(nullptr, [&called, &granted](bool value) {
    called = true;
    granted = value;
  });
  QVERIFY(called);
  QVERIFY(granted);

  ampbeam::ble::ensureBluetoothPermission(nullptr, {});
  QVERIFY(called);
}

QTEST_GUILESS_MAIN(BleTest)

#include "tst_ble.moc"
