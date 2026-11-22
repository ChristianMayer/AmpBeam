// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ampbeam-cli: scan, capture, key/command injection, import and replay.
#include "ampbeam/core/capture.hpp"
#include "ampbeam/core/decoder.hpp"
#include "ampbeam/core/hex.hpp"
#include "ampbeam/core/meter.hpp"
#include "ampbeam/core/profile.hpp"
#include "ampbeam/core/selection.hpp"

#include "ble/device_scanner.h"
#include "ble/multimeter_client.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QStringConverter>
#include <QTextStream>
#include <QTimer>
#include <QVariantMap>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

QTextStream& out() {
  static QTextStream stream(stdout);
  static const bool initialized = [] {
    stream.setEncoding(QStringConverter::Utf8);
    return true;
  }();
  Q_UNUSED(initialized)
  return stream;
}

QTextStream& err() {
  static QTextStream stream(stderr);
  static const bool initialized = [] {
    stream.setEncoding(QStringConverter::Utf8);
    return true;
  }();
  Q_UNUSED(initialized)
  return stream;
}

void emitFrame(qint64 receivedAtMs, const QByteArray& data, const ampbeam::FrameDecoder& decoder) {
  const auto* begin = reinterpret_cast<const std::uint8_t*>(data.constData());
  const std::span<const std::uint8_t> bytes(begin, static_cast<std::size_t>(data.size()));
  const auto decoded = decoder.decode(bytes);

  out() << receivedAtMs << '\t' << data.size() << '\t'
        << QString::fromStdString(ampbeam::toHex(bytes, ' ')) << '\t'
        << QString::fromStdString(decoded.detail) << '\n';
  out().flush();
}

