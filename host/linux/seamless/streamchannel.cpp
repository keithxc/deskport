// SPDX-License-Identifier: GPL-3.0-or-later
#include "streamchannel.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <QTimer>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace {
// A stalled reader must not make the sidecar buffer without bound. Frame
// credits normally keep the queue far below this.
constexpr qsizetype MaxQueuedBytes = 256LL * 1024 * 1024;

bool setNonBlocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
} // namespace

StreamChannel::StreamChannel(int readFd, int writeFd, QObject* parent)
    : QObject(parent), m_ReadFd(readFd), m_WriteFd(writeFd)
{
}

StreamChannel::~StreamChannel()
{
    m_ReadNotifier.reset();
    m_WriteNotifier.reset();
}

bool StreamChannel::open(QString* error)
{
    if (!setNonBlocking(m_ReadFd) || !setNonBlocking(m_WriteFd)) {
        *error = QStringLiteral("Failed to make the stream descriptors non-blocking: %1")
                     .arg(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    m_ReadNotifier = std::make_unique<QSocketNotifier>(m_ReadFd, QSocketNotifier::Read);
    connect(m_ReadNotifier.get(), &QSocketNotifier::activated, this,
            [this](QSocketDescriptor, QSocketNotifier::Type) { readAvailable(); });
    m_WriteNotifier = std::make_unique<QSocketNotifier>(m_WriteFd, QSocketNotifier::Write);
    m_WriteNotifier->setEnabled(false);
    connect(m_WriteNotifier.get(), &QSocketNotifier::activated, this,
            [this](QSocketDescriptor, QSocketNotifier::Type) { flush(); });
    m_Open = true;
    return true;
}

void StreamChannel::sendJson(quint16 type, quint32 windowId, const QJsonObject& object)
{
    const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (json.size() > static_cast<qsizetype>(SeamlessStream::MaxJsonPayload))
        return;
    sendRecord(type, windowId, json);
}

void StreamChannel::sendRecord(quint16 type, quint32 windowId, const QByteArray& payload)
{
    if (!m_Open)
        return;
    if (payload.size() > static_cast<qsizetype>(SeamlessStream::MaxPayload)) {
        close(QStringLiteral("outgoing-record-too-large"));
        return;
    }
    QByteArray record = SeamlessStream::encodeRecord(type, windowId, payload);
    m_QueuedBytes += record.size();
    m_Queue.push_back(std::move(record));
    if (m_QueuedBytes > MaxQueuedBytes) {
        close(QStringLiteral("stream-queue-overflow"));
        return;
    }
    flush();
}

void StreamChannel::flush()
{
    while (m_Open && !m_Queue.empty()) {
        const QByteArray& front = m_Queue.front();
        const qsizetype remaining = front.size() - m_QueueOffset;
        const ssize_t written = ::write(m_WriteFd, front.constData() + m_QueueOffset,
                                        static_cast<size_t>(remaining));
        if (written < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            close(QStringLiteral("stream-write-failed"));
            return;
        }
        m_QueueOffset += written;
        m_QueuedBytes -= written;
        if (m_QueueOffset == front.size()) {
            m_Queue.pop_front();
            m_QueueOffset = 0;
        }
    }
    if (m_WriteNotifier)
        m_WriteNotifier->setEnabled(m_Open && !m_Queue.empty());
}

void StreamChannel::drain(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (m_Open && !m_Queue.empty() && timer.elapsed() < timeoutMs) {
        flush();
        if (m_Queue.empty())
            break;
        pollfd descriptor{m_WriteFd, POLLOUT, 0};
        ::poll(&descriptor, 1, 50);
    }
}

void StreamChannel::readAvailable()
{
    char buffer[65536];
    while (m_Open) {
        const ssize_t count = ::read(m_ReadFd, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            close(QStringLiteral("stream-read-failed"));
            return;
        }
        if (count == 0) {
            close(QStringLiteral("stream-closed"));
            return;
        }
        m_Reader.append(QByteArray(buffer, static_cast<qsizetype>(count)));
        SeamlessStream::RecordReader::Record record;
        while (m_Open && m_Reader.next(&record)) {
            if (recordReceived)
                recordReceived(record);
        }
        if (m_Reader.failed()) {
            close(QStringLiteral("stream-protocol-error: %1")
                      .arg(QString::fromLatin1(m_Reader.error())));
            return;
        }
        // Client-to-server records are small input events. A peer that sends
        // an incomplete record larger than this is not a valid debug client.
        if (m_Reader.buffered() > 1024 * 1024) {
            close(QStringLiteral("stream-input-overflow"));
            return;
        }
    }
}

void StreamChannel::close(const QString& reason)
{
    if (!m_Open)
        return;
    m_Open = false;
    if (m_ReadNotifier)
        m_ReadNotifier->setEnabled(false);
    if (m_WriteNotifier)
        m_WriteNotifier->setEnabled(false);
    m_Queue.clear();
    m_QueuedBytes = 0;
    m_QueueOffset = 0;
    // Report asynchronously: callers may be inside sendRecord()/readAvailable().
    QTimer::singleShot(0, this, [this, reason] {
        if (closed)
            closed(reason);
    });
}
