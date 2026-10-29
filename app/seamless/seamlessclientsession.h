// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "seamlessstream.h"

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QPoint>
#include <QRect>
#include <QStringList>

#include <map>
#include <set>
#include <memory>

class SeamlessWindow;

// Maps one session-scoped remote window ID to one local native window and
// owns create/title/unmap/destroy ordering. IDs are never reused in a session.
class SeamlessWindowRegistry final {
public:
    using InputSink = std::function<void(quint32, const QJsonObject&)>;

    explicit SeamlessWindowRegistry(InputSink sink);
    ~SeamlessWindowRegistry();

    bool create(quint32 id, const QString& title);
    // A popup is placed relative to its parent's window geometry and never
    // takes keyboard focus. The parent must already exist.
    bool createPopup(quint32 id, quint32 parentId);
    void setPopupPosition(quint32 id, const QPoint& position);
    SeamlessWindow* find(quint32 id) const;
    void setTitle(quint32 id, const QString& title);
    void setWindowGeometry(quint32 id, const QRect& geometry);
    void unmap(quint32 id);
    void destroy(quint32 id);
    void clear();
    int count() const { return static_cast<int>(m_Windows.size()); }

private:
    InputSink m_Sink;
    std::map<quint32, std::unique_ptr<SeamlessWindow>> m_Windows;
    quint32 m_HighestId = 0;
};

// One client Seamless session. The debug transport is a child process (for
// example `ssh -T host <sidecar> --stream`) whose stdin/stdout carry
// SeamlessStream records. Authentication is the transport's job.
class SeamlessClientSession final : public QObject {
    Q_OBJECT
public:
    explicit SeamlessClientSession(QObject* parent = nullptr);
    ~SeamlessClientSession() override;

    void start(const QString& program, const QStringList& arguments);
    void stop();
    // Asks the server to start an application from its advertised catalog.
    void launch(const QString& appId);

    struct Stats {
        quint64 frames = 0;
        quint64 rects = 0;
        quint64 encodedBytes = 0;
        quint64 decodedBytes = 0;
    };
    const Stats& stats() const { return m_Stats; }

signals:
    void ready(const QJsonObject& hello);
    // app-start, app-exit and launch-rejected events.
    void appEvent(const QJsonObject& event);
    void finished(const QString& reason);

private:
    void readTransport();
    void handleRecord(const SeamlessStream::RecordReader::Record& record);
    void handleEvent(const QJsonObject& event);
    bool handleFrame(quint32 windowId, const QByteArray& payload);
    void sendInput(quint32 windowId, const QJsonObject& input);
    void sendRecord(quint16 type, quint32 windowId, const QByteArray& payload);
    void fail(const QString& reason);

    QProcess m_Transport;
    SeamlessStream::RecordReader m_Reader;
    SeamlessWindowRegistry m_Registry;
    std::set<quint32> m_FramedWindows;
    bool m_HelloReceived = false;
    bool m_Finished = false;
    Stats m_Stats;
};
