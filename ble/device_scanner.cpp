// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Implementation of Bluetooth LE device discovery.
#include "device_scanner.h"

#include <QVariantMap>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace ampbeam::ble {
namespace {

/// Comma separated UUID list without the braces Qt adds around each entry.
[[nodiscard]] QString joinServices(const QList<QBluetoothUuid>& uuids) {
  QStringList out;
  out.reserve(uuids.size());
  for (const auto& uuid : uuids) {
    QString text = uuid.toString();
    text.remove(QLatin1Char('{'));
    text.remove(QLatin1Char('}'));
    out.append(text);
  }
  return out.join(QLatin1Char(','));
}

} // namespace

DeviceScanner::DeviceScanner(QObject* parent)
    : QObject(parent), m_agent(new QBluetoothDeviceDiscoveryAgent(this)) {
  connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this,
          &DeviceScanner::handleDiscovered);
  connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished, this, &DeviceScanner::handleFinished);
  connect(m_agent, &QBluetoothDeviceDiscoveryAgent::canceled, this, &DeviceScanner::handleFinished);
  connect(m_agent,
          QOverload<QBluetoothDeviceDiscoveryAgent::Error>::of(
              &QBluetoothDeviceDiscoveryAgent::errorOccurred),
          this, &DeviceScanner::handleError);
}

DeviceScanner::~DeviceScanner() {
  if (m_agent != nullptr) {
    m_agent->disconnect(this);
    m_agent->stop();
  }
}

bool DeviceScanner::isRunning() const {
  return m_running;
}

QVariantList DeviceScanner::devices() const {
  return m_model;
}

void DeviceScanner::start(int timeoutMs) {
  if (m_running) {
    return;
  }

  m_timeoutMs = std::max(timeoutMs, 1000);
  m_agent->setLowEnergyDiscoveryTimeout(m_timeoutMs);
  m_found.clear();
  rebuildModel();

  setRunning(true);
  m_agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void DeviceScanner::stop() {
  if (!m_running) {
    return;
  }
  m_agent->stop();
}

QString DeviceScanner::nameForAddress(const QString& address) const {
  const auto it = m_found.constFind(address);
  return it == m_found.cend() ? QString{} : it->name();
}

void DeviceScanner::handleDiscovered(const QBluetoothDeviceInfo& info) {
  if (!info.isValid()) {
    return;
  }

  const QString address = info.address().toString();
  if (address.isEmpty()) {
    return;
  }

  m_found.insert(address, info);
  rebuildModel();
  emit deviceDiscovered(address, info.name(), static_cast<int>(info.rssi()),
                        joinServices(info.serviceUuids()));
}

void DeviceScanner::handleFinished() {
  setRunning(false);
}

void DeviceScanner::handleError(QBluetoothDeviceDiscoveryAgent::Error error) {
  if (error == QBluetoothDeviceDiscoveryAgent::NoError) {
    return;
  }
  emit errorOccurred(m_agent->errorString());
}

void DeviceScanner::setRunning(bool running) {
  if (m_running == running) {
    return;
  }
  m_running = running;
  emit runningChanged();
}

void DeviceScanner::rebuildModel() {
  m_model.clear();
  m_model.reserve(m_found.size());

  QList<QBluetoothDeviceInfo> devices = m_found.values();
  std::sort(devices.begin(), devices.end(), [](const auto& lhs, const auto& rhs) {
    const QString left = lhs.name().isEmpty() ? lhs.address().toString() : lhs.name();
    const QString right = rhs.name().isEmpty() ? rhs.address().toString() : rhs.name();
    return left.compare(right, Qt::CaseInsensitive) < 0;
  });

  for (const auto& device : devices) {
    m_model.append(QVariantMap{
        {QStringLiteral("address"), device.address().toString()},
        {QStringLiteral("name"), device.name()},
        {QStringLiteral("rssi"), static_cast<int>(device.rssi())},
        {QStringLiteral("services"), joinServices(device.serviceUuids())},
    });
  }

  emit devicesChanged();
}

MeterSelection selectMeterFromDevices(const QVariantList& devices, const QString& serviceUuid,
                                      const QString& nameFilter) {
  std::vector<std::string> addresses;
  std::vector<std::string> names;
  std::vector<std::string> services;
  addresses.reserve(static_cast<std::size_t>(devices.size()));
  names.reserve(static_cast<std::size_t>(devices.size()));
  services.reserve(static_cast<std::size_t>(devices.size()));
  for (const QVariant& value : devices) {
    const QVariantMap device = value.toMap();
    addresses.push_back(device.value(QStringLiteral("address")).toString().toStdString());
    names.push_back(device.value(QStringLiteral("name")).toString().toStdString());
    services.push_back(device.value(QStringLiteral("services")).toString().toStdString());
  }

  std::vector<ampbeam::DeviceCandidate> candidates;
  candidates.reserve(addresses.size());
  for (std::size_t index = 0; index < addresses.size(); ++index) {
    candidates.push_back({addresses[index], names[index], services[index]});
  }

  const ampbeam::DeviceSelectionResult selection =
      ampbeam::selectMeterDevice(candidates, serviceUuid.toStdString(), nameFilter.toStdString());
  MeterSelection result;
  result.status = selection.status;
  result.address = QString::fromStdString(std::string(selection.address));
  result.name = QString::fromStdString(std::string(selection.name));
  result.candidateCount = selection.candidateCount;
  result.discoveredCount = selection.discoveredCount;
  result.matchedService = selection.matchedService;
  result.matchedName = selection.matchedName;
  return result;
}

} // namespace ampbeam::ble
