// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runtime Bluetooth permission helper.
#pragma once

#include <QObject>

#include <functional>

namespace ampbeam::ble {

using PermissionCallback = std::function<void(bool granted)>;

/// Asks for the Android Bluetooth runtime permission if needed.
///
/// On every non-Android platform the callback is invoked synchronously with
/// `true`. On Android the callback may run later from the event loop.
void ensureBluetoothPermission(QObject* context, PermissionCallback callback);

} // namespace ampbeam::ble
