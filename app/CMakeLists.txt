# Copyright (C) 2026 Christian Mayer
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the Qt Quick application and its platform resources.
qt_add_executable(ampbeam
  main.cpp
  app_controller.cpp
  app_controller.h
  portal_file_dialog.cpp
  portal_file_dialog.h
)

# RESOURCE_PREFIX puts the generated qmldir where QQmlEngine looks for it
# (qrc:/qt/qml). Without it the module is only found through the build
# directory's AmpBeam/ folder, so an installed or AppImage copy cannot resolve
# AmpBeam.Main.
qt_add_qml_module(ampbeam
  URI AmpBeam
  VERSION 1.0
  RESOURCE_PREFIX "/qt/qml"
  QML_FILES
    Main.qml
    AppButton.qml
    IndicatorChip.qml
    MeterDisplay.qml
    TraceView.qml
    DeviceDialog.qml
)

target_link_libraries(ampbeam
  PRIVATE
    ampbeam_ble
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2
    Qt6::Qml
)

target_compile_definitions(ampbeam PRIVATE AMPBEAM_VERSION="${PROJECT_VERSION}")

# Native file chooser through the desktop portal. This covers desktop themes
# that do not expose a native QFileDialog (for example the KDE theme shipped on
# Kubuntu). Qt Quick's FileDialog remains the fallback when no portal is
# present or the build has no QtDBus.
find_package(Qt6 QUIET OPTIONAL_COMPONENTS DBus)
if(TARGET Qt6::DBus AND NOT ANDROID)
  target_link_libraries(ampbeam PRIVATE Qt6::DBus)
  target_compile_definitions(ampbeam PRIVATE AMPBEAM_HAVE_QTDBUS=1)
endif()

ampbeam_set_warnings(ampbeam)

# ---------------------------------------------------------------------------
# Branding
# ---------------------------------------------------------------------------
# The canonical artwork lives in assets/branding/. File names and the Android
# icon derivation are documented in assets/branding/README.md.
#
# Every hook below is guarded by EXISTS(), so a checkout without artwork still
# configures and builds; dropping the files in is enough to enable them.
set(AMPBEAM_BRAND_DIR "${PROJECT_SOURCE_DIR}/assets/branding")

# Compiled into the binary under qrc:/branding/. The SVG is preferred at
# runtime; the PNG covers deployments without the Qt SVG image plugin. Both
# live outside this directory, so each needs an explicit resource alias to be
# addressable by relative path.
set(AMPBEAM_WINDOW_ICONS)
foreach(icon IN ITEMS ampbeam-icon-flat.svg ampbeam-icon.svg ampbeam-icon.png)
  if(EXISTS "${AMPBEAM_BRAND_DIR}/${icon}")
    set_source_files_properties("${AMPBEAM_BRAND_DIR}/${icon}"
      PROPERTIES QT_RESOURCE_ALIAS "${icon}")
    list(APPEND AMPBEAM_WINDOW_ICONS "${AMPBEAM_BRAND_DIR}/${icon}")
  endif()
endforeach()

if(AMPBEAM_WINDOW_ICONS)
  qt_add_resources(ampbeam "branding"
    PREFIX "/branding"
    FILES ${AMPBEAM_WINDOW_ICONS}
  )
endif()

# Freedesktop icon theme entry for Icon=ampbeam in the .desktop file below.
if(EXISTS "${AMPBEAM_BRAND_DIR}/ampbeam-icon.svg")
  install(FILES "${AMPBEAM_BRAND_DIR}/ampbeam-icon.svg"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
    RENAME ampbeam.svg
  )
endif()

if(EXISTS "${AMPBEAM_BRAND_DIR}/ampbeam.icns")
  set_source_files_properties("${AMPBEAM_BRAND_DIR}/ampbeam.icns"
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  target_sources(ampbeam PRIVATE "${AMPBEAM_BRAND_DIR}/ampbeam.icns")
  set_target_properties(ampbeam PROPERTIES
    MACOSX_BUNDLE_ICON_FILE "ampbeam.icns")
endif()

# Android requires a monotonically increasing integer version code. Map
# 0.1.0 to 100 so a new release can be installed over the previous one.
math(EXPR AMPBEAM_ANDROID_VERSION_CODE
  "${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH}")

set_target_properties(ampbeam PROPERTIES
  WIN32_EXECUTABLE TRUE
  MACOSX_BUNDLE TRUE
  QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
  QT_ANDROID_MIN_SDK_VERSION 24
  QT_ANDROID_TARGET_SDK_VERSION 34
  QT_ANDROID_VERSION_CODE "${AMPBEAM_ANDROID_VERSION_CODE}"
  QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
)

install(TARGETS ampbeam
  BUNDLE DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Desktop entry: resolves Icon=ampbeam through the icon theme once installed.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/ampbeam.desktop"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
)

# AppStream metadata so software centres can describe the installed
# application. The AppImage script also installs a legacy *.appdata.xml copy,
# which older AppImage tooling looks for.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/ampbeam.metainfo.xml"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo"
)
