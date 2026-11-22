// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ampbeam-cli: mirror the meter display, scan, capture, key/command injection,
// import and replay.
#include "ampbeam/core/capture.hpp"
#include "ampbeam/core/csv.hpp"
#include "ampbeam/core/decoder.hpp"
#include "ampbeam/core/hex.hpp"
#include "ampbeam/core/meter.hpp"
#include "ampbeam/core/monitor.hpp"
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
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

#if defined(Q_OS_UNIX)
#include <unistd.h>
#endif

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

/// Writes every decoded reading to a CSV file. One instance per run: the header
/// is written when the file opens and each row is flushed, so `tail -f` works.
class CsvExporter {
public:
  [[nodiscard]] bool open(const QString& path) {
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return false;
    }
    m_stream.setDevice(&m_file);
    m_stream.setEncoding(QStringConverter::Utf8);
    m_stream << QString::fromStdString(ampbeam::csvHeader()) << '\n';
    m_stream.flush();
    return true;
  }

  void write(const ampbeam::Measurement& measurement, qint64 receivedAtMs) {
    ampbeam::Measurement copy = measurement;
    copy.receivedAtMs = static_cast<std::uint64_t>(qMax<qint64>(receivedAtMs, 0));
    const QString iso =
        QDateTime::fromMSecsSinceEpoch(receivedAtMs).toUTC().toString(Qt::ISODateWithMs);
    m_stream << QString::fromStdString(ampbeam::csvRow(copy, iso.toStdString())) << '\n';
    m_stream.flush();
  }

private:
  QFile m_file;
  QTextStream m_stream;
};

void emitFrame(qint64 receivedAtMs, const QByteArray& data, const ampbeam::FrameDecoder& decoder,
               CsvExporter* csv) {
  const auto* begin = reinterpret_cast<const std::uint8_t*>(data.constData());
  const std::span<const std::uint8_t> bytes(begin, static_cast<std::size_t>(data.size()));
  const auto decoded = decoder.decode(bytes);
  if (csv != nullptr && decoded.kind == ampbeam::DecodedFrame::Kind::Measurement) {
    csv->write(decoded.measurement, receivedAtMs);
  }

  out() << receivedAtMs << '\t' << data.size() << '\t'
        << QString::fromStdString(ampbeam::toHex(bytes, ' ')) << '\t'
        << QString::fromStdString(decoded.detail) << '\n';
  out().flush();
}

bool stdoutIsTerminal() {
#if defined(Q_OS_UNIX)
  return ::isatty(STDOUT_FILENO) != 0;
#else
  return false;
#endif
}

/// Prints one mirror line and flushes it. On a terminal the previous line is
/// overwritten; in a pipe, a log or with `--plain` every reading gets its own
/// line.
class MirrorPrinter {
public:
  explicit MirrorPrinter(bool overwrite) : m_overwrite(overwrite) {}

  void show(const std::string& line) {
    if (m_overwrite) {
      out() << '\r' << QString::fromStdString(line) << QStringLiteral("\x1b[K");
    } else {
      out() << QString::fromStdString(line) << '\n';
    }
    out().flush();
  }

  /// Leaves the cursor on a fresh line after the last overwritten sample.
  void finish() {
    if (!m_overwrite) {
      return;
    }
    out() << '\n';
    out().flush();
    m_overwrite = false;
  }

private:
  bool m_overwrite;
};

/// Renders one decoded frame. A frame the decoder could not interpret is shown
/// as its diagnostic (with the hex dump), never as a made-up reading.
/// `verbose` appends the decoder's verified fields (gear, dp, raw, states).
void emitMirrorFrame(std::span<const std::uint8_t> bytes, const ampbeam::FrameDecoder& decoder,
                     MirrorPrinter& printer, bool verbose, CsvExporter* csv, qint64 receivedAtMs) {
  const auto decoded = decoder.decode(bytes);
  if (decoded.kind == ampbeam::DecodedFrame::Kind::Measurement) {
    if (csv != nullptr) {
      csv->write(decoded.measurement, receivedAtMs);
    }
    std::string line = ampbeam::monitorLine(decoded.measurement);
    if (verbose) {
      constexpr std::string_view prefix = "meter: ";
      const std::string_view detail = decoded.detail;
      line += "  |  ";
      line += detail.starts_with(prefix) ? detail.substr(prefix.size()) : detail;
    }
    printer.show(line);
    return;
  }
  printer.show("?  " + decoded.detail);
}

