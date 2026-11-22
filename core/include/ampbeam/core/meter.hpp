// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Frame types and helpers for the supported meter protocol.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "measurement.hpp"

namespace ampbeam {

// MeterGear lives in measurement.hpp because every Measurement carries it.

/// SI prefix encoded in the counting-unit field. The field has three bits, so
/// all eight values are real and are mapped to the standard SI magnitude of the
/// confirmed measurement function.
enum class MeterPrefix : std::uint8_t {
  Pico = 0,
  Nano = 1,
  Micro = 2,
  Milli = 3,
  None = 4,
  Kilo = 5,
  Mega = 6,
  Giga = 7,
};

/// What a frame says about the displayed value. Only the normal and overload
/// status codes are confirmed; any other code makes the frame unknown.
enum class MeterValueStatus : std::uint8_t {
  /// A number the meter is displaying.
  Normal,
  Overload,
  Unknown,
};

/// Parsed gear word (unit / range information).
struct MeterRange {
  std::uint8_t code = 0xFF;
  MeterGear gear = MeterGear::Unknown;
  MeterPrefix prefix = MeterPrefix::None;
  /// Number of decimals. Only 0..4 are confirmed; other codes are rejected.
  std::uint8_t decimalPoint = 0;
  double scale = 1.0;
  /// Bit 12 of the 24 bit gear word.
  bool hasSecondValue = false;
};

/// One decoded measurement slot (primary or secondary).
struct MeterReading {
  bool present = false;
  bool negative = false;
  MeterValueStatus status = MeterValueStatus::Unknown;
  MeterRange range{};
  /// Raw magnitude before the decimal point is applied.
  std::uint32_t magnitude = 0;
  /// Magnitude scaled by the decimal point and, where no prefixed unit exists,
  /// by the SI prefix.
  double value = 0.0;
  Unit unit = Unit::None;
  Quantity quantity = Quantity::Unknown;
};

/// A complete frame as sent by the meter. Only the 15 byte layout is verified.
struct MeterFrame {
  std::size_t size = 0;
  MeterReading primary{};
  std::optional<MeterReading> secondary{};
  /// Front panel state bit mask (see `meterStateName`).
  std::uint32_t state = 0;
  /// Short human readable summary, filled in by `meterDecodeFrame`.
  std::string detail;
};

/// Front-panel indicators confirmed in the state word. Unknown bits are
/// ignored rather than named.
enum class MeterState : std::uint8_t {
  Hold = 0,
  /// Relative measurement; the second measurement is the stored reference.
  Relative = 1,
  AutoRange = 2,
  /// Min/Max/Avg view; the second measurement carries MAX (verified for DC and
  /// AC millivolt readings).
  MinMax = 5,
  Oscilloscope = 18,
};

/// `true` when `state` has the indicator `stateBit` set.
[[nodiscard]] bool meterHasState(std::uint32_t state, MeterState stateBit);

/// Textual indicator name ("HOLD", ...), or nullptr when unknown.
[[nodiscard]] const char* meterStateName(MeterState stateBit);

/// Short gear name ("DCV", "CAP", ...), or "?" when unknown.
[[nodiscard]] const char* meterGearName(MeterGear gear);

/// SI prefix symbol ("p", "n", "µ", "m", "", "k", "M", "G").
[[nodiscard]] const char* meterPrefixSymbol(MeterPrefix prefix);

/// `true` for the verified 15 byte frame size.
[[nodiscard]] bool meterIsFrameSize(std::size_t size);

/// Decodes exactly one 15 byte frame. Returns `std::nullopt` unless the input is
/// the verified size and every claimed field is in the confirmed set.
[[nodiscard]] std::optional<MeterFrame> meterDecodeFrame(std::span<const std::uint8_t> frame);

/// Splits a notification stream into 15 byte frames. Not part of `FrameDecoder`
/// because it is deliberately stateful; the decoder itself stays stateless.
class MeterFramer {
public:
  void reset();

  /// Appends `payload`, returns every complete frame that is now available.
  [[nodiscard]] std::vector<std::vector<std::uint8_t>> push(std::span<const std::uint8_t> payload);

private:
  std::vector<std::uint8_t> m_buffer;
};

/// Device identity read from the info characteristic.
struct MeterDeviceInfo {
  /// Model series id reported by the device (61 on the tested meter).
  std::uint8_t seriesId = 0;
  /// "major.minor.patch" as reported by the meter (may be empty).
  std::string version;
  /// Raw payload for diagnostics.
  std::vector<std::uint8_t> raw;
};

/// Parses the info characteristic payload. Returns `std::nullopt` when the
/// payload is too short to contain a series id.
[[nodiscard]] std::optional<MeterDeviceInfo>
meterParseDeviceInfo(std::span<const std::uint8_t> payload);

// --- Commands written to the command characteristic ------------------------

/// Front-panel key command: two bytes, `[key, 1]` while pressed and
/// `[key, 0]` while released.
///
/// **Confirmed on hardware (series 61):** one `[key, 1]` write performs the key
/// action. Writing the matching `[key, 0]` afterwards makes the meter behave as
/// if nothing happened, so a keystroke is a single write — do not send the pair
/// unless a model is found that needs it.
[[nodiscard]] std::array<std::uint8_t, 2> meterKeyCommand(std::uint8_t key, bool pressed);

inline constexpr std::uint8_t meterRenameMarker = 0x40;

/// Builds the 16 byte rename payload for `name`, or `std::nullopt` when the name
/// is empty, does not fit into the 15 bytes after the marker, or contains
/// non-printable characters. Measured on hardware; see `docs/protocol.md`.
[[nodiscard]] std::optional<std::array<std::uint8_t, 16>> meterRenameCommand(std::string_view name);

} // namespace ampbeam
