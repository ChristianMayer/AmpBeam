// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Time-based history of recent readings for the GUI signal trace.
#pragma once

#include "ampbeam/core/measurement.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace ampbeam {

/// One sample of a trace, prepared for drawing. `x` is the position in the
/// fixed timebase (0 = oldest edge, 1 = newest edge). `y` is the primary value
/// scaled to the visible range, `secondaryY` the second reading of the same
/// frame (for example the duty cycle next to a frequency), scaled on its own.
///
/// A value zoom can push `y`/`secondaryY` outside [0, 1]; the renderer clips.
/// `valid` is false for a gap (an overload or a frame without a numeric value),
/// so a renderer breaks the line there instead of inventing a value.
struct TraceSample {
  double x = 0.0;
  double y = 0.0;
  bool valid = false;
  double secondaryY = 0.0;
  bool secondaryValid = false;

  friend bool operator==(const TraceSample&, const TraceSample&) = default;
};

/// Bounded, time-based window of the most recent proven readings.
///
/// The horizontal axis is a fixed timebase, not a fixed number of samples, so
/// the curve scrolls left as readings arrive instead of being compressed. A
/// different mode or unit starts a new series, so the line never joins two
/// unrelated functions. Both the timebase and the value scale can be zoomed.
class TraceHistory {
public:
  /// `capacity` is the number of readings kept; a capacity of zero is clamped
  /// to one so scaling always has a sample.
  explicit TraceHistory(std::size_t capacity);

  static constexpr std::uint64_t kMinimumTimeSpanMs = 2000;
  static constexpr std::uint64_t kMaximumTimeSpanMs = 60000;
  static constexpr double kMaximumValueZoom = 16.0;

  void clear();

  /// Appends one decoded reading. The oldest reading leaves the history once
  /// the capacity is reached.
  void push(const Measurement& measurement);

  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] std::size_t capacity() const;
  [[nodiscard]] bool empty() const;

  /// Mode and unit of the current series; `MeterGear::Unknown` and `Unit::None`
  /// while the history is empty. NCV readings legitimately carry `Unit::None`.
  [[nodiscard]] MeterGear gear() const;
  [[nodiscard]] Unit unit() const;
  /// Unit of the secondary reading, `Unit::None` when the series has none.
  [[nodiscard]] Unit secondaryUnit() const;
  [[nodiscard]] bool hasSecondary() const;

  /// Fixed horizontal span. Readings older than the newest one minus this span
  /// are no longer drawn. Starts at 10 s.
  [[nodiscard]] std::uint64_t timeSpanMs() const;
  [[nodiscard]] bool canZoomTimeIn() const;
  [[nodiscard]] bool canZoomTimeOut() const;
  std::uint64_t zoomTimeIn();
  std::uint64_t zoomTimeOut();

  /// Vertical magnification around the middle of the auto-scaled range; 1.0
  /// fits the visible values.
  [[nodiscard]] double valueZoom() const;
  [[nodiscard]] bool canZoomValueIn() const;
  [[nodiscard]] bool canZoomValueOut() const;
  double zoomValueIn();
  double zoomValueOut();

  /// Receive time of the newest stored reading; 0 when empty.
  [[nodiscard]] std::uint64_t newestTimeMs() const;

  /// Smallest and largest finite primary value drawn in the timebase; absent
  /// when no visible sample has a numeric value.
  [[nodiscard]] std::optional<double> minimum() const;
  [[nodiscard]] std::optional<double> maximum() const;
  [[nodiscard]] std::optional<double> secondaryMinimum() const;
  [[nodiscard]] std::optional<double> secondaryMaximum() const;

  /// Value at the bottom and top edge of the plot with the current value zoom
  /// applied, so axis labels stay aligned with the clipped curve.
  [[nodiscard]] std::optional<double> visibleMinimum() const;
  [[nodiscard]] std::optional<double> visibleMaximum() const;
  [[nodiscard]] std::optional<double> secondaryVisibleMinimum() const;
  [[nodiscard]] std::optional<double> secondaryVisibleMaximum() const;

  /// Samples from oldest to newest inside the timebase.
  [[nodiscard]] std::vector<TraceSample> samples() const;

private:
  struct Entry {
    std::uint64_t timeMs = 0;
    double primary = 0.0;
    double secondary = 0.0;
    bool hasSecondary = false;
  };

  [[nodiscard]] std::uint64_t windowStartMs() const;
  [[nodiscard]] std::size_t firstVisible() const;

  std::size_t m_capacity;
  std::uint64_t m_timeSpanMs = 10000;
  double m_valueZoom = 1.0;
  std::deque<Entry> m_values;
  MeterGear m_gear = MeterGear::Unknown;
  Unit m_unit = Unit::None;
  Unit m_secondaryUnit = Unit::None;
  bool m_hasSeries = false;
  bool m_hasSecondary = false;
};

} // namespace ampbeam
