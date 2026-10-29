// SPDX-License-Identifier: GPL-3.0-or-later
#include "seamlesshost.h"
#include "streamchannel.h"

#include <QBackingStore>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QExposeEvent>
#include <QFileDevice>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QPainter>
#include <QPointer>
#include <QProcessEnvironment>
#include <QRegion>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QTemporaryFile>
#include <QTimer>
#include <QWindow>

#include <QtWaylandCompositor/QWaylandBufferRef>
#include <QtWaylandCompositor/QWaylandOutput>
#include <QtWaylandCompositor/QWaylandOutputMode>
#include <QtWaylandCompositor/QWaylandSeat>
#include <QtWaylandCompositor/QWaylandSurface>
#include <QtWaylandCompositor/QWaylandSurfaceGrabber>
#include <QtWaylandCompositor/QWaylandView>
#include <QtWaylandCompositor/QWaylandXdgDecorationManagerV1>
#include <QtWaylandCompositor/QWaylandXdgShell>

#include <vector>

#include <cerrno>
#include <cmath>
#include <cstring>
#include <csignal>
#include <cstdio>
#include <limits>
#include <utility>

#include <sys/types.h>
#include <unistd.h>

#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#endif

namespace {
constexpr qsizetype MaxJsonLine = 32768;
constexpr int MaxMetadataText = 1024;
constexpr int MaxAppIdText = 255;
constexpr int MaxDamageRectangles = 128;
constexpr int MinimumDimension = 64;
constexpr int MaximumWidth = 7680;
constexpr int MaximumHeight = 4320;
constexpr qint64 MaximumPixels = 32LL * 1024LL * 1024LL;
constexpr int ChildTerminationTimeoutMs = 3000;
// Stream mode: frames sent but not yet painted by the client.
constexpr int MaxFramesInFlight = 2;
constexpr int MaxStreamRects = 16;
constexpr size_t MaxLauncherApps = 16;
constexpr int MaxLauncherCatalog = 32;

QString boundedText(const QString& value, int maximumBytes = MaxMetadataText)
{
    QString result = value.left(maximumBytes);
    while (result.toUtf8().size() > maximumBytes)
        result.chop(1);
    return result;
}

QJsonArray damageRectangles(const QRegion& damage, bool* truncated)
{
    QJsonArray result;
    int count = 0;
    for (const QRect& rectangle : damage) {
        if (count == MaxDamageRectangles) {
            *truncated = true;
            break;
        }
        result.append(QJsonObject{
            {QStringLiteral("x"), rectangle.x()},
            {QStringLiteral("y"), rectangle.y()},
            {QStringLiteral("width"), rectangle.width()},
            {QStringLiteral("height"), rectangle.height()},
        });
        ++count;
    }
    return result;
}

QString bufferTypeName(const QWaylandBufferRef& buffer)
{
    switch (buffer.bufferType()) {
    case QWaylandBufferRef::BufferType_SharedMemory:
        return QStringLiteral("wl_shm");
    case QWaylandBufferRef::BufferType_Egl:
        return QStringLiteral("egl");
    case QWaylandBufferRef::BufferType_Null:
        return QStringLiteral("none");
    }
    return QStringLiteral("unknown");
}

QString grabErrorName(QWaylandSurfaceGrabber::Error error)
{
    switch (error) {
    case QWaylandSurfaceGrabber::InvalidSurface:
        return QStringLiteral("invalid-surface");
    case QWaylandSurfaceGrabber::NoBufferAttached:
        return QStringLiteral("no-buffer-attached");
    case QWaylandSurfaceGrabber::UnknownBufferType:
        return QStringLiteral("unsupported-buffer-type");
    case QWaylandSurfaceGrabber::RendererNotReady:
        return QStringLiteral("renderer-not-ready");
    }
    return QStringLiteral("unknown-grab-error");
}

bool validateDirectory(const QString& path, bool privateRuntime, QString* error)
{
    const QFileInfo info(path);
    const QString absolute = QDir::cleanPath(info.absoluteFilePath());
    if (!info.isAbsolute() || absolute != path) {
        *error = QStringLiteral("Directory must be an absolute, normalized path: %1").arg(path);
        return false;
    }
    if (!info.exists() || !info.isDir() || info.isSymLink()) {
        *error = QStringLiteral("Directory does not exist or is not a real directory: %1").arg(path);
        return false;
    }
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty() || canonical != absolute) {
        *error = QStringLiteral("Directory or one of its ancestors resolves through a symlink: %1")
                     .arg(path);
        return false;
    }
    if (info.ownerId() != static_cast<uint>(::geteuid())) {
        *error = QStringLiteral("Directory is not owned by the current user: %1").arg(path);
        return false;
    }
    const QFileDevice::Permissions permissions = info.permissions();
    if (!(permissions & QFileDevice::WriteOwner) || !(permissions & QFileDevice::ExeOwner)) {
        *error = QStringLiteral("Directory is not writable and searchable by its owner: %1")
                     .arg(path);
        return false;
    }
    const QFileDevice::Permissions groupOrOtherWrite =
        QFileDevice::WriteGroup | QFileDevice::WriteOther;
    if (permissions & groupOrOtherWrite) {
        *error = QStringLiteral("Directory is writable by another user: %1").arg(path);
        return false;
    }
    if (privateRuntime) {
        const QFileDevice::Permissions anyGroupOrOther =
            QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
            QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
        if (permissions & anyGroupOrOther) {
            *error = QStringLiteral("XDG_RUNTIME_DIR must have mode 0700: %1").arg(path);
            return false;
        }
    }
    return true;
}

void paintKnownPattern(QPainter* painter, const QSize& size)
{
    const int halfWidth = size.width() / 2;
    const int halfHeight = size.height() / 2;
    painter->setCompositionMode(QPainter::CompositionMode_Source);
    painter->fillRect(QRect(QPoint(0, 0), size), QColor(17, 34, 51, 255));
    painter->fillRect(QRect(0, 0, halfWidth, halfHeight), QColor(222, 48, 64, 255));
    painter->fillRect(QRect(halfWidth, 0, size.width() - halfWidth, halfHeight),
                      QColor(30, 190, 105, 255));
    painter->fillRect(QRect(0, halfHeight, halfWidth, size.height() - halfHeight),
                      QColor(42, 91, 220, 255));
    painter->fillRect(QRect(halfWidth, halfHeight, size.width() - halfWidth,
                            size.height() - halfHeight),
                      QColor(245, 201, 48, 255));
    const QRect marker(qMax(0, size.width() / 3), qMax(0, size.height() / 3),
                       qMax(1, size.width() / 7), qMax(1, size.height() / 7));
    painter->fillRect(marker, QColor(151, 71, 255, 255));
}

class KnownPixelWindow final : public QWindow {
public:
    explicit KnownPixelWindow(const QSize& size)
        : m_BackingStore(this)
    {
        setFlags(Qt::FramelessWindowHint);
        setTitle(QStringLiteral("DeskPort Seamless known-pixel test"));
        resize(size);
    }

    void render()
    {
        if (!isExposed() || size().isEmpty())
            return;
        m_BackingStore.resize(size());
        const QRegion region(QRect(QPoint(0, 0), size()));
        m_BackingStore.beginPaint(region);
        QPainter painter(m_BackingStore.paintDevice());
        paintKnownPattern(&painter, size());
        painter.end();
        m_BackingStore.endPaint();
        m_BackingStore.flush(region, this);
        m_Painted = true;
    }

    bool painted() const { return m_Painted; }

protected:
    void exposeEvent(QExposeEvent*) override { render(); }
    void resizeEvent(QResizeEvent*) override { render(); }

private:
    QBackingStore m_BackingStore;
    bool m_Painted = false;
};

QProcessEnvironment sanitizedEnvironment()
{
    const QProcessEnvironment source = QProcessEnvironment::systemEnvironment();
    QProcessEnvironment result;
    static const char* const allowed[] = {
        "DBUS_SESSION_BUS_ADDRESS", "FONTCONFIG_FILE", "FONTCONFIG_PATH", "HOME",
        "LANG", "LC_ALL", "LC_CTYPE", "LD_LIBRARY_PATH", "LOGNAME",
        "NIX_LD_LIBRARY_PATH", "PATH", "QML2_IMPORT_PATH", "QML_IMPORT_PATH",
        "QT_PLUGIN_PATH", "SHELL", "TERM", "USER", "XDG_CACHE_HOME",
        "XDG_CONFIG_DIRS", "XDG_CONFIG_HOME", "XDG_DATA_DIRS", "XDG_DATA_HOME",
        "XDG_RUNTIME_DIR", "XKB_CONFIG_ROOT",
    };
    for (const char* name : allowed) {
        const QString key = QString::fromLatin1(name);
        if (source.contains(key))
            result.insert(key, source.value(key));
    }
    return result;
}
} // namespace

