// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BlueZ workaround: keeps an LE discovery running while a connection is
// established.
#pragma once

#include <QString>

namespace ampbeam::ble {

/// Starts and stops an LE discovery through the system D-Bus.
///
/// BlueZ 5.72 forgets the `org.bluez.Device1` object of an unpaired device when
/// the Qt discovery agent stops, and a later connect then fails with "Remote
/// device cannot be found". Holding a discovery open while the controller
/// connects makes BlueZ create and keep the object. The fallback is best effort:
/// if D-Bus or an adapter is unavailable, `start()` returns false and the
/// connection is attempted normally.
class BluezLeDiscovery final {
public:
  BluezLeDiscovery() = default;
  ~BluezLeDiscovery();

  BluezLeDiscovery(const BluezLeDiscovery&) = delete;
  BluezLeDiscovery& operator=(const BluezLeDiscovery&) = delete;

  /// Starts discovery on the first available adapter. Returns true when a
  /// discovery is running afterwards, including when one was already active.
  [[nodiscard]] bool start();

  /// Stops the discovery if this object started one.
  void stop();

  [[nodiscard]] bool isRunning() const;
  [[nodiscard]] QString errorString() const;

  /// True when BlueZ already exposes the device as an `org.bluez.Device1`
  /// object on the adapter used by `start()`.
  [[nodiscard]] bool hasDevice(const QString& address) const;

private:
  QString m_adapterPath;
  QString m_error;
  bool m_running = false;
};

} // namespace ampbeam::ble
