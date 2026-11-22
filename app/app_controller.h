// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QML-facing controller exposing scan, connection and decoder state.
#pragma once

#include "ble/device_scanner.h"
#include "ble/multimeter_client.h"

#include "ampbeam/core/decoder.hpp"
#include "ampbeam/core/measurement.hpp"
#include "ampbeam/core/meter.hpp"
#include "ampbeam/core/monitor.hpp"

#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqml.h>

class AppController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
  Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
  Q_PROPERTY(bool streaming READ streaming NOTIFY streamingChanged)
  Q_PROPERTY(bool autoConnecting READ autoConnecting NOTIFY autoConnectingChanged)
  Q_PROPERTY(bool deviceSelectionRequired READ deviceSelectionRequired NOTIFY
                 deviceSelectionRequiredChanged)
  Q_PROPERTY(bool hasReading READ hasReading NOTIFY readingChanged)
  Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceChanged)
  Q_PROPERTY(QString deviceAddress READ deviceAddress NOTIFY deviceChanged)
  Q_PROPERTY(QString modeText READ modeText NOTIFY readingChanged)
  Q_PROPERTY(QString numberText READ numberText NOTIFY readingChanged)
  Q_PROPERTY(QString unitText READ unitText NOTIFY readingChanged)
  Q_PROPERTY(QString secondaryText READ secondaryText NOTIFY readingChanged)
  Q_PROPERTY(QVariantList indicators READ indicators NOTIFY readingChanged)
  Q_PROPERTY(QString valueText READ valueText NOTIFY valueTextChanged)
  Q_PROPERTY(QString rawHex READ rawHex NOTIFY rawHexChanged)
  Q_PROPERTY(QString detailText READ detailText NOTIFY detailTextChanged)
  Q_PROPERTY(QString lastUpdateText READ lastUpdateText NOTIFY readingChanged)
  Q_PROPERTY(QString logText READ logText NOTIFY logTextChanged)
  Q_PROPERTY(QString deviceSeriesText READ deviceSeriesText NOTIFY deviceInfoChanged)
  Q_PROPERTY(QString deviceVersionText READ deviceVersionText NOTIFY deviceInfoChanged)
  Q_PROPERTY(bool capturing READ capturing NOTIFY captureChanged)
  Q_PROPERTY(QString capturePath READ capturePath NOTIFY captureChanged)
  Q_PROPERTY(int capturedFrames READ capturedFrames NOTIFY captureChanged)
  Q_PROPERTY(int readingCount READ readingCount NOTIFY readingCountChanged)

public:
  explicit AppController(QObject* parent = nullptr);

  [[nodiscard]] bool scanning() const;
  [[nodiscard]] bool connected() const;
  [[nodiscard]] bool streaming() const;
  [[nodiscard]] bool autoConnecting() const;
  [[nodiscard]] bool deviceSelectionRequired() const;
  [[nodiscard]] bool hasReading() const;
  [[nodiscard]] QVariantList devices() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] QString deviceName() const;
  [[nodiscard]] QString deviceAddress() const;
  [[nodiscard]] QString modeText() const;
  [[nodiscard]] QString numberText() const;
  [[nodiscard]] QString unitText() const;
  [[nodiscard]] QString secondaryText() const;
  [[nodiscard]] QVariantList indicators() const;
  [[nodiscard]] QString valueText() const;
  [[nodiscard]] QString rawHex() const;
  [[nodiscard]] QString detailText() const;
  [[nodiscard]] QString lastUpdateText() const;
  [[nodiscard]] QString logText() const;
  [[nodiscard]] QString deviceSeriesText() const;
  [[nodiscard]] QString deviceVersionText() const;
  [[nodiscard]] bool capturing() const;
  [[nodiscard]] QString capturePath() const;
  [[nodiscard]] int capturedFrames() const;
  [[nodiscard]] int readingCount() const;

  /// Manual scan: fills the device list for an explicit choice.
  Q_INVOKABLE void startScan();
  Q_INVOKABLE void stopScan();

  /// Automatic mode: discovers a meter, connects, and reconnects with backoff
  /// after a lost link. Stops when no unique meter can be selected.
  Q_INVOKABLE void connectAutomatically();
  Q_INVOKABLE void stopAutoConnect();

  Q_INVOKABLE void connectToDevice(const QString& address, const QString& name);
  Q_INVOKABLE void disconnectDevice();

  /// Sends one verified front-panel keystroke (1 Select, 2 Range, 3 Hold).
  /// Returns false when the command was not queued.
  Q_INVOKABLE bool pressKey(int code);

  Q_INVOKABLE void clearLog();

  /// Writes every decoded reading collected this session as CSV. Accepts a
  /// local path or a `file:` URL.
  Q_INVOKABLE bool exportCsv(const QString& pathOrUrl);

  /// Starts/stops writing capture lines (`epochMs TAB length TAB hex TAB
  /// detail`), the format `ampbeam-cli --import` consumes later.
  Q_INVOKABLE bool startCapture(const QString& pathOrUrl);
  Q_INVOKABLE void stopCapture();

