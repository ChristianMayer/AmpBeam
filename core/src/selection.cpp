// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Implementation of the unique-device selection.
#include "ampbeam/core/selection.hpp"

#include <cctype>

namespace ampbeam {
namespace {

[[nodiscard]] std::string_view trim(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.remove_prefix(1);
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.remove_suffix(1);
  }
  return text;
}

[[nodiscard]] bool equalIgnoreCase(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    const auto left = static_cast<unsigned char>(lhs[index]);
    const auto right = static_cast<unsigned char>(rhs[index]);
    if (std::tolower(left) != std::tolower(right)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool advertises(std::string_view services, std::string_view serviceUuid) {
  while (!services.empty()) {
    const auto comma = services.find(',');
    const std::string_view entry =
        trim(services.substr(0, comma == std::string_view::npos ? services.size() : comma));
    if (equalIgnoreCase(entry, serviceUuid)) {
      return true;
    }
    if (comma == std::string_view::npos) {
      break;
    }
    services.remove_prefix(comma + 1);
  }
  return false;
}

} // namespace

DeviceSelectionResult selectMeterDevice(std::span<const DeviceCandidate> candidates,
                                        std::string_view serviceUuid, std::string_view nameFilter) {
  DeviceSelectionResult result{};
  result.discoveredCount = candidates.size();

  const bool filterByName = !nameFilter.empty();
  for (const DeviceCandidate& candidate : candidates) {
    const bool matches = filterByName ? equalIgnoreCase(trim(candidate.name), trim(nameFilter))
                                      : advertises(candidate.services, serviceUuid);
    if (!matches) {
      continue;
    }
    ++result.candidateCount;
    if (result.candidateCount == 1U) {
      result.address = candidate.address;
      result.name = candidate.name;
    }
  }

  result.matchedName = filterByName && result.candidateCount > 0U;
  result.matchedService = !filterByName && result.candidateCount > 0U;

  if (result.candidateCount == 1U) {
    result.status = DeviceSelectionStatus::Selected;
  } else if (result.candidateCount > 1U) {
    result.status = DeviceSelectionStatus::Multiple;
    result.address = {};
    result.name = {};
  } else if (filterByName) {
    result.status = DeviceSelectionStatus::None;
  } else if (result.discoveredCount == 1U) {
    // BlueZ often omits advertised service UUIDs at scan time. With exactly one
    // discovered device there is no ambiguity, so use it.
    result.status = DeviceSelectionStatus::Selected;
    result.address = candidates.front().address;
    result.name = candidates.front().name;
  } else if (result.discoveredCount == 0U) {
    result.status = DeviceSelectionStatus::None;
  } else {
    result.status = DeviceSelectionStatus::Multiple;
  }
  return result;
}

} // namespace ampbeam
