// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Implementation of the QML-facing application controller.
#include "app_controller.h"

#include "ble/permissions.h"

#include "ampbeam/core/capture.hpp"
#include "ampbeam/core/csv.hpp"
#include "ampbeam/core/hex.hpp"
#include "ampbeam/core/profile.hpp"
#include "ampbeam/core/selection.hpp"

#include <QDateTime>
#include <QStringConverter>
#include <QTextStream>
#include <QUrl>
#include <QVariantMap>

#include <span>

AppController::AppController(QObject* parent) : QObject(parent) {
  m_retryTimer.setSingleShot(true);
  connect(&m_retryTimer, &QTimer::timeout, this, &AppController::beginAutoScan);

  connect(&m_scanner, &ampbeam::ble::DeviceScanner::devicesChanged, this,
          &AppController::devicesChanged);
  connect(&m_scanner, &ampbeam::ble::DeviceScanner::runningChanged, this,
          &AppController::handleScannerRunningChanged);
  connect(&m_scanner, &ampbeam::ble::DeviceScanner::errorOccurred, this,
          [this](const QString& message) {
            setStatus(message);
            appendLog(QStringLiteral("scan: ") + message);
            if (m_autoMode && !m_retryTimer.isActive()) {
              scheduleReconnect(tr("Bluetooth scan failed"));
            }
          });

  connect(&m_client, &ampbeam::ble::MultimeterClient::stateChanged, this,
          &AppController::handleClientState);
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
        setDeviceInfo(QString::number(info->seriesId), QString::fromStdString(info->version));
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
  connect(
      &m_client, &ampbeam::ble::MultimeterClient::keyWritten, this,
      [this](const QByteArray& payload) {
        appendLog(
            QStringLiteral("key: acknowledged %1").arg(QString::fromLatin1(payload.toHex(' '))));
      });
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

bool AppController::autoConnecting() const {
  return m_autoConnecting;
}

bool AppController::deviceSelectionRequired() const {
  return m_selectionRequired;
}

bool AppController::hasReading() const {
  return m_hasReading;
}

QVariantList AppController::devices() const {
  return m_scanner.devices();
}

QString AppController::status() const {
  return m_status;
}

QString AppController::deviceName() const {
  return m_deviceName;
}

QString AppController::deviceAddress() const {
  return m_deviceAddress;
}

QString AppController::modeText() const {
  return m_modeText;
}

QString AppController::numberText() const {
  return m_numberText;
}

QString AppController::unitText() const {
  return m_unitText;
}

QString AppController::secondaryText() const {
  return m_secondaryText;
}

QVariantList AppController::indicators() const {
  return m_indicators;
}

QString AppController::valueText() const {
  return m_valueText;
}

QString AppController::rawHex() const {
  return m_rawHex;
}

QString AppController::detailText() const {
  return m_detailText;
}

QString AppController::lastUpdateText() const {
  return m_lastUpdateText;
}

QString AppController::logText() const {
  return m_log.join(QLatin1Char('\n'));
}

QString AppController::deviceSeriesText() const {
  return m_seriesText;
}

QString AppController::deviceVersionText() const {
  return m_versionText;
}

bool AppController::capturing() const {
  return m_captureFile.isOpen();
}

QString AppController::capturePath() const {
  return m_capturePath;
}

int AppController::capturedFrames() const {
  return m_capturedFrames;
}

int AppController::readingCount() const {
  return static_cast<int>(m_readings.size());
}

void AppController::startScan() {
  // A manual scan always wins over an automatic search.
  m_autoMode = false;
  m_autoScan = false;
  m_retryTimer.stop();
  setAutoConnecting(false);
  setDeviceSelectionRequired(false);
  ampbeam::ble::ensureBluetoothPermission(this, [this](bool granted) {
    if (!granted) {
      setStatus(tr("Bluetooth permission denied"));
      appendLog(QStringLiteral("scan: Bluetooth permission denied"));
      return;
    }
    setStatus(tr("Scanning for BLE devices..."));
    m_scanner.start(m_manualScanTimeoutMs);
  });
}

void AppController::stopScan() {
  m_autoScan = false;
  m_scanner.stop();
  // Closing the device picker must not silently end an armed automatic search.
  if (m_autoMode && !connected()) {
    scheduleReconnect(tr("scan stopped"));
  }
}

void AppController::connectAutomatically() {
  if (connected() || m_client.isStreaming()) {
    return;
  }
  m_autoMode = true;
  m_reconnect.reset();
  m_retryTimer.stop();
  setDeviceSelectionRequired(false);
  beginAutoScan();
}

void AppController::stopAutoConnect() {
  m_autoMode = false;
  m_autoScan = false;
  m_retryTimer.stop();
  setAutoConnecting(false);
  if (m_scanner.isRunning()) {
    m_scanner.stop();
  }
  if (!connected()) {
    setStatus(tr("Idle"));
  }
}

void AppController::connectToDevice(const QString& address, const QString& name) {
  m_autoMode = false;
  m_autoScan = false;
  m_retryTimer.stop();
  setAutoConnecting(false);
  setDeviceSelectionRequired(false);
  if (m_scanner.isRunning()) {
    m_scanner.stop();
  }
  connectToDeviceNow(address, name, false);
}

void AppController::disconnectDevice() {
  m_autoMode = false;
  m_autoScan = false;
  m_retryTimer.stop();
  setAutoConnecting(false);
  setDeviceSelectionRequired(false);
  m_framer.reset();
  m_streamingTimer.invalidate();
  m_client.disconnectFromDevice();
}

bool AppController::pressKey(int code) {
  if (code < 1 || code > 3) {
    appendLog(tr("key: code %1 is not a verified front-panel key").arg(code));
    return false;
  }
  if (!m_client.isStreaming()) {
    appendLog(tr("key: not streaming; connect before sending a key"));
    return false;
  }
  // The meter occasionally drops a key write sent the instant notifications
  // begin; a short settle delay avoids that (docs/protocol.md §7.1).
  if (m_streamingTimer.isValid() && m_streamingTimer.elapsed() < 500) {
    QTimer::singleShot(300, this, [this, code]() {
      if (!sendKeyNow(code)) {
        appendLog(tr("key: write failed for code %1").arg(code));
      }
    });
    return true;
  }
  return sendKeyNow(code);
}

bool AppController::sendKeyNow(int code) {
  const auto payload = ampbeam::meterKeyCommand(static_cast<std::uint8_t>(code), true);
  const QByteArray data(reinterpret_cast<const char*>(payload.data()),
                        static_cast<qsizetype>(payload.size()));
  if (!m_client.sendKey(data)) {
    return false;
  }
  appendLog(QStringLiteral("key: sent %1").arg(code));
  return true;
}

void AppController::clearLog() {
  m_log.clear();
  emit logTextChanged();
}

bool AppController::exportCsv(const QString& pathOrUrl) {
  const QString path = localPath(pathOrUrl);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    appendLog(tr("csv: cannot write %1").arg(path));
    setStatus(tr("Cannot write %1").arg(path));
    return false;
  }

  QTextStream stream(&file);
  stream.setEncoding(QStringConverter::Utf8);
  stream << QString::fromStdString(ampbeam::csvHeader()) << '\n';
  for (const auto& measurement : m_readings) {
    const QString iso =
        QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(measurement.receivedAtMs))
            .toUTC()
            .toString(Qt::ISODateWithMs);
    stream << QString::fromStdString(ampbeam::csvRow(measurement, iso.toStdString())) << '\n';
  }
  stream.flush();
  file.close();

  appendLog(tr("csv: wrote %1 reading(s) to %2").arg(m_readings.size()).arg(path));
  setStatus(tr("Exported %1 reading(s)").arg(m_readings.size()));
  return true;
}

