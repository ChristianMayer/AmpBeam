// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Implementation of capture-log parsing and fixture serialisation.
#include "ampbeam/core/capture.hpp"

#include "ampbeam/core/hex.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace ampbeam {
namespace {

[[nodiscard]] bool isSpace(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

[[nodiscard]] std::string_view trim(std::string_view text) {
  while (!text.empty() && isSpace(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && isSpace(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

[[nodiscard]] bool allDigits(std::string_view text) {
  return !text.empty() &&
         std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

[[nodiscard]] std::optional<std::uint64_t> parseUnsigned(std::string_view text) {
  if (!allDigits(text) || text.size() > 20) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  for (const char c : text) {
    const auto digit = static_cast<std::uint64_t>(c - '0');
    if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
      return std::nullopt;
    }
    value = value * 10 + digit;
  }
  return value;
}

[[nodiscard]] std::vector<std::string_view> splitTabs(std::string_view line) {
  std::vector<std::string_view> fields;
  while (true) {
    const auto position = line.find('\t');
    if (position == std::string_view::npos) {
      fields.push_back(line);
      break;
    }
    fields.push_back(line.substr(0, position));
    line.remove_prefix(position + 1);
  }
  return fields;
}

[[nodiscard]] std::string jsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (const char c : text) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20U) {
        static constexpr char kHexDigits[] = "0123456789abcdef";
        const auto value = static_cast<unsigned char>(c);
        out += "\\u00";
        out += kHexDigits[(value >> 4U) & 0x0FU];
        out += kHexDigits[value & 0x0FU];
      } else {
        out += c;
      }
      break;
    }
  }
  return out;
}

} // namespace

CaptureParseResult parseCaptureLog(std::string_view text) {
  CaptureParseResult result;

  std::size_t lineStart = 0;
  std::size_t lineNumber = 0;

  while (lineStart <= text.size()) {
    const std::size_t newline = text.find('\n', lineStart);
    const std::size_t lineEnd = newline == std::string_view::npos ? text.size() : newline;
    std::string_view line = text.substr(lineStart, lineEnd - lineStart);
    ++lineNumber;

    std::string_view trimmed = trim(line);
    const auto comment = trimmed.find(" #");
    if (comment != std::string_view::npos) {
      trimmed = trim(trimmed.substr(0, comment));
    }

    const bool isComment = trimmed.empty() || trimmed.front() == '#';

    if (!isComment) {
      std::optional<std::uint64_t> receivedMs;
      std::string_view hexText;

      const auto fields = splitTabs(trimmed);
      if (fields.size() >= 3 && allDigits(fields[0]) && allDigits(fields[1])) {
        // Canonical ampbeam-cli output.
        receivedMs = parseUnsigned(fields[0]);
        hexText = trim(fields[2]);
      } else {
        // Optional "<milliseconds> <hex>" prefix. Requiring at least ten
        // digits keeps ordinary hex lines unambiguous in practice.
        std::size_t digits = 0;
        while (digits < trimmed.size() && trimmed[digits] >= '0' && trimmed[digits] <= '9') {
          ++digits;
        }
        if (digits >= 10 && digits < trimmed.size() && isSpace(trimmed[digits])) {
          const std::string_view candidate = trim(trimmed.substr(digits + 1));
          if (fromHex(candidate).has_value()) {
            receivedMs = parseUnsigned(trimmed.substr(0, digits));
            hexText = candidate;
          }
        }
        if (hexText.empty()) {
          hexText = trimmed;
        }
      }

      if (auto data = fromHex(hexText); data.has_value()) {
        result.frames.push_back(CaptureFrame{receivedMs, std::move(*data)});
      } else {
        result.warnings.push_back("line " + std::to_string(lineNumber) + ": cannot parse \"" +
                                  std::string(trimmed) + "\"");
      }
    }

    if (newline == std::string_view::npos) {
      break;
    }
    lineStart = lineEnd + 1;
  }

  return result;
}

std::string captureLine(std::uint64_t receivedAtMs, std::span<const std::uint8_t> frame,
                        std::string_view detail) {
  std::string out = std::to_string(receivedAtMs);
  out += '\t';
  out += std::to_string(frame.size());
  out += '\t';
  out += toHex(frame, ' ');
  out += '\t';
  out += detail;
  return out;
}

std::string toFixtureJson(const CaptureParseResult& capture, const FixtureMetadata& metadata) {
  std::string out;
  out.reserve(160 + capture.frames.size() * 48);

  out += "{\n";
  out += "  \"version\": 1,\n";
  out += "  \"name\": \"" + jsonEscape(metadata.name) + "\",\n";
  out += "  \"createdAt\": \"" + jsonEscape(metadata.createdAt) + "\",\n";
  if (!metadata.source.empty()) {
    out += "  \"source\": \"" + jsonEscape(metadata.source) + "\",\n";
  }
  out += "  \"frames\": [\n";

  for (std::size_t index = 0; index < capture.frames.size(); ++index) {
    const CaptureFrame& frame = capture.frames[index];
    out += "    { ";
    if (frame.receivedMs.has_value()) {
      out += "\"receivedMs\": " + std::to_string(*frame.receivedMs) + ", ";
    }
    out += "\"hex\": \"" + toHex(frame.data, ' ') + "\" }";
    if (index + 1 < capture.frames.size()) {
      out += ',';
    }
    out += '\n';
  }

  out += "  ]\n}\n";
  return out;
}

} // namespace ampbeam
