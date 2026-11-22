// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Application entry point and window icon selection.
#include "app_controller.h"

#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QString>

int main(int argc, char* argv[]) {
  // Qt logs this informational message when the process cannot open the BlueZ
  // management socket, which needs CAP_NET_ADMIN. ampbeam talks to BlueZ over
  // D-Bus and does not need random/public address classification, so hide the
  // message while preserving any other logging rules from the environment.
  QString loggingRules = qEnvironmentVariable("QT_LOGGING_RULES");
  loggingRules.replace(QLatin1Char(';'), QLatin1Char('\n'));
  if (!loggingRules.contains(QStringLiteral("qt.bluetooth.bluez"))) {
    if (!loggingRules.isEmpty()) {
      loggingRules += QLatin1Char('\n');
    }
    loggingRules += QStringLiteral("qt.bluetooth.bluez.info=false");
  }
  QLoggingCategory::setFilterRules(loggingRules);

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
  // Qt does not choose a Linux platform theme on its own, so a QML FileDialog
  // would fall back to the generic Qt Quick chooser. The file choosers below
  // use the desktop portal directly; on GTK desktops, also select the GTK
  // theme so a fallback QML dialog is native. Other desktops keep Qt's
  // auto-selected theme (Kubuntu's Qt, for example, has no xdgdesktopportal
  // plugin). QT_QPA_PLATFORMTHEME overrides the choice.
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME")) {
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
    const bool gtkDesktop =
        desktop.contains(QStringLiteral("gnome")) || desktop.contains(QStringLiteral("unity")) ||
        desktop.contains(QStringLiteral("xfce")) || desktop.contains(QStringLiteral("mate")) ||
        desktop.contains(QStringLiteral("cinnamon")) ||
        desktop.contains(QStringLiteral("budgie")) || desktop.contains(QStringLiteral("lxde")) ||
        desktop.contains(QStringLiteral("pantheon"));
    if (gtkDesktop) {
      qputenv("QT_QPA_PLATFORMTHEME", "gtk3");
    }
  }
#endif

  QGuiApplication app(argc, argv);

  QCoreApplication::setOrganizationName(QStringLiteral("AmpBeam"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("ampbeam.invalid"));
  QCoreApplication::setApplicationName(QStringLiteral("AmpBeam"));
  QCoreApplication::setApplicationVersion(QStringLiteral(AMPBEAM_VERSION));

  // Window/task-switcher icon, compiled in from assets/branding/ (see
  // app/CMakeLists.txt). Order matters: the filter-free SVG first (Qt's SVG
  // renderer ignores filters and shadows), then the rich SVG, then the PNG for
  // deployments without the Qt SVG image plugin. A checkout without artwork
  // keeps the platform default.
  for (const QString& icon : {QStringLiteral(":/branding/ampbeam-icon-flat.svg"),
                              QStringLiteral(":/branding/ampbeam-icon.svg"),
                              QStringLiteral(":/branding/ampbeam-icon.png")}) {
    // Existence is checked before constructing QIcon: the constructor itself
    // warns when the resource is absent, which would spam stderr for every
    // artwork file that has not been delivered yet.
    if (!QFile::exists(icon)) {
      continue;
    }
    const QIcon candidate(icon);
    if (!candidate.isNull()) {
      app.setWindowIcon(candidate);
      break;
    }
  }

  QQuickStyle::setStyle(QStringLiteral("Basic"));

  // AppController is registered by qt_add_qml_module via QML_ELEMENT.
  QQmlApplicationEngine engine;
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

  engine.loadFromModule("AmpBeam", "Main");

  if (engine.rootObjects().isEmpty()) {
    return -1;
  }
  return app.exec();
}
