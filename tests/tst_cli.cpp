// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Black-box tests for ampbeam-cli.
//
// Nothing here includes a CLI or core header: the tests start the real binary
// with arguments, inspect stdout/stderr/exit status and re-read the files it
// writes. That is the contract the docs promise, so these tests break when the
// contract does, regardless of how the implementation is arranged.

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

namespace {

struct CliResult {
  int exitCode = -1;
  bool started = false;
  QString standardOutput;
  QString standardError;

  [[nodiscard]] QString combined() const { return standardOutput + standardError; }
};

CliResult runCli(const QStringList& arguments, const QString& workingDirectory) {
  QProcess process;
  process.setWorkingDirectory(workingDirectory);
  process.start(QStringLiteral(AMPBEAM_CLI_PATH), arguments);
  if (!process.waitForStarted(10000)) {
    return CliResult{-1, false, {}, process.errorString()};
  }
  process.waitForFinished(60000);

  CliResult result;
  result.started = true;
  result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
  result.standardOutput = QString::fromUtf8(process.readAllStandardOutput());
  result.standardError = QString::fromUtf8(process.readAllStandardError());
  return result;
}

void writeText(const QString& path, const QByteArray& content) {
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(content);
}

QByteArray readText(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

constexpr auto kLongFrame = "23 00 00 86 13 00 00 00 00 00 00 00 00 00 00";

QString fixtureWithFrames(const QJsonArray& frames,
                          const QString& name = QStringLiteral("fixture")) {
  QJsonObject root;
  root.insert(QStringLiteral("name"), name);
  root.insert(QStringLiteral("createdAt"), QStringLiteral("2026-11-22T00:00:00.000Z"));
  root.insert(QStringLiteral("frames"), frames);
  return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

} // namespace

class CliBlackBoxTest final : public QObject {
  Q_OBJECT

private slots:
  void versionAndHelp();
  void noArgumentsShowsHelp();
  void unknownDecoderIsRejected();
  void importRequiresAnOutputPath();
  void importRefusesMissingInput();
  void importRefusesAnEmptyLog();
  void importRefusesAnUnwritableOutput();
  void importConvertsACaptureLog();
  void replayUsesTheRawDecoderByDefault();
  void replayUsesTheMeterDecoder();
  void replaySkipsMalformedFrames();
  void replayRejectsAMissingFixture();
  void replayRejectsBrokenJson();
};

void CliBlackBoxTest::versionAndHelp() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());

  const auto version = runCli({QStringLiteral("--version")}, directory.path());
  QVERIFY(version.started);
  QCOMPARE(version.exitCode, 0);
  QVERIFY(version.combined().contains(QStringLiteral("0.1.0")));

  const auto help = runCli({QStringLiteral("--help")}, directory.path());
  QVERIFY(help.started);
  QCOMPARE(help.exitCode, 0);
  QVERIFY(help.combined().contains(QStringLiteral("Usage:")));
  QVERIFY(help.combined().contains(QStringLiteral("--replay")));
}

void CliBlackBoxTest::noArgumentsShowsHelp() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto result = runCli({}, directory.path());
  QVERIFY(result.started);
  QCOMPARE(result.exitCode, 1);
  QVERIFY(result.combined().contains(QStringLiteral("Usage:")));
}

void CliBlackBoxTest::unknownDecoderIsRejected() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString fixture = directory.filePath(QStringLiteral("f.json"));
  writeText(fixture, fixtureWithFrames({}).toUtf8());

  const auto result = runCli(
      {QStringLiteral("--replay"), fixture, QStringLiteral("--decoder"), QStringLiteral("nope")},
      directory.path());
  QVERIFY(result.started);
  QCOMPARE(result.exitCode, 1);
  QVERIFY(result.standardError.contains(QStringLiteral("Unknown decoder: nope")));
}

