// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QML-facing controller exposing scan, connection and decoder state.
#pragma once

#include "ble/device_scanner.h"
#include "ble/multimeter_client.h"

#include "ampbeam/core/decoder.hpp"
#include "ampbeam/core/meter.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqml.h>

class AppController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
  Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
  Q_PROPERTY(bool streaming READ streaming NOTIFY streamingChanged)
  Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString valueText READ valueText NOTIFY valueTextChanged)
  Q_PROPERTY(QString rawHex READ rawHex NOTIFY rawHexChanged)
  Q_PROPERTY(QString logText READ logText NOTIFY logTextChanged)

public:
  explicit AppController(QObject* parent = nullptr);

  [[nodiscard]] bool scanning() const;
  [[nodiscard]] bool connected() const;
  [[nodiscard]] bool streaming() const;
  [[nodiscard]] QVariantList devices() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] QString valueText() const;
  [[nodiscard]] QString rawHex() const;
  [[nodiscard]] QString logText() const;

  Q_INVOKABLE void startScan();
  Q_INVOKABLE void stopScan();
  Q_INVOKABLE void connectToDevice(const QString& address, const QString& name);
  Q_INVOKABLE void disconnectDevice();
  Q_INVOKABLE void clearLog();

signals:
  void scanningChanged();
  void connectedChanged();
  void streamingChanged();
  void devicesChanged();
  void statusChanged();
  void valueTextChanged();
  void rawHexChanged();
  void logTextChanged();

private slots:
  /// Decoding entry point, also reachable through the meta-object for tests.
  void handleNotification(const QByteArray& data);

private:
  void setStatus(const QString& status);
  void setValueText(const QString& text);
  void setRawHex(const QString& hex);
  void appendLog(const QString& line);

  ampbeam::ble::DeviceScanner m_scanner;
  ampbeam::ble::MultimeterClient m_client;
  ampbeam::MeterDecoder m_decoder;
  ampbeam::MeterFramer m_framer;

  QString m_status = QStringLiteral("Idle");
  QString m_valueText = QStringLiteral("—");
  QString m_rawHex;
  QStringList m_log;
  int m_maxLogLines = 500;
};
