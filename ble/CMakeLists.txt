# Copyright (C) 2026 Christian Mayer
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the Qt Bluetooth transport library.
add_library(ampbeam_ble STATIC
  bluez_le_discovery.cpp
  bluez_le_discovery.h
  device_scanner.cpp
  device_scanner.h
  multimeter_client.cpp
  multimeter_client.h
  permissions.cpp
  permissions.h
)

target_include_directories(ampbeam_ble PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(ampbeam_ble
  PUBLIC
    ampbeam_core
    Qt6::Core
    Qt6::Bluetooth
)

# Linux desktop only: BlueZ forgets unpaired devices when discovery stops, so a
# short discovery is held open while connecting. Other platforms compile a
# no-op stub instead.
find_package(Qt6 QUIET OPTIONAL_COMPONENTS DBus)
if(TARGET Qt6::DBus AND NOT ANDROID)
  target_link_libraries(ampbeam_ble PRIVATE Qt6::DBus)
  target_compile_definitions(ampbeam_ble PRIVATE AMPBEAM_HAVE_QTDBUS=1)
endif()

ampbeam_set_warnings(ampbeam_ble)
