// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QFile>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QSize>
#include <QString>
#include <QStringList>

#include <QtWaylandCompositor/QWaylandCompositor>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

class QImage;
class QRegion;
class QTimer;
class QWaylandBufferRef;
class QWaylandOutput;
class QWaylandSurface;
class QWaylandSurfaceGrabber;
class QWaylandView;
class QWaylandXdgShell;
class QWaylandXdgPopup;
class QWaylandXdgSurface;
class QWaylandXdgToplevel;
class QWindow;
class StreamChannel;

struct SidecarOptions {
    QString socketName;
    int width = 1280;
    int height = 720;
    QString captureDir;
    QStringList childArgv;
    bool selfTest = false;
    bool testClient = false;
    // Experimental debug stream on stdin/stdout instead of newline JSON.
    bool stream = false;
    int scale = 1;
    int maxWindows = 1;
    // Stream mode launcher allowlist as "id=program" entries. The client may
    // only start an application by its ID; it never supplies a command.
    QStringList apps;
};

struct CapturedFrame {
    quint64 windowId = 0;
    quint64 sequence = 0;
    QSize size;
    QByteArray sha256;
};

class JsonEmitter final {
public:
    JsonEmitter();

    bool isOpen() const;
    bool writeEvent(QJsonObject event);
    void writeStderr(const QByteArray& bytes);
    // In stream mode lifecycle events become ServerEvent records instead of
    // stdout lines. The JSON schema is unchanged.
    void setStream(StreamChannel* stream) { m_Stream = stream; }

private:
    QFile m_Stdout;
    QFile m_Stderr;
    StreamChannel* m_Stream = nullptr;
};

class SeamlessCompositor final : public QWaylandCompositor {
public:
    SeamlessCompositor(const SidecarOptions& options, JsonEmitter& emitter,
                       QObject* parent = nullptr);
    ~SeamlessCompositor() override;

    bool start(QString* error);
    QString socketNameString() const;
    void requestClientClose();

    // Stream mode only.
    void setStream(StreamChannel* stream) { m_Stream = stream; }
    void handleClientInput(quint32 windowId, const QJsonObject& input);
    void handleFrameAck();

    std::function<void(quint64)> windowCreated;
    std::function<void(const CapturedFrame&)> frameCaptured;
    std::function<void(quint64)> windowDestroyed;

protected:
    void grabSurface(QWaylandSurfaceGrabber* grabber,
                     const QWaylandBufferRef& buffer) override;

private:
    struct ActiveWindow;

    void handleToplevel(QWaylandXdgToplevel* toplevel,
                        QWaylandXdgSurface* xdgSurface);
    void handlePopup(QWaylandXdgPopup* popup, QWaylandXdgSurface* xdgSurface);
    ActiveWindow* registerWindow(QWaylandSurface* surface, QWaylandXdgSurface* xdgSurface);
    ActiveWindow* findWindowForXdgSurface(QWaylandXdgSurface* xdgSurface) const;
    quint64 rootWindowId(const ActiveWindow* window) const;
    bool dismissPopups(quint64 rootId);
    void emitPopupPosition(quint64 windowId);
    ActiveWindow* findWindow(quint64 windowId) const;
    void handleDamage(quint64 windowId, const QRegion& damage);
    void streamDamage(ActiveWindow* window, const QRegion& damage);
    void sendStreamFrame(ActiveWindow* window);
    void completeCapture(quint64 windowId, QWaylandSurface* surface);
    void finishFrame(QWaylandSurface* surface);
    void handleWindowDestroyed(quint64 windowId, const QString& reason);
    void releaseHeldInput();
    void sendKey(quint32 code, int qtKey, int modifiers, const QString& text, bool pressed);
    void configureWindow(ActiveWindow* window);
    void emitTitle(quint64 windowId);
    void emitAppId(quint64 windowId);
    bool saveCapture(const QImage& image, quint64 windowId, quint64 sequence,
                     QString* fileName, QString* error) const;

    SidecarOptions m_Options;
    JsonEmitter& m_Emitter;
    std::unique_ptr<QWindow> m_ParentWindow;
    QWaylandOutput* m_Output = nullptr;
    QWaylandXdgShell* m_XdgShell = nullptr;
    std::map<quint64, std::unique_ptr<ActiveWindow>> m_Windows;
    quint64 m_NextWindowId = 1;
    StreamChannel* m_Stream = nullptr;
    int m_FramesInFlight = 0;
    bool m_FrameCallbacksDeferred = false;
    quint64 m_KeyboardFocusId = 0;
    quint64 m_PointerFocusId = 0;
    std::set<quint32> m_PressedKeys;
    std::set<int> m_PressedButtons;
};

class SidecarHost final : public QObject {
public:
    SidecarHost(const SidecarOptions& options, JsonEmitter& emitter,
                QObject* parent = nullptr);
    ~SidecarHost() override;

    bool start(QString* error);
    bool launchChild(const QStringList& argv, QString* error);
    // Stream mode only: session-scoped requests (windowId 0), such as launch.
    void handleSessionInput(const QJsonObject& input);
    void beginShutdown(const QString& reason, int exitCode);
    SeamlessCompositor* compositor() const { return m_Compositor.get(); }
    void setStream(StreamChannel* stream) { m_Stream = stream; m_Compositor->setStream(stream); }

    std::function<void(quint64)> windowCreated;
    std::function<void(const CapturedFrame&)> frameCaptured;
    std::function<void(quint64)> windowDestroyed;
    std::function<void(int, QProcess::ExitStatus)> childFinished;

private:
    void terminateOwnedChild(int signalNumber);
    bool childGroupAlive() const;
    void settleChildGroup();
    void exitWhenChildStops();
    QProcessEnvironment childEnvironment() const;
    bool configureOwnedProcess(QProcess* process, const QStringList& argv, QString* error);
    bool launchApp(const QString& id, QString* error);
    bool launcherMode() const { return !m_Options.apps.isEmpty(); }

    SidecarOptions m_Options;
    JsonEmitter& m_Emitter;
    std::unique_ptr<SeamlessCompositor> m_Compositor;
    StreamChannel* m_Stream = nullptr;
    QProcess m_Child;
    // Launcher applications, each in its own owned process group.
    std::map<QString, QStringList> m_AppCatalog;
    std::vector<std::unique_ptr<QProcess>> m_Apps;
    std::set<qint64> m_AppGroups;
    std::unique_ptr<QTimer> m_KillTimer;
    qint64 m_ChildGroupId = 0;
    int m_KillChecks = 0;
    bool m_KillEscalated = false;
    bool m_ShuttingDown = false;
    int m_ExitCode = 0;
};

bool validateSidecarOptions(SidecarOptions* options, QString* error);
bool validateLaunchArguments(const QStringList& argv, QString* error);
QByteArray normalizedImageSha256(const QImage& image);
QByteArray knownPatternSha256(const QSize& size);
int runKnownPixelTestClient(const SidecarOptions& options);
