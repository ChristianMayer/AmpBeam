// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Implementation of the Bluetooth LE connection and command transport.
#include "multimeter_client.h"

#include "permissions.h"

#include "bluez_le_discovery.h"

#include "ampbeam/core/profile.hpp"

#include <QBluetoothAddress>
#include <QBluetoothDeviceInfo>
#include <QLowEnergyController>
#include <QLowEnergyDescriptor>
#include <QTimer>

#include <memory>
#include <string_view>
#include <utility>

namespace ampbeam::ble {
namespace {

[[nodiscard]] QString toQString(std::string_view text) {
  return QString::fromLatin1(text.data(), static_cast<int>(text.size()));
}

[[nodiscard]] QString compactUuid(const QBluetoothUuid& uuid) {
  QString text = uuid.toString();
  text.remove(QLatin1Char('{'));
  text.remove(QLatin1Char('}'));
  text.remove(QLatin1Char('-'));
  return text.toLower();
}

[[nodiscard]] bool uuidMatches(const QBluetoothUuid& candidate, const QString& wanted) {
  const QBluetoothUuid wantedUuid(wanted);
  if (wantedUuid.isNull()) {
    return false;
  }
  return compactUuid(candidate) == compactUuid(wantedUuid);
}

[[nodiscard]] QString serviceErrorName(QLowEnergyService::ServiceError error) {
  switch (error) {
  case QLowEnergyService::NoError:
    return QStringLiteral("no error");
  case QLowEnergyService::OperationError:
    return QStringLiteral("operation error");
  case QLowEnergyService::CharacteristicWriteError:
    return QStringLiteral("characteristic write error");
  case QLowEnergyService::DescriptorWriteError:
    return QStringLiteral("descriptor write error");
  case QLowEnergyService::UnknownError:
    return QStringLiteral("unknown error");
  case QLowEnergyService::CharacteristicReadError:
    return QStringLiteral("characteristic read error");
  case QLowEnergyService::DescriptorReadError:
    return QStringLiteral("descriptor read error");
  }
  return QStringLiteral("unknown error");
}

} // namespace

MultimeterClient::MultimeterClient(QObject* parent)
    : QObject(parent), m_serviceUuid(toQString(ampbeam::meterBleProfile.serviceUuid)),
      m_dataUuid(toQString(ampbeam::meterBleProfile.dataCharacteristicUuid)),
      m_commandUuid(toQString(ampbeam::meterBleProfile.commandCharacteristicUuid)),
      m_keyUuid(toQString(ampbeam::meterBleProfile.keyCharacteristicUuid)),
      m_infoUuid(toQString(ampbeam::meterBleProfile.infoCharacteristicUuid)) {
  m_leDiscovery = std::make_unique<BluezLeDiscovery>();
}

MultimeterClient::~MultimeterClient() {
  ++m_connectGeneration;
  if (m_controller != nullptr) {
    m_controller->disconnect(this);
    if (m_controller->state() != QLowEnergyController::UnconnectedState) {
      m_controller->disconnectFromDevice();
    }
    delete m_controller;
    m_controller = nullptr;
  }
}

MultimeterClient::State MultimeterClient::state() const {
  return m_state;
}

QString MultimeterClient::stateName() const {
  switch (m_state) {
  case State::Disconnected:
    return tr("Disconnected");
  case State::Connecting:
    return tr("Connecting");
  case State::Discovering:
    return tr("Discovering services");
  case State::Subscribing:
    return tr("Subscribing");
  case State::Streaming:
    return tr("Streaming");
  case State::Failed:
    return tr("Failed");
  }
  return tr("Unknown");
}

QString MultimeterClient::address() const {
  return m_address;
}

QString MultimeterClient::name() const {
  return m_name;
}

bool MultimeterClient::isStreaming() const {
  return m_state == State::Streaming;
}

void MultimeterClient::setGattProfile(const QString& serviceUuid, const QString& dataUuid,
                                      const QString& commandUuid, const QString& keyUuid,
                                      const QString& infoUuid) {
  m_serviceUuid = serviceUuid;
  m_dataUuid = dataUuid;
  m_commandUuid = commandUuid;
  if (!keyUuid.isEmpty()) {
    m_keyUuid = keyUuid;
  }
  m_infoUuid = infoUuid;
}

void MultimeterClient::connectToDevice(const QString& address, const QString& name) {
  if (address.trimmed().isEmpty()) {
    fail(tr("Empty device address"));
    return;
  }

  disconnectFromDevice();
  m_address = address;
  m_name = name;

  const std::uint64_t generation = ++m_connectGeneration;
  ensureBluetoothPermission(this, [this, generation](bool granted) {
    if (generation != m_connectGeneration) {
      return; // superseded by a newer request or a disconnect
    }
    if (!granted) {
      fail(tr("Bluetooth permission was not granted"));
      return;
    }
    beginConnect();
  });
}

void MultimeterClient::beginConnect() {
  if (m_controller != nullptr) {
    return;
  }

  // BlueZ 5.72 drops the `org.bluez.Device1` object of an unpaired device when
  // discovery ends, so a later connect fails with "Remote device cannot be
  // found". Hold an LE discovery open and wait until BlueZ has created the
  // object again. Best effort: without D-Bus or an adapter the normal
  // connection attempt below takes over.
  if (m_leDiscovery != nullptr && m_leDiscovery->start()) {
    pollForDiscoveredDevice(m_connectGeneration, 50);
    return;
  }

  createController();
}

void MultimeterClient::pollForDiscoveredDevice(std::uint64_t generation, int attemptsLeft) {
  if (generation != m_connectGeneration || m_controller != nullptr) {
    return;
  }
  if (m_leDiscovery == nullptr || m_leDiscovery->hasDevice(m_address) || attemptsLeft <= 0) {
    createController();
    return;
  }
  // BlueZ creates the object asynchronously after StartDiscovery; polling avoids
  // a connect that is guaranteed to fail while the object does not exist yet.
  QTimer::singleShot(100, this, [this, generation, attemptsLeft]() {
    pollForDiscoveredDevice(generation, attemptsLeft - 1);
  });
}

void MultimeterClient::createController() {
  if (m_controller != nullptr) {
    return;
  }

  const QBluetoothDeviceInfo device(QBluetoothAddress(m_address), m_name, 0);
  m_controller = QLowEnergyController::createCentral(device, this);
  if (m_controller == nullptr) {
    fail(tr("Cannot create a BLE controller for %1").arg(m_address));
    return;
  }

  connect(m_controller, &QLowEnergyController::connected, this, [this]() {
    if (m_controller == nullptr) {
      return;
    }
    if (m_leDiscovery != nullptr) {
      m_leDiscovery->stop();
    }
    setState(State::Discovering);
    m_controller->discoverServices();
  });
  connect(m_controller, &QLowEnergyController::disconnected, this,
          &MultimeterClient::handleControllerDisconnected);
  connect(m_controller, &QLowEnergyController::serviceDiscovered, this,
          &MultimeterClient::handleServiceDiscovered);
  connect(m_controller, &QLowEnergyController::discoveryFinished, this,
          &MultimeterClient::handleDiscoveryFinished);
  connect(m_controller, &QLowEnergyController::errorOccurred, this,
          [this](QLowEnergyController::Error /*error*/) {
            if (m_controller != nullptr) {
              fail(m_controller->errorString());
            }
          });

  setState(State::Connecting);
  m_controller->connectToDevice();
}

void MultimeterClient::disconnectFromDevice() {
  ++m_connectGeneration;

  if (m_leDiscovery != nullptr) {
    m_leDiscovery->stop();
  }

  if (m_controller != nullptr) {
    QLowEnergyController* controller = m_controller;
    m_controller = nullptr;
    m_service = nullptr;
    m_dataCharacteristic = {};
    m_commandCharacteristic = {};
    m_keyCharacteristic = {};
    m_infoCharacteristic = {};

    controller->disconnect(this);
    if (controller->state() != QLowEnergyController::UnconnectedState) {
      controller->disconnectFromDevice();
    }
    controller->deleteLater();
  }

  setState(State::Disconnected);
}

bool MultimeterClient::writeCharacteristic(QLowEnergyCharacteristic characteristic,
                                           const QByteArray& payload, WriteError error) {
  if (m_service == nullptr || !characteristic.isValid()) {
    emit writeFailed(error == WriteError::NoCommandCharacteristic
                         ? tr("No writable command characteristic available")
                         : tr("No writable key characteristic available"));
    return false;
  }

  // Writes go out with a response when the characteristic offers it; otherwise
  // fall back to a write without response.
  const auto properties = characteristic.properties();
  const auto mode = properties.testFlag(QLowEnergyCharacteristic::Write)
                        ? QLowEnergyService::WriteWithResponse
                        : QLowEnergyService::WriteWithoutResponse;
  m_service->writeCharacteristic(characteristic, payload, mode);
  return true;
}

bool MultimeterClient::sendCommand(const QByteArray& payload) {
  return writeCharacteristic(m_commandCharacteristic, payload, WriteError::NoCommandCharacteristic);
}

bool MultimeterClient::sendKey(const QByteArray& payload) {
  return writeCharacteristic(m_keyCharacteristic, payload, WriteError::NoKeyCharacteristic);
}

bool MultimeterClient::readCommand() {
  if (m_service == nullptr || !m_commandCharacteristic.isValid()) {
    return false;
  }
  if (!m_commandCharacteristic.properties().testFlag(QLowEnergyCharacteristic::Read)) {
    return false;
  }
  m_service->readCharacteristic(m_commandCharacteristic);
  return true;
}

bool MultimeterClient::requestInfo() {
  if (m_service == nullptr || !m_infoCharacteristic.isValid()) {
    return false;
  }
  if (!m_infoCharacteristic.properties().testFlag(QLowEnergyCharacteristic::Read)) {
    return false;
  }
  m_service->readCharacteristic(m_infoCharacteristic);
  return true;
}

void MultimeterClient::handleServiceDiscovered(const QBluetoothUuid& uuid) {
  if (m_service != nullptr || m_controller == nullptr) {
    return;
  }
  if (!uuidMatches(uuid, m_serviceUuid)) {
    return;
  }

  m_service = m_controller->createServiceObject(uuid, this);
  if (m_service == nullptr) {
    fail(tr("Cannot access service %1").arg(uuid.toString()));
    return;
  }

  connect(m_service, &QLowEnergyService::stateChanged, this, &MultimeterClient::handleServiceState);
  connect(m_service, &QLowEnergyService::characteristicChanged, this,
          &MultimeterClient::handleCharacteristicChanged);
  connect(m_service, &QLowEnergyService::characteristicRead, this,
          &MultimeterClient::handleCharacteristicRead);
  connect(m_service, &QLowEnergyService::characteristicWritten, this,
          [this](const QLowEnergyCharacteristic& characteristic, const QByteArray& value) {
            if (uuidMatches(characteristic.uuid(), m_commandUuid)) {
              emit commandWritten(value);
            } else if (uuidMatches(characteristic.uuid(), m_keyUuid)) {
              emit keyWritten(value);
            }
          });
  connect(m_service, &QLowEnergyService::descriptorWritten, this,
          &MultimeterClient::handleDescriptorWritten);
  connect(m_service, &QLowEnergyService::errorOccurred, this,
          [this](QLowEnergyService::ServiceError error) {
            fail(tr("GATT service error: %1").arg(serviceErrorName(error)));
          });

  setState(State::Discovering);
  m_service->discoverDetails();
}

void MultimeterClient::handleServiceState(QLowEnergyService::ServiceState state) {
  if (state != QLowEnergyService::RemoteServiceDiscovered) {
    return;
  }

  m_dataCharacteristic = findCharacteristic(m_dataUuid);
  m_commandCharacteristic = findCharacteristic(m_commandUuid);
  m_keyCharacteristic = findCharacteristic(m_keyUuid);
  m_infoCharacteristic = findCharacteristic(m_infoUuid);

  if (!m_dataCharacteristic.isValid()) {
    fail(tr("Data characteristic %1 not found").arg(m_dataUuid));
    return;
  }
  if (!m_commandCharacteristic.isValid()) {
    // Not fatal: a notification-only session can still be useful.
    emit writeFailed(tr("Command characteristic %1 not found").arg(m_commandUuid));
  }

  const auto properties = m_dataCharacteristic.properties();
  const bool canNotify = properties.testFlag(QLowEnergyCharacteristic::Notify);
  const bool canIndicate = properties.testFlag(QLowEnergyCharacteristic::Indicate);
  if (!canNotify && !canIndicate) {
    fail(tr("Characteristic %1 is neither notifiable nor indicatable").arg(m_dataUuid));
    return;
  }

  const QLowEnergyDescriptor cccd = m_dataCharacteristic.clientCharacteristicConfiguration();
  if (!cccd.isValid()) {
    fail(tr("Characteristic %1 has no client configuration descriptor").arg(m_dataUuid));
    return;
  }

  // Identity first: the answer arrives asynchronously and is not required to
  // start streaming.
  requestInfo();

  setState(State::Subscribing);
  m_service->writeDescriptor(cccd,
                             canNotify ? QByteArray::fromHex("0100") : QByteArray::fromHex("0200"));
}

void MultimeterClient::handleCharacteristicChanged(const QLowEnergyCharacteristic& characteristic,
                                                   const QByteArray& value) {
  if (characteristic.uuid() == m_dataCharacteristic.uuid()) {
    emit notification(value);
  }
}

void MultimeterClient::handleCharacteristicRead(const QLowEnergyCharacteristic& characteristic,
                                                const QByteArray& value) {
  if (characteristic.uuid() == m_infoCharacteristic.uuid()) {
    emit infoReceived(value);
  } else if (characteristic.uuid() == m_commandCharacteristic.uuid()) {
    emit commandRead(value);
  }
}

void MultimeterClient::handleDescriptorWritten(const QLowEnergyDescriptor& descriptor,
                                               const QByteArray& value) {
  Q_UNUSED(value)
  if (descriptor == m_dataCharacteristic.clientCharacteristicConfiguration()) {
    setState(State::Streaming);
  }
}

void MultimeterClient::handleControllerDisconnected() {
  if (m_leDiscovery != nullptr) {
    m_leDiscovery->stop();
  }
  m_service = nullptr;
  m_dataCharacteristic = {};
  m_commandCharacteristic = {};
  m_keyCharacteristic = {};
  m_infoCharacteristic = {};
  setState(State::Disconnected);
}

void MultimeterClient::handleDiscoveryFinished() {
  if (m_service == nullptr) {
    fail(tr("Service %1 not found on %2").arg(m_serviceUuid, m_address));
  }
}

void MultimeterClient::setState(State state) {
  if (m_state == state) {
    return;
  }
  m_state = state;
  emit stateChanged(m_state);
}

void MultimeterClient::fail(const QString& message) {
  if (m_leDiscovery != nullptr) {
    m_leDiscovery->stop();
  }
  setState(State::Failed);
  emit errorOccurred(message);
}

QLowEnergyCharacteristic MultimeterClient::findCharacteristic(const QString& uuid) const {
  if (m_service == nullptr) {
    return {};
  }
  for (const auto& characteristic : m_service->characteristics()) {
    if (uuidMatches(characteristic.uuid(), uuid)) {
      return characteristic;
    }
  }
  return {};
}

} // namespace ampbeam::ble