signals:
  void scanningChanged();
  void connectedChanged();
  void streamingChanged();
  void autoConnectingChanged();
  void deviceSelectionRequiredChanged();
  void readingChanged();
  void devicesChanged();
  void statusChanged();
  void deviceChanged();
  void valueTextChanged();
  void rawHexChanged();
  void detailTextChanged();
  void logTextChanged();
  void deviceInfoChanged();
  void captureChanged();
  void readingCountChanged();

private slots:
  /// Decoding entry point, also reachable through the meta-object for tests.
  void handleNotification(const QByteArray& data);

private:
  void handleScannerRunningChanged();
  void handleAutoScanFinished();
  void beginAutoScan();
  void connectToDeviceNow(const QString& address, const QString& name, bool automatic);
  void scheduleReconnect(const QString& reason);
  void handleClientState(ampbeam::ble::MultimeterClient::State state);
  void applyMeasurement(const ampbeam::Measurement& measurement);
  bool sendKeyNow(int code);
  void writeCaptureLine(const QString& line);

  void setStatus(const QString& status);
  void setModeText(const QString& text);
  void setNumberText(const QString& text);
  void setUnitText(const QString& text);
  void setSecondaryText(const QString& text);
  void setValueText(const QString& text);
  void setRawHex(const QString& hex);
  void setDetailText(const QString& text);
  void setLastUpdateText(const QString& text);
  void setDeviceInfo(const QString& series, const QString& version);
  void setAutoConnecting(bool connecting);
  void setDeviceSelectionRequired(bool required);
  void appendLog(const QString& line);

  [[nodiscard]] static QString localPath(const QString& pathOrUrl);

  ampbeam::ble::DeviceScanner m_scanner;
  ampbeam::ble::MultimeterClient m_client;
  ampbeam::MeterDecoder m_decoder;
  ampbeam::MeterFramer m_framer;
  ampbeam::ReconnectPolicy m_reconnect;
  QTimer m_retryTimer;
  QElapsedTimer m_streamingTimer;
  QFile m_captureFile;

  QVector<ampbeam::Measurement> m_readings;

  QString m_status = QStringLiteral("Idle");
  QString m_deviceName;
  QString m_deviceAddress;
  QString m_modeText;
  QString m_numberText = QStringLiteral("—");
  QString m_unitText;
  QString m_secondaryText;
  QString m_valueText = QStringLiteral("—");
  QString m_rawHex;
  QString m_detailText;
  QString m_lastUpdateText;
  QString m_seriesText = QStringLiteral("—");
  QString m_versionText = QStringLiteral("—");
  QVariantList m_indicators;

  QStringList m_log;
  int m_maxLogLines = 500;

  QString m_capturePath;
  int m_capturedFrames = 0;

  bool m_hasReading = false;
  bool m_autoMode = false;
  bool m_autoScan = false;
  bool m_autoConnecting = false;
  bool m_wasStreaming = false;
  bool m_selectionRequired = false;
  int m_autoScanTimeoutMs = 5000;
  int m_manualScanTimeoutMs = 12000;
};
