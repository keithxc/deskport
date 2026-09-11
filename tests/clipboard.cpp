#include <QtTest>
#include <functional>
#include <QTemporaryDir>
#include <QClipboard>
#include <QMimeData>
#include <QSettings>
#include "peermanager.h"
#include "peerstore.h"
#include "clipboardchannel.h"
#include "clipboardprotocol.h"
#include "streaming/clipboardsync.h"
#include <SDL.h>

static QByteArray credential(const char* name) {
    QFile f(qEnvironmentVariable(name)); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}
class ClipboardTests : public QObject {
    Q_OBJECT
private slots:
    void codecLimitsAndUnicode() {
        QString encoded, text;
        const QString sample = QString::fromUtf8("你好 👩🏽‍💻\r\nsecond line\t日本語");
        QVERIFY(DeskPortClipboard::encode(sample, encoded));
        QVERIFY(DeskPortClipboard::decode(encoded, text)); QCOMPARE(text, sample);
        QVERIFY(DeskPortClipboard::encode(QString(DeskPortClipboard::MaxText, 'a'), encoded));
        QVERIFY(DeskPortClipboard::decode(encoded, text)); QCOMPARE(text.size(), DeskPortClipboard::MaxText);
        QVERIFY(!DeskPortClipboard::encode(QString(DeskPortClipboard::MaxText + 1, 'a'), encoded));
        QVERIFY(!DeskPortClipboard::decode(QStringLiteral("/w=="), text));
        QVERIFY(!DeskPortClipboard::decode(QStringLiteral("YQ==!"), text));
        QVERIFY(!DeskPortClipboard::decode(QStringLiteral("AA=="), text));
        QVERIFY(!DeskPortClipboard::decode(QJsonValue(42), text));
        QVERIFY(DeskPortClipboard::decode(QStringLiteral(""), text)); QVERIFY(text.isEmpty());
    }
    void authenticatedSessionAndOrdering() {
        // Offscreen Qt owns an in-memory clipboard, never the user's clipboard.
        QCOMPARE(QGuiApplication::platformName(), QString("offscreen"));
        QTemporaryDir dir;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QCoreApplication::setOrganizationName("DeskPortTest");
        QCoreApplication::setApplicationName("Clipboard");
        QSettings().setValue("sharedClipboard", true);
        const auto a = credential("TEST_CERT_A"), b = credential("TEST_CERT_B"), c = credential("TEST_CERT_C");
        const auto fp = QString::fromLatin1(QSslCertificate(a).digest(QCryptographicHash::Sha256).toHex());
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json", {{"version", 1}, {"peers", QJsonObject{
            {fp, QJsonObject{{"ready", true}, {"granted", true}}}}}}));
        HostManager host(nullptr, dir.path()+"/host");
        PeerManager server(&host, b, credential("TEST_KEY_B"), dir.path()+"/binding", 0, QHostAddress::LocalHost);
        host.start(1280, 720); QTRY_VERIFY_WITH_TIMEOUT(host.running(), 5000);
        auto create = [&](const QByteArray& cert, const QByteArray& key, const QByteArray& pin) {
            return std::unique_ptr<ClipboardChannel>(new ClipboardChannel("127.0.0.1", server.port(), QSslCertificate(pin), cert, key));
        };
        auto clipboard = QGuiApplication::clipboard(); clipboard->setText("initial host");
        {
            auto unknown = create(c, credential("TEST_KEY_C"), b);
            QTRY_VERIFY_WITH_TIMEOUT(!unknown->error().isEmpty(), 6000); QVERIFY(!unknown->ready());
        }
        QTRY_VERIFY(!server.busy());
        {
            auto wrongPin = create(a, credential("TEST_KEY_A"), c);
            QTRY_VERIFY_WITH_TIMEOUT(!wrongPin->error().isEmpty(), 6000);
        }
        QTRY_VERIFY(!server.busy());
        auto channel = create(a, credential("TEST_KEY_A"), b);
        QTRY_VERIFY_WITH_TIMEOUT(channel->ready(), 6000);
        QCOMPARE(clipboard->text(), QString("initial host"));
        int seq = 0, rev = 0;
        auto exchange = [&](QJsonObject request) {
            request["type"] = "clipboard-poll"; request["seq"] = ++seq; request["rev"] = rev;
            if (!channel->submit(request)) return QJsonObject();
            QJsonObject reply; QElapsedTimer wait; wait.start();
            while (!channel->take(reply) && wait.elapsed() < 6000) QTest::qWait(5);
            rev = reply["rev"].toInt(); return reply;
        };
        const auto initial = exchange({}); QVERIFY(!initial.contains("text")); QCOMPARE(rev, 0);
        {
            auto second = create(a, credential("TEST_KEY_A"), b);
            QTRY_VERIFY_WITH_TIMEOUT(!second->error().isEmpty(), 6000);
        }
        for (int i = 0; i < 100; ++i) {
            QString encoded, decoded;
            const QString local = QString::fromUtf8("客户端 你好 🌏\nline\t%1").arg(i);
            QVERIFY(DeskPortClipboard::encode(local, encoded));
            const auto ack = exchange({{"text", encoded}}); QVERIFY(!ack.contains("text"));
            QCOMPARE(clipboard->text(), local);
            const QString remote = QString::fromUtf8("主机 日本語\r\ncopy %1").arg(i);
            clipboard->setText(remote);
            const auto reply = exchange({}); QVERIFY(DeskPortClipboard::decode(reply["text"], decoded));
            QCOMPARE(decoded, remote);
            QVERIFY(!exchange({}).contains("text")); // No echo.
        }
        QString encoded, decoded;
        clipboard->setText("concurrent host");
        QVERIFY(DeskPortClipboard::encode("concurrent client", encoded));
        const auto conflict = exchange({{"text", encoded}});
        QVERIFY(DeskPortClipboard::decode(conflict["text"], decoded));
        QCOMPARE(decoded, QString("concurrent host")); QCOMPARE(clipboard->text(), decoded);
        auto mime = new QMimeData; mime->setData("image/png", "synthetic unsupported image"); clipboard->setMimeData(mime);
        QVERIFY(exchange({}).contains("error"));
        QVERIFY(DeskPortClipboard::encode(QString(DeskPortClipboard::MaxText, 'x'), encoded));
        QVERIFY(!exchange({{"text", encoded}}).contains("error"));
        QCOMPARE(clipboard->text().size(), DeskPortClipboard::MaxText);
        QVERIFY(channel->submit({{"type", "clipboard-poll"}, {"seq", seq + 1}, {"rev", rev}, {"text", "invalid!"}}));
        QTRY_VERIFY_WITH_TIMEOUT(!channel->error().isEmpty(), 6000);
        QCOMPARE(clipboard->text().size(), DeskPortClipboard::MaxText);
        channel.reset(); QTest::qWait(100);
        clipboard->setText("offline copy");
        channel = create(a, credential("TEST_KEY_A"), b);
        QTRY_VERIFY_WITH_TIMEOUT(channel->ready(), 6000); seq = rev = 0;
        QVERIFY(!exchange({}).contains("text")); QCOMPARE(clipboard->text(), QString("offline copy"));
        QSettings().setValue("sharedClipboard", false);
        QTRY_VERIFY_WITH_TIMEOUT(!channel->error().isEmpty(), 12000);
        channel.reset();
        auto disabled = create(a, credential("TEST_KEY_A"), b);
        QTRY_VERIFY_WITH_TIMEOUT(!disabled->error().isEmpty(), 6000);
        QCOMPARE(clipboard->text(), QString("offline copy"));
        disabled.reset(); QTest::qWait(100);
        QSettings().setValue("sharedClipboard", true);
        qputenv("SDL_VIDEODRIVER", "dummy");
        QVERIFY(SDL_Init(SDL_INIT_VIDEO) == 0);
        auto localText = [] {
            char* raw = SDL_GetClipboardText();
            const QString text = QString::fromUtf8(raw ? raw : ""); SDL_free(raw); return text;
        };
        SDL_SetClipboardText("initial client"); clipboard->setText("initial host");
        {
            ClipboardSync sync(create(a, credential("TEST_KEY_A"), b));
            auto pump = [&](const std::function<bool()>& done) {
                QElapsedTimer wait; wait.start();
                do { sync.tick(); QTest::qWait(10); } while (!done() && wait.elapsed() < 5000);
                return done();
            };
            QElapsedTimer baseline; baseline.start();
            QVERIFY(pump([&] { return baseline.elapsed() > 1000; }));
            QCOMPARE(localText(), QString("initial client")); QCOMPARE(clipboard->text(), QString("initial host"));
            for (int i = 0; i < 5; ++i) {
                const auto fromClient = QString::fromUtf8("SDL 客户端 👋\n%1").arg(i);
                SDL_SetClipboardText(fromClient.toUtf8().constData());
                QVERIFY(pump([&] { return clipboard->text() == fromClient; }));
                const auto fromHost = QString::fromUtf8("Qt 主机 🌏\n%1").arg(i);
                clipboard->setText(fromHost);
                QVERIFY(pump([&] { return localText() == fromHost; }));
            }
            QVERIFY(sync.status().isEmpty());
        }
        SDL_Quit();
    }
};
QTEST_MAIN(ClipboardTests)
#include "clipboard.moc"