[[nodiscard]] int clampTimerMs(qint64 milliseconds) {
  if (milliseconds <= 0) {
    return 0;
  }
  return static_cast<int>(qMin<qint64>(milliseconds, std::numeric_limits<int>::max()));
}

int replay(const QString& path, const ampbeam::FrameDecoder& decoder, MirrorPrinter* printer,
           bool verbose, CsvExporter* csv) {
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
    if (printer != nullptr) {
      emitMirrorFrame(std::span<const std::uint8_t>(*bytes), decoder, *printer, verbose, csv,
                      receivedAtMs);
      continue;
    }
    emitFrame(receivedAtMs,
              QByteArray(reinterpret_cast<const char*>(bytes->data()),
                         static_cast<qsizetype>(bytes->size())),
              decoder, csv);
  }
  if (printer != nullptr) {
    printer->finish();
  }
  return 0;
}

struct MeterScan {
  ampbeam::DeviceSelectionStatus status = ampbeam::DeviceSelectionStatus::None;
  QString address;
  QString name;
  bool matchedService = false;
  bool matchedName = false;
  std::size_t candidateCount = 0;
  std::size_t discoveredCount = 0;
  QString error;
};

/// Scans for `timeoutMs` (or until `deadlineMs`) and runs the shared
/// `selectMeterDevice` decision. Does not print and never guesses between
/// several candidates.
MeterScan scanForMeter(const QString& serviceUuid, const QString& nameFilter, int timeoutMs,
                       qint64 deadlineMs) {
  MeterScan result;

  int scanMs = qMax(1000, timeoutMs);
  int deadlineTimerMs = 0;
  if (deadlineMs > 0) {
    const qint64 remaining = deadlineMs - QDateTime::currentMSecsSinceEpoch();
    if (remaining <= 0) {
      return result;
    }
    deadlineTimerMs = clampTimerMs(remaining);
    scanMs = qMax(1000, clampTimerMs(qMin<qint64>(remaining, scanMs)));
  }

  ampbeam::ble::DeviceScanner scanner;
  QEventLoop loop;
  QObject::connect(&scanner, &ampbeam::ble::DeviceScanner::errorOccurred, &loop,
                   [&result](const QString& message) { result.error = message; });
  QObject::connect(&scanner, &ampbeam::ble::DeviceScanner::runningChanged, &loop,
                   [&scanner, &loop]() {
                     if (!scanner.isRunning()) {
                       loop.quit();
                     }
                   });
  // With an explicit name the user already chose the device, so there is no
  // ambiguity to wait for: stop the scan as soon as it advertises. Without a
  // name, a second meter could still appear, so the full window is used.
  QObject::connect(
      &scanner, &ampbeam::ble::DeviceScanner::deviceDiscovered, &loop,
      [&scanner, &loop, nameFilter](const QString&, const QString& name, int, const QString&) {
        if (!nameFilter.isEmpty() && name.compare(nameFilter, Qt::CaseInsensitive) == 0) {
          scanner.stop();
          loop.quit();
        }
      });
  QTimer::singleShot(scanMs, &scanner, &ampbeam::ble::DeviceScanner::stop);
  QTimer::singleShot(scanMs + 500, &loop, &QEventLoop::quit);
  if (deadlineTimerMs > 0) {
    QTimer::singleShot(deadlineTimerMs, &loop, &QEventLoop::quit);
  }

  scanner.start(scanMs);
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
  const std::string filter = nameFilter.toStdString();
  const ampbeam::DeviceSelectionResult selection =
      ampbeam::selectMeterDevice(candidates, service, filter);

  result.status = selection.status;
  result.matchedService = selection.matchedService;
  result.matchedName = selection.matchedName;
  result.candidateCount = selection.candidateCount;
  result.discoveredCount = selection.discoveredCount;
  if (selection.status == ampbeam::DeviceSelectionStatus::Selected) {
    result.address = QString::fromStdString(std::string(selection.address));
    result.name = QString::fromStdString(std::string(selection.name));
  }
  return result;
}

