// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// CSV export of decoded readings.
#pragma once

#include <string>
#include <string_view>

#include "measurement.hpp"

namespace ampbeam {

/// Header line for `csvRow`, without a trailing newline.
[[nodiscard]] std::string csvHeader();

/// One CSV row for a decoded reading, without a trailing newline.
///
/// `receivedAtIso` is a human readable timestamp (normally ISO 8601 UTC); the
/// numeric `Measurement::receivedAtMs` is exported next to it. A reading with no
/// number (blank display or overload) leaves the value column empty instead of
/// writing `nan`. Fields that need it are quoted.
[[nodiscard]] std::string csvRow(const Measurement& measurement, std::string_view receivedAtIso);

} // namespace ampbeam