void CliBlackBoxTest::importRequiresAnOutputPath() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString log = directory.filePath(QStringLiteral("capture.log"));
  writeText(log, QByteArrayLiteral("01 02\n"));

  const auto result = runCli({QStringLiteral("--import"), log}, directory.path());
  QVERIFY(result.started);
  QCOMPARE(result.exitCode, 1);
  QVERIFY(result.standardError.contains(QStringLiteral("--import requires --output")));
}

void CliBlackBoxTest::importRefusesMissingInput() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString log = directory.filePath(QStringLiteral("does-not-exist.log"));
  const QString output = directory.filePath(QStringLiteral("out.json"));

  const auto result = runCli({QStringLiteral("--import"), log, QStringLiteral("--output"), output},
                             directory.path());
  QVERIFY(result.started);
  QCOMPARE(result.exitCode, 2);
  QVERIFY(result.standardError.contains(QStringLiteral("Cannot open capture log")));
  QVERIFY(!QFile::exists(output));
}

void CliBlackBoxTest::importRefusesAnEmptyLog() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString log = directory.filePath(QStringLiteral("capture.log"));
  writeText(log, QByteArrayLiteral("# nothing here\n\n"));
  const QString output = directory.filePath(QStringLiteral("out.json"));

  const auto result = runCli({QStringLiteral("--import"), log, QStringLiteral("--output"), output},
                             directory.path());
  QVERIFY(result.started);
  QCOMPARE(result.exitCode, 2);
  QVERIFY(result.standardError.contains(QStringLiteral("No frames found")));
  QVERIFY(!QFile::exists(output));
}

void CliBlackBoxTest::importRefusesAnUnwritableOutput() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString log = directory.filePath(QStringLiteral("capture.log"));
  writeText(log, QByteArrayLiteral("01 02\n"));
  const QString output = directory.filePath(QStringLiteral("missing-dir/out.json"));

  const auto result = runCli({QStringLiteral("--import"), log, QStringLiteral("--output"), output},
                             directory.path());
  QVERIFY(result.started);
  QCOMPARE(result.exitCode, 2);
  QVERIFY(result.standardError.contains(QStringLiteral("Cannot write fixture")));
}

void CliBlackBoxTest::importConvertsACaptureLog() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());

  const QString log = directory.filePath(QStringLiteral("capture.log"));
  writeText(log, QByteArrayLiteral("1732180000123\t15\t") + QByteArray(kLongFrame) +
                     QByteArrayLiteral("\tDCV\n# comment\nnot a frame\n"
                                       "01 02\n"));
  const QString output = directory.filePath(QStringLiteral("fixture.json"));

  const auto result = runCli({QStringLiteral("--import"), log, QStringLiteral("--output"), output,
                              QStringLiteral("--fixture-name"), QStringLiteral("bench"),
                              QStringLiteral("--source"), QStringLiteral("desk capture")},
                             directory.path());
  QVERIFY(result.started);
  QCOMPARE(result.exitCode, 0);
  QVERIFY(result.standardOutput.contains(QStringLiteral("wrote 2 frame(s)")));
  QVERIFY(result.standardError.contains(QStringLiteral("cannot parse")));

  const QByteArray written = readText(output);
  QVERIFY(!written.isEmpty());
  const QJsonDocument document = QJsonDocument::fromJson(written);
  QVERIFY(document.isObject());
  const QJsonObject root = document.object();
  QCOMPARE(root.value(QStringLiteral("name")).toString(), QStringLiteral("bench"));
  QCOMPARE(root.value(QStringLiteral("source")).toString(), QStringLiteral("desk capture"));
  QVERIFY(!root.value(QStringLiteral("createdAt")).toString().isEmpty());
  const QJsonArray frames = root.value(QStringLiteral("frames")).toArray();
  QCOMPARE(frames.size(), 2);
  QCOMPARE(frames.at(0).toObject().value(QStringLiteral("receivedMs")).toDouble(), 1732180000123.0);
  QCOMPARE(frames.at(0).toObject().value(QStringLiteral("hex")).toString(),
           QString::fromLatin1(kLongFrame));
  QVERIFY(!frames.at(1).toObject().contains(QStringLiteral("receivedMs")));
}

