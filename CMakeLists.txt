# Copyright (C) 2026 Christian Mayer
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Top-level build configuration for the AmpBeam targets and tests.
cmake_minimum_required(VERSION 3.21)

project(AmpBeam
  VERSION 0.1.0
  DESCRIPTION "Linux and Android BLE multimeter bindings and application"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Line coverage for the testable code. The `linux-coverage` preset turns this on
# and `tests/coverage/coverage_gate.py` enforces the result; see docs/testing.md.
option(AMPBEAM_ENABLE_COVERAGE "Instrument C++ code with gcov line coverage" OFF)
if(AMPBEAM_ENABLE_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(WARNING "AMPBEAM_ENABLE_COVERAGE expects GCC and gcov; "
                    "measured with ${CMAKE_CXX_COMPILER_ID}.")
  endif()
  add_compile_options(--coverage -O0 -g)
  add_link_options(--coverage)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Bluetooth Quick QuickControls2 Qml)
qt_standard_project_setup()

if(ANDROID)
  # Unit tests are host-only; never cross-compile them for the device.
  set(AMPBEAM_BUILD_TESTS OFF CACHE BOOL "Build unit tests" FORCE)
else()
  option(AMPBEAM_BUILD_TESTS "Build unit tests" ON)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

add_subdirectory(core)
add_subdirectory(ble)
add_subdirectory(cli)
add_subdirectory(app)

if(AMPBEAM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
