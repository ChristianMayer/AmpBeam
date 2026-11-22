// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Time-based history of recent readings for the GUI signal trace.
#include "ampbeam/core/trace.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace ampbeam {
namespace {

/// Timebase steps the zoom buttons walk through, in milliseconds.
constexpr std::array<std::uint64_t, 5> kTimeSpansMs{2000, 5000, 10000, 30000, 60000};

/// Edge of the value range after the zoom has been applied around the middle.
[[nodiscard]] std::optional<double> zoomedEdge(const std::optional<double>& low,
                                               const std::optional<double>& high, double zoom,
                                               bool top) {
  if (!low.has_value() || !high.has_value()) {
    return std::nullopt;
  }
  const double centre = (*low + *high) / 2.0;
  const double half = (*high - *low) / 2.0 / zoom;
  return top ? centre + half : centre - half;
}

} // namespace

TraceHistory::TraceHistory(std::size_t capacity) : m_capacity(capacity == 0 ? 1 : capacity) {}

void TraceHistory::clear() {
  m_values.clear();
  m_gear = MeterGear::Unknown;
  m_unit = Unit::None;
  m_secondaryUnit = Unit::None;
  m_hasSeries = false;
  m_hasSecondary = false;
}

void TraceHistory::push(const Measurement& measurement) {
  if (m_hasSeries && (measurement.gear != m_gear || measurement.unit != m_unit)) {
    clear();
  }
  m_gear = measurement.gear;
  m_unit = measurement.unit;
  m_hasSeries = true;
  if (measurement.secondary.present) {
    m_secondaryUnit = measurement.secondary.unit;
    m_hasSecondary = true;
  }

  Entry entry;
  entry.timeMs = measurement.receivedAtMs;
  entry.primary = measurement.value;
  entry.secondary = measurement.secondary.value;
  entry.hasSecondary = measurement.secondary.present;
  m_values.push_back(entry);
  if (m_values.size() > m_capacity) {
    m_values.pop_front();
  }
}

std::size_t TraceHistory::size() const {
  return m_values.size();
}

std::size_t TraceHistory::capacity() const {
  return m_capacity;
}

bool TraceHistory::empty() const {
  return m_values.empty();
}

MeterGear TraceHistory::gear() const {
  return m_gear;
}

Unit TraceHistory::unit() const {
  return m_unit;
}

Unit TraceHistory::secondaryUnit() const {
  return m_secondaryUnit;
}

bool TraceHistory::hasSecondary() const {
  return m_hasSecondary;
}

std::uint64_t TraceHistory::timeSpanMs() const {
  return m_timeSpanMs;
}

bool TraceHistory::canZoomTimeIn() const {
  return m_timeSpanMs > kMinimumTimeSpanMs;
}

bool TraceHistory::canZoomTimeOut() const {
  return m_timeSpanMs < kMaximumTimeSpanMs;
}

std::uint64_t TraceHistory::zoomTimeIn() {
  for (std::size_t index = 1; index < kTimeSpansMs.size(); ++index) {
    if (kTimeSpansMs[index] == m_timeSpanMs) {
      m_timeSpanMs = kTimeSpansMs[index - 1];
      break;
    }
  }
  return m_timeSpanMs;
}

std::uint64_t TraceHistory::zoomTimeOut() {
  for (std::size_t index = 0; index + 1 < kTimeSpansMs.size(); ++index) {
    if (kTimeSpansMs[index] == m_timeSpanMs) {
      m_timeSpanMs = kTimeSpansMs[index + 1];
      break;
    }
  }
  return m_timeSpanMs;
}

double TraceHistory::valueZoom() const {
  return m_valueZoom;
}

bool TraceHistory::canZoomValueIn() const {
  return m_valueZoom < kMaximumValueZoom;
}

bool TraceHistory::canZoomValueOut() const {
  return m_valueZoom > 1.0;
}

double TraceHistory::zoomValueIn() {
  m_valueZoom = std::min(kMaximumValueZoom, m_valueZoom * 2.0);
  return m_valueZoom;
}

double TraceHistory::zoomValueOut() {
  m_valueZoom = std::max(1.0, m_valueZoom / 2.0);
  return m_valueZoom;
}

std::uint64_t TraceHistory::newestTimeMs() const {
  return m_values.empty() ? 0 : m_values.back().timeMs;
}

