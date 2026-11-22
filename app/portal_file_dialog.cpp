// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// XDG desktop portal file chooser implementation.
#include "portal_file_dialog.h"

#include <QFileInfo>
#include <QUrl>

#if defined(AMPBEAM_HAVE_QTDBUS)
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QRandomGenerator>
#include <QStringList>
#include <QVariantMap>
#endif

namespace ampbeam::portal {

QString requestPathForUniqueName(const QString& uniqueName, const QString& token) {
  QString sender = uniqueName;
  if (sender.startsWith(QLatin1Char(':'))) {
    sender.remove(0, 1);
  }
  sender.replace(QLatin1Char('.'), QLatin1Char('_'));
  return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
}

QVariantMap fileChooserOptions(const QString& token, bool save, const QString& defaultName) {
  QVariantMap options;
  options.insert(QStringLiteral("handle_token"), token);
  options.insert(QStringLiteral("modal"), true);
  if (save) {
    options.insert(QStringLiteral("current_name"), QFileInfo(defaultName).fileName());
  }
  return options;
}

namespace {

constexpr auto kPortalService = "org.freedesktop.portal.Desktop";
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kFileChooserInterface = "org.freedesktop.portal.FileChooser";
constexpr auto kRequestInterface = "org.freedesktop.portal.Request";

} // namespace

PortalFileDialog::PortalFileDialog(QObject* parent) : QObject(parent) {}

PortalFileDialog::~PortalFileDialog() {
#if defined(AMPBEAM_HAVE_QTDBUS)
  disconnectRequest();
#endif
}

bool PortalFileDialog::isAvailable() const {
#if defined(AMPBEAM_HAVE_QTDBUS)
  const QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.isConnected()) {
    return false;
  }
  QDBusConnectionInterface* interface = bus.interface();
  return interface != nullptr &&
         interface->isServiceRegistered(QString::fromLatin1(kPortalService)).value();
#else
  return false;
#endif
}

void PortalFileDialog::saveFile(const QString& title, const QString& defaultName) {
#if defined(AMPBEAM_HAVE_QTDBUS)
  start(true, title, defaultName);
#else
  Q_UNUSED(title)
  Q_UNUSED(defaultName)
  emit failed(tr("The desktop file chooser is not available on this platform."));
#endif
}

void PortalFileDialog::openFile(const QString& title) {
#if defined(AMPBEAM_HAVE_QTDBUS)
  start(false, title, QString{});
#else
  Q_UNUSED(title)
  emit failed(tr("The desktop file chooser is not available on this platform."));
#endif
}

#if defined(AMPBEAM_HAVE_QTDBUS)

void PortalFileDialog::start(bool save, const QString& title, const QString& defaultName) {
  if (m_pending) {
    emit failed(tr("A file chooser is already open."));
    return;
  }
  if (!isAvailable()) {
    emit failed(tr("The desktop file chooser portal is not available."));
    return;
  }

  const QString token = QStringLiteral("ampbeam_%1_%2")
                            .arg(QCoreApplication::applicationPid())
                            .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));

  const QVariantMap options = fileChooserOptions(token, save, defaultName);
  const QString expectedPath =
      requestPathForUniqueName(QDBusConnection::sessionBus().baseService(), token);

  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.connect(QString::fromLatin1(kPortalService), expectedPath,
                   QString::fromLatin1(kRequestInterface), QStringLiteral("Response"), this,
                   SLOT(handleResponse(uint, QVariantMap)))) {
    emit failed(tr("Cannot listen for the file chooser result."));
    return;
  }
  m_requestPath = expectedPath;
  m_pending = true;

  QDBusMessage message = QDBusMessage::createMethodCall(
      QString::fromLatin1(kPortalService), QString::fromLatin1(kPortalPath),
      QString::fromLatin1(kFileChooserInterface),
      save ? QStringLiteral("SaveFile") : QStringLiteral("OpenFile"));
  message << QString{} << title << options;

  QDBusPendingCall call = bus.asyncCall(message);
  auto* watcher = new QDBusPendingCallWatcher(call, this);
  QObject::connect(
      watcher, &QDBusPendingCallWatcher::finished, this,
      [this, watcher, expectedPath](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError()) {
          disconnectRequest();
          emit failed(reply.error().message());
          return;
        }
        const QString handle = reply.value().path();
        if (handle.isEmpty()) {
          disconnectRequest();
          emit failed(tr("The file chooser returned no request handle."));
          return;
        }
        if (handle == expectedPath) {
          return;
        }
        // Older portals use a different request path. Move the
        // subscription before the user can interact with the dialog.
        QDBusConnection connection = QDBusConnection::sessionBus();
        connection.disconnect(QString::fromLatin1(kPortalService), expectedPath,
                              QString::fromLatin1(kRequestInterface), QStringLiteral("Response"),
                              this, SLOT(handleResponse(uint, QVariantMap)));
        m_requestPath = handle;
        if (!connection.connect(QString::fromLatin1(kPortalService), handle,
                                QString::fromLatin1(kRequestInterface), QStringLiteral("Response"),
                                this, SLOT(handleResponse(uint, QVariantMap)))) {
          disconnectRequest();
          emit failed(tr("Cannot listen for the file chooser result."));
        }
      });
}

void PortalFileDialog::handleResponse(uint response, const QVariantMap& results) {
  disconnectRequest();
  switch (response) {
  case 0: {
    const QStringList uris = results.value(QStringLiteral("uris")).toStringList();
    if (uris.isEmpty()) {
      emit rejected();
      return;
    }
    const QUrl url(uris.constFirst());
    emit accepted(url.isLocalFile() ? url.toLocalFile() : uris.constFirst());
    return;
  }
  case 1:
    emit rejected();
    return;
  default:
    emit failed(tr("The file chooser was closed unexpectedly."));
    return;
  }
}

void PortalFileDialog::disconnectRequest() {
  if (m_requestPath.isEmpty()) {
    m_pending = false;
    return;
  }
  QDBusConnection::sessionBus().disconnect(
      QString::fromLatin1(kPortalService), m_requestPath, QString::fromLatin1(kRequestInterface),
      QStringLiteral("Response"), this, SLOT(handleResponse(uint, QVariantMap)));
  m_requestPath.clear();
  m_pending = false;
}

#endif

} // namespace ampbeam::portal
