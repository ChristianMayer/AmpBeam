# Copyright (C) 2026 Christian Mayer
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the ampbeam-cli capture and replay tool.
add_executable(ampbeam-cli main.cpp)

target_link_libraries(ampbeam-cli PRIVATE ampbeam_ble Qt6::Core)
target_compile_definitions(ampbeam-cli PRIVATE AMPBEAM_VERSION="${PROJECT_VERSION}")

ampbeam_set_warnings(ampbeam-cli)

install(TARGETS ampbeam-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
