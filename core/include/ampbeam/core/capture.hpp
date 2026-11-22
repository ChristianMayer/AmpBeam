// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Capture-log parsing and JSON fixture serialisation.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ampbeam {

/// One frame read from a capture log.
struct CaptureFrame {
  std::optional<std::uint64_t> receivedMs;
  std::vector<std::uint8_t> data;
};

struct CaptureParseResult {
  std::vector<CaptureFrame> frames;
  /// Human readable diagnostics, one per skipped line.
  std::vector<std::string> warnings;
};

/// Parses the output of `ampbeam-cli` as well as simpler hand-written logs.
///
/// Accepted line formats:
///
///     <epochMs>TAB<length>TAB<hex>[TAB<detail>]   (ampbeam-cli output)
///     <epochMs> <hex>                            (milliseconds prefix)
///     <hex>                                      (no timestamp)
///
/// Blank lines, lines starting with '#' and trailing " # ..." comments are
/// ignored.
[[nodiscard]] CaptureParseResult parseCaptureLog(std::string_view text);

struct FixtureMetadata {
  std::string name;
  /// ISO-8601 timestamp, e.g. "2026-11-21T12:00:00.000Z".
  std::string createdAt;
  /// Optional note; omitted from the JSON when empty.
  std::string source;
};

/// Serialises frames as the JSON fixture consumed by `ampbeam-cli --replay`.
[[nodiscard]] std::string toFixtureJson(const CaptureParseResult& capture,
                                        const FixtureMetadata& metadata);

} // namespace ampbeam