int replay(const QString& path, const ampbeam::FrameDecoder& decoder) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    err() << "Cannot open fixture: " << path << '\n';
    return 2;
  }

  QJsonParseError parseError{};
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    err() << "Invalid fixture JSON: " << parseError.errorString() << '\n';
    return 2;
  }

  const QJsonArray frames = document.object().value(QStringLiteral("frames")).toArray();
  qint64 fallbackTime = QDateTime::currentMSecsSinceEpoch();
  for (qsizetype index = 0; index < frames.size(); ++index) {
    const QJsonObject frame = frames.at(index).toObject();
    const qint64 receivedAtMs =
        static_cast<qint64>(frame.value(QStringLiteral("receivedMs")).toDouble(fallbackTime));
    const auto bytes =
        ampbeam::fromHex(frame.value(QStringLiteral("hex")).toString().toStdString());
    if (!bytes.has_value()) {
      err() << "Skipping malformed frame at index " << index << '\n';
      continue;
    }
    emitFrame(receivedAtMs,
              QByteArray(reinterpret_cast<const char*>(bytes->data()),
                         static_cast<qsizetype>(bytes->size())),
              decoder);
  }
  return 0;
}

} // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("ampbeam-cli"));
  QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("BLE tracer for the supported multimeter protocol.\n"
                     "Output format: <epochMs>TAB<length>TAB<hex>TAB<detail>"));
  parser.addHelpOption();
  parser.addVersionOption();

  const QCommandLineOption scanOption({QStringLiteral("s"), QStringLiteral("scan")},
                                      QStringLiteral("Scan for BLE devices and exit."));
  const QCommandLineOption scanTimeoutOption(
      QStringLiteral("scan-timeout"),
      QStringLiteral("Scan duration in milliseconds (default 12000)."), QStringLiteral("ms"),
      QStringLiteral("12000"));
  const QCommandLineOption deviceOption(
      {QStringLiteral("d"), QStringLiteral("device")},
      QStringLiteral("Connect to this BLE address, or \"auto\" to use the only discovered "
                     "device that advertises the meter service."),
      QStringLiteral("address"));
  const QCommandLineOption nameOption(
      QStringLiteral("name"),
      QStringLiteral("Advertised device name; with --device auto it also selects the matching "
                     "device."),
      QStringLiteral("name"));
  const QCommandLineOption serviceOption(QStringLiteral("service-uuid"),
                                         QStringLiteral("Override the service UUID."),
                                         QStringLiteral("uuid"));
  const QCommandLineOption dataOption({QStringLiteral("notify-uuid"), QStringLiteral("data-uuid")},
                                      QStringLiteral("Override the meter->app data UUID."),
                                      QStringLiteral("uuid"));
  const QCommandLineOption commandOption(
      {QStringLiteral("write-uuid"), QStringLiteral("command-uuid")},
      QStringLiteral("Override the app->meter command UUID."), QStringLiteral("uuid"));
  const QCommandLineOption keyUuidOption(QStringLiteral("key-uuid"),
                                         QStringLiteral("Override the front-panel key UUID."),
                                         QStringLiteral("uuid"));
  const QCommandLineOption infoOption(QStringLiteral("info-uuid"),
                                      QStringLiteral("Override the device info UUID."),
                                      QStringLiteral("uuid"));
  const QCommandLineOption sendOption(
      QStringLiteral("send"), QStringLiteral("Send a hex payload after subscribing (repeatable)."),
      QStringLiteral("hex"));
  const QCommandLineOption keyOption(
      QStringLiteral("key"),
      QStringLiteral("Send a front-panel key press after subscribing (decimal or 0x hex). "
                     "Repeatable; presses are staggered by --key-delay."),
      QStringLiteral("code"));
  const QCommandLineOption keyReleaseOption(
      QStringLiteral("key-release"),
      QStringLiteral("Also send the [code,0] release write 200 ms after each press. "
                     "Not needed by the tested meter: the release negates the press."));
  const QCommandLineOption keyDelayOption(
      QStringLiteral("key-delay"),
      QStringLiteral("Milliseconds between repeated --key presses (default 1500)."),
      QStringLiteral("ms"), QStringLiteral("1500"));
  const QCommandLineOption challengeOption(
      QStringLiteral("challenge"),
      QStringLiteral("Run the compatibility challenge on the command characteristic: write a "
                     "16 byte nonce, read the answer back and log both. See docs/protocol.md."));
  const QCommandLineOption durationOption(
      QStringLiteral("duration"), QStringLiteral("Exit after N seconds (0 = until Ctrl+C)."),
      QStringLiteral("seconds"), QStringLiteral("0"));
  const QCommandLineOption replayOption(
      QStringLiteral("replay"),
      QStringLiteral("Replay frames from a JSON fixture instead of using BLE."),
      QStringLiteral("file"));
  const QCommandLineOption decoderOption(QStringLiteral("decoder"),
                                         QStringLiteral("Decoder: raw or meter (default raw)."),
                                         QStringLiteral("name"), QStringLiteral("raw"));
  const QCommandLineOption importOption(
      QStringLiteral("import"),
      QStringLiteral("Convert a capture log into a JSON fixture and exit."), QStringLiteral("log"));
  const QCommandLineOption outputOption(
      QStringLiteral("output"), QStringLiteral("Fixture output path (required with --import)."),
      QStringLiteral("file"));
  const QCommandLineOption fixtureNameOption(QStringLiteral("fixture-name"),
                                             QStringLiteral("Name stored in the fixture."),
                                             QStringLiteral("name"), QStringLiteral("capture"));
  const QCommandLineOption sourceOption(
      QStringLiteral("source"), QStringLiteral("Optional source note stored in the fixture."),
      QStringLiteral("text"));

  parser.addOptions({scanOption,      scanTimeoutOption, deviceOption,  nameOption,
                     serviceOption,   dataOption,        commandOption, keyUuidOption,
                     infoOption,      sendOption,        keyOption,     keyReleaseOption,
                     keyDelayOption,  durationOption,    replayOption,  decoderOption,
                     challengeOption, importOption,      outputOption,  fixtureNameOption,
                     sourceOption});
  parser.process(app);

  if (parser.isSet(importOption)) {
    if (!parser.isSet(outputOption)) {
      err() << "--import requires --output\n";
      return 1;
    }

    const QString inputPath = parser.value(importOption);
    QFile input(inputPath);
    if (!input.open(QIODevice::ReadOnly)) {
      err() << "Cannot open capture log: " << inputPath << '\n';
      return 2;
    }

    const QByteArray content = input.readAll();
    const ampbeam::CaptureParseResult capture = ampbeam::parseCaptureLog(
        std::string_view(content.constData(), static_cast<std::size_t>(content.size())));

    for (const auto& warning : capture.warnings) {
      err() << QString::fromStdString(warning) << '\n';
    }
    if (capture.frames.empty()) {
      err() << "No frames found in " << inputPath << '\n';
      return 2;
    }

    ampbeam::FixtureMetadata metadata;
    metadata.name = parser.value(fixtureNameOption).toStdString();
    metadata.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    metadata.source = parser.value(sourceOption).toStdString();

    const QString outputPath = parser.value(outputOption);
    QFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      err() << "Cannot write fixture: " << outputPath << '\n';
      return 2;
    }

    const std::string json = ampbeam::toFixtureJson(capture, metadata);
    output.write(json.data(), static_cast<qsizetype>(json.size()));
    output.close();

    out() << "wrote " << static_cast<qulonglong>(capture.frames.size()) << " frame(s) to "
          << outputPath << '\n';
    return 0;
  }

  std::unique_ptr<ampbeam::FrameDecoder> decoder;
  if (parser.value(decoderOption) == QStringLiteral("meter")) {
    decoder = std::make_unique<ampbeam::MeterDecoder>();
  } else if (parser.value(decoderOption) == QStringLiteral("raw")) {
    decoder = std::make_unique<ampbeam::RawDecoder>();
  } else {
    err() << "Unknown decoder: " << parser.value(decoderOption) << '\n';
    return 1;
  }

  if (parser.isSet(replayOption)) {
    return replay(parser.value(replayOption), *decoder);
  }

  const int durationSeconds = parser.value(durationOption).toInt();
  if (durationSeconds > 0) {
    QTimer::singleShot(durationSeconds * 1000, &app, [&app]() { app.quit(); });
  }

  if (parser.isSet(scanOption)) {
    const int timeoutMs = parser.value(scanTimeoutOption).toInt();
    ampbeam::ble::DeviceScanner scanner;
    QObject::connect(
        &scanner, &ampbeam::ble::DeviceScanner::deviceDiscovered,
        [](const QString& address, const QString& name, int rssi, const QString& services) {
          out() << QStringLiteral("rssi=%1\t%2\t%3\t%4")
                       .arg(QString::number(rssi),
                            name.isEmpty() ? QStringLiteral("(unnamed)") : name, address,
                            services.isEmpty() ? QStringLiteral("-") : services)
                << '\n';
          out().flush();
        });
    QObject::connect(&scanner, &ampbeam::ble::DeviceScanner::errorOccurred,
                     [](const QString& message) { err() << "scan error: " << message << '\n'; });

    QTimer::singleShot(timeoutMs, &scanner, &ampbeam::ble::DeviceScanner::stop);
    QTimer::singleShot(timeoutMs + 250, &app, [&app]() { app.quit(); });

    err() << "Scanning for " << timeoutMs << " ms...\n";
    scanner.start(timeoutMs);
    return app.exec();
  }

  if (!parser.isSet(deviceOption)) {
    parser.showHelp(1);
  }

  const ampbeam::GattProfile& fallback = ampbeam::meterBleProfile;
  const QString serviceUuid =
      parser.isSet(serviceOption)
          ? parser.value(serviceOption)
          : QString::fromLatin1(fallback.serviceUuid.data(),
                                static_cast<int>(fallback.serviceUuid.size()));
  QString deviceAddress = parser.value(deviceOption);
  QString deviceName = parser.value(nameOption);

  if (deviceAddress == QStringLiteral("auto")) {
    const int timeoutMs = qMax(1000, parser.value(scanTimeoutOption).toInt());
    ampbeam::ble::DeviceScanner scanner;
    QEventLoop loop;
    QString scanError;
    QObject::connect(&scanner, &ampbeam::ble::DeviceScanner::errorOccurred, &loop,
                     [&scanError](const QString& message) { scanError = message; });
    QObject::connect(&scanner, &ampbeam::ble::DeviceScanner::runningChanged, &loop,
                     [&scanner, &loop]() {
                       if (!scanner.isRunning()) {
                         loop.quit();
                       }
                     });
    QTimer::singleShot(timeoutMs, &scanner, &ampbeam::ble::DeviceScanner::stop);
    QTimer::singleShot(timeoutMs + 500, &loop, &QEventLoop::quit);

    err() << "Scanning for " << timeoutMs << " ms to select the meter...\n";
    scanner.start(timeoutMs);
    loop.exec();

    const QVariantList discovered = scanner.devices();
    std::vector<std::string> addresses;
    std::vector<std::string> names;
    std::vector<std::string> services;
    addresses.reserve(static_cast<std::size_t>(discovered.size()));
    names.reserve(static_cast<std::size_t>(discovered.size()));
    services.reserve(static_cast<std::size_t>(discovered.size()));
    for (const QVariant& value : discovered) {
      const QVariantMap device = value.toMap();
      addresses.push_back(device.value(QStringLiteral("address")).toString().toStdString());
      names.push_back(device.value(QStringLiteral("name")).toString().toStdString());
      services.push_back(device.value(QStringLiteral("services")).toString().toStdString());
    }

    std::vector<ampbeam::DeviceCandidate> candidates;
    candidates.reserve(addresses.size());
    for (std::size_t index = 0; index < addresses.size(); ++index) {
      candidates.push_back({addresses[index], names[index], services[index]});
    }

    const std::string service = serviceUuid.toStdString();
    const std::string nameFilter = deviceName.toStdString();
    const ampbeam::DeviceSelectionResult selection =
        ampbeam::selectMeterDevice(candidates, service, nameFilter);
    if (selection.status == ampbeam::DeviceSelectionStatus::Selected) {
      deviceAddress = QString::fromStdString(std::string(selection.address));
      deviceName = QString::fromStdString(std::string(selection.name));
      err() << "Selected " << (deviceName.isEmpty() ? deviceAddress : deviceName) << " ("
            << deviceAddress << ")";
      if (!selection.matchedService) {
        err() << (selection.matchedName ? " — matched --name" : " — only discovered device")
              << "; its advertisement did not list " << serviceUuid;
      }
      err() << '\n';
    } else if (selection.status == ampbeam::DeviceSelectionStatus::Multiple) {
      if (selection.matchedName) {
        err() << "Found " << selection.candidateCount << " devices named "
              << (deviceName.isEmpty() ? QStringLiteral("<empty>") : deviceName)
              << "; pass --device <address> (use --scan to list them)\n";
      } else if (selection.matchedService) {
        err() << "Found " << selection.candidateCount << " devices advertising " << serviceUuid
              << "; pass --device <address> (use --scan to list them)\n";
      } else {
        err() << "Found " << selection.discoveredCount << " BLE devices but none advertised "
              << serviceUuid << "; pass --device <address> (use --scan to list them)\n";
      }
      return 1;
    } else {
      if (!deviceName.isEmpty()) {
        err() << "No device named " << deviceName << " found";
      } else {
        err() << "No BLE device found";
      }
      if (!scanError.isEmpty()) {
        err() << ": " << scanError;
      }
      err() << '\n';
      return 1;
    }
  }

  ampbeam::ble::MultimeterClient client;
  if (parser.isSet(serviceOption) || parser.isSet(dataOption) || parser.isSet(commandOption) ||
      parser.isSet(keyUuidOption) || parser.isSet(infoOption)) {
    const auto pick = [&parser](const QCommandLineOption& option, std::string_view uuid) {
      return parser.isSet(option) ? parser.value(option)
                                  : QString::fromLatin1(uuid.data(), static_cast<int>(uuid.size()));
    };
    client.setGattProfile(pick(serviceOption, fallback.serviceUuid),
                          pick(dataOption, fallback.dataCharacteristicUuid),
                          pick(commandOption, fallback.commandCharacteristicUuid),
                          pick(keyUuidOption, fallback.keyCharacteristicUuid),
                          pick(infoOption, fallback.infoCharacteristicUuid));
  }

  const bool useMeterFraming = parser.value(decoderOption) == QStringLiteral("meter");
  auto framer = std::make_shared<ampbeam::MeterFramer>();

  QObject::connect(&client, &ampbeam::ble::MultimeterClient::stateChanged,
                   [&client, &parser, sendOption, keyOption, keyReleaseOption, keyDelayOption,
                    challengeOption, &app](ampbeam::ble::MultimeterClient::State state) {
                     err() << "state: " << client.stateName() << '\n';
                     if (state == ampbeam::ble::MultimeterClient::State::Failed) {
                       QTimer::singleShot(0, &app, [&app]() { app.quit(); });
                     }
                     if (state != ampbeam::ble::MultimeterClient::State::Streaming) {
                       return;
                     }
                     const auto sendPayloads = [&client, &parser, sendOption]() {
                       for (const auto& payload : parser.values(sendOption)) {
                         const auto bytes = ampbeam::fromHex(payload.toStdString());
                         if (!bytes.has_value()) {
                           err() << "invalid --send payload: " << payload << '\n';
                           continue;
                         }
                         const QByteArray data(reinterpret_cast<const char*>(bytes->data()),
                                               static_cast<qsizetype>(bytes->size()));
                         client.sendCommand(data);
                       }
                     };

                     // The compatibility challenge has to complete before any other
                     // command is likely to be honoured, so `--send` payloads are
                     // deferred until after it.
                     if (parser.isSet(challengeOption)) {
                       QByteArray nonce(16, '\0');
                       QString values;
                       for (int i = 0; i < 6; ++i) {
                         const int index = QRandomGenerator::global()->bounded(36);
                         nonce[i] = static_cast<char>(index);
                         values += QString::number(index) + QLatin1Char(' ');
                       }
                       err() << "challenge: nonce values = " << values.trimmed() << '\n';
                       err().flush();
                       client.sendCommand(nonce);
                       QTimer::singleShot(400, &client, [&client]() { client.readCommand(); });
                       QTimer::singleShot(900, &client, sendPayloads);
                     } else {
                       sendPayloads();
                     }

                     if (!parser.isSet(keyOption)) {
                       return;
                     }
                     const int spacingMs = qMax(300, parser.value(keyDelayOption).toInt());
                     const bool alsoRelease = parser.isSet(keyReleaseOption);
                     // A key written the instant streaming starts is sometimes
                     // dropped by the meter, so the first press waits for the
                     // link to settle.
                     constexpr int kKeySettleMs = 500;
                     int slot = 0;
                     for (const auto& value : parser.values(keyOption)) {
                       bool ok = false;
                       const int code = value.toInt(&ok, 0);
                       if (!ok || code < 0 || code > 0xFF) {
                         err() << "invalid --key code: " << value << '\n';
                         continue;
                       }
                       const auto key = static_cast<std::uint8_t>(code);
                       const int delayMs = kKeySettleMs + slot * spacingMs;
                       ++slot;
                       QTimer::singleShot(delayMs, &client, [&client, key, alsoRelease]() {
                         err() << "key: " << static_cast<int>(key) << " press\n";
                         err().flush();
                         const auto down = ampbeam::meterKeyCommand(key, true);
                         client.sendKey(QByteArray(reinterpret_cast<const char*>(down.data()),
                                                   static_cast<qsizetype>(down.size())));
                         if (!alsoRelease) {
                           return;
                         }
                         QTimer::singleShot(200, &client, [&client, key]() {
                           const auto up = ampbeam::meterKeyCommand(key, false);
                           client.sendKey(QByteArray(reinterpret_cast<const char*>(up.data()),
                                                     static_cast<qsizetype>(up.size())));
                         });
                       });
                     }
                   });
  QObject::connect(&client, &ampbeam::ble::MultimeterClient::notification,
                   [&decoder, framer, useMeterFraming](const QByteArray& data) {
                     const auto* begin = reinterpret_cast<const std::uint8_t*>(data.constData());
                     const std::span<const std::uint8_t> bytes(
                         begin, static_cast<std::size_t>(data.size()));
                     const qint64 receivedAtMs = QDateTime::currentMSecsSinceEpoch();

                     if (!useMeterFraming) {
                       emitFrame(receivedAtMs, data, *decoder);
                       return;
                     }
                     const auto frames = framer->push(bytes);
                     if (frames.empty()) {
                       // Not a complete frame yet; keep buffering. Nothing to report.
                       return;
                     }
                     for (const auto& frame : frames) {
                       emitFrame(receivedAtMs,
                                 QByteArray(reinterpret_cast<const char*>(frame.data()),
                                            static_cast<qsizetype>(frame.size())),
                                 *decoder);
                     }
                   });
  QObject::connect(
      &client, &ampbeam::ble::MultimeterClient::infoReceived, [](const QByteArray& payload) {
        const auto* begin = reinterpret_cast<const std::uint8_t*>(payload.constData());
        const std::span<const std::uint8_t> bytes(begin, static_cast<std::size_t>(payload.size()));
        const auto info = ampbeam::meterParseDeviceInfo(bytes);
        if (!info.has_value()) {
          err() << "info: empty payload\n";
          return;
        }
        out() << "info: series=" << static_cast<unsigned>(info->seriesId)
              << " version=" << QString::fromStdString(info->version) << '\n';
        out().flush();
      });
  QObject::connect(&client, &ampbeam::ble::MultimeterClient::errorOccurred,
                   [](const QString& message) { err() << "error: " << message << '\n'; });
  QObject::connect(&client, &ampbeam::ble::MultimeterClient::writeFailed,
                   [](const QString& message) { err() << "write: " << message << '\n'; });
  QObject::connect(
      &client, &ampbeam::ble::MultimeterClient::commandWritten, [](const QByteArray& payload) {
        err() << "command acknowledged: " << payload.toHex(' ').toUpper().constData() << '\n';
      });
  QObject::connect(
      &client, &ampbeam::ble::MultimeterClient::keyWritten, [](const QByteArray& payload) {
        err() << "key acknowledged: " << payload.toHex(' ').toUpper().constData() << '\n';
      });
  QObject::connect(&client, &ampbeam::ble::MultimeterClient::commandRead,
                   [](const QByteArray& payload) {
                     err() << "challenge answer: " << payload.size()
                           << " byte(s), hex=" << payload.toHex(' ').constData() << " text=\""
                           << QString::fromLatin1(payload).trimmed() << "\"\n";
                     err().flush();
                   });

  err() << "Connecting to " << deviceAddress << "...\n";
  client.connectToDevice(deviceAddress, deviceName);

  return app.exec();
}
