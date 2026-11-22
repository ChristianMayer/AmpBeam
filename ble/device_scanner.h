// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Qt Bluetooth LE device discovery.
#pragma once

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QHash>
#include <QObject>
#include <QVariantList>

#include "ampbeam/core/selection.hpp"

#include <cstddef>

namespace ampbeam::ble {

/// Discovers BLE devices and exposes them to QML/CLI code.
///
/// On Android the address is the device MAC; on Linux it is the BlueZ address.
/// Both are accepted by MultimeterClient::connectToDevice().
class DeviceScanner final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
  Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)

public:
  explicit DeviceScanner(QObject* parent = nullptr);
  ~DeviceScanner() override;

  [[nodiscard]] bool isRunning() const;
  [[nodiscard]] QVariantList devices() const;

  /// Starts a low-energy scan. The agent stops itself after `timeoutMs`.
  void start(int timeoutMs = 12000);
  void stop();

  [[nodiscard]] QString nameForAddress(const QString& address) const;

signals:
  void runningChanged();
  void devicesChanged();
  /// `services` is a comma separated list of the advertised service UUIDs.
  void deviceDiscovered(const QString& address, const QString& name, int rssi,
                        const QString& services);
  void errorOccurred(const QString& message);

private:
  void handleDiscovered(const QBluetoothDeviceInfo& info);
  void handleFinished();
  void handleError(QBluetoothDeviceDiscoveryAgent::Error error);
  void setRunning(bool running);
  void rebuildModel();

  QBluetoothDeviceDiscoveryAgent* m_agent = nullptr;
  QHash<QString, QBluetoothDeviceInfo> m_found;
  QVariantList m_model;
  bool m_running = false;
  int m_timeoutMs = 12000;
};

/// Owned form of `ampbeam::DeviceSelectionResult` for a scanner's device model.
struct MeterSelection {
  ampbeam::DeviceSelectionStatus status = ampbeam::DeviceSelectionStatus::None;
  QString address;
  QString name;
  std::size_t candidateCount = 0;
  std::size_t discoveredCount = 0;
  bool matchedService = false;
  bool matchedName = false;
};

/// Applies the core selection algorithm to `DeviceScanner::devices()` entries.
///
/// Both front-ends use this so the QVariant-to-candidate translation and the
/// "never guess between several meters" policy live in one place.
[[nodiscard]] MeterSelection selectMeterFromDevices(const QVariantList& devices,
                                                    const QString& serviceUuid,
                                                    const QString& nameFilter = {});

} // namespace ampbeam::ble