struct SeamlessCompositor::ActiveWindow {
    quint64 id = 0;
    QPointer<QWaylandSurface> surface;
    QPointer<QWaylandXdgSurface> xdgSurface;
    QPointer<QWaylandXdgToplevel> toplevel;
    // Popups: the parent window ID (0 for top-levels).
    QPointer<QWaylandXdgPopup> popup;
    quint64 parentId = 0;
    QWaylandView* view = nullptr;
    quint64 nextFrameSequence = 1;
    bool captureInFlight = false;
    QRegion pendingDamage;
    // Stream mode state.
    QSize lastSentSize;
    QSize requestedSize;
    bool mapped = false;
    bool activated = false;
    bool unsupportedReported = false;
};

JsonEmitter::JsonEmitter()
{
    const bool stdoutOpened = m_Stdout.open(stdout,
        QIODevice::WriteOnly | QIODevice::Unbuffered, QFileDevice::DontCloseHandle);
    const bool stderrOpened = m_Stderr.open(stderr,
        QIODevice::WriteOnly | QIODevice::Unbuffered, QFileDevice::DontCloseHandle);
    Q_UNUSED(stdoutOpened);
    Q_UNUSED(stderrOpened);
}

bool JsonEmitter::isOpen() const
{
    return m_Stdout.isOpen();
}

bool JsonEmitter::writeEvent(QJsonObject event)
{
    event.insert(QStringLiteral("version"), 1);
    if (m_Stream) {
        m_Stream->sendJson(SeamlessStream::ServerEvent, 0, event);
        return m_Stream->isOpen();
    }
    if (!m_Stdout.isOpen())
        return false;
    QByteArray line = QJsonDocument(event).toJson(QJsonDocument::Compact);
    if (line.size() + 1 > MaxJsonLine) {
        line = QJsonDocument(QJsonObject{
            {QStringLiteral("version"), 1},
            {QStringLiteral("type"), QStringLiteral("error")},
            {QStringLiteral("code"), QStringLiteral("event-too-large")},
            {QStringLiteral("message"), QStringLiteral("A sidecar event exceeded the 32 KiB limit")},
        }).toJson(QJsonDocument::Compact);
    }
    line.append('\n');
    return m_Stdout.write(line) == line.size() && m_Stdout.flush();
}

void JsonEmitter::writeStderr(const QByteArray& bytes)
{
    if (m_Stderr.isOpen()) {
        m_Stderr.write(bytes);
        m_Stderr.flush();
    }
}

SeamlessCompositor::SeamlessCompositor(const SidecarOptions& options,
                                       JsonEmitter& emitter, QObject* parent)
    : QWaylandCompositor(parent), m_Options(options), m_Emitter(emitter),
      m_ParentWindow(std::make_unique<QWindow>())
{
    m_ParentWindow->setSurfaceType(QSurface::RasterSurface);
    m_ParentWindow->setFlags(Qt::FramelessWindowHint);
    m_ParentWindow->resize(m_Options.width, m_Options.height);
}

SeamlessCompositor::~SeamlessCompositor()
{
    if (m_XdgShell)
        QObject::disconnect(m_XdgShell, nullptr, this, nullptr);
    for (auto& [id, window] : m_Windows) {
        if (window->surface)
            QObject::disconnect(window->surface, nullptr, this, nullptr);
        if (window->toplevel)
            QObject::disconnect(window->toplevel, nullptr, this, nullptr);
        if (window->popup)
            QObject::disconnect(window->popup, nullptr, this, nullptr);
        if (window->xdgSurface)
            QObject::disconnect(window->xdgSurface, nullptr, this, nullptr);
        if (window->view)
            QObject::disconnect(window->view, nullptr, this, nullptr);
        m_Emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("window-destroy")},
            {QStringLiteral("id"), static_cast<qint64>(id)},
            {QStringLiteral("reason"), QStringLiteral("compositor-shutdown")},
        });
        delete window->view;
    }
    m_Windows.clear();
    delete m_XdgShell;
    m_XdgShell = nullptr;
    delete m_Output;
    m_Output = nullptr;
    m_ParentWindow.reset();
}

bool SeamlessCompositor::start(QString* error)
{
    setSocketName(m_Options.socketName.toLocal8Bit());
    setUseHardwareIntegrationExtension(false);
    m_ParentWindow->create();

    m_Output = new QWaylandOutput(this, m_ParentWindow.get());
    m_Output->setSizeFollowsWindow(false);
    const QWaylandOutputMode outputMode(
        QSize(m_Options.width * m_Options.scale, m_Options.height * m_Options.scale), 60000);
    m_Output->addMode(outputMode, true);
    m_Output->setScaleFactor(m_Options.scale);

    QWaylandCompositor::create();
    if (!isCreated()) {
        *error = QStringLiteral("QtWaylandCompositor did not initialize");
        return false;
    }
    m_Output->setCurrentMode(outputMode);

    m_XdgShell = new QWaylandXdgShell(this);
    connect(m_XdgShell, &QWaylandXdgShell::toplevelCreated, this,
            [this](QWaylandXdgToplevel* toplevel, QWaylandXdgSurface* surface) {
                handleToplevel(toplevel, surface);
            });
    connect(m_XdgShell, &QWaylandXdgShell::popupCreated, this,
            [this](QWaylandXdgPopup* popup, QWaylandXdgSurface* surface) {
                handlePopup(popup, surface);
            });
    if (m_Options.stream) {
        // The local native window supplies the title bar and frame.
        auto* decorations = new QWaylandXdgDecorationManagerV1();
        decorations->setParent(this);
        decorations->setExtensionContainer(this);
        decorations->setPreferredMode(QWaylandXdgToplevel::ServerSideDecoration);
        decorations->initialize();
    }
    return true;
}

QString SeamlessCompositor::socketNameString() const
{
    return QString::fromLocal8Bit(socketName());
}

void SeamlessCompositor::requestClientClose()
{
    for (auto& [id, window] : m_Windows) {
        Q_UNUSED(id);
        if (window->toplevel)
            window->toplevel->sendClose();
    }
}

void SeamlessCompositor::grabSurface(QWaylandSurfaceGrabber* grabber,
                                     const QWaylandBufferRef& buffer)
{
    // M2 is intentionally wl_shm-only. In particular, do not let a current GL
    // context turn an EGL/dmabuf client into an accidental GPU dependency.
    if (!buffer.isSharedMemory()) {
        emit grabber->failed(QWaylandSurfaceGrabber::UnknownBufferType);
        return;
    }
    QWaylandCompositor::grabSurface(grabber, buffer);
}

SeamlessCompositor::ActiveWindow* SeamlessCompositor::findWindow(quint64 windowId) const
{
    const auto found = m_Windows.find(windowId);
    return found == m_Windows.end() ? nullptr : found->second.get();
}

