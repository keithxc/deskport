#include "backend/clipboardchannel.h"
#include "backend/clipboardprotocol.h"
#include "clipboard/agent.h"
#include <QtTest>
#include <QGuiApplication>
#include <QTcpServer>
#include <QSslSocket>
#include <QSslError>
#include <QJsonDocument>
#include <QFile>
#include <QPointer>
#include <QSocketNotifier>
#include <atomic>
#include <cstdio>
#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

// Count only timers delivered in this process's transport worker. The synthetic
// helper is a separate process, and the agent's GUI-thread fallback is excluded.
class TestApplication : public QGuiApplication {
public:
    using QGuiApplication::QGuiApplication;
    std::atomic<int> workerTimers{0};
    bool notify(QObject* receiver, QEvent* event) override {
        if (event->type() == QEvent::Timer && QThread::currentThread() != thread()) ++workerTimers;
        return QGuiApplication::notify(receiver, event);
    }
};

class MemoryClipboard final : public ClipboardNative {
public:
    qint64 generation = 0;
    int revisions = 0;
    QStringList types;
    QByteArray bytes;
    Reader reader;
    qint64 revision() override { ++revisions; return generation; }
    QStringList formats() override { return types; }
    QByteArray read(const QString& mime) override { return reader ? reader(mime) : bytes; }
    void publish(const QStringList& formats, Reader next, int = 1) override {
        types = formats; reader = std::move(next); ++generation;
    }
    void copy(const QByteArray& text) {
        reader = {}; types = QStringList{"text/plain;charset=utf-8"}; bytes = text; ++generation;
    }
};
// Satisfy agent.cpp's helper factory without linking any system clipboard backend.
std::unique_ptr<ClipboardNative> makeClipboardNative() {
    return std::unique_ptr<ClipboardNative>(new MemoryClipboard);
}

