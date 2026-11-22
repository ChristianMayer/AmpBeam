// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// GATT characteristic profile measured on the tested meter.
#pragma once

#include <string_view>

namespace ampbeam {

/// GATT characteristics used by a meter family.
///
/// Measured on the tested meter (series 61, firmware 1.4.0) and described in
/// `docs/protocol.md`. The roles are not interchangeable: front-panel keys go
/// to one characteristic and every other command to another.
struct GattProfile {
  std::string_view name;
  std::string_view serviceUuid;
  /// Meter -> app measurement stream (notifications).
  std::string_view dataCharacteristicUuid;
  /// App -> meter command channel: 16 byte payloads.
  std::string_view commandCharacteristicUuid;
  /// App -> meter front-panel keys: one two byte key event per write.
  std::string_view keyCharacteristicUuid;
  /// Read-only device identity: model series id and firmware version.
  std::string_view infoCharacteristicUuid;
};

/// GATT profile measured on the tested meter.
inline constexpr GattProfile meterBleProfile{
    .name = "BLE multimeter",
    .serviceUuid = "0000fff0-0000-1000-8000-00805f9b34fb",
    .dataCharacteristicUuid = "0000fff4-0000-1000-8000-00805f9b34fb",
    .commandCharacteristicUuid = "0000fff1-0000-1000-8000-00805f9b34fb",
    .keyCharacteristicUuid = "0000fff3-0000-1000-8000-00805f9b34fb",
    .infoCharacteristicUuid = "0000fff2-0000-1000-8000-00805f9b34fb",
};

} // namespace ampbeam
