// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Application entry point and window icon selection.
#include "app_controller.h"

#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QString>

int main(int argc, char* argv[]) {
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
