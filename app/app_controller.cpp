// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Implementation of the QML-facing application controller.
#include "app_controller.h"

#include "ble/permissions.h"

#include "ampbeam/core/hex.hpp"

#include <QDateTime>

#include <span>

AppController::AppController(QObject* parent) : QObject(parent) {
  connect(&m_scanner, &ampbeam::ble::DeviceScanner::devicesChanged, this,
          &AppController::devicesChanged);
  connect(&m_scanner, &ampbeam::ble::DeviceScanner::runningChanged, this,
          &AppController::scanningChanged);
  connect(&m_scanner, &ampbeam::ble::DeviceScanner::errorOccurred, this,
          [this](const QString& message) {
            setStatus(message);
            appendLog(QStringLiteral("scan: ") + message);
          });

  connect(&m_client, &ampbeam::ble::MultimeterClient::stateChanged, this,
          [this](ampbeam::ble::MultimeterClient::State) {
            setStatus(m_client.stateName());
            emit connectedChanged();
            emit streamingChanged();
          });
  connect(&m_client, &ampbeam::ble::MultimeterClient::notification, this,
          &AppController::handleNotification);
  connect(
      &m_client, &ampbeam::ble::MultimeterClient::infoReceived, this,
      [this](const QByteArray& payload) {
        const auto* begin = reinterpret_cast<const std::uint8_t*>(payload.constData());
        const std::span<const std::uint8_t> bytes(begin, static_cast<std::size_t>(payload.size()));
        const auto info = ampbeam::meterParseDeviceInfo(bytes);
        if (!info.has_value()) {
          appendLog(QStringLiteral("info: empty payload"));
          return;
        }
        appendLog(QStringLiteral("info: series=%1 version=%2")
                      .arg(QString::number(info->seriesId), QString::fromStdString(info->version)));
      });
  connect(&m_client, &ampbeam::ble::MultimeterClient::errorOccurred, this,
          [this](const QString& message) {
            setStatus(message);
            appendLog(QStringLiteral("error: ") + message);
          });
  connect(&m_client, &ampbeam::ble::MultimeterClient::writeFailed, this,
          [this](const QString& message) { appendLog(QStringLiteral("write: ") + message); });
}

bool AppController::scanning() const {
  return m_scanner.isRunning();
}

bool AppController::connected() const {
  const auto state = m_client.state();
  return state != ampbeam::ble::MultimeterClient::State::Disconnected &&
         state != ampbeam::ble::MultimeterClient::State::Failed;
}

bool AppController::streaming() const {
  return m_client.isStreaming();
}

QVariantList AppController::devices() const {
  return m_scanner.devices();
}

QString AppController::status() const {
  return m_status;
}

QString AppController::valueText() const {
  return m_valueText;
}

QString AppController::rawHex() const {
  return m_rawHex;
}

QString AppController::logText() const {
  return m_log.join(QLatin1Char('\n'));
}

void AppController::startScan() {
  ampbeam::ble::ensureBluetoothPermission(this, [this](bool granted) {
    if (!granted) {
      setStatus(tr("Bluetooth permission denied"));
      return;
    }
    m_scanner.start();
  });
}

void AppController::stopScan() {
  m_scanner.stop();
}

void AppController::connectToDevice(const QString& address, const QString& name) {
  setStatus(tr("Connecting to %1").arg(address));
  appendLog(QStringLiteral("connect: %1 (%2)").arg(address, name));
  m_client.connectToDevice(address, name);
}

void AppController::disconnectDevice() {
  m_framer.reset();
  m_client.disconnectFromDevice();
}

void AppController::clearLog() {
  m_log.clear();
  emit logTextChanged();
}

void AppController::handleNotification(const QByteArray& data) {
  const auto* begin = reinterpret_cast<const std::uint8_t*>(data.constData());
  const std::span<const std::uint8_t> bytes(begin, static_cast<std::size_t>(data.size()));

  const QString hex = QString::fromStdString(ampbeam::toHex(bytes, ' '));

  setRawHex(hex);
  appendLog(QStringLiteral("%1\t%2\t%3")
                .arg(QString::number(QDateTime::currentMSecsSinceEpoch()),
                     QString::number(data.size()), hex));

  // A notification may contain several frames (or a split frame).
  for (const auto& frame : m_framer.push(bytes)) {
    const auto decoded = m_decoder.decode(frame);
    if (decoded.kind == ampbeam::DecodedFrame::Kind::Measurement) {
      setValueText(QString::fromStdString(ampbeam::format(decoded.measurement)));
    }
  }
}

void AppController::setStatus(const QString& status) {
  if (m_status == status) {
    return;
  }
  m_status = status;
  emit statusChanged();
}

void AppController::setValueText(const QString& text) {
  if (m_valueText == text) {
    return;
  }
  m_valueText = text;
  emit valueTextChanged();
}

void AppController::setRawHex(const QString& hex) {
  if (m_rawHex == hex) {
    return;
  }
  m_rawHex = hex;
  emit rawHexChanged();
}

void AppController::appendLog(const QString& line) {
  m_log.append(line);
  while (m_log.size() > m_maxLogLines) {
    m_log.removeFirst();
  }
  emit logTextChanged();
}