SeamlessCompositor::ActiveWindow* SeamlessCompositor::registerWindow(QWaylandSurface* surface,
                                                                     QWaylandXdgSurface* xdgSurface)
{
    if (m_NextWindowId > static_cast<quint64>(std::numeric_limits<qint32>::max())) {
        m_Emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("error")},
            {QStringLiteral("code"), QStringLiteral("window-id-exhausted")},
            {QStringLiteral("message"), QStringLiteral("The monotonic window ID space is exhausted")},
        });
        destroyClientForSurface(surface);
        return nullptr;
    }

    auto active = std::make_unique<ActiveWindow>();
    active->id = m_NextWindowId++;
    active->surface = surface;
    active->xdgSurface = xdgSurface;
    active->view = new QWaylandView(nullptr, this);
    active->view->setOutput(m_Output);
    active->view->setSurface(surface);
    active->view->setPrimary();
    const quint64 windowId = active->id;
    ActiveWindow* window = active.get();
    m_Windows.emplace(windowId, std::move(active));

    connect(surface, &QWaylandSurface::damaged, this,
            [this, windowId](const QRegion& damage) { handleDamage(windowId, damage); });
    connect(window->view, &QWaylandView::surfaceDestroyed, this,
            [this, windowId] { handleWindowDestroyed(windowId, QStringLiteral("surface-destroyed")); });
    if (m_Options.stream) {
        connect(surface, &QWaylandSurface::hasContentChanged, this, [this, windowId] {
            ActiveWindow* changed = findWindow(windowId);
            if (!changed || !changed->surface || changed->surface->hasContent() || !changed->mapped)
                return;
            changed->mapped = false;
            changed->lastSentSize = QSize();
            m_Emitter.writeEvent(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("window-unmap")},
                {QStringLiteral("id"), static_cast<qint64>(windowId)},
            });
        });
        // XDG configure sizes refer to the window geometry, which excludes
        // client-side shadows. The client sizes and crops its native window
        // to this rectangle; following the buffer size instead makes every
        // configure round trip grow the window by the shadow margins.
        const QPointer<QWaylandXdgSurface> geometrySource(xdgSurface);
        connect(xdgSurface, &QWaylandXdgSurface::windowGeometryChanged, this,
                [this, windowId, geometrySource] {
                    if (!geometrySource || !findWindow(windowId))
                        return;
                    const QRect geometry = geometrySource->windowGeometry();
                    if (geometry.width() <= 0 || geometry.height() <= 0 ||
                        geometry.width() > MaximumWidth || geometry.height() > MaximumHeight ||
                        qAbs(geometry.x()) > MaximumWidth || qAbs(geometry.y()) > MaximumHeight)
                        return;
                    m_Emitter.writeEvent(QJsonObject{
                        {QStringLiteral("type"), QStringLiteral("window-geometry")},
                        {QStringLiteral("id"), static_cast<qint64>(windowId)},
                        {QStringLiteral("x"), geometry.x()},
                        {QStringLiteral("y"), geometry.y()},
                        {QStringLiteral("width"), geometry.width()},
                        {QStringLiteral("height"), geometry.height()},
                    });
                });
    }
    return window;
}

void SeamlessCompositor::handleToplevel(QWaylandXdgToplevel* toplevel,
                                        QWaylandXdgSurface* xdgSurface)
{
    QWaylandSurface* surface = xdgSurface ? xdgSurface->surface() : nullptr;
    if (!surface) {
        m_Emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("error")},
            {QStringLiteral("code"), QStringLiteral("invalid-xdg-surface")},
            {QStringLiteral("message"), QStringLiteral("An XDG top-level had no Wayland surface")},
        });
        return;
    }
    if (static_cast<int>(m_Windows.size()) >= m_Options.maxWindows) {
        m_Emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("window-rejected")},
            {QStringLiteral("reason"), m_Options.maxWindows == 1
                                           ? QStringLiteral("m2-single-toplevel-limit")
                                           : QStringLiteral("window-limit")},
        });
        toplevel->sendClose();
        if (m_Options.maxWindows == 1) {
            const QPointer<QWaylandSurface> rejected(surface);
            QTimer::singleShot(0, this, [this, rejected] {
                if (rejected)
                    destroyClientForSurface(rejected);
            });
        }
        return;
    }

    ActiveWindow* window = registerWindow(surface, xdgSurface);
    if (!window)
        return;
    window->toplevel = toplevel;
    const quint64 windowId = window->id;

    connect(toplevel, &QWaylandXdgToplevel::titleChanged, this,
            [this, windowId] { emitTitle(windowId); });
    connect(toplevel, &QWaylandXdgToplevel::appIdChanged, this,
            [this, windowId] { emitAppId(windowId); });
    connect(toplevel, &QObject::destroyed, this,
            [this, windowId] { handleWindowDestroyed(windowId, QStringLiteral("toplevel-destroyed")); });

    m_Emitter.writeEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("window-create")},
        {QStringLiteral("id"), static_cast<qint64>(windowId)},
        {QStringLiteral("title"), boundedText(toplevel->title())},
        {QStringLiteral("appId"), boundedText(toplevel->appId(), MaxAppIdText)},
        {QStringLiteral("width"), m_Options.width},
        {QStringLiteral("height"), m_Options.height},
    });
    if (windowCreated)
        windowCreated(windowId);

    if (m_Options.stream) {
        // Let the application choose its natural size; the client follows it
        // and later requests local resizes explicitly.
        configureWindow(window);
        return;
    }
    const QList<QWaylandXdgToplevel::State> states{
        QWaylandXdgToplevel::ActivatedState,
    };
    toplevel->sendConfigure(QSize(m_Options.width, m_Options.height), states);
}

void SeamlessCompositor::handlePopup(QWaylandXdgPopup* popup, QWaylandXdgSurface* xdgSurface)
{
    QWaylandSurface* surface = xdgSurface ? xdgSurface->surface() : nullptr;
    ActiveWindow* parent = popup ? findWindowForXdgSurface(popup->parentXdgSurface()) : nullptr;
    // Popups are stream-only: the M2 lifecycle contract has no parent field.
    if (!m_Options.stream || !surface || !parent ||
        static_cast<int>(m_Windows.size()) >= m_Options.maxWindows) {
        m_Emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("popup-rejected")},
            {QStringLiteral("reason"), !m_Options.stream ? QStringLiteral("m2-single-toplevel-only")
                                       : !parent         ? QStringLiteral("unknown-parent")
                                                         : QStringLiteral("window-limit")},
        });
        if (popup)
            popup->sendPopupDone();
        return;
    }
    const quint64 parentId = parent->id;
    ActiveWindow* window = registerWindow(surface, xdgSurface);
    if (!window)
        return;
    window->popup = popup;
    window->parentId = parentId;
    const quint64 windowId = window->id;

    connect(popup, &QObject::destroyed, this,
            [this, windowId] { handleWindowDestroyed(windowId, QStringLiteral("popup-destroyed")); });
    connect(popup, &QWaylandXdgPopup::configuredGeometryChanged, this,
            [this, windowId] { emitPopupPosition(windowId); });

    m_Emitter.writeEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("window-create")},
        {QStringLiteral("id"), static_cast<qint64>(windowId)},
        {QStringLiteral("parent"), static_cast<qint64>(parentId)},
        {QStringLiteral("popup"), true},
        {QStringLiteral("title"), QString()},
        {QStringLiteral("appId"), QString()},
        {QStringLiteral("width"), m_Options.width},
        {QStringLiteral("height"), m_Options.height},
    });
    // There is no output edge to constrain against: every popup is a separate
    // local window, so the unconstrained position is the final one.
    popup->sendConfigure(QRect(popup->unconstrainedPosition(), popup->positionerSize()));
    emitPopupPosition(windowId);
}

void SeamlessCompositor::emitPopupPosition(quint64 windowId)
{
    ActiveWindow* window = findWindow(windowId);
    if (!window || !window->popup)
        return;
    const QRect geometry = window->popup->configuredGeometry();
    if (qAbs(geometry.x()) > MaximumWidth || qAbs(geometry.y()) > MaximumHeight)
        return;
    // Relative to the parent's window geometry, which is the parent's local
    // native window origin on the client.
    m_Emitter.writeEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("popup-position")},
        {QStringLiteral("id"), static_cast<qint64>(windowId)},
        {QStringLiteral("parent"), static_cast<qint64>(window->parentId)},
        {QStringLiteral("x"), geometry.x()},
        {QStringLiteral("y"), geometry.y()},
    });
}

SeamlessCompositor::ActiveWindow* SeamlessCompositor::findWindowForXdgSurface(
    QWaylandXdgSurface* xdgSurface) const
{
    if (!xdgSurface)
        return nullptr;
    for (const auto& [id, window] : m_Windows) {
        Q_UNUSED(id);
        if (window->xdgSurface == xdgSurface)
            return window.get();
    }
    return nullptr;
}

quint64 SeamlessCompositor::rootWindowId(const ActiveWindow* window) const
{
    // Parents always have lower IDs, so this walk terminates.
    while (window && window->parentId != 0)
        window = findWindow(window->parentId);
    return window ? window->id : 0;
}

