// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Qt Bluetooth LE connection, notification and command transport.
#pragma once

#include <QBluetoothUuid>
#include <QByteArray>
#include <QObject>
#include <QString>

#include <QLowEnergyCharacteristic>
#include <QLowEnergyService>

#include <cstdint>
#include <memory>

QT_FORWARD_DECLARE_CLASS(QLowEnergyController)
QT_FORWARD_DECLARE_CLASS(QLowEnergyDescriptor)

namespace ampbeam::ble {

class BluezLeDiscovery;

/// Connects to a multimeter, subscribes to its notification characteristic and
/// forwards raw frames.
///
/// The client is intentionally protocol agnostic: decoding happens in
/// ampbeam::FrameDecoder implementations. That keeps the transport reusable for
/// other meter families (see MultimeterClient::setGattProfile).
class MultimeterClient final : public QObject {
  Q_OBJECT

public:
  enum class State : std::uint8_t {
    Disconnected,
    Connecting,
    Discovering,
    Subscribing,
    Streaming,
    Failed,
  };
  Q_ENUM(State)

  explicit MultimeterClient(QObject* parent = nullptr);
  ~MultimeterClient() override;

  [[nodiscard]] State state() const;
  [[nodiscard]] QString stateName() const;
  [[nodiscard]] QString address() const;
  [[nodiscard]] QString name() const;
  [[nodiscard]] bool isStreaming() const;

  /// Overrides the default GATT UUIDs measured on the tested meter, e.g. for
  /// protocol exploration with another device. The key and info UUIDs are
  /// optional.
  void setGattProfile(const QString& serviceUuid, const QString& dataUuid,
                      const QString& commandUuid, const QString& keyUuid = QString{},
                      const QString& infoUuid = QString{});

public slots:
  void connectToDevice(const QString& address, const QString& name = QString{});
  void disconnectFromDevice();

  /// Writes a command to the command characteristic. Returns false if there is
  /// no writable characteristic yet.
  bool sendCommand(const QByteArray& payload);

  /// Writes a front-panel key event to the key characteristic. One write is one
  /// keystroke; see `docs/protocol.md`.
  bool sendKey(const QByteArray& payload);

  /// Reads the command characteristic, so a meter that answers writes can be
  /// observed.
  bool readCommand();

  /// Reads the device identity characteristic (model series id and firmware
  /// version). The answer is delivered through `infoReceived`.
  bool requestInfo();

signals:
  void stateChanged(ampbeam::ble::MultimeterClient::State state);
  void notification(const QByteArray& data);
  /// Payload of the info characteristic, see `ampbeam::meterParseDeviceInfo`.
  void infoReceived(const QByteArray& payload);
  void errorOccurred(const QString& message);
  void writeFailed(const QString& message);
  /// A command write was acknowledged by the ATT layer. This says nothing about
  /// whether the meter acted on the payload, but it does rule out a write that
  /// never left the stack.
  void commandWritten(const QByteArray& payload);
  /// Same for key writes on the key characteristic.
  void keyWritten(const QByteArray& payload);
  /// Answer to a command-channel read (challenge response).
  void commandRead(const QByteArray& payload);

private:
  enum class WriteError : std::uint8_t { NoCommandCharacteristic, NoKeyCharacteristic };

  bool writeCharacteristic(QLowEnergyCharacteristic characteristic, const QByteArray& payload,
                           WriteError error);
  void beginConnect();
  void createController();
  void pollForDiscoveredDevice(std::uint64_t generation, int attemptsLeft);
  void handleServiceDiscovered(const QBluetoothUuid& uuid);
  void handleServiceState(QLowEnergyService::ServiceState state);
  void handleCharacteristicChanged(const QLowEnergyCharacteristic& characteristic,
                                   const QByteArray& value);
  void handleCharacteristicRead(const QLowEnergyCharacteristic& characteristic,
                                const QByteArray& value);
  void handleDescriptorWritten(const QLowEnergyDescriptor& descriptor, const QByteArray& value);
  void handleControllerDisconnected();
  void handleDiscoveryFinished();
  void setState(State state);
  void fail(const QString& message);
  [[nodiscard]] QLowEnergyCharacteristic findCharacteristic(const QString& uuid) const;

  QLowEnergyController* m_controller = nullptr;
  QLowEnergyService* m_service = nullptr;
  QLowEnergyCharacteristic m_dataCharacteristic;
  QLowEnergyCharacteristic m_commandCharacteristic;
  QLowEnergyCharacteristic m_keyCharacteristic;
  QLowEnergyCharacteristic m_infoCharacteristic;

  QString m_address;
  QString m_name;
  QString m_serviceUuid;
  QString m_dataUuid;
  QString m_commandUuid;
  QString m_keyUuid;
  QString m_infoUuid;
  State m_state = State::Disconnected;
  std::uint64_t m_connectGeneration = 0;
  std::unique_ptr<BluezLeDiscovery> m_leDiscovery;
};

} // namespace ampbeam::ble
