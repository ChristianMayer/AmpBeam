// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Implementation of hex encoding and parsing.
#include "ampbeam/core/hex.hpp"

#include <array>
#include <cctype>

namespace ampbeam {
namespace {

[[nodiscard]] constexpr int hexValue(char c) noexcept {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

[[nodiscard]] constexpr bool isSeparator(char c) noexcept {
  switch (c) {
  case ' ':
  case '\t':
  case '\r':
  case '\n':
  case ':':
  case '-':
  case '_':
  case ',':
  case ';':
  case '|':
  case '/':
  case '.':
  case '{':
  case '}':
  case '[':
  case ']':
  case '(':
  case ')':
    return true;
  default:
    return false;
  }
}

constexpr std::array<char, 16> kUpperDigits{'0', '1', '2', '3', '4', '5', '6', '7',
                                            '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
constexpr std::array<char, 16> kLowerDigits{'0', '1', '2', '3', '4', '5', '6', '7',
                                            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

} // namespace

std::string toHex(std::span<const std::uint8_t> bytes, char separator, bool upperCase) {
  const auto& digits = upperCase ? kUpperDigits : kLowerDigits;
  std::string out;
  out.reserve(bytes.size() * (separator == '\0' ? 2U : 3U));

  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (separator != '\0' && i != 0) {
      out.push_back(separator);
    }
    const auto value = bytes[i];
    out.push_back(digits[static_cast<std::size_t>(value >> 4U)]);
    out.push_back(digits[static_cast<std::size_t>(value & 0x0FU)]);
  }
  return out;
}

std::optional<std::vector<std::uint8_t>> fromHex(std::string_view text) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(text.size() / 2);

  int pendingHigh = -1;

  for (std::size_t i = 0; i < text.size();) {
    const char c = text[i];

    // Skip a "0x"/"0X" prefix.
    if (c == '0' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
      i += 2;
      continue;
    }

    const int value = hexValue(c);
    if (value >= 0) {
      if (pendingHigh < 0) {
        pendingHigh = value;
      } else {
        bytes.push_back(static_cast<std::uint8_t>((pendingHigh << 4) | value));
        pendingHigh = -1;
      }
      ++i;
      continue;
    }

    if (isSeparator(c)) {
      if (pendingHigh >= 0) {
        return std::nullopt; // separator in the middle of a byte
      }
      ++i;
      continue;
    }

    return std::nullopt;
  }

  if (pendingHigh >= 0) {
    return std::nullopt; // odd number of digits
  }
  return bytes;
}

} // namespace ampbeam