bool SeamlessCompositor::dismissPopups(quint64 rootId)
{
    // Highest IDs are the innermost popups; xdg-shell dismisses them first.
    std::vector<QPointer<QWaylandXdgPopup>> popups;
    for (auto it = m_Windows.rbegin(); it != m_Windows.rend(); ++it) {
        if (it->second->popup && rootWindowId(it->second.get()) == rootId)
            popups.emplace_back(it->second->popup);
    }
    for (const QPointer<QWaylandXdgPopup>& popup : popups) {
        if (popup)
            popup->sendPopupDone();
    }
    return !popups.empty();
}

void SeamlessCompositor::configureWindow(ActiveWindow* window)
{
    if (!window || !window->toplevel)
        return;
    QList<QWaylandXdgToplevel::State> states;
    if (window->activated)
        states.append(QWaylandXdgToplevel::ActivatedState);
    const QSize size = window->requestedSize.isValid() ? window->requestedSize : QSize(0, 0);
    window->toplevel->sendConfigure(size, states);
}

void SeamlessCompositor::handleDamage(quint64 windowId, const QRegion& damage)
{
    ActiveWindow* window = findWindow(windowId);
    if (!window || !window->surface || !window->view)
        return;
    if (m_Stream) {
        streamDamage(window, damage);
        return;
    }

    // QWaylandSurfaceGrabber is asynchronous. Keep at most one readback alive
    // per surface and collapse a burst of commits to one bounded follow-up
    // capture. This keeps frame sequences ordered and prevents a fast client
    // from allocating an unbounded number of full-size QImages.
    if (window->captureInFlight) {
        if (!damage.isEmpty())
            window->pendingDamage |= damage.boundingRect();
        return;
    }
    window->captureInFlight = true;

    QWaylandSurface* surface = window->surface;
    m_Output->frameStarted();
    const bool advanced = window->view->advance();
    const QWaylandBufferRef buffer = window->view->currentBuffer();

    if (damage.isEmpty()) {
        completeCapture(windowId, surface);
        return;
    }
    const quint64 sequence = window->nextFrameSequence++;
    if (!advanced || !buffer.hasBuffer()) {
        m_Emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("frame-capture-failed")},
            {QStringLiteral("id"), static_cast<qint64>(windowId)},
            {QStringLiteral("sequence"), static_cast<qint64>(sequence)},
            {QStringLiteral("error"), QStringLiteral("view-buffer-not-ready")},
        });
        completeCapture(windowId, surface);
        return;
    }
    const QSize bufferSize = buffer.size();
    if (bufferSize.width() <= 0 || bufferSize.height() <= 0 ||
        bufferSize.width() > m_Options.width || bufferSize.height() > m_Options.height ||
        static_cast<qint64>(bufferSize.width()) * bufferSize.height() > MaximumPixels) {
        m_Emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("frame-capture-failed")},
            {QStringLiteral("id"), static_cast<qint64>(windowId)},
            {QStringLiteral("sequence"), static_cast<qint64>(sequence)},
            {QStringLiteral("error"), QStringLiteral("unsafe-buffer-size")},
            {QStringLiteral("width"), bufferSize.width()},
            {QStringLiteral("height"), bufferSize.height()},
        });
        completeCapture(windowId, surface);
        return;
    }

    const QRegion boundedDamage = damage.intersected(QRect(QPoint(0, 0), bufferSize));
    bool damageTruncated = false;
    const QJsonArray rectangles = damageRectangles(boundedDamage, &damageTruncated);
    m_Emitter.writeEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("window-damage")},
        {QStringLiteral("id"), static_cast<qint64>(windowId)},
        {QStringLiteral("sequence"), static_cast<qint64>(sequence)},
        {QStringLiteral("bufferType"), bufferTypeName(buffer)},
        {QStringLiteral("rectangles"), rectangles},
        {QStringLiteral("rectanglesTruncated"), damageTruncated},
    });

    auto* grabber = new QWaylandSurfaceGrabber(surface, this);
    connect(grabber, &QWaylandSurfaceGrabber::success, this,
            [this, grabber, surface = QPointer<QWaylandSurface>(surface), windowId, sequence,
             rectangles, damageTruncated](const QImage& grabbed) {
                ActiveWindow* current = findWindow(windowId);
                if (!current || current->surface != surface) {
                    grabber->deleteLater();
                    return;
                }
                if (grabbed.width() <= 0 || grabbed.height() <= 0 ||
                    grabbed.width() > m_Options.width || grabbed.height() > m_Options.height ||
                    static_cast<qint64>(grabbed.width()) * grabbed.height() > MaximumPixels) {
                    m_Emitter.writeEvent(QJsonObject{
                        {QStringLiteral("type"), QStringLiteral("frame-capture-failed")},
                        {QStringLiteral("id"), static_cast<qint64>(windowId)},
                        {QStringLiteral("sequence"), static_cast<qint64>(sequence)},
                        {QStringLiteral("error"), QStringLiteral("unsafe-grab-size")},
                    });
                } else {
                    const QImage normalized = grabbed.convertToFormat(
                        QImage::Format_RGBA8888_Premultiplied);
                    const QByteArray digest = normalizedImageSha256(normalized);
                    QString captureFile;
                    QString captureError;
                    if (!m_Options.captureDir.isEmpty() &&
                        !saveCapture(normalized, windowId, sequence, &captureFile, &captureError)) {
                        m_Emitter.writeEvent(QJsonObject{
                            {QStringLiteral("type"), QStringLiteral("capture-file-error")},
                            {QStringLiteral("id"), static_cast<qint64>(windowId)},
                            {QStringLiteral("sequence"), static_cast<qint64>(sequence)},
                            {QStringLiteral("message"), boundedText(captureError)},
                        });
                    }
                    QJsonObject metadata{
                        {QStringLiteral("type"), QStringLiteral("frame-metadata")},
                        {QStringLiteral("id"), static_cast<qint64>(windowId)},
                        {QStringLiteral("sequence"), static_cast<qint64>(sequence)},
                        {QStringLiteral("width"), normalized.width()},
                        {QStringLiteral("height"), normalized.height()},
                        {QStringLiteral("pixelFormat"), QStringLiteral("rgba8888-premultiplied")},
                        {QStringLiteral("sha256"), QString::fromLatin1(digest)},
                        {QStringLiteral("damage"), rectangles},
                        {QStringLiteral("damageTruncated"), damageTruncated},
                    };
                    if (!captureFile.isEmpty())
                        metadata.insert(QStringLiteral("captureFile"), captureFile);
                    m_Emitter.writeEvent(metadata);
                    if (frameCaptured) {
                        frameCaptured(CapturedFrame{
                            windowId,
                            sequence,
                            normalized.size(),
                            digest,
                        });
                    }
                }
                if (surface)
                    completeCapture(windowId, surface);
                grabber->deleteLater();
            });
    connect(grabber, &QWaylandSurfaceGrabber::failed, this,
            [this, grabber, surface = QPointer<QWaylandSurface>(surface), windowId, sequence,
             bufferType = bufferTypeName(buffer)](QWaylandSurfaceGrabber::Error error) {
                ActiveWindow* current = findWindow(windowId);
                if (!current || current->surface != surface) {
                    grabber->deleteLater();
                    return;
                }
                m_Emitter.writeEvent(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("frame-capture-failed")},
                    {QStringLiteral("id"), static_cast<qint64>(windowId)},
                    {QStringLiteral("sequence"), static_cast<qint64>(sequence)},
                    {QStringLiteral("bufferType"), bufferType},
                    {QStringLiteral("error"), grabErrorName(error)},
                });
                if (surface)
                    completeCapture(windowId, surface);
                grabber->deleteLater();
            });
    grabber->grab();
}

void SeamlessCompositor::streamDamage(ActiveWindow* window, const QRegion& damage)
{
    m_Output->frameStarted();
    window->view->advance();
    // Surface damage is in surface-local (logical) coordinates.
    const int scale = qMax(1, window->surface->bufferScale());
    for (const QRect& rectangle : damage) {
        window->pendingDamage |= QRect(rectangle.x() * scale, rectangle.y() * scale,
                                       rectangle.width() * scale, rectangle.height() * scale);
    }
    if (m_FramesInFlight >= MaxFramesInFlight) {
        // The client has not painted earlier frames yet. Keep the union of
        // damage and withhold frame callbacks so the application slows down.
        m_FrameCallbacksDeferred = true;
        return;
    }
    sendStreamFrame(window);
}

