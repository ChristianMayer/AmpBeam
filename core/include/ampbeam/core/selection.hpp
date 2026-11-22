// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Chooses the unique discovered device that advertises the meter service.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ampbeam {

/// One device as reported by the scanner.
struct DeviceCandidate {
  std::string_view address;
  std::string_view name;
  /// Comma separated advertised service UUIDs, as printed by `ampbeam-cli`.
  std::string_view services;
};

enum class DeviceSelectionStatus : std::uint8_t {
  /// A single device can be used.
  Selected,
  /// No device was discovered.
  None,
  /// More than one device is a possible match.
  Multiple,
};

struct DeviceSelectionResult {
  DeviceSelectionStatus status = DeviceSelectionStatus::None;
  /// Address and name of the selected candidate; empty unless `Selected`.
  std::string_view address;
  std::string_view name;
  /// Number of candidates that matched the active filter.
  std::size_t candidateCount = 0;
  /// Number of candidates the caller supplied.
  std::size_t discoveredCount = 0;
  /// `true` when the selection came from a service match.
  bool matchedService = false;
  /// `true` when the selection came from a name filter.
  bool matchedName = false;
};

/// Selects the only usable candidate.
///
/// With a non-empty `nameFilter`, the only device whose name equals it
/// (case-insensitive) is selected. Otherwise the only candidate that advertises
/// `serviceUuid` is selected; if none do but exactly one device was discovered,
/// that device is used because BlueZ often omits service UUIDs at scan time.
/// Surrounding whitespace is ignored. A caller that gets `Multiple` must ask the
/// user for an explicit address instead of picking one.
[[nodiscard]] DeviceSelectionResult selectMeterDevice(std::span<const DeviceCandidate> candidates,
                                                      std::string_view serviceUuid,
                                                      std::string_view nameFilter = {});

} // namespace ampbeam
