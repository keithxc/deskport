#include <QtTest>

#include "seamlesshostmanager.h"

#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <csignal>
#endif

namespace {
static QByteArray readyEvent() {
    return R"({"version":1,"type":"ready","socket":"deskport-test-1","width":1280,"height":720})";
}
static QByteArray createEvent(int id = 1) {
    return "{\"version\":1,\"type\":\"window-create\",\"id\":" + QByteArray::number(id) +
        R"(,"width":320,"height":200,"title":"Terminal","appId":"org.example.Terminal"})";
}
static QByteArray damageEvent(int sequence = 1) {
    return "{\"version\":1,\"type\":\"window-damage\",\"id\":1,\"sequence\":" + QByteArray::number(sequence) +
        R"(,"bufferType":"wl_shm","rectangles":[{"x":0,"y":0,"width":320,"height":200}],"rectanglesTruncated":false})";
}
static QByteArray frameEvent(int sequence = 1) {
    return "{\"version\":1,\"type\":\"frame-metadata\",\"id\":1,\"sequence\":" + QByteArray::number(sequence) +
        R"(,"width":320,"height":200,"pixelFormat":"rgba8888-premultiplied","sha256":")" + QByteArray(64, 'a') +
        R"(","damage":[{"x":0,"y":0,"width":320,"height":200}],"damageTruncated":false})";
}
static QByteArray emits(const QByteArray& output, const QByteArray& after = "time.sleep(30)\n") {
    return "import sys, time\nsys.stdout.buffer.write(bytes.fromhex('" + output.toHex() +
        "'))\nsys.stdout.flush()\n" + after;
}
static void useHelper(const QString& path) {
    qputenv("DESKPORT_SEAMLESS_HOST_BINARY", QFile::encodeName(path));
}
}

class SeamlessHostManagerTest : public QObject {
    Q_OBJECT
private:
    static QString helper(const QTemporaryDir& directory, const QByteArray& body) {
        const auto python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (python.isEmpty()) return {};
        const auto path = directory.path() + QStringLiteral("/fake-seamless-host");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return {};
        file.write("#!" + QFile::encodeName(python) + "\n" + body);
        file.close();
        if (!file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return {};
        return path;
    }

private slots:
    void cleanup() {
        qunsetenv("DESKPORT_SEAMLESS_HOST_BINARY");
    }

    void missingHelperFailsClosed() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        qputenv("DESKPORT_SEAMLESS_HOST_BINARY", QFile::encodeName(directory.path() + "/missing"));
        SeamlessHostManager manager;
        QVERIFY(!manager.available());
        QVERIFY(!manager.start());
        QVERIFY(!manager.running());
        QVERIFY(!manager.ready());
        qunsetenv("DESKPORT_SEAMLESS_HOST_BINARY");
    }

    void lifecycleAndExactChildTeardown() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = helper(directory, R"PY(
import json, signal, sys, time
running = True
def stop(_signum, _frame):
    global running
    print(json.dumps({"version": 1, "type": "window-destroy", "id": 1}), flush=True)
    running = False
signal.signal(signal.SIGTERM, stop)
print(json.dumps({"version": 1, "type": "ready", "socket": "deskport-test-1", "width": 1280, "height": 720}), flush=True)
print(json.dumps({"version": 1, "type": "window-create", "id": 1, "width": 320, "height": 200, "title": "Terminal", "appId": "org.example.Terminal"}), flush=True)
while running:
    time.sleep(0.02)
)PY");
        QVERIFY(!path.isEmpty());
        qputenv("DESKPORT_SEAMLESS_HOST_BINARY", QFile::encodeName(path));
        SeamlessHostManager manager;
        QSignalSpy events(&manager, &SeamlessHostManager::eventReceived);
        QSignalSpy stopped(&manager, &SeamlessHostManager::stopped);
        QStringList statuses;
        connect(&manager, &SeamlessHostManager::changed, &manager,
                [&] { statuses.append(manager.status()); });
        QVERIFY(manager.available());
        QVERIFY(manager.start({QStringLiteral("terminal"), QStringLiteral("--safe-argument")}));
        QTRY_VERIFY_WITH_TIMEOUT(manager.ready(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(manager.windowCount(), 1, 5000);
        const auto pid = manager.processId();
        QVERIFY(pid > 0);
        manager.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.running(), 7000);
        QTRY_VERIFY_WITH_TIMEOUT(!stopped.isEmpty(), 7000);
        QCOMPARE(manager.windowCount(), 0);
        QCOMPARE(manager.status(), QStringLiteral("Seamless host is off"));
        QCOMPARE(stopped.size(), 1);
        manager.stop();
        QCOMPARE(stopped.size(), 1);
        QVERIFY(std::none_of(statuses.cbegin(), statuses.cend(), [](const QString& status) {
            return status.contains(QStringLiteral("invalid"), Qt::CaseInsensitive);
        }));
        QVERIFY(events.size() >= 2);
#ifdef Q_OS_UNIX
        errno = 0;
        QCOMPARE(::kill(pid_t(pid), 0), -1);
        QCOMPARE(errno, ESRCH);
#endif
        qunsetenv("DESKPORT_SEAMLESS_HOST_BINARY");
    }