void SeamlessCompositor::sendStreamFrame(ActiveWindow* window)
{
    const QWaylandBufferRef buffer = window->view->currentBuffer();
    QRegion damage = std::exchange(window->pendingDamage, QRegion());
    if (!buffer.hasBuffer()) {
        m_Output->sendFrameCallbacks();
        return;
    }
    if (!buffer.isSharedMemory()) {
        if (!window->unsupportedReported) {
            window->unsupportedReported = true;
            m_Emitter.writeEvent(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("frame-capture-failed")},
                {QStringLiteral("id"), static_cast<qint64>(window->id)},
                {QStringLiteral("sequence"), static_cast<qint64>(window->nextFrameSequence)},
                {QStringLiteral("bufferType"), bufferTypeName(buffer)},
                {QStringLiteral("error"), QStringLiteral("unsupported-buffer-type")},
            });
        }
        m_Output->sendFrameCallbacks();
        return;
    }

    QImage image = buffer.image();
    if (image.isNull() || image.width() <= 0 || image.height() <= 0 ||
        image.width() > MaximumWidth || image.height() > MaximumHeight ||
        static_cast<qint64>(image.width()) * image.height() > MaximumPixels) {
        m_Emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("frame-capture-failed")},
            {QStringLiteral("id"), static_cast<qint64>(window->id)},
            {QStringLiteral("sequence"), static_cast<qint64>(window->nextFrameSequence)},
            {QStringLiteral("error"), QStringLiteral("unsafe-buffer-size")},
            {QStringLiteral("imageNull"), image.isNull()},
            {QStringLiteral("width"), image.width()},
            {QStringLiteral("height"), image.height()},
            {QStringLiteral("bufferWidth"), buffer.size().width()},
            {QStringLiteral("bufferHeight"), buffer.size().height()},
        });
        m_Output->sendFrameCallbacks();
        return;
    }
    const bool opaque = image.format() == QImage::Format_RGB32;
    if (!opaque && image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    const QRect full(QPoint(0, 0), image.size());
    if (image.size() != window->lastSentSize)
        damage = full;
    else
        damage = damage.intersected(full);
    if (damage.isEmpty()) {
        m_Output->sendFrameCallbacks();
        return;
    }

    QList<QRect> rectangles;
    if (damage.rectCount() > MaxStreamRects)
        rectangles.append(damage.boundingRect());
    else
        rectangles = QList<QRect>(damage.begin(), damage.end());

    const quint64 sequence = window->nextFrameSequence++;
    SeamlessStream::FrameHeader header {};
    header.sequence = qToLittleEndian(sequence);
    header.width = qToLittleEndian(static_cast<quint32>(image.width()));
    header.height = qToLittleEndian(static_cast<quint32>(image.height()));
    header.bufferScale = qToLittleEndian(static_cast<quint16>(qBound(1, window->surface->bufferScale(), 8)));
    header.rectCount = qToLittleEndian(static_cast<quint16>(rectangles.size()));
    header.encoding = SeamlessStream::EncodingZlib;
    header.pixelFormat = SeamlessStream::PixelArgb32Premultiplied;

    QByteArray payload(reinterpret_cast<const char*>(&header), sizeof(header));
    QByteArray raw;
    for (const QRect& rectangle : std::as_const(rectangles)) {
        const qsizetype rowBytes = static_cast<qsizetype>(rectangle.width()) * 4;
        raw.resize(rowBytes * rectangle.height());
        for (int row = 0; row < rectangle.height(); ++row) {
            const uchar* source = image.constScanLine(rectangle.y() + row) + rectangle.x() * 4;
            char* target = raw.data() + row * rowBytes;
            std::memcpy(target, source, static_cast<size_t>(rowBytes));
            if (opaque) {
                // XRGB padding bytes are undefined; make them opaque alpha.
                auto* pixels = reinterpret_cast<quint32*>(target);
                for (int column = 0; column < rectangle.width(); ++column)
                    pixels[column] |= 0xff000000u;
            }
        }
        const QByteArray encoded = qCompress(raw, 1);
        SeamlessStream::FrameRect rect {};
        rect.x = qToLittleEndian(static_cast<quint32>(rectangle.x()));
        rect.y = qToLittleEndian(static_cast<quint32>(rectangle.y()));
        rect.width = qToLittleEndian(static_cast<quint32>(rectangle.width()));
        rect.height = qToLittleEndian(static_cast<quint32>(rectangle.height()));
        rect.dataLength = qToLittleEndian(static_cast<quint32>(encoded.size()));
        payload.append(reinterpret_cast<const char*>(&rect), sizeof(rect));
        payload.append(encoded);
    }

    m_Stream->sendRecord(SeamlessStream::Frame, static_cast<quint32>(window->id), payload);
    ++m_FramesInFlight;
    window->lastSentSize = image.size();
    window->mapped = true;
    if (m_FramesInFlight < MaxFramesInFlight) {
        m_Output->sendFrameCallbacks();
        m_FrameCallbacksDeferred = false;
    } else {
        m_FrameCallbacksDeferred = true;
    }
}

void SeamlessCompositor::handleFrameAck()
{
    if (m_FramesInFlight > 0)
        --m_FramesInFlight;
    for (auto& [id, window] : m_Windows) {
        Q_UNUSED(id);
        if (m_FramesInFlight >= MaxFramesInFlight)
            break;
        if (!window->pendingDamage.isEmpty() && window->view && window->surface)
            sendStreamFrame(window.get());
    }
    if (m_FrameCallbacksDeferred && m_FramesInFlight < MaxFramesInFlight) {
        m_FrameCallbacksDeferred = false;
        m_Output->sendFrameCallbacks();
    }
}

void SeamlessCompositor::handleClientInput(quint32 windowId, const QJsonObject& input)
{
    const QString type = input.value(QStringLiteral("type")).toString();
    ActiveWindow* window = findWindow(windowId);
    if (!window || !window->surface || !window->view)
        return; // Stale input for a destroyed window is ignored.
    QWaylandSeat* seat = defaultSeat();
    if (!seat)
        return;

    const auto boundedCoordinate = [](const QJsonValue& value) {
        const double number = value.toDouble();
        return std::isfinite(number) ? qBound(-65536.0, number, 65536.0) : 0.0;
    };

    if (type == QStringLiteral("pointer-move")) {
        const QPointF position(boundedCoordinate(input.value(QStringLiteral("x"))),
                               boundedCoordinate(input.value(QStringLiteral("y"))));
        seat->sendMouseMoveEvent(window->view, position, position);
        m_PointerFocusId = windowId;
    } else if (type == QStringLiteral("pointer-button")) {
        const int button = input.value(QStringLiteral("button")).toInt();
        static const QList<int> allowed{Qt::LeftButton, Qt::RightButton, Qt::MiddleButton,
                                        Qt::BackButton, Qt::ForwardButton};
        if (!allowed.contains(button) || m_PointerFocusId != windowId)
            return;
        if (input.value(QStringLiteral("pressed")).toBool()) {
            if (!window->popup && dismissPopups(windowId))
                return;
            m_PressedButtons.insert(button);
            seat->sendMousePressEvent(static_cast<Qt::MouseButton>(button));
        } else if (m_PressedButtons.erase(button) > 0) {
            seat->sendMouseReleaseEvent(static_cast<Qt::MouseButton>(button));
        }
    } else if (type == QStringLiteral("pointer-axis")) {
        if (m_PointerFocusId != windowId)
            return;
        const int dx = qBound(-12000, input.value(QStringLiteral("dx")).toInt(), 12000);
        const int dy = qBound(-12000, input.value(QStringLiteral("dy")).toInt(), 12000);
        if (dy != 0)
            seat->sendMouseWheelEvent(Qt::Vertical, dy);
        if (dx != 0)
            seat->sendMouseWheelEvent(Qt::Horizontal, dx);
    } else if (type == QStringLiteral("pointer-leave")) {
        if (m_PointerFocusId == windowId && m_PressedButtons.empty()) {
            seat->setMouseFocus(nullptr);
            m_PointerFocusId = 0;
        }
    } else if (type == QStringLiteral("key")) {
        if (m_KeyboardFocusId != windowId)
            return;
        const qint64 code = input.value(QStringLiteral("code")).toInteger();
        const int qtKey = input.value(QStringLiteral("qtKey")).toInt();
        if (code < 0 || code > 767)
            return;
        sendKey(static_cast<quint32>(code), qtKey,
                input.value(QStringLiteral("modifiers")).toInt(),
                boundedText(input.value(QStringLiteral("text")).toString(), 64),
                input.value(QStringLiteral("pressed")).toBool());
    } else if (type == QStringLiteral("focus")) {
        if (window->popup)
            return; // Keyboard focus stays with the root top-level.
        const bool focused = input.value(QStringLiteral("focused")).toBool();
        if (!focused)
            dismissPopups(windowId);
        if (focused) {
            if (m_KeyboardFocusId != 0 && m_KeyboardFocusId != windowId)
                releaseHeldInput();
            for (auto& [id, other] : m_Windows) {
                if (id != windowId && other->activated) {
                    other->activated = false;
                    configureWindow(other.get());
                }
            }
            seat->setKeyboardFocus(window->surface);
            m_KeyboardFocusId = windowId;
            if (!window->activated) {
                window->activated = true;
                configureWindow(window);
            }
        } else if (m_KeyboardFocusId == windowId) {
            releaseHeldInput();
            seat->setKeyboardFocus(nullptr);
            m_KeyboardFocusId = 0;
            window->activated = false;
            configureWindow(window);
        }
    } else if (type == QStringLiteral("configure")) {
        const int width = input.value(QStringLiteral("width")).toInt();
        const int height = input.value(QStringLiteral("height")).toInt();
        if (width < 1 || height < 1 || width > MaximumWidth || height > MaximumHeight)
            return;
        const QSize size(width, height);
        if (window->requestedSize == size)
            return;
        window->requestedSize = size;
        configureWindow(window);
    } else if (type == QStringLiteral("close")) {
        if (window->toplevel)
            window->toplevel->sendClose();
    }
}