static QByteArray credential(const char* name) {
    QFile file(qEnvironmentVariable(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
static QByteArray frame(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

class SyntheticPeer : public QTcpServer {
public:
    enum Hold { None, Tls, Hello, Ready };
    Hold hold = None;
    bool native = false, admitted = false, started = false, coalescedResult = false;
    QPointer<QSslSocket> socket;
    QList<QJsonObject> requests;
    std::function<void(const QJsonObject&)> answer;
    QByteArray certificate = credential("IDLE_TEST_CERT"), key = credential("IDLE_TEST_KEY");
    SyntheticPeer() { listen(QHostAddress::LocalHost); }
    void send(const QJsonObject& object) { if (socket) { socket->write(frame(object)); socket->flush(); } }
protected:
    void incomingConnection(qintptr descriptor) override {
        auto connection = new QSslSocket(this);
        socket = connection;
        connection->setSocketDescriptor(descriptor);
        if (hold == Tls) return;
        connection->setLocalCertificate(QSslCertificate(certificate));
        connection->setPrivateKey(QSslKey(key, QSsl::Rsa));
        connection->setProtocol(QSsl::TlsV1_2OrLater);
        connection->setPeerVerifyMode(QSslSocket::VerifyPeer);
        connect(connection, qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors), connection,
                [connection](const QList<QSslError>& errors) { connection->ignoreSslErrors(errors); });
        connect(connection, &QSslSocket::encrypted, this, [this, connection] {
            admitted = connection->peerCertificate() == QSslCertificate(certificate);
            if (!admitted) { connection->abort(); return; }
            if (hold != Hello) send({{"type", "hello"}, {"meta", QJsonObject{{"clipboard", 1}, {"clipboardV2", native ? 1 : 0}}}});
        });
        auto buffer = std::make_shared<QByteArray>();
        connect(connection, &QSslSocket::readyRead, this, [this, connection, buffer] {
            *buffer += connection->readAll();
            int end;
            while ((end = buffer->indexOf('\n')) >= 0) {
                const auto message = QJsonDocument::fromJson(buffer->left(end)).object();
                buffer->remove(0, end + 1);
                const auto type = message["type"].toString();
                if (type == "clipboard-start" || type == "clipboard-v2-start") {
                    started = true;
                    if (hold == Ready) continue;
                    auto bytes = frame({{"type", native ? "clipboard-v2-ready" : "clipboard-ready"}, {"maxText", 4096}});
                    if (native) bytes += frame({{"type", "clipboard-v2-offer"}, {"id", "coalesced-offer"}});
                    else if (coalescedResult) bytes += frame({{"type", "clipboard-result"}, {"seq", 1}});
                    connection->write(bytes); connection->flush();
                } else if (type == "clipboard-poll") {
                    requests.append(message);
                    if (answer) answer(message);
                    else if (!coalescedResult) send({{"type", "clipboard-result"}, {"seq", message["seq"]}});
                } else if (type == "clipboard-v2-ping") send({{"type", "clipboard-v2-pong"}});
                else requests.append(message);
            }
        });
        connection->startServerEncryption();
    }
};

class ClipboardIdleTests : public QObject {
    Q_OBJECT
    std::unique_ptr<ClipboardChannel> channel(SyntheticPeer& peer, bool wrongPin = false) {
        return std::unique_ptr<ClipboardChannel>(new ClipboardChannel("127.0.0.1", peer.serverPort(),
            QSslCertificate(wrongPin ? credential("IDLE_TEST_BAD_CERT") : peer.certificate), peer.certificate, peer.key, peer.native));
    }
    int timers() { return static_cast<TestApplication*>(qApp)->workerTimers.load(); }
private slots:
    void initTestCase() {
        QVERIFY(QSslSocket::supportsSsl());
        QVERIFY(!credential("IDLE_TEST_CERT").isEmpty());
        QCOMPARE(QGuiApplication::platformName(), QString("offscreen"));
    }
    void idleAndImmediateRequests() {
        SyntheticPeer peer;
        QVERIFY(peer.isListening());
        auto transport = channel(peer);
        QTRY_VERIFY_WITH_TIMEOUT(transport->ready(), 5000);
        QVERIFY(peer.admitted);
        QCOMPARE(transport->maxText(), 4096);
        QTest::qWait(100);
        const int before = timers();
        QTest::qWait(400);
        QCOMPARE(timers(), before);
        QVERIFY(peer.requests.isEmpty());
        for (int seq = 1; seq <= 30; ++seq) {
            QVERIFY(transport->submit({{"type", "clipboard-poll"}, {"seq", seq}}));
            QVERIFY(!transport->submit({{"type", "clipboard-poll"}, {"seq", seq + 1}}));
            QJsonObject reply;
            reply = {};
            QTRY_VERIFY_WITH_TIMEOUT(!reply.isEmpty() || transport->take(reply), 1000);
            QCOMPARE(reply["seq"].toInt(), seq);
            QVERIFY(!transport->take(reply));
        }
        QElapsedTimer elapsed; elapsed.start();
        transport.reset();
        QVERIFY2(elapsed.elapsed() < 500, "Idle stop did not wake the worker promptly");
    }
    void fragmentedAndBufferedReplies() {
        SyntheticPeer peer;
        peer.coalescedResult = true;
        auto transport = channel(peer);
        QTRY_VERIFY_WITH_TIMEOUT(transport->ready(), 5000);
        QVERIFY(transport->submit({{"type", "clipboard-poll"}, {"seq", 1}}));
        QJsonObject reply;
        reply = {};
        QTRY_VERIFY_WITH_TIMEOUT(!reply.isEmpty() || transport->take(reply), 1000);
        QCOMPARE(reply["seq"].toInt(), 1);
        // Include two frames in one read: only one request may complete at once.
        peer.coalescedResult = false;
        peer.answer = [&](const QJsonObject& request) {
            if (request["seq"].toInt() != 2) return;
            const auto bytes = frame({{"type", "clipboard-result"}, {"seq", 2}}) +
                               frame({{"type", "clipboard-result"}, {"seq", 3}});
            peer.socket->write(bytes.left(13)); peer.socket->flush();
            QTimer::singleShot(40, &peer, [&, bytes] { if (peer.socket) { peer.socket->write(bytes.mid(13)); peer.socket->flush(); } });
        };
        QVERIFY(transport->submit({{"type", "clipboard-poll"}, {"seq", 2}}));
        reply = {};
        QTRY_VERIFY_WITH_TIMEOUT(!reply.isEmpty() || transport->take(reply), 1000);
        QCOMPARE(reply["seq"].toInt(), 2);
        QVERIFY(!transport->take(reply));
        QVERIFY(transport->submit({{"type", "clipboard-poll"}, {"seq", 3}}));
        reply = {};
        QTRY_VERIFY_WITH_TIMEOUT(!reply.isEmpty() || transport->take(reply), 1000);
        QCOMPARE(reply["seq"].toInt(), 3);
    }
    void invalidReply_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("malformed") << QByteArray("invalid-json\n");
        QTest::newRow("array") << QByteArray("[]\n");
        QTest::newRow("wrong-sequence") << frame({{"type", "clipboard-result"}, {"seq", 99}});
        QTest::newRow("wrong-type") << frame({{"type", "hello"}, {"seq", 1}});
    }
    void invalidReply() {
        QFETCH(QByteArray, bytes);
        SyntheticPeer peer;
        peer.answer = [&](const QJsonObject&) { peer.socket->write(bytes); peer.socket->flush(); };
        auto transport = channel(peer);
        QTRY_VERIFY_WITH_TIMEOUT(transport->ready(), 5000);
        QVERIFY(transport->submit({{"type", "clipboard-poll"}, {"seq", 1}}));
        QTRY_VERIFY_WITH_TIMEOUT(!transport->error().isEmpty(), 2000);
        QVERIFY(!transport->ready());
        QJsonObject reply;
        QVERIFY(!transport->take(reply));
    }
    void disconnect_data() {
        QTest::addColumn<bool>("pending");
        QTest::newRow("idle") << false;
        QTest::newRow("partial-reply") << true;
    }
    void disconnect() {
        QFETCH(bool, pending);
        SyntheticPeer peer;
        peer.answer = [&](const QJsonObject&) { peer.socket->write("{\"type\":"); peer.socket->flush(); };
        auto transport = channel(peer);
        QTRY_VERIFY_WITH_TIMEOUT(transport->ready(), 5000);
        if (pending) {
            QVERIFY(transport->submit({{"type", "clipboard-poll"}, {"seq", 1}}));
            QTRY_COMPARE_WITH_TIMEOUT(peer.requests.size(), 1, 1000);
        }
        peer.socket->abort();
        QTRY_VERIFY_WITH_TIMEOUT(!transport->error().isEmpty(), 1000);
        QVERIFY(!transport->ready());
    }
    void stopDuringStartup_data() {
        QTest::addColumn<int>("hold");
        QTest::newRow("tls") << int(SyntheticPeer::Tls);
        QTest::newRow("hello") << int(SyntheticPeer::Hello);
        QTest::newRow("ready") << int(SyntheticPeer::Ready);
    }
    void stopDuringStartup() {
        QFETCH(int, hold);
        SyntheticPeer peer;
        peer.hold = SyntheticPeer::Hold(hold);
        auto transport = channel(peer);
        QTRY_VERIFY_WITH_TIMEOUT(peer.socket, 2000);
        if (peer.hold != SyntheticPeer::Tls) QTRY_VERIFY_WITH_TIMEOUT(peer.admitted, 3000);
        if (peer.hold == SyntheticPeer::Ready) QTRY_VERIFY_WITH_TIMEOUT(peer.started, 3000);
        QElapsedTimer elapsed; elapsed.start();
        transport.reset();
        QVERIFY2(elapsed.elapsed() < 500, "Stop was lost in a startup wait");
    }
    void immediateShutdown() {
        SyntheticPeer peer;
        for (int i = 0; i < 100; ++i) {
            QElapsedTimer elapsed; elapsed.start();
            auto transport = channel(peer);
            transport.reset();
            QVERIFY2(elapsed.elapsed() < 500, "Stop was lost before worker startup");
        }
    }
    void stopPendingReply() {
        SyntheticPeer peer;
        peer.answer = [&](const QJsonObject&) { peer.socket->write("{\"type\":"); peer.socket->flush(); };
        auto transport = channel(peer);
        QTRY_VERIFY_WITH_TIMEOUT(transport->ready(), 5000);
        QVERIFY(transport->submit({{"type", "clipboard-poll"}, {"seq", 1}}));
        QTRY_COMPARE_WITH_TIMEOUT(peer.requests.size(), 1, 1000);
        QTest::qWait(100);
        const int before = timers();
        QTest::qWait(300);
        QCOMPARE(timers(), before);
        QElapsedTimer elapsed; elapsed.start();
        transport.reset();
        QVERIFY2(elapsed.elapsed() < 500, "Pending reply delayed stop");
    }
    void certificatePin() {
        SyntheticPeer peer;
        auto transport = channel(peer, true);
        QTRY_VERIFY_WITH_TIMEOUT(!transport->error().isEmpty(), 5000);
        QVERIFY(!transport->ready());
        QVERIFY(!peer.started);
    }
    void nativeIdleAndBufferedOffer() {
#ifndef Q_OS_UNIX
        QSKIP("The synthetic pipe helper uses Unix file descriptors");
#else
        SyntheticPeer peer;
        peer.native = true;
        auto transport = channel(peer);
        QTRY_VERIFY_WITH_TIMEOUT(transport->ready() && transport->nativeSharing(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(transport->notice(), QString("coalesced-offer"), 2000);
        QTest::qWait(100);
        const int before = timers();
        QTest::qWait(400);
        QCOMPARE(timers(), before);
        QCOMPARE(peer.requests.size(), 1); // Synthetic helper response, no idle traffic.
        peer.send({{"type", "clipboard-v2-pong"}});
        peer.send({{"type", "clipboard-v2-read"}, {"id", "second-offer"}});
        QTRY_COMPARE_WITH_TIMEOUT(transport->notice(), QString("second-offer"), 2000);
        QElapsedTimer elapsed; elapsed.start();
        transport.reset();
        QVERIFY2(elapsed.elapsed() < 1000, "Native stop or helper EOF cleanup stalled");
#endif
    }
    void nativeFailure_data() {
        QTest::addColumn<int>("failure");
        QTest::newRow("disconnect") << 0;
        QTest::newRow("malformed") << 1;
        QTest::newRow("helper-exit") << 2;
    }
    void nativeFailure() {
#ifndef Q_OS_UNIX
        QSKIP("The synthetic pipe helper uses Unix file descriptors");
#else
        QFETCH(int, failure);
        SyntheticPeer peer;
        peer.native = true;
        auto transport = channel(peer);
        QTRY_VERIFY_WITH_TIMEOUT(transport->ready(), 5000);
        if (failure == 0) peer.socket->abort();
        else if (failure == 1) { peer.socket->write("invalid-json\n"); peer.socket->flush(); }
        else peer.send({{"type", "clipboard-v2-offer"}, {"id", "exit-helper"}});
        QTRY_VERIFY_WITH_TIMEOUT(!transport->error().isEmpty(), 2000);
        QVERIFY(!transport->ready());
#endif
    }
    void backgroundAgentAndActivation() {
        auto raw = new MemoryClipboard;
        raw->copy("pre-connection text");
        QList<QJsonObject> sent;
        ClipboardAgent agent(std::unique_ptr<ClipboardNative>(raw), [&](const QJsonObject& message) { sent.append(message); }, true);
        QTest::qWait(300);
        QVERIFY(sent.isEmpty());
        raw->copy("synthetic background copy");
        QTRY_VERIFY_WITH_TIMEOUT(!sent.isEmpty(), 450);
        QString text;
        QVERIFY(DeskPortClipboard::decode(sent.last()["text"], text));
        QCOMPARE(text, QString("synthetic background copy"));
        sent.clear();
        raw->copy("synthetic focus copy");
        QVERIFY(QMetaObject::invokeMethod(qGuiApp, "applicationStateChanged", Qt::DirectConnection,
            Q_ARG(Qt::ApplicationState, Qt::ApplicationActive)));
        QCoreApplication::processEvents();
        QVERIFY(!sent.isEmpty());
        QVERIFY(DeskPortClipboard::decode(sent.last()["text"], text));
        QCOMPARE(text, QString("synthetic focus copy"));
        QString encoded;
        QVERIFY(DeskPortClipboard::encode("synthetic remote copy", encoded));
        agent.receive({{"type", "clipboard-v2-offer"}, {"id", "remote"}, {"kind", "text"}, {"base", 2}, {"text", encoded}});
        QCOMPARE(raw->read("text/plain;charset=utf-8"), QByteArray("synthetic remote copy"));
        agent.stop();
        const int revisions = raw->revisions;
        QTest::qWait(350);
        QCOMPARE(raw->revisions, revisions);
    }
    void frameLimit() {
        if (!qEnvironmentVariableIsSet("DESKPORT_CLIPBOARD_IDLE_LARGE")) QSKIP("Enable the bounded 171 MiB transport overflow fixture explicitly");
        SyntheticPeer peer;
        peer.answer = [](const QJsonObject&) {};
        auto transport = channel(peer);
        QTRY_VERIFY_WITH_TIMEOUT(transport->ready(), 5000);
        QVERIFY(transport->submit({{"type", "clipboard-poll"}, {"seq", 1}}));
        QTRY_COMPARE_WITH_TIMEOUT(peer.requests.size(), 1, 1000);
        QTimer writer;
        qint64 remaining = qint64(DeskPortClipboard::MaxFrame) + 1;
        const QByteArray chunk(1024 * 1024, 'x');
        connect(&writer, &QTimer::timeout, &peer, [&] {
            if (!remaining || !peer.socket || peer.socket->state() != QAbstractSocket::ConnectedState) { writer.stop(); return; }
            if (peer.socket->bytesToWrite() > 4 * 1024 * 1024) return;
            const auto count = qMin<qint64>(remaining, chunk.size());
            peer.socket->write(chunk.constData(), count); remaining -= count;
        });
        writer.start(1);
        QTRY_VERIFY_WITH_TIMEOUT(!transport->error().isEmpty(), 15000);
        QVERIFY(!transport->ready());
    }
    void absoluteRequestTimeout() {
        if (!qEnvironmentVariableIsSet("DESKPORT_CLIPBOARD_IDLE_LONG_TIMEOUT")) QSKIP("Enable the real 120-second deadline fixture explicitly");
        SyntheticPeer peer;
        peer.answer = [](const QJsonObject&) {};
        auto transport = channel(peer);
        QTRY_VERIFY_WITH_TIMEOUT(transport->ready(), 5000);
        QElapsedTimer elapsed; elapsed.start();
        QVERIFY(transport->submit({{"type", "clipboard-poll"}, {"seq", 1}}));
        QTimer fragments;
        connect(&fragments, &QTimer::timeout, &peer, [&] { if (peer.socket) { peer.socket->write(" "); peer.socket->flush(); } });
        fragments.start(1000);
        QTRY_VERIFY_WITH_TIMEOUT(!transport->error().isEmpty(), 126000);
        QVERIFY(elapsed.elapsed() >= 119000);
        QVERIFY(elapsed.elapsed() < 126000);
        QVERIFY(!transport->ready());
    }
};

// This executable's --clipboard-helper mode exercises QProcess lifetime and
// buffering using only synthetic stdin/stdout frames, never ClipboardNative.
static int syntheticHelper(int argc, char** argv) {
#ifdef Q_OS_UNIX
    QCoreApplication app(argc, argv);
    const int flags = fcntl(STDIN_FILENO, F_GETFL);
    if (flags < 0 || fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) return 2;
    auto send = [](const QJsonObject& object) {
        const auto bytes = frame(object);
        if (fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout) != size_t(bytes.size())) return false;
        return fflush(stdout) == 0;
    };
    QByteArray buffer;
    QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&input, &QSocketNotifier::activated, &app, [&] {
        char bytes[8192];
        ssize_t count;
        while ((count = ::read(STDIN_FILENO, bytes, sizeof(bytes))) > 0) buffer.append(bytes, int(count));
        if (!count) { app.quit(); return; }
        int end;
        while ((end = buffer.indexOf('\n')) >= 0) {
            const auto message = QJsonDocument::fromJson(buffer.left(end)).object();
            buffer.remove(0, end + 1);
            if (message["id"] == "exit-helper") { app.quit(); return; }
            if (!send({{"type", "clipboard-v2-status"}, {"message", message["id"]}}) ||
                !send({{"type", "clipboard-v2-data"}, {"request", "synthetic"}})) { app.exit(3); return; }
        }
    });
    return app.exec();
#else
    Q_UNUSED(argc); Q_UNUSED(argv); return 2;
#endif
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (QByteArray(argv[i]) == "--clipboard-helper") return syntheticHelper(argc, argv);
    TestApplication app(argc, argv);
    ClipboardIdleTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "clipboard-idle.moc"