std::uint64_t TraceHistory::windowStartMs() const {
  const std::uint64_t end = newestTimeMs();
  return end > m_timeSpanMs ? end - m_timeSpanMs : 0;
}

std::size_t TraceHistory::firstVisible() const {
  const std::uint64_t start = windowStartMs();
  std::size_t index = 0;
  while (index < m_values.size() && m_values.at(index).timeMs < start) {
    ++index;
  }
  return index;
}

std::optional<double> TraceHistory::minimum() const {
  std::optional<double> result;
  for (std::size_t index = firstVisible(); index < m_values.size(); ++index) {
    const double value = m_values.at(index).primary;
    if (!std::isfinite(value)) {
      continue;
    }
    if (!result.has_value() || value < *result) {
      result = value;
    }
  }
  return result;
}

std::optional<double> TraceHistory::maximum() const {
  std::optional<double> result;
  for (std::size_t index = firstVisible(); index < m_values.size(); ++index) {
    const double value = m_values.at(index).primary;
    if (!std::isfinite(value)) {
      continue;
    }
    if (!result.has_value() || value > *result) {
      result = value;
    }
  }
  return result;
}

std::optional<double> TraceHistory::secondaryMinimum() const {
  std::optional<double> result;
  for (std::size_t index = firstVisible(); index < m_values.size(); ++index) {
    const Entry& entry = m_values.at(index);
    if (!entry.hasSecondary || !std::isfinite(entry.secondary)) {
      continue;
    }
    if (!result.has_value() || entry.secondary < *result) {
      result = entry.secondary;
    }
  }
  return result;
}

std::optional<double> TraceHistory::secondaryMaximum() const {
  std::optional<double> result;
  for (std::size_t index = firstVisible(); index < m_values.size(); ++index) {
    const Entry& entry = m_values.at(index);
    if (!entry.hasSecondary || !std::isfinite(entry.secondary)) {
      continue;
    }
    if (!result.has_value() || entry.secondary > *result) {
      result = entry.secondary;
    }
  }
  return result;
}

std::optional<double> TraceHistory::visibleMinimum() const {
  return zoomedEdge(minimum(), maximum(), m_valueZoom, false);
}

std::optional<double> TraceHistory::visibleMaximum() const {
  return zoomedEdge(minimum(), maximum(), m_valueZoom, true);
}

std::optional<double> TraceHistory::secondaryVisibleMinimum() const {
  return zoomedEdge(secondaryMinimum(), secondaryMaximum(), m_valueZoom, false);
}

std::optional<double> TraceHistory::secondaryVisibleMaximum() const {
  return zoomedEdge(secondaryMinimum(), secondaryMaximum(), m_valueZoom, true);
}

std::vector<TraceSample> TraceHistory::samples() const {
  const std::optional<double> low = minimum();
  const std::optional<double> high = maximum();
  const std::optional<double> secondaryLow = secondaryMinimum();
  const std::optional<double> secondaryHigh = secondaryMaximum();
  const std::uint64_t start = windowStartMs();
  const std::size_t first = firstVisible();

  std::vector<TraceSample> result;
  result.reserve(m_values.size() - first);
  for (std::size_t index = first; index < m_values.size(); ++index) {
    const Entry& entry = m_values.at(index);
    TraceSample sample;
    sample.x = static_cast<double>(entry.timeMs - start) / static_cast<double>(m_timeSpanMs);
    if (low.has_value() && high.has_value() && std::isfinite(entry.primary)) {
      const double span = *high - *low;
      const double normalized = span > 0.0 ? (entry.primary - *low) / span : 0.5;
      sample.y = 0.5 + (normalized - 0.5) * m_valueZoom;
      sample.valid = true;
    }
    if (entry.hasSecondary && secondaryLow.has_value() && secondaryHigh.has_value() &&
        std::isfinite(entry.secondary)) {
      const double span = *secondaryHigh - *secondaryLow;
      const double normalized = span > 0.0 ? (entry.secondary - *secondaryLow) / span : 0.5;
      sample.secondaryY = 0.5 + (normalized - 0.5) * m_valueZoom;
      sample.secondaryValid = true;
    }
    result.push_back(sample);
  }
  return result;
}

} // namespace ampbeam