/// Sleeps for `delayMs`, but never past `deadlineMs` (0 = no deadline).
/// Returns false when the deadline was reached.
bool waitFor(int delayMs, qint64 deadlineMs) {
  qint64 sleepMs = delayMs;
  if (deadlineMs > 0) {
    const qint64 remaining = deadlineMs - QDateTime::currentMSecsSinceEpoch();
    if (remaining <= 0) {
      return false;
    }
    sleepMs = qMin(sleepMs, remaining);
  }
  if (sleepMs > 0) {
    QEventLoop loop;
    QTimer::singleShot(clampTimerMs(sleepMs), &loop, &QEventLoop::quit);
    loop.exec();
  }
  return deadlineMs <= 0 || QDateTime::currentMSecsSinceEpoch() < deadlineMs;
}

struct GattSelection {
  QString service;
  QString data;
  QString command;
  QString key;
  QString info;
  bool overridden = false;
};

void applyGattProfile(ampbeam::ble::MultimeterClient& client, const GattSelection& gatt) {
  if (!gatt.overridden) {
    return;
  }
  client.setGattProfile(gatt.service, gatt.data, gatt.command, gatt.key, gatt.info);
}

struct MirrorSettings {
  QString address;
  QString name;
  QString serviceUuid;
  int scanTimeoutMs = 12000;
  int durationSeconds = 0;
  bool overwrite = false;
  bool verbose = false;
  GattSelection gatt;
  CsvExporter* csv = nullptr;
};