bool AppController::startCapture(const QString& pathOrUrl) {
  stopCapture();
  const QString path = localPath(pathOrUrl);
  m_captureFile.setFileName(path);
  if (!m_captureFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    appendLog(tr("capture: cannot write %1").arg(path));
    setStatus(tr("Cannot write %1").arg(path));
    return false;
  }
  m_capturePath = path;
  m_capturedFrames = 0;
  emit captureChanged();
  appendLog(tr("capture: writing to %1").arg(path));
  return true;
}

void AppController::stopCapture() {
  if (!m_captureFile.isOpen()) {
    return;
  }
  m_captureFile.flush();
  m_captureFile.close();
  appendLog(tr("capture: wrote %1 frame(s) to %2").arg(m_capturedFrames).arg(m_capturePath));
  emit captureChanged();
}

void AppController::handleNotification(const QByteArray& data) {
  const auto* begin = reinterpret_cast<const std::uint8_t*>(data.constData());
  const std::span<const std::uint8_t> bytes(begin, static_cast<std::size_t>(data.size()));

  const QString hex = QString::fromStdString(ampbeam::toHex(bytes, ' '));
  setRawHex(hex);

  const qint64 receivedAtMs = QDateTime::currentMSecsSinceEpoch();
  // A notification may contain several frames (or a split frame).
  for (const auto& frame : m_framer.push(bytes)) {
    const auto decoded = m_decoder.decode(frame);
    const QString detail = QString::fromStdString(decoded.detail);
    const QString line = QString::fromStdString(ampbeam::captureLine(
        static_cast<std::uint64_t>(qMax<qint64>(receivedAtMs, 0)), frame, decoded.detail));
    writeCaptureLine(line);
    appendLog(line);
    setDetailText(detail);
    if (decoded.kind == ampbeam::DecodedFrame::Kind::Measurement) {
      applyMeasurement(decoded.measurement);
    }
  }
}

