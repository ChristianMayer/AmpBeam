# Copyright (C) 2026 Christian Mayer
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Registers the unit, black-box, bench and coverage test targets.
find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

# ---------------------------------------------------------------------------
# White-box suite: drives every branch of ampbeam_core with constructed frames.
# ---------------------------------------------------------------------------
add_executable(tst_core_whitebox tst_core_whitebox.cpp)
target_link_libraries(tst_core_whitebox PRIVATE ampbeam_core Qt6::Test)
ampbeam_set_strict_warnings(tst_core_whitebox)

# ---------------------------------------------------------------------------
# Black-box suite: only public behaviour, driven by captured hardware fixtures.
# ---------------------------------------------------------------------------
add_executable(tst_core_blackbox tst_core_blackbox.cpp)
target_link_libraries(tst_core_blackbox PRIVATE ampbeam_core Qt6::Test)
target_compile_definitions(tst_core_blackbox PRIVATE
  AMPBEAM_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
ampbeam_set_warnings(tst_core_blackbox)

# ---------------------------------------------------------------------------
# Black-box CLI suite: runs the real ampbeam-cli binary as a subprocess and
# checks documented command-line behaviour end to end.
# ---------------------------------------------------------------------------
add_executable(tst_cli tst_cli.cpp)
target_link_libraries(tst_cli PRIVATE Qt6::Core Qt6::Test)
target_compile_definitions(tst_cli PRIVATE
  AMPBEAM_CLI_PATH="$<TARGET_FILE:ampbeam-cli>"
)
add_dependencies(tst_cli ampbeam-cli)
ampbeam_set_warnings(tst_cli)

add_test(NAME core-whitebox COMMAND tst_core_whitebox)
add_test(NAME core-blackbox COMMAND tst_core_blackbox)
add_test(NAME cli-blackbox COMMAND tst_cli)

# ---------------------------------------------------------------------------
# Headless unit tests for the transport's state and error handling. Anything
# that needs an adapter stays in the hardware bench suite.
# ---------------------------------------------------------------------------
add_executable(tst_ble tst_ble.cpp)
target_link_libraries(tst_ble PRIVATE ampbeam_ble Qt6::Test)
ampbeam_set_warnings(tst_ble)
add_test(NAME ble-unit COMMAND tst_ble)

# ---------------------------------------------------------------------------
# Pure request-construction tests for the desktop portal file chooser. The
# portal itself needs a running desktop session, so only the deterministic
# helpers are tested here.
# ---------------------------------------------------------------------------
find_package(Qt6 QUIET OPTIONAL_COMPONENTS DBus)
if(TARGET Qt6::DBus AND NOT ANDROID)
  add_executable(tst_portal
    tst_portal.cpp
    ${PROJECT_SOURCE_DIR}/app/portal_file_dialog.cpp
    ${PROJECT_SOURCE_DIR}/app/portal_file_dialog.h
  )
  target_include_directories(tst_portal PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/app
  )
  target_link_libraries(tst_portal PRIVATE Qt6::Core Qt6::DBus Qt6::Qml Qt6::Test)
  target_compile_definitions(tst_portal PRIVATE AMPBEAM_HAVE_QTDBUS=1)
  ampbeam_set_warnings(tst_portal)
  add_test(NAME portal-unit COMMAND tst_portal)
endif()

# ---------------------------------------------------------------------------
# Headless tests for the Qt Quick controller's notification pipeline.
# ---------------------------------------------------------------------------
add_executable(tst_app
  tst_app.cpp
  ${PROJECT_SOURCE_DIR}/app/app_controller.cpp
  ${PROJECT_SOURCE_DIR}/app/app_controller.h
)
target_include_directories(tst_app PRIVATE
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/app
)
target_link_libraries(tst_app PRIVATE ampbeam_ble Qt6::Qml Qt6::Test)
ampbeam_set_warnings(tst_app)
add_test(NAME app-unit COMMAND tst_app)

# ---------------------------------------------------------------------------
# Coverage gate. Enabled with the linux-coverage preset; it runs after every
# instrumented suite has written its .gcda files and fails unless each file in
# tests/coverage/required.txt reports 100% line coverage.
# ---------------------------------------------------------------------------
if(AMPBEAM_ENABLE_COVERAGE)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(
    NAME coverage-gate
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/coverage/coverage_gate.py
            --build-dir ${CMAKE_BINARY_DIR}
            --source-root ${CMAKE_SOURCE_DIR}
  )
  set_tests_properties(coverage-gate PROPERTIES
    LABELS coverage
    DEPENDS "core-whitebox;core-blackbox;cli-blackbox;ble-unit;app-unit"
  )
endif()

# Bench test: drives a real meter through the measurement modes its Select key can
# reach and checks that every frame decodes. It needs hardware, so it is only
# registered when a device address is supplied at configure time:
#
#   AMPBEAM_BENCH_DEVICE=<address> cmake --preset linux-debug
#   ctest --preset linux-debug -L bench
#
# Without that variable nothing is added, which keeps CI hardware-free.
if(DEFINED ENV{AMPBEAM_BENCH_DEVICE})
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(
    NAME bench_mode_sweep
    COMMAND
      ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/bench/mode_sweep.py
      --device $ENV{AMPBEAM_BENCH_DEVICE}
      --cli $<TARGET_FILE:ampbeam-cli>
      --codes 1 1 1 1 1 1
      --duration 3
  )
  set_tests_properties(bench_mode_sweep PROPERTIES LABELS bench TIMEOUT 900)
  message(STATUS "Bench test enabled for device $ENV{AMPBEAM_BENCH_DEVICE}")
endif()
