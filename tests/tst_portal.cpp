// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the pure parts of the desktop portal file chooser.
#include "portal_file_dialog.h"

#include <QTest>
#include <QVariantMap>

class PortalFileDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void requestPathEncodesUniqueName();
  void saveOptionsCarryCurrentName();
  void openOptionsHaveNoCurrentName();
  void availabilityCheckDoesNotCrash();
};

void PortalFileDialogTest::requestPathEncodesUniqueName() {
  QCOMPARE(ampbeam::portal::requestPathForUniqueName(QStringLiteral(":1.42"),
                                                     QStringLiteral("ampbeam_7_ab")),
           QStringLiteral("/org/freedesktop/portal/desktop/request/1_42/ampbeam_7_ab"));

  // A well-known name has no leading colon; dots still become underscores.
  QCOMPARE(ampbeam::portal::requestPathForUniqueName(QStringLiteral("org.example.App"),
                                                     QStringLiteral("tok")),
           QStringLiteral("/org/freedesktop/portal/desktop/request/org_example_App/tok"));
}

void PortalFileDialogTest::saveOptionsCarryCurrentName() {
  const QVariantMap options = ampbeam::portal::fileChooserOptions(
      QStringLiteral("tok"), true, QStringLiteral("/tmp/somewhere/readings.csv"));
  QCOMPARE(options.value(QStringLiteral("handle_token")).toString(), QStringLiteral("tok"));
  QCOMPARE(options.value(QStringLiteral("modal")).toBool(), true);
  QCOMPARE(options.value(QStringLiteral("current_name")).toString(),
           QStringLiteral("readings.csv"));
}

void PortalFileDialogTest::openOptionsHaveNoCurrentName() {
  const QVariantMap options = ampbeam::portal::fileChooserOptions(
      QStringLiteral("tok"), false, QStringLiteral("/tmp/ignored.csv"));
  QCOMPARE(options.value(QStringLiteral("handle_token")).toString(), QStringLiteral("tok"));
  QVERIFY(!options.contains(QStringLiteral("current_name")));
}

void PortalFileDialogTest::availabilityCheckDoesNotCrash() {
  // The answer depends on the session bus; the call itself must be safe even
  // when no portal is running.
  ampbeam::portal::PortalFileDialog dialog;
  (void)dialog.isAvailable();
}

QTEST_GUILESS_MAIN(PortalFileDialogTest)

#include "tst_portal.moc"