void AppController::handleScannerRunningChanged() {
  emit scanningChanged();
  if (!m_scanner.isRunning() && m_autoScan) {
    m_autoScan = false;
    handleAutoScanFinished();
  }
}

void AppController::handleAutoScanFinished() {
  if (!m_autoMode) {
    return;
  }
  const auto state = m_client.state();
  if (state != ampbeam::ble::MultimeterClient::State::Disconnected &&
      state != ampbeam::ble::MultimeterClient::State::Failed) {
    return;
  }

  const QString serviceUuid =
      QString::fromLatin1(ampbeam::meterBleProfile.serviceUuid.data(),
                          static_cast<int>(ampbeam::meterBleProfile.serviceUuid.size()));
  const ampbeam::ble::MeterSelection selection =
      ampbeam::ble::selectMeterFromDevices(m_scanner.devices(), serviceUuid);
  if (selection.status == ampbeam::DeviceSelectionStatus::Selected) {
    appendLog(
        tr("auto: selected %1 (%2)")
            .arg(selection.name.isEmpty() ? selection.address : selection.name, selection.address));
    connectToDeviceNow(selection.address, selection.name, true);
    return;
  }

  if (selection.status == ampbeam::DeviceSelectionStatus::Multiple) {
    m_autoMode = false;
    setAutoConnecting(false);
    setDeviceSelectionRequired(true);
    setStatus(tr("Several BLE devices found - choose a meter in Devices"));
    appendLog(tr("auto: %1 device(s) are possible meters; choose one explicitly")
                  .arg(selection.candidateCount));
    return;
  }

  scheduleReconnect(tr("no BLE device found"));
}

