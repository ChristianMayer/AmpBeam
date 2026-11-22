// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Implementation of the runtime Bluetooth permission helper.
#include "permissions.h"

#include <QCoreApplication>

#include <utility>

#if QT_CONFIG(permissions) && defined(Q_OS_ANDROID)
#include <QBluetoothPermission>
#include <QPermissions>
#endif

namespace ampbeam::ble {

void ensureBluetoothPermission(QObject* context, PermissionCallback callback) {
  if (!callback) {
    return;
  }

#if QT_CONFIG(permissions) && defined(Q_OS_ANDROID)
  QBluetoothPermission permission;
  permission.setCommunicationModes(QBluetoothPermission::Access);

  auto* app = QCoreApplication::instance();
  const auto status = app->checkPermission(permission);
  if (status == Qt::PermissionStatus::Granted) {
    callback(true);
    return;
  }
  if (status == Qt::PermissionStatus::Denied) {
    callback(false);
    return;
  }

  app->requestPermission(permission, context,
                         [callback = std::move(callback)](const QPermission& result) {
                           callback(result.status() == Qt::PermissionStatus::Granted);
                         });
#else
  Q_UNUSED(context)
  callback(true);
#endif
}

} // namespace ampbeam::ble
