// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Implementation of the BlueZ LE discovery workaround.
#include "bluez_le_discovery.h"

#if defined(AMPBEAM_HAVE_QTDBUS)

#include <QDBusConnection>
#include <QDBusMessage>
#include <QVariantMap>

namespace ampbeam::ble {
namespace {

constexpr auto kBluezService = "org.bluez";
constexpr auto kAdapterInterface = "org.bluez.Adapter1";

[[nodiscard]] QDBusMessage adapterCall(const QString& path, const char* method) {
  return QDBusMessage::createMethodCall(QString::fromLatin1(kBluezService), path,
                                        QString::fromLatin1(kAdapterInterface),
                                        QString::fromLatin1(method));
}

/// True when `path` exists and exposes `interface`. `QDBusInterface::isValid()`
/// returns true for a missing object, so the introspection answer is checked
/// instead.
[[nodiscard]] bool objectExposes(const QDBusConnection& bus, const QString& path,
                                 const QString& interface) {
  QDBusMessage call = QDBusMessage::createMethodCall(
      QString::fromLatin1(kBluezService), path,
      QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect"));
  const QDBusMessage reply = bus.call(call);
  if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
    return false;
  }
  return reply.arguments().front().toString().contains(interface);
}

/// First adapter that exposes `org.bluez.Adapter1`, or an empty string.
[[nodiscard]] QString findAdapter(const QDBusConnection& bus) {
  for (int index = 0; index < 8; ++index) {
    const QString path = QStringLiteral("/org/bluez/hci%1").arg(index);
    if (objectExposes(bus, path, QString::fromLatin1(kAdapterInterface))) {
      return path;
    }
  }
  return {};
}

} // namespace

BluezLeDiscovery::~BluezLeDiscovery() {
  stop();
}

bool BluezLeDiscovery::start() {
  if (m_running) {
    return true;
  }

  m_error.clear();
  const QDBusConnection bus = QDBusConnection::systemBus();
  if (!bus.isConnected()) {
    m_error = QStringLiteral("system D-Bus is not available");
    return false;
  }

  const QString path = findAdapter(bus);
  if (path.isEmpty()) {
    m_error = QStringLiteral("no BlueZ adapter found");
    return false;
  }

  // A stale discovery from another client keeps its old filter, which may not
  // include LE. Reset it before applying ours.
  bus.call(adapterCall(path, "StopDiscovery"));

  // A different filter may already be active; StartDiscovery below is what
  // actually matters.
  QVariantMap filter;
  filter.insert(QStringLiteral("Transport"), QStringLiteral("le"));
  QDBusMessage filterCall = adapterCall(path, "SetDiscoveryFilter");
  filterCall << filter;
  bus.call(filterCall);

  QDBusMessage startResult = bus.call(adapterCall(path, "StartDiscovery"));
  if (startResult.type() == QDBusMessage::ErrorMessage &&
      startResult.errorName() != QStringLiteral("org.bluez.Error.InProgress")) {
    m_error = startResult.errorMessage();
    return false;
  }

  m_adapterPath = path;
  m_running = true;
  return true;
}

void BluezLeDiscovery::stop() {
  if (!m_running) {
    return;
  }

  const QDBusConnection bus = QDBusConnection::systemBus();
  if (bus.isConnected() && !m_adapterPath.isEmpty()) {
    bus.call(adapterCall(m_adapterPath, "StopDiscovery"));
  }
  m_running = false;
}

bool BluezLeDiscovery::isRunning() const {
  return m_running;
}

QString BluezLeDiscovery::errorString() const {
  return m_error;
}

bool BluezLeDiscovery::hasDevice(const QString& address) const {
  if (m_adapterPath.isEmpty()) {
    return false;
  }
  QString leaf = address;
  leaf.replace(QLatin1Char(':'), QLatin1Char('_'));
  const QString path = m_adapterPath + QStringLiteral("/dev_") + leaf.toUpper();
  return objectExposes(QDBusConnection::systemBus(), path, QStringLiteral("org.bluez.Device1"));
}

} // namespace ampbeam::ble

#else

namespace ampbeam::ble {

BluezLeDiscovery::~BluezLeDiscovery() = default;

bool BluezLeDiscovery::start() {
  return false;
}

void BluezLeDiscovery::stop() {}

bool BluezLeDiscovery::isRunning() const {
  return false;
}

QString BluezLeDiscovery::errorString() const {
  return QStringLiteral("BlueZ discovery fallback is not available");
}

bool BluezLeDiscovery::hasDevice(const QString& address) const {
  Q_UNUSED(address)
  return false;
}

} // namespace ampbeam::ble

#endif
