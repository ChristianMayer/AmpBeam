// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hex encoding and parsing helpers.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ampbeam {

/// Encodes bytes as uppercase hex. With `separator == '\0'` the output is
/// contiguous ("001A2B"), otherwise the separator is placed between bytes
/// ("00:1A:2B").
[[nodiscard]] std::string toHex(std::span<const std::uint8_t> bytes, char separator = '\0',
                                bool upperCase = true);

/// Parses hex text. Accepts optional "0x" prefixes, whitespace and the common
/// separators ':', '-', '_', ',', ';', '|', '/', '.'. Returns std::nullopt for
/// any malformed input (including an odd number of digits).
[[nodiscard]] std::optional<std::vector<std::uint8_t>> fromHex(std::string_view text);

} // namespace ampbeam