void SeamlessCompositor::sendKey(quint32 code, int qtKey, int modifiers, const QString& text,
                                 bool pressed)
{
    QWaylandSeat* seat = defaultSeat();
    if (!seat || (code == 0 && qtKey == 0))
        return;
    if (code != 0) {
        if (pressed) {
            if (!m_PressedKeys.insert(code).second)
                return; // Repeats are generated by the Wayland client itself.
        } else if (m_PressedKeys.erase(code) == 0) {
            return;
        }
    }
    QKeyEvent event(pressed ? QEvent::KeyPress : QEvent::KeyRelease, qtKey,
                    Qt::KeyboardModifiers(modifiers) & Qt::KeyboardModifierMask,
                    code != 0 ? code + 8 : 0, 0, 0, pressed ? text : QString());
    seat->sendFullKeyEvent(&event);
}

void SeamlessCompositor::releaseHeldInput()
{
    QWaylandSeat* seat = defaultSeat();
    if (!seat)
        return;
    const std::set<quint32> keys = std::exchange(m_PressedKeys, {});
    for (const quint32 code : keys) {
        QKeyEvent event(QEvent::KeyRelease, 0, Qt::NoModifier, code + 8, 0, 0);
        seat->sendFullKeyEvent(&event);
    }
    const std::set<int> buttons = std::exchange(m_PressedButtons, {});
    for (const int button : buttons)
        seat->sendMouseReleaseEvent(static_cast<Qt::MouseButton>(button));
}

void SeamlessCompositor::completeCapture(quint64 windowId, QWaylandSurface* surface)
{
    ActiveWindow* window = findWindow(windowId);
    if (!window || window->surface != surface)
        return;
    window->captureInFlight = false;
    finishFrame(surface);
    const QRegion pending = std::exchange(window->pendingDamage, QRegion());
    if (!pending.isEmpty()) {
        const QPointer<QWaylandSurface> expected(surface);
        QTimer::singleShot(0, this, [this, windowId, expected, pending] {
            ActiveWindow* current = findWindow(windowId);
            if (expected && current && current->surface == expected)
                handleDamage(windowId, pending);
        });
    }
}

void SeamlessCompositor::finishFrame(QWaylandSurface*)
{
    if (m_Output)
        m_Output->sendFrameCallbacks();
}

void SeamlessCompositor::handleWindowDestroyed(quint64 windowId, const QString& reason)
{
    const auto found = m_Windows.find(windowId);
    if (found == m_Windows.end())
        return;
    std::unique_ptr<ActiveWindow> window = std::move(found->second);
    m_Windows.erase(found);
    if (window->surface)
        QObject::disconnect(window->surface, nullptr, this, nullptr);
    if (window->toplevel)
        QObject::disconnect(window->toplevel, nullptr, this, nullptr);
    if (window->popup)
        QObject::disconnect(window->popup, nullptr, this, nullptr);
    if (window->xdgSurface)
        QObject::disconnect(window->xdgSurface, nullptr, this, nullptr);
    if (window->view)
        QObject::disconnect(window->view, nullptr, this, nullptr);
    if (m_KeyboardFocusId == windowId) {
        releaseHeldInput();
        m_KeyboardFocusId = 0;
    }
    if (m_PointerFocusId == windowId) {
        m_PressedButtons.clear();
        m_PointerFocusId = 0;
    }
    m_Emitter.writeEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("window-destroy")},
        {QStringLiteral("id"), static_cast<qint64>(windowId)},
        {QStringLiteral("reason"), reason},
    });
    if (window->view)
        window->view->deleteLater();
    if (windowDestroyed)
        windowDestroyed(windowId);
}

void SeamlessCompositor::emitTitle(quint64 windowId)
{
    ActiveWindow* window = findWindow(windowId);
    if (!window || !window->toplevel)
        return;
    m_Emitter.writeEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("window-title")},
        {QStringLiteral("id"), static_cast<qint64>(windowId)},
        {QStringLiteral("title"), boundedText(window->toplevel->title())},
    });
}

void SeamlessCompositor::emitAppId(quint64 windowId)
{
    ActiveWindow* window = findWindow(windowId);
    if (!window || !window->toplevel)
        return;
    m_Emitter.writeEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("window-app-id")},
        {QStringLiteral("id"), static_cast<qint64>(windowId)},
        {QStringLiteral("appId"), boundedText(window->toplevel->appId(), MaxAppIdText)},
    });
}

bool SeamlessCompositor::saveCapture(const QImage& image, quint64 windowId, quint64 sequence,
                                     QString* fileName, QString* error) const
{
    const QString pattern = QDir(m_Options.captureDir).filePath(
        QStringLiteral("deskport-window-%1-frame-%2-XXXXXX.png").arg(windowId).arg(sequence));
    QTemporaryFile output(pattern);
    output.setAutoRemove(true);
    if (!output.open()) {
        *error = output.errorString();
        return false;
    }
    output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!image.save(&output, "PNG") || !output.flush()) {
        *error = output.errorString().isEmpty() ? QStringLiteral("PNG encoding failed")
                                                : output.errorString();
        return false;
    }
    *fileName = QFileInfo(output.fileName()).fileName();
    output.setAutoRemove(false);
    output.close();
    return true;
}

SidecarHost::SidecarHost(const SidecarOptions& options, JsonEmitter& emitter, QObject* parent)
    : QObject(parent), m_Options(options), m_Emitter(emitter),
      m_Compositor(std::make_unique<SeamlessCompositor>(options, emitter)),
      m_KillTimer(std::make_unique<QTimer>())
{
    for (const QString& entry : m_Options.apps) {
        const qsizetype separator = entry.indexOf(QLatin1Char('='));
        m_AppCatalog.emplace(entry.left(separator), QStringList{entry.mid(separator + 1)});
    }
    m_Compositor->windowCreated = [this](quint64 id) {
        if (windowCreated)
            windowCreated(id);
    };
    m_Compositor->frameCaptured = [this](const CapturedFrame& frame) {
        if (frameCaptured)
            frameCaptured(frame);
    };
    m_Compositor->windowDestroyed = [this](quint64 id) {
        if (windowDestroyed)
            windowDestroyed(id);
    };

    m_KillTimer->setSingleShot(true);
    m_KillTimer->setInterval(ChildTerminationTimeoutMs);
    connect(m_KillTimer.get(), &QTimer::timeout, this, [this] {
        if (!childGroupAlive()) {
            m_ChildGroupId = 0;
            exitWhenChildStops();
            return;
        }
        m_KillEscalated = true;
        terminateOwnedChild(SIGKILL);
        if (++m_KillChecks < 20) {
            m_KillTimer->setInterval(50);
            m_KillTimer->start();
        } else {
            m_Emitter.writeEvent(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("error")},
                {QStringLiteral("code"), QStringLiteral("child-group-cleanup-timeout")},
                {QStringLiteral("message"), QStringLiteral("The owned application process group did not disappear after SIGKILL")},
            });
            exitWhenChildStops();
        }
    });
    connect(&m_Child, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                m_Emitter.writeEvent(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("child-exit")},
                    {QStringLiteral("exitCode"), code},
                    {QStringLiteral("exitStatus"), status == QProcess::NormalExit
                                                          ? QStringLiteral("normal")
                                                          : QStringLiteral("crashed")},
                });
                if (childFinished)
                    childFinished(code, status);
                if (!childGroupAlive()) {
                    m_ChildGroupId = 0;
                    m_KillTimer->stop();
                }
                if (m_ShuttingDown)
                    settleChildGroup();
                else if (!m_Options.selfTest && !launcherMode())
                    beginShutdown(QStringLiteral("child-exited"),
                                  status == QProcess::NormalExit ? code : 1);
            });
    connect(&m_Child, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_Emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("error")},
            {QStringLiteral("code"), QStringLiteral("child-start-failed")},
            {QStringLiteral("message"), boundedText(m_Child.errorString())},
        });
        if (childFinished)
            childFinished(-1, QProcess::CrashExit);
        beginShutdown(QStringLiteral("child-start-failed"), 1);
    });
}