void AppController::beginAutoScan() {
  if (!m_autoMode) {
    return;
  }
  const auto state = m_client.state();
  if (state != ampbeam::ble::MultimeterClient::State::Disconnected &&
      state != ampbeam::ble::MultimeterClient::State::Failed) {
    return;
  }

  setAutoConnecting(true);
  setDeviceSelectionRequired(false);
  setStatus(tr("Searching for a meter..."));
  if (m_scanner.isRunning()) {
    m_autoScan = true;
    return;
  }

  m_autoScan = true;
  ampbeam::ble::ensureBluetoothPermission(this, [this](bool granted) {
    if (!granted) {
      m_autoMode = false;
      m_autoScan = false;
      setAutoConnecting(false);
      setStatus(tr("Bluetooth permission denied"));
      appendLog(QStringLiteral("auto: Bluetooth permission denied"));
      return;
    }
    if (m_autoMode) {
      m_scanner.start(m_autoScanTimeoutMs);
    }
  });
}

void AppController::connectToDeviceNow(const QString& address, const QString& name,
                                       bool automatic) {
  m_retryTimer.stop();
  setAutoConnecting(false);
  setDeviceSelectionRequired(false);
  m_framer.reset();
  m_streamingTimer.invalidate();
  m_deviceAddress = address;
  m_deviceName = name;
  emit deviceChanged();
  setDeviceInfo(QStringLiteral("—"), QStringLiteral("—"));
  setStatus(tr("Connecting to %1").arg(name.isEmpty() ? address : name));
  appendLog(QStringLiteral("connect: %1 (%2)%3")
                .arg(address, name, automatic ? QStringLiteral(" [auto]") : QString()));
  m_client.connectToDevice(address, name);
}

void AppController::scheduleReconnect(const QString& reason) {
  if (!m_autoMode || m_retryTimer.isActive()) {
    return;
  }
  const int delayMs = m_reconnect.nextDelayMs();
  const qint64 seconds = static_cast<qint64>(delayMs / 1000);
  setAutoConnecting(true);
  setStatus(tr("%1 - retrying in %2 s").arg(reason, QString::number(seconds)));
  appendLog(tr("auto: %1; retrying in %2 s").arg(reason, QString::number(seconds)));
  m_retryTimer.start(delayMs);
}

void AppController::handleClientState(ampbeam::ble::MultimeterClient::State state) {
  setStatus(m_client.stateName());
  emit connectedChanged();
  emit streamingChanged();

  switch (state) {
  case ampbeam::ble::MultimeterClient::State::Streaming:
    m_reconnect.reset();
    m_wasStreaming = true;
    m_streamingTimer.start();
    setAutoConnecting(false);
    setDeviceSelectionRequired(false);
    m_framer.reset();
    appendLog(QStringLiteral("streaming"));
    break;
  case ampbeam::ble::MultimeterClient::State::Failed:
    if (m_autoMode) {
      scheduleReconnect(tr("connection failed"));
    }
    m_wasStreaming = false;
    break;
  case ampbeam::ble::MultimeterClient::State::Disconnected:
    if (m_wasStreaming && m_autoMode) {
      scheduleReconnect(tr("connection lost"));
    }
    m_wasStreaming = false;
    m_streamingTimer.invalidate();
    m_framer.reset();
    break;
  case ampbeam::ble::MultimeterClient::State::Connecting:
  case ampbeam::ble::MultimeterClient::State::Discovering:
  case ampbeam::ble::MultimeterClient::State::Subscribing:
    break;
  }
}

