// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../../../app/seamless/seamlessstream.h"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>

#include <deque>
#include <functional>
#include <memory>

class QSocketNotifier;

// Non-blocking SeamlessStream transport over an inherited descriptor pair.
// The experimental debug route uses stdin/stdout of a process started by an
// authenticated SSH session; this class never opens a listener.
class StreamChannel final : public QObject {
public:
    StreamChannel(int readFd, int writeFd, QObject* parent = nullptr);
    ~StreamChannel() override;

    bool open(QString* error);
    bool isOpen() const { return m_Open; }

    void sendJson(quint16 type, quint32 windowId, const QJsonObject& object);
    void sendRecord(quint16 type, quint32 windowId, const QByteArray& payload);
    qsizetype queuedBytes() const { return m_QueuedBytes; }
    // Best-effort final drain before process exit.
    void drain(int timeoutMs);

    std::function<void(const SeamlessStream::RecordReader::Record&)> recordReceived;
    std::function<void(const QString&)> closed;

private:
    void readAvailable();
    void flush();
    void close(const QString& reason);

    int m_ReadFd;
    int m_WriteFd;
    bool m_Open = false;
    std::unique_ptr<QSocketNotifier> m_ReadNotifier;
    std::unique_ptr<QSocketNotifier> m_WriteNotifier;
    std::deque<QByteArray> m_Queue;
    qsizetype m_QueueOffset = 0;
    qsizetype m_QueuedBytes = 0;
    SeamlessStream::RecordReader m_Reader;
};