SidecarHost::~SidecarHost()
{
    // QProcess destructors may emit finished(); keep that away from members
    // that are already being destroyed.
    for (const std::unique_ptr<QProcess>& app : m_Apps)
        app->disconnect(this);
    if (m_Child.state() != QProcess::NotRunning || childGroupAlive()) {
        terminateOwnedChild(SIGTERM);
        if (m_Child.state() != QProcess::NotRunning &&
            !m_Child.waitForFinished(ChildTerminationTimeoutMs)) {
            terminateOwnedChild(SIGKILL);
            m_Child.waitForFinished(1000);
        }
        if (childGroupAlive())
            terminateOwnedChild(SIGKILL);
    }
}

bool SidecarHost::start(QString* error)
{
    if (!m_Compositor->start(error))
        return false;
    if (!m_Options.childArgv.isEmpty() && !launchChild(m_Options.childArgv, error))
        return false;
    m_Emitter.writeEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("ready")},
        {QStringLiteral("socket"), m_Compositor->socketNameString()},
        {QStringLiteral("width"), m_Options.width},
        {QStringLiteral("height"), m_Options.height},
        {QStringLiteral("capture"), QStringLiteral("wl_shm-only")},
        {QStringLiteral("networkStreaming"), false},
        {QStringLiteral("stream"), m_Options.stream ? QStringLiteral("stdio-debug")
                                                     : QStringLiteral("none")},
    });
    return true;
}

bool SidecarHost::launchChild(const QStringList& argv, QString* error)
{
    if (m_Child.state() != QProcess::NotRunning) {
        *error = QStringLiteral("The sidecar already owns a child process");
        return false;
    }
    if (!configureOwnedProcess(&m_Child, argv, error))
        return false;
    m_Child.start();
    if (!m_Child.waitForStarted(5000)) {
        *error = m_Child.errorString();
        return false;
    }
    m_ChildGroupId = m_Child.processId();
    m_Emitter.writeEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("child-start")},
        {QStringLiteral("pid"), static_cast<qint64>(m_Child.processId())},
        {QStringLiteral("program"), boundedText(argv.first())},
    });
    return true;
}

bool SidecarHost::configureOwnedProcess(QProcess* process, const QStringList& argv,
                                        QString* error)
{
    if (!validateLaunchArguments(argv, error))
        return false;

    process->setProgram(argv.first());
    process->setArguments(argv.mid(1));
    process->setProcessEnvironment(childEnvironment());
    process->setStandardOutputFile(QStringLiteral("/dev/stderr"), QIODevice::Append);
    process->setStandardErrorFile(QStringLiteral("/dev/stderr"), QIODevice::Append);
#ifdef Q_OS_LINUX
    process->setChildProcessModifier([process] {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
        const auto fail = [process](const char* call) { process->failChildProcessModifier(call, errno); };
#else
        // Qt 6.4 (Ubuntu 24.04 packages) has no failure channel; the child exits
        // before exec and QProcess reports that it did not start.
        const auto fail = [](const char*) { ::_exit(127); };
#endif
        if (::setpgid(0, 0) != 0)
            fail("setpgid");
        if (::prctl(PR_SET_PDEATHSIG, SIGTERM) != 0)
            fail("prctl(PR_SET_PDEATHSIG)");
        if (::getppid() == 1)
            ::raise(SIGTERM);
    });
#endif
    return true;
}

bool SidecarHost::launchApp(const QString& id, QString* error)
{
    const auto entry = m_AppCatalog.find(id);
    if (!m_Options.stream || entry == m_AppCatalog.end()) {
        *error = QStringLiteral("unknown-app");
        return false;
    }
    if (m_ShuttingDown) {
        *error = QStringLiteral("shutting-down");
        return false;
    }
    if (m_Apps.size() >= MaxLauncherApps) {
        *error = QStringLiteral("app-limit");
        return false;
    }
    auto process = std::make_unique<QProcess>();
    if (!configureOwnedProcess(process.get(), entry->second, error))
        return false;
    QProcess* started = process.get();
    connect(started, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, started, id](int code, QProcess::ExitStatus status) {
                m_Emitter.writeEvent(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("app-exit")},
                    {QStringLiteral("app"), id},
                    {QStringLiteral("exitCode"), code},
                    {QStringLiteral("exitStatus"), status == QProcess::NormalExit
                                                          ? QStringLiteral("normal")
                                                          : QStringLiteral("crashed")},
                });
                for (auto it = m_AppGroups.begin(); it != m_AppGroups.end();) {
                    errno = 0;
                    const bool alive = ::kill(-static_cast<pid_t>(*it), 0) == 0 || errno == EPERM;
                    it = alive ? std::next(it) : m_AppGroups.erase(it);
                }
                for (auto it = m_Apps.begin(); it != m_Apps.end(); ++it) {
                    if (it->get() == started) {
                        it->release()->deleteLater();
                        m_Apps.erase(it);
                        break;
                    }
                }
                if (m_ShuttingDown)
                    settleChildGroup();
            });
    started->start();
    if (!started->waitForStarted(5000)) {
        *error = QStringLiteral("start-failed");
        return false;
    }
    m_AppGroups.insert(started->processId());
    m_Emitter.writeEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("app-start")},
        {QStringLiteral("app"), id},
        {QStringLiteral("pid"), static_cast<qint64>(started->processId())},
    });
    m_Apps.push_back(std::move(process));
    return true;
}

void SidecarHost::handleSessionInput(const QJsonObject& input)
{
    if (input.value(QStringLiteral("type")).toString() != QStringLiteral("launch"))
        return;
    const QString id = input.value(QStringLiteral("app")).toString().left(64);
    QString error;
    if (!launchApp(id, &error)) {
        m_Emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("launch-rejected")},
            {QStringLiteral("app"), boundedText(id, 64)},
            {QStringLiteral("reason"), boundedText(error, 256)},
        });
    }
}

void SidecarHost::beginShutdown(const QString& reason, int exitCode)
{
    if (m_ShuttingDown)
        return;
    m_ShuttingDown = true;
    m_ExitCode = exitCode;
    m_Emitter.writeEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("stopping")},
        {QStringLiteral("reason"), boundedText(reason)},
    });
    m_Compositor->requestClientClose();
    if (m_Child.state() != QProcess::NotRunning || childGroupAlive()) {
        m_KillChecks = 0;
        m_KillEscalated = false;
        terminateOwnedChild(SIGTERM);
        m_KillTimer->setInterval(ChildTerminationTimeoutMs);
        m_KillTimer->start();
        return;
    }
    exitWhenChildStops();
}

void SidecarHost::terminateOwnedChild(int signalNumber)
{
    for (const qint64 group : m_AppGroups) {
        if (group > 1)
            ::kill(-static_cast<pid_t>(group), signalNumber);
    }
    const qint64 processId = m_ChildGroupId;
    if (processId <= 1)
        return;
    if (::kill(-static_cast<pid_t>(processId), signalNumber) != 0 && errno != ESRCH) {
        if (m_Child.state() != QProcess::NotRunning && signalNumber == SIGKILL)
            m_Child.kill();
        else if (m_Child.state() != QProcess::NotRunning)
            m_Child.terminate();
    }
}