    void lifecycleBeforeReadyStopsHelper() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = helper(directory, R"PY(
import json, time
print(json.dumps({"version": 1, "type": "window-create", "id": 1, "width": 320, "height": 200, "title": "bad", "appId": "bad"}), flush=True)
time.sleep(30)
)PY");
        QVERIFY(!path.isEmpty());
        qputenv("DESKPORT_SEAMLESS_HOST_BINARY", QFile::encodeName(path));
        SeamlessHostManager manager;
        QStringList statuses;
        connect(&manager, &SeamlessHostManager::changed, &manager,
                [&] { statuses.append(manager.status()); });
        QVERIFY(manager.start());
        QTRY_VERIFY_WITH_TIMEOUT(!manager.running(), 8000);
        QVERIFY(std::any_of(statuses.cbegin(), statuses.cend(), [](const QString& status) {
            return status.contains(QStringLiteral("lifecycle"), Qt::CaseInsensitive);
        }));
        qunsetenv("DESKPORT_SEAMLESS_HOST_BINARY");
    }

    void oversizedControlLineStopsHelper() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = helper(directory, R"PY(
import sys, time
sys.stdout.write("x" * 33000)
sys.stdout.flush()
time.sleep(30)
)PY");
        QVERIFY(!path.isEmpty());
        qputenv("DESKPORT_SEAMLESS_HOST_BINARY", QFile::encodeName(path));
        SeamlessHostManager manager;
        QStringList statuses;
        connect(&manager, &SeamlessHostManager::changed, &manager,
                [&] { statuses.append(manager.status()); });
        QVERIFY(manager.start());
        QTRY_VERIFY_WITH_TIMEOUT(!manager.running(), 8000);
        QVERIFY(std::any_of(statuses.cbegin(), statuses.cend(), [](const QString& status) {
            return status.contains(QStringLiteral("oversized"), Qt::CaseInsensitive);
        }));
        qunsetenv("DESKPORT_SEAMLESS_HOST_BINARY");
    }

    void invalidEvents_data() {
        QTest::addColumn<QByteArray>("output");
        const auto ready = readyEvent() + '\n';
        const auto window = ready + createEvent() + '\n';
        const auto destroy = QByteArray(R"({"version":1,"type":"window-destroy","id":1})") + '\n';
        const auto title = QByteArray(R"({"version":1,"type":"window-title","id":1,"title":"New title"})");
        QTest::newRow("unknown-type") << ready + R"({"version":1,"type":"unexpected"})" + '\n';
        QTest::newRow("fractional-version") << QByteArray(ready).replace("\"version\":1", "\"version\":1.5");
        QTest::newRow("duplicate-ready") << ready + ready;
        QTest::newRow("socket-trailing-newline") << QByteArray(ready).replace("deskport-test-1", "deskport-test-1\\n");
        QTest::newRow("unknown-window") << ready + title + '\n';
        QTest::newRow("destroyed-window") << window + destroy + title + '\n';
        QTest::newRow("reused-id") << window + destroy + createEvent() + '\n';
        QTest::newRow("fractional-id") << window + QByteArray(title).replace("\"id\":1", "\"id\":1.5") + '\n';
        QTest::newRow("oversized-title") << window + QByteArray(title).replace("New title", QByteArray(1025, 'a')) + '\n';
        QTest::newRow("utf8-title-byte-limit") << window + QByteArray(title).replace("New title", QString(513, QChar(0x00e9)).toUtf8()) + '\n';
        QTest::newRow("nul-title") << window + QByteArray(title).replace("New title", "bad\\u0000title") + '\n';
        QTest::newRow("numeric-app-id") << window + R"({"version":1,"type":"window-app-id","id":1,"appId":42})" + '\n';
        QTest::newRow("frame-without-damage") << window + frameEvent() + '\n';
        QTest::newRow("fractional-sequence") << window + QByteArray(damageEvent()).replace("\"sequence\":1", "\"sequence\":1.5") + '\n';
        QTest::newRow("duplicate-damage") << window + damageEvent() + '\n' + damageEvent() + '\n';
        QTest::newRow("wrong-frame-sequence") << window + damageEvent() + '\n' + frameEvent(2) + '\n';
        QTest::newRow("duplicate-frame") << window + damageEvent() + '\n' + frameEvent() + '\n' + frameEvent() + '\n';
        QTest::newRow("stale-damage") << window + damageEvent() + '\n' + frameEvent() + '\n' + damageEvent() + '\n';
        QTest::newRow("invalid-hash") << window + damageEvent() + '\n' + QByteArray(frameEvent()).replace(QByteArray(64, 'a'), QByteArray(64, 'z')) + '\n';
        QTest::newRow("unsafe-frame-size") << window + damageEvent() + '\n' + QByteArray(frameEvent()).replace("\"width\":320", "\"width\":8000") + '\n';
        QTest::newRow("unsafe-damage-rectangle") << window + QByteArray(damageEvent()).replace("\"x\":0", "\"x\":7680") + '\n';
        QTest::newRow("malformed-error") << QByteArray(R"({"version":1,"type":"error","message":123})") + '\n';
        QTest::newRow("invalid-json") << ready + "{\n";
        QTest::newRow("oversized-tail") << ready + QByteArray(32768, ' ');
        QTest::newRow("oversized-record") << QByteArray(32768, ' ') + '\n';
    }

    void invalidEvents() {
        QFETCH(QByteArray, output);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = helper(directory, emits(output));
        QVERIFY(!path.isEmpty());
        useHelper(path);
        SeamlessHostManager manager;
        QSignalSpy stopped(&manager, &SeamlessHostManager::stopped);
        QVERIFY(manager.start());
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 8000);
        QVERIFY(!manager.running());
        QVERIFY(!manager.ready());
        QCOMPARE(manager.windowCount(), 0);
        QVERIFY2(manager.status().contains("invalid") || manager.status().contains("oversized"),
                 qPrintable(manager.status()));
        const auto failure = manager.status();
        manager.stop();
        QCOMPARE(stopped.size(), 1);
        QCOMPARE(manager.status(), failure);
    }

    void fragmentedLifecycleAndFrameEvents() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray output = QByteArray(R"({"version":1,"type":"child-start","pid":123,"program":"terminal"})") + '\n' +
            readyEvent() + '\n' + createEvent() + '\n' +
            R"({"version":1,"type":"window-title","id":1,"title":"Updated title"})" "\n" +
            R"({"version":1,"type":"window-app-id","id":1,"appId":"org.example.App"})" "\n" +
            R"({"version":1,"type":"frame-capture-failed","id":1,"sequence":1,"error":"unsafe-buffer-size","width":-1,"height":99999})" "\n" +
            damageEvent(2) + '\n' +
            R"({"version":1,"type":"capture-file-error","id":1,"sequence":2,"message":"local capture unavailable"})" "\n" +
            frameEvent(2) + '\n' +
            R"({"version":1,"type":"window-destroy","id":1})" "\n" + createEvent(2) + '\n' +
            R"({"version":1,"type":"child-exit","exitCode":0,"exitStatus":"normal"})" "\n";
        const auto path = helper(directory,
            "import sys, time\ndata = bytes.fromhex('" + output.toHex() + "')\n"
            "for i in range(0, len(data), 7):\n"
            "    sys.stdout.buffer.write(data[i:i+7])\n    sys.stdout.flush()\n    time.sleep(0.001)\n"
            "time.sleep(30)\n");
        QVERIFY(!path.isEmpty());
        useHelper(path);
        SeamlessHostManager manager;
        QSignalSpy events(&manager, &SeamlessHostManager::eventReceived);
        QVERIFY(manager.start());
        QTRY_COMPARE_WITH_TIMEOUT(events.size(), 12, 5000);
        QVERIFY(manager.ready());
        QCOMPARE(manager.windowCount(), 1);
        manager.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.running(), 7000);
    }

    void windowSnapshotsTrackCaptureAndReset() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray output = readyEvent() + '\n' + createEvent() + '\n' + createEvent(3) + '\n' +
            R"({"version":1,"type":"window-title","id":1,"title":"Updated title"})" "\n" +
            R"({"version":1,"type":"window-app-id","id":1,"appId":"org.example.App"})" "\n" +
            damageEvent() + '\n' + frameEvent() + '\n' + damageEvent(2) + '\n' +
            R"({"version":1,"type":"frame-capture-failed","id":1,"sequence":2,"error":"unsupported-buffer"})" "\n" +
            damageEvent(3) + '\n' +
            R"({"version":1,"type":"capture-file-error","id":1,"sequence":3,"message":"disk full"})" "\n" +
            frameEvent(3) + '\n' + damageEvent(4) + '\n' + frameEvent(4) + '\n' +
            R"({"version":1,"type":"window-destroy","id":1})" "\n";
        const auto path = helper(directory, emits(output));
        QVERIFY(!path.isEmpty());
        useHelper(path);
        SeamlessHostManager manager;
        QList<QJsonArray> snapshots;
        connect(&manager, &SeamlessHostManager::eventReceived, &manager,
                [&] { snapshots.append(manager.windowStates()); });
        QSignalSpy stopped(&manager, &SeamlessHostManager::stopped);
        QVERIFY(manager.windowStates().isEmpty());
        QVERIFY(manager.start());
        QTRY_COMPARE_WITH_TIMEOUT(snapshots.size(), 15, 5000);
        const auto initial = snapshots[1].first().toObject();
        QCOMPARE(initial["title"].toString(), QString("Terminal"));
        QCOMPARE(initial["initialWidth"].toInt(), 320);
        QCOMPARE(initial["initialHeight"].toInt(), 200);
        QVERIFY(initial["lastFrame"].toObject().isEmpty());
        QCOMPARE(snapshots[2].size(), 2);
        QCOMPARE(snapshots[2][0].toObject()["id"].toInt(), 1);
        QCOMPARE(snapshots[2][1].toObject()["id"].toInt(), 3);
        QCOMPARE(snapshots[4][0].toObject()["title"].toString(), QString("Updated title"));
        QCOMPARE(snapshots[4][0].toObject()["appId"].toString(), QString("org.example.App"));
        QCOMPARE(snapshots[5][0].toObject()["pendingSequence"].toInt(), 1);
        const auto captured = snapshots[6][0].toObject();
        QCOMPARE(captured["pendingSequence"].toInt(), 0);
        QCOMPARE(captured["lastSequence"].toInt(), 1);
        const auto frame = captured["lastFrame"].toObject();
        QCOMPARE(frame["width"].toInt(), 320);
        QCOMPARE(frame["height"].toInt(), 200);
        QCOMPARE(frame["sha256"].toString(), QString(64, 'a'));
        const auto failed = snapshots[8][0].toObject();
        QCOMPARE(failed["lastFrame"].toObject(), frame);
        QCOMPARE(failed["lastSequence"].toInt(), 2);
        QCOMPARE(failed["captureError"].toString(), QString("unsupported-buffer"));
        const auto recovered = snapshots[11][0].toObject();
        QCOMPARE(recovered["lastFrame"].toObject()["sequence"].toInt(), 3);
        QVERIFY(recovered["captureError"].toString().isEmpty());
        QCOMPARE(recovered["captureFileError"].toString(), QString("disk full"));
        QVERIFY(snapshots[13][0].toObject()["captureFileError"].toString().isEmpty());
        QCOMPARE(manager.windowStates().size(), 1);
        QCOMPARE(manager.windowStates().first().toObject()["id"].toInt(), 3);
        manager.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 7000);
        QVERIFY(manager.windowStates().isEmpty());
        // Earlier copies stay intact after metadata changes, destruction and stop.
        QCOMPARE(snapshots[1].first().toObject(), initial);
        QVERIFY(!helper(directory, emits(readyEvent() + '\n' + createEvent() + '\n')).isEmpty());
        QVERIFY(manager.start());
        QTRY_COMPARE_WITH_TIMEOUT(manager.windowCount(), 1, 5000);
        const auto restarted = manager.windowStates().first().toObject();
        QCOMPARE(restarted, initial);
        manager.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 2, 7000);
    }

    void windowSnapshotsFitLocalControlLimit() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QByteArray output = readyEvent() + '\n';
        const QString escapedText(1024, QChar(1));
        const auto append = [&](QJsonObject event) {
            event["version"] = 1;
            output += QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
        };
        for (int id = 1; id <= 64; ++id) {
            append(QJsonObject{{"type", "window-create"}, {"id", id}, {"width", 320}, {"height", 200},
                               {"title", escapedText}, {"appId", escapedText.left(255)}});
            append(QJsonObject{{"type", "window-damage"}, {"id", id}, {"sequence", 1}, {"bufferType", "wl_shm"},
                               {"rectangles", QJsonArray{}}, {"rectanglesTruncated", false}});
            append(QJsonObject{{"type", "capture-file-error"}, {"id", id}, {"sequence", 1}, {"message", escapedText}});
            append(QJsonObject{{"type", "frame-capture-failed"}, {"id", id}, {"sequence", 1}, {"error", escapedText}});
        }
        const auto path = helper(directory, emits(output));
        QVERIFY(!path.isEmpty());
        useHelper(path);
        SeamlessHostManager manager;
        QSignalSpy events(&manager, &SeamlessHostManager::eventReceived);
        QVERIFY(manager.start());
        QTRY_COMPARE_WITH_TIMEOUT(events.size(), 257, 5000);
        const auto windows = manager.windowStates();
        QCOMPARE(windows.size(), 64);
        for (int index = 0; index < windows.size(); ++index) {
            const auto window = windows[index].toObject();
            QCOMPARE(window["id"].toInt(), index + 1);
            QCOMPARE(window["title"].toString(), escapedText);
            QCOMPARE(window["captureError"].toString(), escapedText.left(512));
            QCOMPARE(window["captureFileError"].toString(), escapedText.left(512));
        }
        // Include the real response envelope with headroom for ordinary status.
        const auto response = QJsonDocument(QJsonObject{{"version", 1}, {"ok", true},
            {"data", QJsonObject{{"windows", 64}, {"windowStates", windows}}}}).toJson(QJsonDocument::Compact);
        QVERIFY(response.size() < 960 * 1024);
        QSignalSpy stopped(&manager, &SeamlessHostManager::stopped);
        manager.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 7000);
    }

    void maximumControlRecordAccepted() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto output = readyEvent();
        output.chop(1);
        output += ",\"padding\":\"";
        output += QByteArray(32768 - output.size() - 3, ' ');
        output += "\"}\n";
        QCOMPARE(output.size(), 32768);
        const auto path = helper(directory, emits(output + createEvent() + '\n'));
        QVERIFY(!path.isEmpty());
        useHelper(path);
        SeamlessHostManager manager;
        QVERIFY(manager.start());
        QTRY_COMPARE_WITH_TIMEOUT(manager.windowCount(), 1, 5000);
        QVERIFY(manager.ready());
        manager.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.running(), 7000);
    }

    void stopRejectsLateReady_data() {
        QTest::addColumn<bool>("partial");
        QTest::newRow("whole-ready-after-stop") << false;
        QTest::newRow("fragmented-ready-after-stop") << true;
    }

    void stopRejectsLateReady() {
        QFETCH(bool, partial);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto ready = readyEvent() + '\n';
        const auto prefix = partial ? ready.left(25) : QByteArray();
        const auto suffix = ready.mid(prefix.size());
        const auto path = helper(directory,
            "import sys, time, signal\n"
            "def stop(_signum, _frame):\n"
            "    sys.stdout.buffer.write(bytes.fromhex('" + suffix.toHex() + "'))\n"
            "    sys.stdout.flush()\n    time.sleep(0.15)\n    sys.exit(0)\n"
            "signal.signal(signal.SIGTERM, stop)\n"
            "sys.stdout.buffer.write(bytes.fromhex('" + prefix.toHex() + "'))\n"
            "sys.stdout.flush()\n"
            // This event confirms the handler was installed, independently of stdout framing.
            "open('" + QFile::encodeName(directory.path() + "/started") + "', 'w').close()\n"
            "time.sleep(30)\n");
        QVERIFY(!path.isEmpty());
        useHelper(path);
        SeamlessHostManager manager;
        QSignalSpy events(&manager, &SeamlessHostManager::eventReceived);
        QSignalSpy stopped(&manager, &SeamlessHostManager::stopped);
        QVERIFY(manager.start());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(directory.path() + "/started"), 5000);
        manager.stop();
        bool revived = false;
        connect(&manager, &SeamlessHostManager::changed, &manager, [&] { revived |= manager.ready(); });
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 7000);
        QVERIFY(!revived);
        QVERIFY(!manager.ready());
        QVERIFY(events.isEmpty());
        QCOMPARE(manager.status(), QStringLiteral("Seamless host is off"));
    }

    void startupTimeoutAndRestart() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = helper(directory, "import time\ntime.sleep(30)\n");
        QVERIFY(!path.isEmpty());
        useHelper(path);
        SeamlessHostManager manager;
        QSignalSpy stopped(&manager, &SeamlessHostManager::stopped);
        QVERIFY(manager.start());
        QVERIFY(!manager.start());
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 17000);
        QVERIFY(manager.status().contains("timed out"));
        QVERIFY(!manager.ready());
        QVERIFY(!manager.running());
        QVERIFY(!helper(directory, emits(readyEvent() + '\n')).isEmpty());
        QVERIFY(manager.start());
        QTRY_VERIFY_WITH_TIMEOUT(manager.ready(), 5000);
        QVERIFY(!manager.status().contains("timed out"));
        manager.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 2, 7000);
    }

    void childErrorPreserved() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = helper(directory, emits(
            R"({"version":1,"type":"error","code":"sidecar-start-failed","message":"Compositor initialization failed"})" "\n"));
        QVERIFY(!path.isEmpty());
        useHelper(path);
        SeamlessHostManager manager;
        QSignalSpy stopped(&manager, &SeamlessHostManager::stopped);
        QVERIFY(manager.start());
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 7000);
        QVERIFY(manager.status().contains("Compositor initialization failed"));
    }

    void shutdownDiagnostics_data() {
        QTest::addColumn<QByteArray>("tail");
        QTest::addColumn<int>("exitCode");
        QTest::addColumn<QString>("expected");
        const auto error = QByteArray(R"({"version":1,"type":"error","code":"child-group-cleanup-timeout","message":"Owned child cleanup failed"})") + '\n';
        QTest::newRow("cleanup-error") << error << 0 << QStringLiteral("Owned child cleanup failed");
        QTest::newRow("cleanup-error-after-oversized-record") << QByteArray(33000, 'x') + '\n' + error
            << 0 << QStringLiteral("Owned child cleanup failed");
        QTest::newRow("unexpected-nonzero-exit") << QByteArray() << 17 << QStringLiteral("stopped (17");
    }

    void shutdownDiagnostics() {
        QFETCH(QByteArray, tail);
        QFETCH(int, exitCode);
        QFETCH(QString, expected);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto output = readyEvent() + '\n' +
            R"({"version":1,"type":"stopping","reason":"child-exited"})" "\n" + tail;
        const auto path = helper(directory, emits(output, "sys.exit(" + QByteArray::number(exitCode) + ")\n"));
        QVERIFY(!path.isEmpty());
        useHelper(path);
        SeamlessHostManager manager;
        QSignalSpy stopped(&manager, &SeamlessHostManager::stopped);
        QSignalSpy events(&manager, &SeamlessHostManager::eventReceived);
        QVERIFY(manager.start());
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 7000);
        QVERIFY2(manager.status().contains(expected), qPrintable(manager.status()));
        QVERIFY(!manager.ready());
        QCOMPARE(events.size(), 1); // Only ready; shutdown diagnostics never reopen lifecycle dispatch.
    }

    void truncatedOutputPreserved() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = helper(directory, emits(readyEvent() + '\n' + "{", ""));
        QVERIFY(!path.isEmpty());
        useHelper(path);
        SeamlessHostManager manager;
        QSignalSpy stopped(&manager, &SeamlessHostManager::stopped);
        QVERIFY(manager.start());
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 7000);
        QVERIFY(manager.status().contains("truncated"));
    }

    void failedExecStopsOnce() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = helper(directory, "");
        QVERIFY(!path.isEmpty());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("#!/definitely/missing/deskport-test-interpreter\n");
        file.close();
        useHelper(path);
        SeamlessHostManager manager;
        QSignalSpy stopped(&manager, &SeamlessHostManager::stopped);
        QVERIFY(manager.start());
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 5000);
        QVERIFY(manager.status().contains("failed"));
        manager.stop();
        QCOMPARE(stopped.size(), 1);
    }

    void cancelledFailedExecCanRestart() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = helper(directory, "");
        QVERIFY(!path.isEmpty());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("#!/definitely/missing/deskport-test-interpreter\n");
        file.close();
        useHelper(path);
        SeamlessHostManager manager;
        QSignalSpy stopped(&manager, &SeamlessHostManager::stopped);
        QVERIFY(manager.start());
        manager.stop(); // QProcess is still Starting; failed exec has no finished signal.
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 5000);
        QVERIFY(!manager.running());
        QVERIFY(!manager.ready());
        QVERIFY(!helper(directory, emits(readyEvent() + '\n')).isEmpty());
        QVERIFY(manager.start());
        QTRY_VERIFY_WITH_TIMEOUT(manager.ready(), 5000);
        manager.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 2, 7000);
    }

    void reentrantStopDoesNotForwardQueuedEvents() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = helper(directory, emits(readyEvent() + '\n' + createEvent() + '\n'));
        QVERIFY(!path.isEmpty());
        useHelper(path);
        SeamlessHostManager manager;
        QSignalSpy events(&manager, &SeamlessHostManager::eventReceived);
        connect(&manager, &SeamlessHostManager::changed, &manager, [&] {
            if (manager.ready()) manager.stop();
        });
        QVERIFY(manager.start());
        QTRY_VERIFY_WITH_TIMEOUT(!manager.running(), 7000);
        QVERIFY(events.isEmpty());
        QCOMPARE(manager.windowCount(), 0);
    }

    void restartFromStoppedCallback() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = helper(directory, emits(readyEvent() + '\n' + "{", ""));
        QVERIFY(!path.isEmpty());
        useHelper(path);
        SeamlessHostManager manager;
        QSignalSpy stopped(&manager, &SeamlessHostManager::stopped);
        bool restarted = false;
        connect(&manager, &SeamlessHostManager::stopped, &manager, [&] {
            if (stopped.size() != 1) return;
            if (helper(directory, emits(readyEvent() + '\n')).isEmpty()) return;
            restarted = manager.start();
        });
        QVERIFY(manager.start());
        QTRY_VERIFY_WITH_TIMEOUT(restarted && manager.ready(), 5000);
        QCOMPARE(stopped.size(), 1);
        QVERIFY(manager.running());
        manager.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 2, 7000);
    }

    void restartAfterReentrantStartupCancellation() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = helper(directory, emits(readyEvent() + '\n'));
        QVERIFY(!path.isEmpty());
        useHelper(path);
        SeamlessHostManager manager;
        QSignalSpy stopped(&manager, &SeamlessHostManager::stopped);
        bool cancelled = false;
        bool restarted = false;
        bool restartDuringFinish = true;
        connect(&manager, &SeamlessHostManager::changed, &manager, [&] {
            if (!cancelled && manager.status().contains("Starting")) {
                cancelled = true;
                manager.stop();
            } else if (!restarted && manager.status() == QStringLiteral("Seamless host is off")) {
                restartDuringFinish = manager.start();
                manager.stop();
            }
        });
        connect(&manager, &SeamlessHostManager::stopped, &manager, [&] {
            if (stopped.size() == 1) restarted = manager.start();
        });
        QVERIFY(!manager.start()); // The original start was cancelled by its observer.
        QVERIFY(!restartDuringFinish);
        QTRY_VERIFY_WITH_TIMEOUT(restarted && manager.ready(), 5000);
        QCOMPARE(stopped.size(), 1);
        manager.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 2, 7000);
    }

    void stopEscalatesForUnresponsiveHelper() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = helper(directory,
            "import signal\nsignal.signal(signal.SIGTERM, signal.SIG_IGN)\n" + emits(readyEvent() + '\n'));
        QVERIFY(!path.isEmpty());
        useHelper(path);
        SeamlessHostManager manager;
        QSignalSpy stopped(&manager, &SeamlessHostManager::stopped);
        QVERIFY(manager.start());
        QTRY_VERIFY_WITH_TIMEOUT(manager.ready(), 5000);
        const auto pid = manager.processId();
        manager.stop();
        manager.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 8000);
        QVERIFY(!manager.running());
        QVERIFY(manager.status().contains("forcing termination"));
#ifdef Q_OS_UNIX
        errno = 0;
        QCOMPARE(::kill(pid_t(pid), 0), -1);
        QCOMPARE(errno, ESRCH);
#endif
    }

};

QTEST_GUILESS_MAIN(SeamlessHostManagerTest)
#include "seamless-host-manager.moc"