/// Keeps one session alive as long as the user wants it: discovers, connects,
/// mirrors every frame, and reconnects after a lost link or a failed attempt.
/// The meter can legitimately stay quiet for a while (HOLD, continuity, steady
/// readings), so an idle stream is not treated as an error. Only the deadline
/// (or Ctrl+C) ends it.
int runMirror(const MirrorSettings& settings) {
  constexpr int kConnectTimeoutMs = 30000;

  const qint64 deadlineMs = settings.durationSeconds > 0
                                ? QDateTime::currentMSecsSinceEpoch() +
                                      static_cast<qint64>(settings.durationSeconds) * 1000
                                : 0;
  const auto expired = [deadlineMs]() {
    return deadlineMs > 0 && QDateTime::currentMSecsSinceEpoch() >= deadlineMs;
  };

  MirrorPrinter printer(settings.overwrite);
  const ampbeam::MeterDecoder decoder;
  ampbeam::ReconnectPolicy policy;
  bool announcedSearch = false;

  while (!expired()) {
    QString address = settings.address;
    QString name = settings.name;

    if (address.isEmpty()) {
      if (!announcedSearch) {
        err() << "mirror: looking for a meter advertising " << settings.serviceUuid << "...\n";
        err().flush();
        announcedSearch = true;
      }
      const MeterScan scan =
          scanForMeter(settings.serviceUuid, settings.name, settings.scanTimeoutMs, deadlineMs);
      if (scan.status != ampbeam::DeviceSelectionStatus::Selected) {
        const int delay = policy.nextDelayMs();
        err() << "mirror: no usable meter";
        if (!scan.error.isEmpty()) {
          err() << " (" << scan.error << ')';
        }
        if (scan.status == ampbeam::DeviceSelectionStatus::Multiple) {
          if (scan.matchedName) {
            err() << "; " << scan.candidateCount << " devices are named " << settings.name;
          } else if (scan.matchedService) {
            err() << "; " << scan.candidateCount << " devices advertise the meter service";
          } else {
            err() << "; found " << scan.discoveredCount
                  << " BLE devices and none advertised the meter service";
          }
          err() << ", narrow it down with --name or --device";
        } else if (!settings.name.isEmpty()) {
          err() << "; no device named " << settings.name << " found";
        } else {
          err() << "; no BLE device found";
        }
        err() << "; retrying in " << (delay / 1000) << " s\n";
        err().flush();
        if (!waitFor(delay, deadlineMs)) {
          break;
        }
        continue;
      }
      address = scan.address;
      name = scan.name;
      announcedSearch = false;
      err() << "mirror: using " << (name.isEmpty() ? address : name) << " (" << address << ")\n";
      err().flush();
    }

    ampbeam::ble::MultimeterClient client;
    applyGattProfile(client, settings.gatt);

    ampbeam::MeterFramer framer;
    QEventLoop loop;
    QTimer attemptTimer;
    bool streamed = false;
    QString failure;

    QObject::connect(&client, &ampbeam::ble::MultimeterClient::stateChanged, &loop,
                     [&](ampbeam::ble::MultimeterClient::State state) {
                       if (state == ampbeam::ble::MultimeterClient::State::Streaming) {
                         streamed = true;
                         policy.reset();
                         attemptTimer.stop();
                         err() << "mirror: streaming; press Ctrl+C to stop\n";
                         err().flush();
                       } else if (state == ampbeam::ble::MultimeterClient::State::Failed) {
                         loop.quit();
                       } else if (state == ampbeam::ble::MultimeterClient::State::Disconnected &&
                                  streamed) {
                         loop.quit();
                       }
                     });
    QObject::connect(
        &client, &ampbeam::ble::MultimeterClient::notification, &loop, [&](const QByteArray& data) {
          const auto* begin = reinterpret_cast<const std::uint8_t*>(data.constData());
          const std::span<const std::uint8_t> bytes(begin, static_cast<std::size_t>(data.size()));
          const qint64 receivedAtMs = QDateTime::currentMSecsSinceEpoch();
          for (const auto& frame : framer.push(bytes)) {
            emitMirrorFrame(frame, decoder, printer, settings.verbose, settings.csv, receivedAtMs);
          }
        });
    QObject::connect(&client, &ampbeam::ble::MultimeterClient::errorOccurred, &loop,
                     [&failure](const QString& message) { failure = message; });

    attemptTimer.setSingleShot(true);
    QObject::connect(&attemptTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    if (deadlineMs > 0) {
      const qint64 remaining = deadlineMs - QDateTime::currentMSecsSinceEpoch();
      if (remaining <= 0) {
        break;
      }
      QTimer::singleShot(clampTimerMs(remaining), &loop, &QEventLoop::quit);
    }

    err() << "mirror: connecting to " << (name.isEmpty() ? address : name) << " (" << address
          << ")...\n";
    err().flush();
    attemptTimer.start(kConnectTimeoutMs);
    client.connectToDevice(address, name);
    loop.exec();

    client.disconnectFromDevice();

    if (expired()) {
      break;
    }

    const int delay = policy.nextDelayMs();
    err() << "mirror: " << (streamed ? "lost the meter" : "could not connect");
    if (!failure.isEmpty()) {
      err() << ": " << failure;
    }
    err() << "; reconnecting in " << (delay / 1000) << " s\n";
    err().flush();
    if (!waitFor(delay, deadlineMs)) {
      break;
    }
  }

  printer.finish();
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
                     "Default action: connect to the meter and mirror its display on one "
                     "self-overwriting line.\n"
                     "Capture format: <epochMs>TAB<length>TAB<hex>TAB<detail>"));
  parser.addHelpOption();
  parser.addVersionOption();

  const QCommandLineOption mirrorOption(
      QStringLiteral("mirror"),
      QStringLiteral("Mirror the meter display (default when no other action is given). "
                     "Reconnects automatically and never exits on its own."));
  const QCommandLineOption plainOption(
      QStringLiteral("plain"),
      QStringLiteral("Mirror mode: print one line per reading instead of overwriting a single "
                     "line. Automatic when stdout is not a terminal."));
  const QCommandLineOption verboseOption(
      QStringLiteral("verbose"),
      QStringLiteral("Mirror mode: append the decoded frame fields (gear, dp, raw, states)."));
  const QCommandLineOption csvOption(
      QStringLiteral("csv"),
      QStringLiteral("Write every decoded reading to this CSV file (selects the meter decoder)."),
      QStringLiteral("file"));
  const QCommandLineOption scanOption({QStringLiteral("s"), QStringLiteral("scan")},
                                      QStringLiteral("Scan for BLE devices and exit."));
  const QCommandLineOption scanTimeoutOption(
      QStringLiteral("scan-timeout"),
      QStringLiteral("Scan duration in milliseconds (default 12000; mirror retries use 5000 "
                     "unless this option is set)."),
      QStringLiteral("ms"), QStringLiteral("12000"));
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

  parser.addOptions(
      {scanOption,    scanTimeoutOption, deviceOption,   nameOption,     serviceOption,
       dataOption,    commandOption,     keyUuidOption,  infoOption,     sendOption,
       keyOption,     keyReleaseOption,  keyDelayOption, durationOption, replayOption,
       decoderOption, challengeOption,   importOption,   outputOption,   fixtureNameOption,
       sourceOption,  mirrorOption,      plainOption,    verboseOption,  csvOption});
  parser.process(app);

  const bool mirrorRequested = parser.isSet(mirrorOption);
  if (mirrorRequested && parser.isSet(importOption)) {
    err() << "--mirror cannot be combined with --import\n";
    return 1;
  }
  if (mirrorRequested && parser.isSet(scanOption)) {
    err() << "--mirror cannot be combined with --scan\n";
    return 1;
  }
  if (mirrorRequested &&
      (parser.isSet(sendOption) || parser.isSet(keyOption) || parser.isSet(challengeOption))) {
    err() << "--mirror does not send commands; drop --send/--key/--challenge or use "
             "--device <address> without --mirror\n";
    return 1;
  }

  const bool csvRequested = parser.isSet(csvOption);
  if (csvRequested && parser.isSet(importOption)) {
    err() << "--csv cannot be combined with --import\n";
    return 1;
  }
  if (csvRequested && parser.isSet(scanOption)) {
    err() << "--csv cannot be combined with --scan\n";
    return 1;
  }

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
  const bool replayRequested = parser.isSet(replayOption);
  const bool mirrorMode = mirrorRequested || (!replayRequested && !parser.isSet(scanOption) &&
                                              !parser.isSet(deviceOption));
  if ((mirrorMode || csvRequested) && parser.isSet(decoderOption) &&
      parser.value(decoderOption) != QStringLiteral("meter")) {
    err() << (mirrorMode ? "mirror mode" : "CSV export") << " needs the meter decoder\n";
    return 1;
  }
  const bool meterDecoding =
      mirrorMode || csvRequested || parser.value(decoderOption) == QStringLiteral("meter");
  if (meterDecoding) {
    decoder = std::make_unique<ampbeam::MeterDecoder>();
  } else if (parser.value(decoderOption) == QStringLiteral("raw")) {
    decoder = std::make_unique<ampbeam::RawDecoder>();
  } else {
    err() << "Unknown decoder: " << parser.value(decoderOption) << '\n';
    return 1;
  }

  const bool overwrite = !parser.isSet(plainOption) && stdoutIsTerminal();

  CsvExporter csvExporter;
  CsvExporter* csv = nullptr;
  if (csvRequested) {
    if (!csvExporter.open(parser.value(csvOption))) {
      err() << "Cannot write CSV: " << parser.value(csvOption) << '\n';
      return 2;
    }
    csv = &csvExporter;
  }

  if (replayRequested) {
    if (mirrorMode) {
      MirrorPrinter printer(overwrite);
      return replay(parser.value(replayOption), *decoder, &printer, parser.isSet(verboseOption),
                    csv);
    }
    return replay(parser.value(replayOption), *decoder, nullptr, false, csv);
  }

  const int durationSeconds = parser.value(durationOption).toInt();

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

  if (!parser.isSet(deviceOption) && !mirrorMode) {
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

  const auto profileValue = [&parser](const QCommandLineOption& option, std::string_view uuid) {
    return parser.isSet(option) ? parser.value(option)
                                : QString::fromLatin1(uuid.data(), static_cast<int>(uuid.size()));
  };
  const GattSelection gatt{.service = profileValue(serviceOption, fallback.serviceUuid),
                           .data = profileValue(dataOption, fallback.dataCharacteristicUuid),
                           .command =
                               profileValue(commandOption, fallback.commandCharacteristicUuid),
                           .key = profileValue(keyUuidOption, fallback.keyCharacteristicUuid),
                           .info = profileValue(infoOption, fallback.infoCharacteristicUuid),
                           .overridden = parser.isSet(serviceOption) || parser.isSet(dataOption) ||
                                         parser.isSet(commandOption) ||
                                         parser.isSet(keyUuidOption) || parser.isSet(infoOption)};

  if (mirrorMode) {
    MirrorSettings settings;
    settings.address = deviceAddress == QStringLiteral("auto") ? QString{} : deviceAddress;
    settings.name = deviceName;
    settings.serviceUuid = serviceUuid;
    settings.scanTimeoutMs = parser.isSet(scanTimeoutOption)
                                 ? qMax(1000, parser.value(scanTimeoutOption).toInt())
                                 : 5000;
    settings.durationSeconds = qMax(0, durationSeconds);
    settings.overwrite = overwrite;
    settings.verbose = parser.isSet(verboseOption);
    settings.gatt = gatt;
    settings.csv = csv;
    return runMirror(settings);
  }

  if (deviceAddress == QStringLiteral("auto")) {
    const int timeoutMs = qMax(1000, parser.value(scanTimeoutOption).toInt());
    err() << "Scanning for " << timeoutMs << " ms to select the meter...\n";
    const MeterScan scan = scanForMeter(serviceUuid, deviceName, timeoutMs, 0);
    if (scan.status == ampbeam::DeviceSelectionStatus::Selected) {
      deviceAddress = scan.address;
      deviceName = scan.name;
      err() << "Selected " << (deviceName.isEmpty() ? deviceAddress : deviceName) << " ("
            << deviceAddress << ")";
      if (!scan.matchedService) {
        err() << (scan.matchedName ? " — matched --name" : " — only discovered device")
              << "; its advertisement did not list " << serviceUuid;
      }
      err() << '\n';
    } else if (scan.status == ampbeam::DeviceSelectionStatus::Multiple) {
      if (scan.matchedName) {
        err() << "Found " << scan.candidateCount << " devices named "
              << (deviceName.isEmpty() ? QStringLiteral("<empty>") : deviceName)
              << "; pass --device <address> (use --scan to list them)\n";
      } else if (scan.matchedService) {
        err() << "Found " << scan.candidateCount << " devices advertising " << serviceUuid
              << "; pass --device <address> (use --scan to list them)\n";
      } else {
        err() << "Found " << scan.discoveredCount << " BLE devices but none advertised "
              << serviceUuid << "; pass --device <address> (use --scan to list them)\n";
      }
      return 1;
    } else {
      if (!deviceName.isEmpty()) {
        err() << "No device named " << deviceName << " found";
      } else {
        err() << "No BLE device found";
      }
      if (!scan.error.isEmpty()) {
        err() << ": " << scan.error;
      }
      err() << '\n';
      return 1;
    }
  }

  ampbeam::ble::MultimeterClient client;
  applyGattProfile(client, gatt);

  const bool useMeterFraming = meterDecoding;
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
                   [&decoder, framer, useMeterFraming, csv](const QByteArray& data) {
                     const auto* begin = reinterpret_cast<const std::uint8_t*>(data.constData());
                     const std::span<const std::uint8_t> bytes(
                         begin, static_cast<std::size_t>(data.size()));
                     const qint64 receivedAtMs = QDateTime::currentMSecsSinceEpoch();

                     if (!useMeterFraming) {
                       emitFrame(receivedAtMs, data, *decoder, csv);
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
                                 *decoder, csv);
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

  if (durationSeconds > 0) {
    QTimer::singleShot(durationSeconds * 1000, &app, [&app]() { app.quit(); });
  }

  err() << "Connecting to " << deviceAddress << "...\n";
  client.connectToDevice(deviceAddress, deviceName);

  return app.exec();
}