bool SidecarHost::childGroupAlive() const
{
    const auto alive = [](qint64 group) {
        if (group <= 1)
            return false;
        errno = 0;
        return ::kill(-static_cast<pid_t>(group), 0) == 0 || errno == EPERM;
    };
    if (alive(m_ChildGroupId))
        return true;
    for (const qint64 group : m_AppGroups) {
        if (alive(group))
            return true;
    }
    return false;
}

void SidecarHost::settleChildGroup()
{
    if (!childGroupAlive()) {
        m_ChildGroupId = 0;
        m_KillTimer->stop();
        exitWhenChildStops();
        return;
    }
    if (!m_KillTimer->isActive()) {
        if (!m_KillEscalated)
            terminateOwnedChild(SIGTERM);
        m_KillTimer->setInterval(m_KillEscalated ? 50 : ChildTerminationTimeoutMs);
        m_KillTimer->start();
    }
}

void SidecarHost::exitWhenChildStops()
{
    QTimer::singleShot(0, QCoreApplication::instance(), [this] {
        if (m_Stream)
            m_Stream->drain(1000);
        QCoreApplication::exit(m_ExitCode);
    });
}

QProcessEnvironment SidecarHost::childEnvironment() const
{
    QProcessEnvironment environment = sanitizedEnvironment();
    environment.insert(QStringLiteral("WAYLAND_DISPLAY"), m_Compositor->socketNameString());
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("wayland"));
    environment.insert(QStringLiteral("QT_WAYLAND_DISABLE_WINDOWDECORATION"), QStringLiteral("1"));
    environment.insert(QStringLiteral("GDK_BACKEND"), QStringLiteral("wayland"));
    environment.insert(QStringLiteral("SDL_VIDEODRIVER"), QStringLiteral("wayland"));
    environment.insert(QStringLiteral("MOZ_ENABLE_WAYLAND"), QStringLiteral("1"));
    environment.insert(QStringLiteral("XDG_SESSION_TYPE"), QStringLiteral("wayland"));
    return environment;
}

bool validateSidecarOptions(SidecarOptions* options, QString* error)
{
    if (options->width < MinimumDimension || options->height < MinimumDimension ||
        options->width > MaximumWidth || options->height > MaximumHeight ||
        static_cast<qint64>(options->width) * options->height > MaximumPixels) {
        *error = QStringLiteral("Width must be 64..7680 and height 64..4320 with at most 33554432 pixels");
        return false;
    }
    if (options->scale < 1 || options->scale > 4) {
        *error = QStringLiteral("Scale must be 1..4");
        return false;
    }
    if (options->stream && options->selfTest) {
        *error = QStringLiteral("--stream cannot be combined with --self-test");
        return false;
    }
    options->maxWindows = options->stream ? 16 : 1;
    if (options->testClient)
        return options->childArgv.isEmpty();

    const QString runtimeDir = QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR"));
    if (runtimeDir.isEmpty()) {
        *error = QStringLiteral("XDG_RUNTIME_DIR is required");
        return false;
    }
    if (!validateDirectory(runtimeDir, true, error))
        return false;

    if (options->socketName.isEmpty()) {
        options->socketName = QStringLiteral("deskport-seamless-%1")
                                  .arg(QCoreApplication::applicationPid());
    }
    static const QRegularExpression socketSyntax(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$"));
    if (!socketSyntax.match(options->socketName).hasMatch()) {
        *error = QStringLiteral("Socket name must match [A-Za-z0-9][A-Za-z0-9_.-]{0,63}");
        return false;
    }
    const QByteArray socketPath = QDir(runtimeDir).filePath(options->socketName).toLocal8Bit();
    if (socketPath.size() >= 104) {
        *error = QStringLiteral("The Wayland socket path is too long");
        return false;
    }
    if (QFileInfo::exists(QString::fromLocal8Bit(socketPath))) {
        *error = QStringLiteral("The requested Wayland socket already exists");
        return false;
    }

    if (!options->captureDir.isEmpty() &&
        !validateDirectory(options->captureDir, false, error)) {
        return false;
    }
    if (options->selfTest && !options->childArgv.isEmpty()) {
        *error = QStringLiteral("--self-test cannot be combined with an application after --");
        return false;
    }
    if (!options->apps.isEmpty() && !options->stream) {
        *error = QStringLiteral("--app requires --stream");
        return false;
    }
    if (options->apps.size() > MaxLauncherCatalog) {
        *error = QStringLiteral("At most 32 --app entries are allowed");
        return false;
    }
    static const QRegularExpression appId(QStringLiteral("^[a-z0-9][a-z0-9._-]{0,31}$"));
    QStringList seenIds;
    for (const QString& entry : options->apps) {
        const qsizetype separator = entry.indexOf(QLatin1Char('='));
        const QString id = entry.left(separator);
        if (separator <= 0 || !appId.match(id).hasMatch() || seenIds.contains(id)) {
            *error = QStringLiteral("--app requires a unique ID=PROGRAM with ID matching [a-z0-9][a-z0-9._-]{0,31}");
            return false;
        }
        seenIds.append(id);
        if (!validateLaunchArguments(QStringList{entry.mid(separator + 1)}, error))
            return false;
    }
    return options->childArgv.isEmpty() || validateLaunchArguments(options->childArgv, error);
}

bool validateLaunchArguments(const QStringList& argv, QString* error)
{
    if (argv.isEmpty()) {
        *error = QStringLiteral("An application is required after --");
        return false;
    }
    if (argv.size() > 256) {
        *error = QStringLiteral("At most 256 application arguments are allowed");
        return false;
    }
    qsizetype total = 0;
    for (const QString& argument : argv) {
        if (argument.contains(QChar::Null) || argument.size() > 16384) {
            *error = QStringLiteral("An application argument is invalid or too long");
            return false;
        }
        total += argument.size();
        if (total > 131072) {
            *error = QStringLiteral("Application arguments exceed the 128 KiB limit");
            return false;
        }
    }
    const QString program = argv.first();
    if (program.isEmpty()) {
        *error = QStringLiteral("The application program is empty");
        return false;
    }
    if (program.contains(QLatin1Char('/'))) {
        const QFileInfo executable(program);
        if (!executable.isAbsolute() || !executable.isFile() || !executable.isExecutable()) {
            *error = QStringLiteral("An application path containing / must name an absolute executable");
            return false;
        }
    } else {
        static const QRegularExpression bareProgram(
            QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.+:-]{0,255}$"));
        if (!bareProgram.match(program).hasMatch()) {
            *error = QStringLiteral("The application program name contains unsafe characters");
            return false;
        }
    }
    return true;
}

QByteArray normalizedImageSha256(const QImage& image)
{
    const QImage normalized = image.format() == QImage::Format_RGBA8888_Premultiplied
        ? image
        : image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    QCryptographicHash digest(QCryptographicHash::Sha256);
    const qsizetype rowBytes = static_cast<qsizetype>(normalized.width()) * 4;
    for (int row = 0; row < normalized.height(); ++row) {
        digest.addData(QByteArrayView(
            reinterpret_cast<const char*>(normalized.constScanLine(row)), rowBytes));
    }
    return digest.result().toHex();
}

QByteArray knownPatternSha256(const QSize& size)
{
    QImage image(size, QImage::Format_RGBA8888_Premultiplied);
    QPainter painter(&image);
    paintKnownPattern(&painter, size);
    painter.end();
    return normalizedImageSha256(image);
}

int runKnownPixelTestClient(const SidecarOptions& options)
{
    QGuiApplication::setApplicationName(QStringLiteral("deskport-seamless-test-client"));
    QGuiApplication::setDesktopFileName(QStringLiteral("org.deskport.SeamlessSelfTest"));
    auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (!app)
        return 2;
    app->setQuitOnLastWindowClosed(false);

    KnownPixelWindow window(QSize(options.width, options.height));
    window.show();
    QTimer::singleShot(100, &window, [&window] { window.render(); });
    QTimer::singleShot(1500, app, [&window, app] {
        const bool painted = window.painted();
        window.close();
        app->exit(painted ? 0 : 1);
    });
    return app->exec();
}
