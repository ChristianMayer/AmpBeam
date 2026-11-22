// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Decoder interface shared by the GUI, the CLI and the tests.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "measurement.hpp"

namespace ampbeam {

/// Result of interpreting one BLE notification.
struct DecodedFrame {
  enum class Kind : std::uint8_t {
    /// The payload could not be interpreted; `detail` contains diagnostics.
    Unknown,
    /// A complete reading was decoded into `measurement`.
    Measurement,
  };

  Kind kind = Kind::Unknown;
  Measurement measurement{};
  /// Human readable explanation, always filled in.
  std::string detail;
};

/// Turns raw notification payloads into readings.
///
/// Implementations must be stateless and thread-safe; framing/stateful
/// reassembly belongs in the transport layer.
class FrameDecoder {
public:
  virtual ~FrameDecoder() = default;

  [[nodiscard]] virtual DecodedFrame decode(std::span<const std::uint8_t> frame) const = 0;

  /// Short identifier used in logs ("raw", "meter", ...).
  [[nodiscard]] virtual std::string_view name() const = 0;
};

/// Pass-through decoder used until the wire format is known. Never claims to
/// have decoded anything; it only produces a hex dump.
class RawDecoder final : public FrameDecoder {
public:
  [[nodiscard]] DecodedFrame decode(std::span<const std::uint8_t> frame) const override;
  [[nodiscard]] std::string_view name() const override;
};

/// Decoder for the 6 and 15 byte frame layouts described in `docs/protocol.md`.
///
/// Only what a capture proves is decoded; every other field is reported as
/// unknown instead of guessed.
/// Only complete frames are decoded; anything else is reported as unknown with a
/// hex dump, and frames from modes that carry no measurement (oscilloscope) are
/// reported as unknown as well.
class MeterDecoder final : public FrameDecoder {
public:
  [[nodiscard]] DecodedFrame decode(std::span<const std::uint8_t> frame) const override;
  [[nodiscard]] std::string_view name() const override;
};

} // namespace ampbeam
