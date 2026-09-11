#pragma once
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDir>
#include <QThread>
#include <QTimer>
#include <functional>
#include <memory>

// One GUI owner per user configuration, including across installed versions.
// The lock owns stale-socket cleanup; a second process never removes a live
// server and never opens another GUI when activation delivery fails.
class SingleInstance {
public:
    std::function<void()> activate;
    bool delivered = false;
    bool start(const QString& directory = QString()) {
        const auto path = directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) : directory;
        if (!QDir().mkpath(path)) return false;
        const auto name = "deskport-" + QString::fromLatin1(QCryptographicHash::hash(QDir(path).absolutePath().toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
        lock = std::make_unique<QLockFile>(path + "/gui.lock");
        lock->setStaleLockTime(0);
        if (!lock->tryLock()) {
            for (int attempt = 0; attempt < 30; ++attempt) {
                QLocalSocket socket;
                socket.connectToServer(name);
                if (socket.waitForConnected(50)) {
                    socket.write("activate\n");
                    delivered = socket.waitForBytesWritten(500);
                    return false;
                }
                QThread::msleep(50);
            }
            return false;
        }
        QLocalServer::removeServer(name);
        server.setSocketOptions(QLocalServer::UserAccessOption);
        QObject::connect(&server, &QLocalServer::newConnection, &server, [this] {
            while (auto socket = server.nextPendingConnection()) {
                socket->setReadBufferSize(64);
                const auto receive = [this, socket] {
                    if (!socket->canReadLine()) return;
                    if (socket->readLine(64) == "activate\n" && activate) activate();
                    socket->disconnectFromServer();
                };
                QObject::connect(socket, &QLocalSocket::readyRead, socket, receive);
                QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                QTimer::singleShot(2000, socket, &QObject::deleteLater);
                receive();
            }
        });
        return server.listen(name);
    }
private:
    std::unique_ptr<QLockFile> lock;
    QLocalServer server;
};
