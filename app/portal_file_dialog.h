// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Native file chooser through the XDG desktop portal.
#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqml.h>

namespace ampbeam::portal {

/// Request path derived from the caller's unique bus name and `handle_token`.
/// `:1.42` and `ampbeam_7_ab` become
/// `/org/freedesktop/portal/desktop/request/1_42/ampbeam_7_ab`.
[[nodiscard]] QString requestPathForUniqueName(const QString& uniqueName, const QString& token);

/// Options vardict for `SaveFile`/`OpenFile`. `current_name` is only set for a
/// save dialog.
[[nodiscard]] QVariantMap fileChooserOptions(const QString& token, bool save,
                                             const QString& defaultName);

/// Opens the desktop's own file chooser through `org.freedesktop.portal.FileChooser`.
///
/// Qt Quick's `FileDialog` can only use a native dialog when the Qt platform
/// theme exposes one. Some distributions (for example Kubuntu) ship a KDE
/// platform theme without `xdgdesktopportal`, so the QML dialog silently falls
/// back to the Qt Quick chooser. Calling the portal directly avoids that and
/// gives the real KDE/GNOME dialog. QML keeps the Qt Quick dialog as a fallback
/// when no portal is available.
class PortalFileDialog : public QObject {
  Q_OBJECT
  QML_ELEMENT

public:
  explicit PortalFileDialog(QObject* parent = nullptr);
  ~PortalFileDialog() override;

  /// `true` when a session bus with `org.freedesktop.portal.Desktop` is present.
  [[nodiscard]] Q_INVOKABLE bool isAvailable() const;

  Q_INVOKABLE void saveFile(const QString& title, const QString& defaultName);
  Q_INVOKABLE void openFile(const QString& title);

signals:
  /// `filePath` is a local path when the portal returned a `file://` URI.
  void accepted(const QString& filePath);
  void rejected();
  void failed(const QString& message);

private slots:
#if defined(AMPBEAM_HAVE_QTDBUS)
  void handleResponse(uint response, const QVariantMap& results);
#endif

private:
#if defined(AMPBEAM_HAVE_QTDBUS)
  void start(bool save, const QString& title, const QString& defaultName);
  void disconnectRequest();
#endif

  bool m_pending = false;
  QString m_requestPath;
};

} // namespace ampbeam::portal