void CliBlackBoxTest::replayUsesTheRawDecoderByDefault() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());

  QJsonArray frames;
  QJsonObject frame;
  frame.insert(QStringLiteral("receivedMs"), 42);
  frame.insert(QStringLiteral("hex"), QStringLiteral("01 02 03"));
  frames.append(frame);
  const QString fixture = directory.filePath(QStringLiteral("raw.json"));
  writeText(fixture, fixtureWithFrames(frames).toUtf8());

  const auto result = runCli({QStringLiteral("--replay"), fixture}, directory.path());
  QVERIFY(result.started);
  QCOMPARE(result.exitCode, 0);
  const QStringList lines = result.standardOutput.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  QCOMPARE(lines.size(), 1);
  const QStringList fields = lines.first().split(QLatin1Char('\t'));
  QCOMPARE(fields.size(), 4);
  QCOMPARE(fields.at(0), QStringLiteral("42"));
  QCOMPARE(fields.at(1), QStringLiteral("3"));
  QCOMPARE(fields.at(2), QStringLiteral("01 02 03"));
  QCOMPARE(fields.at(3), QStringLiteral("01:02:03"));
}

void CliBlackBoxTest::replayUsesTheMeterDecoder() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());

  QJsonArray frames;
  QJsonObject frame;
  frame.insert(QStringLiteral("hex"), QString::fromLatin1(kLongFrame));
  frames.append(frame);
  const QString fixture = directory.filePath(QStringLiteral("meter.json"));
  writeText(fixture, fixtureWithFrames(frames).toUtf8());

  const auto result = runCli(
      {QStringLiteral("--replay"), fixture, QStringLiteral("--decoder"), QStringLiteral("meter")},
      directory.path());
  QVERIFY(result.started);
  QCOMPARE(result.exitCode, 0);
  QVERIFY(result.standardOutput.contains(QStringLiteral("meter:")));
  QVERIFY(result.standardOutput.contains(QStringLiteral("DCV")));
  QVERIFY(result.standardOutput.contains(QStringLiteral("4.998")));
}

void CliBlackBoxTest::replaySkipsMalformedFrames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());

  QJsonArray frames;
  QJsonObject broken;
  broken.insert(QStringLiteral("hex"), QStringLiteral("zz"));
  frames.append(broken);
  QJsonObject good;
  good.insert(QStringLiteral("hex"), QStringLiteral("01 02"));
  frames.append(good);
  const QString fixture = directory.filePath(QStringLiteral("mixed.json"));
  writeText(fixture, fixtureWithFrames(frames).toUtf8());

  const auto result = runCli({QStringLiteral("--replay"), fixture}, directory.path());
  QVERIFY(result.started);
  QCOMPARE(result.exitCode, 0);
  QVERIFY(result.standardError.contains(QStringLiteral("Skipping malformed frame at index 0")));
  const QStringList lines = result.standardOutput.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  QCOMPARE(lines.size(), 1);
  QVERIFY(lines.first().contains(QStringLiteral("01 02")));
}

void CliBlackBoxTest::replayRejectsAMissingFixture() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto result =
      runCli({QStringLiteral("--replay"), directory.filePath(QStringLiteral("nope.json"))},
             directory.path());
  QVERIFY(result.started);
  QCOMPARE(result.exitCode, 2);
  QVERIFY(result.standardError.contains(QStringLiteral("Cannot open fixture")));
}

void CliBlackBoxTest::replayRejectsBrokenJson() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString fixture = directory.filePath(QStringLiteral("broken.json"));
  writeText(fixture, QByteArrayLiteral("{\"frames\": [}"));

  const auto result = runCli({QStringLiteral("--replay"), fixture}, directory.path());
  QVERIFY(result.started);
  QCOMPARE(result.exitCode, 2);
  QVERIFY(result.standardError.contains(QStringLiteral("Invalid fixture JSON")));
}

QTEST_GUILESS_MAIN(CliBlackBoxTest)

#include "tst_cli.moc"
