# Copyright (C) 2026 Christian Mayer
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the dependency-free protocol library.
add_library(ampbeam_core STATIC
  src/capture.cpp
  src/csv.cpp
  src/decoder.cpp
  src/hex.cpp
  src/measurement.cpp
  src/meter.cpp
  src/monitor.cpp
  src/selection.cpp
  src/trace.cpp
)

target_include_directories(ampbeam_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(ampbeam_core PUBLIC cxx_std_23)

ampbeam_set_strict_warnings(ampbeam_core)