void AppController::applyMeasurement(const ampbeam::Measurement& measurement) {
  ampbeam::Measurement stored = measurement;
  stored.receivedAtMs = static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch());
  m_readings.append(stored);
  emit readingCountChanged();

  setModeText(QString::fromStdString(ampbeam::monitorModeName(stored)));
  setNumberText(QString::fromStdString(ampbeam::monitorValueText(stored)));
  setUnitText(QString::fromStdString(ampbeam::monitorUnitText(stored)));
  setSecondaryText(QString::fromStdString(ampbeam::monitorSecondaryText(stored)));
  setValueText(QString::fromStdString(ampbeam::format(stored)));

  QVariantList indicators;
  const auto add = [&indicators](const QString& label, const QString& tone) {
    indicators.append(
        QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("tone"), tone}});
  };
  if (stored.flags.overload) {
    add(tr("OVERLOAD"), QStringLiteral("warn"));
  }
  if (stored.flags.hold) {
    add(tr("HOLD"), QStringLiteral("hold"));
  }
  if (stored.flags.relative) {
    add(tr("REL"), QStringLiteral("accent"));
  }
  if (stored.flags.minMax) {
    add(tr("MINMAX"), QStringLiteral("accent"));
  }
  if (stored.flags.autoRange) {
    add(tr("AUTO"), QStringLiteral("neutral"));
  } else if (ampbeam::monitorGearHasRanges(stored.gear)) {
    add(tr("MANUAL"), QStringLiteral("muted"));
  }
  if (m_indicators != indicators) {
    m_indicators = indicators;
    emit readingChanged();
  }

  if (!m_hasReading) {
    m_hasReading = true;
    emit readingChanged();
  }
  setLastUpdateText(QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(stored.receivedAtMs))
                        .toString(QStringLiteral("HH:mm:ss")));
}

void AppController::writeCaptureLine(const QString& line) {
  if (!m_captureFile.isOpen()) {
    return;
  }
  const QByteArray bytes = (line + QLatin1Char('\n')).toUtf8();
  if (m_captureFile.write(bytes) != static_cast<qint64>(bytes.size())) {
    appendLog(tr("capture: write failed"));
    stopCapture();
    return;
  }
  m_captureFile.flush();
  ++m_capturedFrames;
  emit captureChanged();
}

void AppController::setStatus(const QString& status) {
  if (m_status == status) {
    return;
  }
  m_status = status;
  emit statusChanged();
}

void AppController::setModeText(const QString& text) {
  if (m_modeText == text) {
    return;
  }
  m_modeText = text;
  emit readingChanged();
}

void AppController::setNumberText(const QString& text) {
  if (m_numberText == text) {
    return;
  }
  m_numberText = text;
  emit readingChanged();
}

void AppController::setUnitText(const QString& text) {
  if (m_unitText == text) {
    return;
  }
  m_unitText = text;
  emit readingChanged();
}

void AppController::setSecondaryText(const QString& text) {
  if (m_secondaryText == text) {
    return;
  }
  m_secondaryText = text;
  emit readingChanged();
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

void AppController::setDetailText(const QString& text) {
  if (m_detailText == text) {
    return;
  }
  m_detailText = text;
  emit detailTextChanged();
}

void AppController::setLastUpdateText(const QString& text) {
  if (m_lastUpdateText == text) {
    return;
  }
  m_lastUpdateText = text;
  emit readingChanged();
}

void AppController::setDeviceInfo(const QString& series, const QString& version) {
  const QString seriesText = series.isEmpty() ? QStringLiteral("—") : series;
  const QString versionText = version.isEmpty() ? QStringLiteral("—") : version;
  if (m_seriesText == seriesText && m_versionText == versionText) {
    return;
  }
  m_seriesText = seriesText;
  m_versionText = versionText;
  emit deviceInfoChanged();
}

void AppController::setAutoConnecting(bool connecting) {
  if (m_autoConnecting == connecting) {
    return;
  }
  m_autoConnecting = connecting;
  emit autoConnectingChanged();
}

void AppController::setDeviceSelectionRequired(bool required) {
  if (m_selectionRequired == required) {
    return;
  }
  m_selectionRequired = required;
  emit deviceSelectionRequiredChanged();
}

void AppController::appendLog(const QString& line) {
  m_log.append(line);
  while (m_log.size() > m_maxLogLines) {
    m_log.removeFirst();
  }
  emit logTextChanged();
}

QString AppController::localPath(const QString& pathOrUrl) {
  const QUrl url(pathOrUrl);
  if (url.isLocalFile()) {
    return url.toLocalFile();
  }
  return pathOrUrl;
}
