#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QDesktopServices>
#include <QUrlQuery>
#include "diagnostics.h"
#include "sessionfailure.h"

class DiagnosticsTest : public QObject {
    Q_OBJECT
public:
    QUrl opened;
public slots:
    void captureUrl(const QUrl& url) { opened=url; }
private slots:
    void initTestCase() {
        QCoreApplication::setApplicationVersion(qEnvironmentVariable("TEST_DESKPORT_VERSION", "development"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,qEnvironmentVariable("TEST_DIAGNOSTICS_ROOT"));
    }
    void preparationErrorsAndStructuredPrivacy() {
        for (const auto code : {"cancelled", "unauthorized", "busy", "stale", "cycle", "unavailable", "timeout", "display-failed", "capture-encoder-failed"}) {
            const QString value(code);
            QVERIFY(!DeskPortSessionFailure::message(value).isEmpty());
            if (value != "unauthorized") QVERIFY(!DeskPortSessionFailure::message(value).contains("denied"));
            auto event = Diagnostics::project("DeskPort failure stage=session-prepare code=" + value);
            QCOMPARE(event["code"].toString(), value);
            event["source"] = "client"; event["run"] = QString(32, 'a'); event["elapsed_ms"] = 1;
            event["message"] = "PRIVATE_SECRET";
            const auto safe = Diagnostics::validate(event);
            QCOMPARE(safe["code"].toString(), value); QVERIFY(!safe.contains("message"));
            event["code"] = "private.example.net"; QVERIFY(Diagnostics::validate(event).isEmpty());
        }
        QVERIFY(!DeskPortSessionFailure::message("unknown-private-host").contains("unknown-private-host"));
        QCOMPARE(Diagnostics::project("[Info]: Found H.264 encoder: h264_vaapi [vaapi]")["backend"].toString(), QString("vaapi"));
        QVERIFY(!Diagnostics::project("Found H.264 encoder: secret [private-host]").contains("backend"));
        auto runtime = Diagnostics::project("DeskPort runtime kind=capture backend=kwin");
        runtime["source"] = "host"; runtime["run"] = QString(32, 'a'); runtime["elapsed_ms"] = 1;
        QCOMPARE(Diagnostics::validate(runtime)["backend"].toString(), QString("kwin"));
        runtime["backend"] = "/private/config"; QVERIFY(Diagnostics::validate(runtime).isEmpty());
        QVERIFY(!Diagnostics::project("DeskPort failure stage=session-prepare code=private-host").contains("code"));
        QCOMPARE(Diagnostics::project("Video failed to find working encoder: /private/path")["code"].toString(), QString("encoder-unavailable"));
        QTemporaryDir dir; Diagnostics logs(nullptr, dir.path()); logs.setEnabled(true);
        logs.record("client", "DeskPort failure stage=session-prepare code=display-failed");
        logs.record("host", "DeskPort runtime kind=capture backend=kwin");
        QFile archive(logs.createBundle()); QVERIFY(archive.open(QIODevice::ReadOnly));
        const auto bytes = archive.readAll(); QVERIFY(bytes.contains("display-failed")); QVERIFY(bytes.contains("kwin"));
    }
    void defaultAndSavedPreference() {
        QStandardPaths::setTestModeEnabled(true);
        QSettings settings;
        settings.remove("diagnostics/enabled");
        { Diagnostics logs; QVERIFY(logs.enabled()); }
        settings.setValue("diagnostics/enabled", false);
        { Diagnostics logs; QVERIFY(!logs.enabled()); }
        settings.setValue("diagnostics/enabled", true);
        { Diagnostics logs; QVERIFY(logs.enabled()); }
        settings.remove("diagnostics/enabled");
        QStandardPaths::setTestModeEnabled(false);
    }
    void manifestVersion_data() {
        QTest::addColumn<QString>("version");
        QTest::addColumn<QString>("expected");
        for (auto version : {"0.4.6", "0.4.6-D", "0.4.6-preview.2"})
            QTest::newRow(version) << QString(version) << QString(version);
        for (auto version : {"private.example.net", "0.4.6-D\n", "0.4.6-/Users/alice", "0.4.6-"})
            QTest::newRow(version) << QString(version) << QString("development");
    }
    void manifestVersion() {
        QFETCH(QString, version); QFETCH(QString, expected);
        QCoreApplication::setApplicationVersion(version);
        QTemporaryDir dir; Diagnostics logs(nullptr,dir.path());
        QFile archive(logs.createBundle()); QVERIFY(archive.open(QIODevice::ReadOnly));
        QVERIFY(archive.readAll().contains("\"version\": \"" + expected.toUtf8() + "\""));
        QCoreApplication::setApplicationVersion(qEnvironmentVariable("TEST_DESKPORT_VERSION", "development"));
    }
    void privacy_data() {
        QTest::addColumn<QString>("secret");
        for (auto s : {"192.168.10.42", "2001:db8::abcd", "fe80::1234%en0", "::ffff:192.0.2.9", "[2001:db8::1]:443", "private.example.net", "my-host", "办公室电脑", "https://user:password@example.net/api?access_token=abc#key=123", "token=abcd", "Authorization: Bearer abcdef", "-----BEGIN PRIVATE KEY-----", "/Users/alice/Documents/private.txt", "/home/bob/.ssh/id_ed25519", "C:\\Users\\Jane\\private.txt", "clipboard: private sentence", "keycode=65", "text input: secret words"})
            QTest::newRow(s) << QString::fromUtf8(s);
    }
    void privacy() {
        QFETCH(QString,secret);
        QTemporaryDir dir; Diagnostics logs(nullptr,dir.path()); logs.setEnabled(true);
        logs.record("client","Connection failed: "+secret);
        logs.ingest("host",("Encoder failed: "+secret+"\n").toUtf8());
        logs.ingest("display",("display error: "+secret+"\n").toUtf8());
        const QString path=logs.createBundle(); QVERIFY(!path.isEmpty()); QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes=file.readAll(); QVERIFY(!bytes.contains(secret.toUtf8()));
        for (auto name : QDir(dir.path()).entryList({"*.jsonl"})) {
            QFile log(dir.filePath(name)); QVERIFY(log.open(QIODevice::ReadOnly)); QVERIFY(!log.readAll().contains(secret.toUtf8()));
        }
    }
    void defaultOffToggleAndPartialLines() {
        QTemporaryDir dir; Diagnostics logs(nullptr,dir.path()); QVERIFY(!logs.enabled());
        logs.record("client","Connection failed: example.net"); logs.ingest("host","Host started\n");
        QVERIFY(QDir(dir.path()).entryList({"*.jsonl"}).isEmpty());
        logs.setEnabled(true); logs.ingest("host","Connection failed: 192.168.");
        logs.setEnabled(false); logs.ingest("host","1.2\n");
        logs.setEnabled(true); logs.ingest("host","Encoder started\n");
        QFile file(dir.filePath("host-0.jsonl")); QVERIFY(file.open(QIODevice::ReadOnly)); const auto data=file.readAll();
        QVERIFY(data.contains("started")); QVERIFY(!data.contains("failed"));
        logs.setEnabled(false); const auto size=file.size(); logs.ingest("host","Connection failed\n"); QCOMPARE(file.size(),size);
    }
    void structuredTimingAndValidation() {
        const auto projected=Diagnostics::project("00:01 - SDL Info: DeskPort resize stage=input-init-end tick_ms=1234 width=1920 height=1080\n");
        QCOMPARE(projected.value("stage").toString(),QString("input-init-end"));
        QCOMPARE(projected.value("width").toInt(),1920);
        QJsonObject object=projected; object["source"]="client"; object["run"]=QString(32,'a'); object["elapsed_ms"]=100;
        object["private"]="192.0.2.1"; object["token"]="secret";
        auto validated=Diagnostics::validate(object); QVERIFY(!validated.contains("private")); QVERIFY(!validated.contains("token"));
        object["run"]="my-host"; QVERIFY(Diagnostics::validate(object).isEmpty());
        object["run"]=QString(32,'a'); object["source"]="private.example"; QVERIFY(Diagnostics::validate(object).isEmpty());
    }
    void navigationTimingKeepsOnlyFixedStages() {
        for (const auto& stage : {"devices-request", "devices-ready", "devices-frame", "event-loop-delay"}) {
            auto record = Diagnostics::project(QString("DeskPort navigation stage=%1 duration_ms=875").arg(stage));
            QCOMPARE(record.value("event").toString(), QString("navigation"));
            QCOMPARE(record.value("duration_ms").toInt(), 875);
            record["source"] = "client"; record["run"] = QString(32, 'a'); record["elapsed_ms"] = 100;
            QCOMPARE(Diagnostics::validate(record).value("stage").toString(), QString(stage));
        }
        QVERIFY(Diagnostics::project("DeskPort navigation stage=private-host duration_ms=875").isEmpty());
    }
    void networkRecordsKeepOnlyIntegerMeasurements() {
        auto net = Diagnostics::project("DeskPort network rtt_ms=331 rtt_var_ms=12 received_fps=58 rendered_fps=57 network_drop_permille=4 pacer_drop_permille=20 received_kbps=2875");
        QCOMPARE(net.value("event").toString(), QString("network"));
        QCOMPARE(net.value("rtt_ms").toInt(), 331);
        QCOMPARE(net.value("received_kbps").toInt(), 2875);
        net["source"] = "client"; net["run"] = QString(32, 'a'); net["elapsed_ms"] = 100; net["host"] = "192.0.2.1";
        auto safe = Diagnostics::validate(net);
        QCOMPARE(safe.value("pacer_drop_permille").toInt(), 20);
        QVERIFY(!safe.contains("host"));
        net["rtt_ms"] = -5; net["received_fps"] = 1.5;
        safe = Diagnostics::validate(net);
        QVERIFY(!safe.contains("rtt_ms") && !safe.contains("received_fps"));
        QVERIFY(Diagnostics::project("DeskPort network rtt_ms=331 rtt_var_ms=12 host=192.0.2.1").value("event").toString() != "network");
        QCOMPARE(Diagnostics::project("[2026-10-14 16:00:00.000]: Info: DeskPort network path: remote").value("path").toString(), QString("remote"));
        auto rate = Diagnostics::project("[2026-10-14 16:00:00.000]: Info: DeskPort remote rate: target_kbps=5600, rtt_ms=430, baseline_ms=330, quality_floor=off");
        QCOMPARE(rate.value("event").toString(), QString("rate"));
        QCOMPARE(rate.value("target_kbps").toInt(), 5600);
        QCOMPARE(rate.value("quality_floor").toString(), QString("off"));
        QVERIFY(!rate.contains("ceiling_kbps") && !rate.contains("floor_kbps") && !rate.contains("resume_kbps"));
        rate["source"] = "host"; rate["run"] = QString(32, 'b'); rate["elapsed_ms"] = 5;
        QCOMPARE(Diagnostics::validate(rate).value("baseline_ms").toInt(), 330);
        auto start = Diagnostics::project("[2026-10-14 16:00:00.000]: Info: DeskPort remote rate control: ceiling_kbps=14268, floor_kbps=1783, resume_kbps=0, live_encoder=yes");
        QCOMPARE(start.value("ceiling_kbps").toInt(), 14268);
        start["source"] = "host"; start["run"] = QString(32, 'b'); start["elapsed_ms"] = 5;
        QCOMPARE(Diagnostics::validate(start).value("live_encoder").toString(), QString("yes"));
        auto ceiling = Diagnostics::project("[2026-10-15 10:00:00.000]: Info: DeskPort live ceiling: kbps=9454, permille=214");
        QCOMPARE(ceiling.value("event").toString(), QString("rate"));
        QCOMPARE(ceiling.value("ceiling_kbps").toInt(), 9454);
        auto encoded = Diagnostics::project("[2026-10-14 16:00:00.000]: Info: DeskPort encoded video: Mbps=0.410372, packets=168, keyframes=0, max_packet_bytes=8944 (before transport/FEC)");
        QCOMPARE(encoded.value("event").toString(), QString("encoded"));
        QCOMPARE(encoded.value("encoded_kbps").toInt(), 410);
        QCOMPARE(encoded.value("max_frame_bytes").toInt(), 8944);
        encoded["source"] = "host"; encoded["run"] = QString(32, 'b'); encoded["elapsed_ms"] = 5;
        QCOMPARE(Diagnostics::validate(encoded).value("frames").toInt(), 168);
    }
    void liveRatePreservesPolicyWithoutPrivateText() {
        for (bool saver : {false, true}) for (const auto& quality : {"off", "on"}) {
            const auto line = QString("[2026-11-02 18:31:00.000]: Info: DeskPort live rate: target_kbps=992, rtt_ms=27, quality_floor=%1, saver=%2")
                .arg(quality).arg(saver ? "on" : "off");
            auto record = Diagnostics::project(line);
            QCOMPARE(record.value("event").toString(), QString("rate"));
            QCOMPARE(record.value("target_kbps").toInt(), 992);
            QCOMPARE(record.value("rtt_ms").toInt(), 27);
            QCOMPARE(record.value("quality_floor").toString(), QString(quality));
            QCOMPARE(record.value("saver").toBool(), saver);
            QVERIFY(record.value("saver").isBool());
            QVERIFY(!record.contains("baseline_ms") && !record.contains("ceiling_kbps"));
            record["source"] = "host"; record["run"] = QString(32, 'b'); record["elapsed_ms"] = 5;
            record["address"] = "192.0.2.1"; record["codec"] = "private.example";
            const auto safe = Diagnostics::validate(record);
            QCOMPARE(safe.value("target_kbps").toInt(), 992);
            QCOMPARE(safe.value("rtt_ms").toInt(), 27);
            QCOMPARE(safe.value("quality_floor").toString(), QString(quality));
            QCOMPARE(safe.value("saver").toBool(), saver);
            QVERIFY(!safe.contains("address") && !safe.contains("codec"));
            for (const auto& invalid : {QJsonValue("private.example"), QJsonValue("on"), QJsonValue(1)}) {
                record["saver"] = invalid;
                QVERIFY(!Diagnostics::validate(record).contains("saver"));
            }
            QVERIFY(Diagnostics::project(line + " address=192.0.2.1").isEmpty());
            QVERIFY(Diagnostics::project(line + " clipboard=private sentence").isEmpty());
        }
        QVERIFY(Diagnostics::project("DeskPort live rate: target_kbps=-1, rtt_ms=27, quality_floor=off, saver=on").isEmpty());
        QVERIFY(Diagnostics::project("DeskPort live rate: target_kbps=992, rtt_ms=27, quality_floor=private, saver=on").isEmpty());
        QVERIFY(Diagnostics::project("DeskPort live rate: target_kbps=992, rtt_ms=27, quality_floor=off, saver=private").isEmpty());
    }
    void liveRateKeepsNumericQpSeparateFromCongestion() {
        for (int qp : {0, 22, 24, 32, 51}) {
            auto record = Diagnostics::project(QString("DeskPort live rate: target_kbps=992, rtt_ms=27, quality_floor=off, saver=on, min_qp=%1").arg(qp));
            QCOMPARE(record.value("event").toString(), QString("rate"));
            QCOMPARE(record.value("quality_floor").toString(), QString("off"));
            QCOMPARE(record.value("min_qp").toInt(-1), qp);
            record["source"] = "host"; record["run"] = QString(32, 'b'); record["elapsed_ms"] = 5;
            QCOMPARE(Diagnostics::validate(record).value("min_qp").toInt(-1), qp);
            for (const auto& invalid : {QJsonValue(-1), QJsonValue(52), QJsonValue(22.5), QJsonValue("22"), QJsonValue(true), QJsonValue()}) {
                auto altered = record; altered["min_qp"] = invalid;
                QVERIFY(Diagnostics::validate(altered).isEmpty());
            }
        }
        auto congested = Diagnostics::project("DeskPort live rate: target_kbps=992, rtt_ms=27, quality_floor=on, saver=off, min_qp=32");
        QCOMPARE(congested.value("quality_floor").toString(), QString("on"));
        QCOMPARE(congested.value("min_qp").toInt(), 32);
        for (const auto& qp : {"-1", "52", "22.5", "private.example", "22 host=192.0.2.1", "22 clipboard=private sentence"})
            QVERIFY(Diagnostics::project(QString("DeskPort live rate: target_kbps=992, rtt_ms=27, quality_floor=off, saver=on, min_qp=%1").arg(qp)).isEmpty());
    }
    void liveRateQualityRangeRejectsInconsistentBounds() {
        for (const auto& range : {QPair<int,int>{0, 51}, {22, 26}, {32, 51}, {0, 0}, {51, 51}}) {
            auto record = Diagnostics::project(QString("DeskPort live rate: target_kbps=992, rtt_ms=0, quality_floor=off, saver=on, min_qp=%1, max_qp=%2")
                .arg(range.first).arg(range.second));
            QCOMPARE(record.value("event").toString(), QString("rate"));
            QCOMPARE(record.value("min_qp").toInt(-1), range.first);
            QCOMPARE(record.value("max_qp").toInt(-1), range.second);
            record["source"] = "host"; record["run"] = QString(32, 'b'); record["elapsed_ms"] = 5;
            QCOMPARE(Diagnostics::validate(record).value("max_qp").toInt(-1), range.second);
            for (const auto& invalid : {QJsonValue(-1), QJsonValue(52), QJsonValue(26.5), QJsonValue("26"), QJsonValue(true), QJsonValue()}) {
                auto altered = record; altered["max_qp"] = invalid;
                QVERIFY(Diagnostics::validate(altered).isEmpty());
            }
            auto altered = record; altered["min_qp"] = 32; altered["max_qp"] = 26;
            QVERIFY(Diagnostics::validate(altered).isEmpty());
        }
        for (const auto& suffix : {"max_qp=-1", "max_qp=52", "max_qp=26.5", "max_qp=private.example", "max_qp=21", "max_qp=26 host=192.0.2.1"})
            QVERIFY(Diagnostics::project(QString("DeskPort live rate: target_kbps=992, rtt_ms=0, quality_floor=off, saver=on, min_qp=22, %1").arg(suffix)).isEmpty());
    }
    void pictureQualityStrictKeepsActualRange() {
        for (const bool strict : {false, true}) {
            const int minimum = 22;
            const int maximum = strict ? 26 : 51;
            const auto line = QString("[2026-11-02 18:31:00.000]: Info: DeskPort live quality: min_qp=%1, max_qp=%2, strict=%3")
                .arg(minimum).arg(maximum).arg(strict ? "on" : "off");
            auto record = Diagnostics::project(line);
            QCOMPARE(record.size(), 4);
            QCOMPARE(record.value("event").toString(), QString("rate"));
            QCOMPARE(record.value("min_qp").toInt(-1), minimum);
            QCOMPARE(record.value("max_qp").toInt(-1), maximum);
            QVERIFY(record.value("quality_strict").isBool());
            QCOMPARE(record.value("quality_strict").toBool(), strict);
            QVERIFY(!record.contains("target_kbps") && !record.contains("rtt_ms") &&
                    !record.contains("quality_floor") && !record.contains("saver"));
            record["source"] = "host"; record["run"] = QString(32, 'b'); record["elapsed_ms"] = 5;
            record["address"] = "192.0.2.1";
            const auto safe = Diagnostics::validate(record);
            QCOMPARE(safe.value("quality_strict").toBool(), strict);
            QCOMPARE(safe.value("min_qp").toInt(-1), minimum);
            QCOMPARE(safe.value("max_qp").toInt(-1), maximum);
            QVERIFY(!safe.contains("address"));
            for (const auto& invalid : {QJsonValue("on"), QJsonValue(1), QJsonValue()}) {
                auto altered = record; altered["quality_strict"] = invalid;
                QVERIFY(Diagnostics::validate(altered).isEmpty());
            }
            auto altered = record; altered["quality_strict"] = !strict;
            QVERIFY(Diagnostics::validate(altered).isEmpty());
            altered = record; altered["min_qp"] = 32;
            QVERIFY(Diagnostics::validate(altered).isEmpty());
            altered = record; altered.remove("min_qp");
            QVERIFY(Diagnostics::validate(altered).isEmpty());
            altered = record; altered.remove("max_qp");
            QVERIFY(Diagnostics::validate(altered).isEmpty());
            altered = record; altered["event"] = "network";
            QVERIFY(!Diagnostics::validate(altered).contains("quality_strict"));
            QVERIFY(Diagnostics::project(line + " host=private.example").isEmpty());
            QVERIFY(Diagnostics::project(line + " clipboard=private sentence").isEmpty());
        }
        for (const auto& suffix : {"min_qp=22, max_qp=26, strict=off", "min_qp=32, max_qp=51, strict=off",
                 "min_qp=22, max_qp=51, strict=on", "min_qp=32, max_qp=52, strict=on",
                 "min_qp=-1, max_qp=51, strict=on", "min_qp=22, max_qp=51, strict=true",
                 "min_qp=22, max_qp=51, strict=private.example"})
            QVERIFY(Diagnostics::project(QString("DeskPort live quality: %1").arg(suffix)).isEmpty());
    }
    void selectedDecoderMetadata_data() {
        QTest::addColumn<int>("format"); QTest::addColumn<QString>("codec");
        for (int format : {0x0001, 0x0004})
            QTest::newRow(qPrintable(QString::number(format, 16))) << format << QString("h264");
        for (int format : {0x0100, 0x0200, 0x0400, 0x0800})
            QTest::newRow(qPrintable(QString::number(format, 16))) << format << QString("hevc");
        for (int format : {0x1000, 0x2000, 0x4000, 0x8000})
            QTest::newRow(qPrintable(QString::number(format, 16))) << format << QString("av1");
    }
    void selectedDecoderMetadata() {
        QFETCH(int, format); QFETCH(QString, codec);
        const auto line = QString("00:00:28 - SDL Info: Video stream is 2040x1228x60 (format 0x%1)").arg(format, 0, 16);
        auto record = Diagnostics::project(line);
        QCOMPARE(record.value("event").toString(), QString("decoder"));
        QCOMPARE(record.value("codec").toString(), codec);
        QCOMPARE(record.value("video_format").toInt(), format);
        QCOMPARE(record.value("fps").toInt(), 60);
        QCOMPARE(record.value("width").toInt(), 2040);
        QCOMPARE(record.value("height").toInt(), 1228);
        record["source"] = "client"; record["run"] = QString(32, 'a'); record["elapsed_ms"] = 10;
        record["address"] = "192.0.2.1"; record["saver"] = true; record["min_qp"] = 22; record["max_qp"] = 26;
        const auto safe = Diagnostics::validate(record);
        QCOMPARE(safe.value("codec").toString(), codec);
        QCOMPARE(safe.value("video_format").toInt(), format);
        QCOMPARE(safe.value("fps").toInt(), 60);
        QVERIFY(!safe.contains("address") && !safe.contains("saver") && !safe.contains("min_qp") && !safe.contains("max_qp"));
        QVERIFY(Diagnostics::project(line + " host=private.example").isEmpty());
        for (const auto& invalid : {QJsonValue(-1), QJsonValue(0), QJsonValue(1001), QJsonValue(59.5), QJsonValue("60")}) {
            auto altered = record; altered["fps"] = invalid;
            QVERIFY(Diagnostics::validate(altered).isEmpty());
        }
        auto altered = record; altered["codec"] = "private.example";
        QVERIFY(Diagnostics::validate(altered).isEmpty());
        altered = record; altered["video_format"] = 0x0101;
        QVERIFY(Diagnostics::validate(altered).isEmpty());
        altered = record; altered["video_format"] = format + 0.5;
        QVERIFY(Diagnostics::validate(altered).isEmpty());
        altered = record; altered["height"] = 0;
        QVERIFY(Diagnostics::validate(altered).isEmpty());
    }
    void selectedDecoderRejectsUnknownFormatsAndBounds() {
        for (const auto& line : {
                 "Video stream is 2040x1228x60 (format 0x101)",
                 "Video stream is 2040x1228x60 (format 0xffffffff)",
                 "Video stream is 2040x1228x0 (format 0x100)",
                 "Video stream is 2040x1228x1001 (format 0x100)",
                 "Video stream is 0x1228x60 (format 0x100)",
                 "Video stream is 2040x65536x60 (format 0x100)",
                 "Video stream is 2040x1228x60 (format 0x100) clipboard=private sentence"})
            QVERIFY(Diagnostics::project(line).isEmpty());
        // Probing a decoder must not masquerade as the selected stream.
        QVERIFY(!Diagnostics::project("Found HEVC decoder: available").contains("codec"));
    }
    void rotationRetentionAndExportAllowlist() {
        QTemporaryDir dir; Diagnostics logs(nullptr,dir.path()); logs.setEnabled(true);
        for (int i=0;i<28000;++i) logs.record("host","Connection failed");
        logs.ingest("host", "DeskPort live rate: target_kbps=992, rtt_ms=27, quality_floor=off, saver=on, min_qp=22, max_qp=26\n");
        logs.ingest("host", "DeskPort live quality: min_qp=22, max_qp=26, strict=on\n");
        logs.ingest("host", "DeskPort live quality: min_qp=22, max_qp=51, strict=off\n");
        logs.record("client", "Video stream is 2040x1228x60 (format 0x100)");
        QVERIFY(QFile::exists(dir.filePath("host-2.jsonl")));
        for (auto name:QDir(dir.path()).entryList({"*.jsonl"})) QVERIFY(QFileInfo(dir.filePath(name)).size()<=Diagnostics::FileLimit);
        QFile secret(dir.filePath("state.json")); QVERIFY(secret.open(QIODevice::WriteOnly)); secret.write("PAIRING_SECRET"); secret.close();
        QFile linkTarget(dir.filePath("credentials")); QVERIFY(linkTarget.open(QIODevice::WriteOnly)); linkTarget.write("KEY_SECRET"); linkTarget.close();
        QVERIFY(QFile::link(linkTarget.fileName(),dir.filePath("display-0.jsonl")));
        QFile old(dir.filePath("display-1.jsonl")); QVERIFY(old.open(QIODevice::WriteOnly)); old.write("OLD_SECRET");
        QVERIFY(old.flush());
        QVERIFY(old.setFileTime(QDateTime::currentDateTimeUtc().addDays(-8),QFileDevice::FileModificationTime)); old.close();
        // Valid records with injected extra keys are reserialized from allowed fields.
        QFile altered(dir.filePath("client-0.jsonl")); QVERIFY(altered.open(QIODevice::WriteOnly|QIODevice::Append));
        altered.write("{\"source\":\"client\",\"run\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"event\":\"failed\",\"elapsed_ms\":5,\"message\":\"INJECTED_SECRET\"}\n"); altered.close();
        const QString viewer=dir.filePath("viewers/"+QString(32,'b'));
        QVERIFY(QDir().mkpath(viewer));
        QVERIFY(QFile::copy(dir.filePath("client-0.jsonl"),viewer+"/client-0.jsonl"));
        QFile privateFile(viewer+"/private.json"); QVERIFY(privateFile.open(QIODevice::WriteOnly)); privateFile.write("WORKER_SECRET"); privateFile.close();
        const auto path=logs.createBundle(); QVERIFY(!path.isEmpty()); QFile archive(path); QVERIFY(archive.open(QIODevice::ReadOnly)); const auto bytes=archive.readAll();
        for (auto secretValue:{"PAIRING_SECRET","KEY_SECRET","OLD_SECRET","INJECTED_SECRET","state.json","credentials","WORKER_SECRET","private.json"}) QVERIFY(!bytes.contains(secretValue));
        QVERIFY(!QFile::exists(old.fileName()));
        const auto root=qEnvironmentVariable("TEST_DIAGNOSTICS_ROOT"); QFile::remove(root+"/verified.zip"); QVERIFY(QFile::copy(path,root+"/verified.zip"));
        QVERIFY(bytes.contains("viewer-0-client-0.jsonl"));
        logs.clear(); QVERIFY(!QFile::exists(viewer+"/client-0.jsonl")); QVERIFY(QFile::exists(privateFile.fileName())); QVERIFY(!QFile::exists(path)); QVERIFY(QFile::exists(secret.fileName()));
    }
    void connectionCodesAndExportFailure() {
        QTemporaryDir dir; Diagnostics logs(nullptr,dir.filePath("logs")); logs.setEnabled(true);
        logs.connection(3,-10060,"failed"); logs.connection(3,0,"starting");
        QFile file(dir.filePath("logs/client-0.jsonl")); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto lines=file.readAll().trimmed().split('\n'); QVERIFY(lines.size()>=3);
        auto failed=QJsonDocument::fromJson(lines[lines.size()-2]).object();
        auto starting=QJsonDocument::fromJson(lines.last()).object();
        QCOMPARE(failed.value("error").toInt(),-10060); QCOMPARE(failed.value("phase").toInt(),3);
        QCOMPARE(failed.value("run"),starting.value("run"));
        QVERIFY(starting.value("elapsed_ms").toDouble()>=failed.value("elapsed_ms").toDouble());
        QFile blocker(dir.filePath("blocked")); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        Diagnostics broken(nullptr,blocker.fileName()+"/logs"); QVERIFY(broken.createBundle().isEmpty()); QVERIFY(!broken.status().isEmpty());
        Diagnostics unknown(nullptr,dir.filePath("unknown")); unknown.setEnabled(true);
        unknown.ingest("host",QByteArray(20000,'x')+"connection failed\n");
        QVERIFY(!QFile::exists(dir.filePath("unknown/host-0.jsonl")));
    }
    void feedbackOpensOnlyStaticPublicUrl() {
        QTemporaryDir dir; Diagnostics logs(nullptr,dir.path());
        QDesktopServices::setUrlHandler("https",this,"captureUrl"); QVERIFY(logs.feedback());
        QCOMPARE(opened.host(),QString("github.com")); QCOMPARE(opened.path(),QString("/keithxc/deskport/issues/new"));
        QVERIFY(!opened.toString().contains(dir.path())); QVERIFY(!opened.toString().contains(".jsonl"));
        QVERIFY(QUrlQuery(opened).queryItemValue("body").contains("PUBLIC"));
        QVERIFY(logs.status().contains("version information only"));
        QDesktopServices::unsetUrlHandler("https");
    }
};
QTEST_MAIN(DiagnosticsTest)
#include "diagnostics-test.moc"
