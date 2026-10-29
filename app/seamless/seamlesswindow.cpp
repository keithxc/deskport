// SPDX-License-Identifier: GPL-3.0-or-later
#include "seamlesswindow.h"

#include "seamlesskeymap.h"

#include <QExposeEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>

#include <cstring>

SeamlessWindow::SeamlessWindow(quint32 remoteId, InputSink sink, SeamlessWindow* popupParent)
    : m_RemoteId(remoteId), m_Sink(std::move(sink)), m_BackingStore(this),
      m_PopupParent(popupParent)
{
    setSurfaceType(QSurface::RasterSurface);
    if (popupParent) {
        // A tool window receives pointer input without becoming the key
        // window, so keyboard focus stays with the remote top-level. The
        // server dismisses popups on outside clicks and focus loss.
        setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
        setTransientParent(popupParent);
    } else {
        setFlags(Qt::Window);
    }
    m_ConfigureTimer.setSingleShot(true);
    m_ConfigureTimer.setInterval(40);
    connect(&m_ConfigureTimer, &QTimer::timeout, this, [this] {
        const QSize requested = size();
        if (!m_Mapped || requested == m_LastRequestedSize || requested == logicalFrameSize())
            return;
        m_LastRequestedSize = requested;
        send(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("configure")},
            {QStringLiteral("width"), requested.width()},
            {QStringLiteral("height"), requested.height()},
        });
    });
}

SeamlessWindow::~SeamlessWindow() = default;

QRect SeamlessWindow::visibleRect() const
{
    if (m_Image.isNull())
        return QRect();
    const QRect buffer(0, 0, (m_Image.width() + m_BufferScale - 1) / m_BufferScale,
                       (m_Image.height() + m_BufferScale - 1) / m_BufferScale);
    // Geometry and buffer arrive separately; ignore a stale rectangle that
    // does not fit the current buffer rather than showing undefined pixels.
    if (m_WindowGeometry.isValid() && buffer.contains(m_WindowGeometry))
        return m_WindowGeometry;
    return buffer;
}

QSize SeamlessWindow::logicalFrameSize() const
{
    return visibleRect().size();
}

void SeamlessWindow::setWindowGeometry(const QRect& geometry)
{
    m_WindowGeometry = geometry.isValid() ? geometry : QRect();
}

void SeamlessWindow::setPopupPosition(const QPoint& position)
{
    m_PopupPosition = position;
    if (m_Mapped)
        placePopup();
}

void SeamlessWindow::placePopup()
{
    // The parent shows its window geometry at its local origin, which is
    // exactly the reference point of xdg_popup positions.
    if (m_PopupParent)
        setPosition(m_PopupParent->mapToGlobal(m_PopupPosition));
}

bool SeamlessWindow::beginFrame(const QSize& bufferSize, int bufferScale)
{
    if (bufferSize.isEmpty() || bufferScale < 1 || bufferScale > 8)
        return false;
    m_BufferScale = bufferScale;
    if (m_Image.size() != bufferSize) {
        m_Image = QImage(bufferSize, QImage::Format_ARGB32_Premultiplied);
        if (m_Image.isNull())
            return false;
        m_Image.fill(Qt::transparent);
    }
    m_Image.setDevicePixelRatio(bufferScale);
    return true;
}

bool SeamlessWindow::applyRect(const QRect& rect, const QByteArray& pixels)
{
    if (!QRect(QPoint(0, 0), m_Image.size()).contains(rect))
        return false;
    const qsizetype rowBytes = static_cast<qsizetype>(rect.width()) * 4;
    if (pixels.size() != rowBytes * rect.height())
        return false;
    for (int row = 0; row < rect.height(); ++row) {
        std::memcpy(m_Image.scanLine(rect.y() + row) + rect.x() * 4,
                    pixels.constData() + row * rowBytes, static_cast<size_t>(rowBytes));
    }
    return true;
}

void SeamlessWindow::endFrame()
{
    const QSize logical = logicalFrameSize();
    if (!m_Mapped) {
        m_Mapped = true;
        m_ProgrammaticSize = logical;
        m_LastRequestedSize = logical;
        resize(logical);
        if (m_PopupParent) {
            placePopup();
            show();
        } else {
            show();
            requestActivate();
        }
    } else if (logical != size() && logical != m_LastRequestedSize && !m_ConfigureTimer.isActive()) {
        // The application chose a different size on its own (or constrained
        // our request); follow it rather than stretching pixels.
        m_ProgrammaticSize = logical;
        m_LastRequestedSize = logical;
        resize(logical);
    }
    requestUpdate();
}

void SeamlessWindow::unmap()
{
    m_Mapped = false;
    hide();
}

bool SeamlessWindow::event(QEvent* event)
{
    switch (event->type()) {
    case QEvent::UpdateRequest:
        render();
        return true;
    case QEvent::Close:
        // Ask the remote application; the window disappears only when the
        // server reports that the top-level was destroyed or unmapped.
        event->ignore();
        send(QJsonObject{{QStringLiteral("type"), QStringLiteral("close")}});
        return true;
    case QEvent::Leave:
        send(QJsonObject{{QStringLiteral("type"), QStringLiteral("pointer-leave")}});
        break;
    default:
        break;
    }
    return QWindow::event(event);
}

void SeamlessWindow::exposeEvent(QExposeEvent*)
{
    if (isExposed())
        render();
}

void SeamlessWindow::resizeEvent(QResizeEvent*)
{
    if (!m_Mapped)
        return;
    if (m_PopupParent || size() == m_ProgrammaticSize) {
        render();
        return;
    }
    m_ProgrammaticSize = QSize();
    m_ConfigureTimer.start();
    render();
}

void SeamlessWindow::render()
{
    if (!isExposed() || size().isEmpty())
        return;
    const QRect area(QPoint(0, 0), size());
    m_BackingStore.resize(size());
    m_BackingStore.beginPaint(area);
    {
        QPainter painter(m_BackingStore.paintDevice());
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(area, QColor(24, 24, 24));
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        if (!m_Image.isNull())
            painter.drawImage(-QPointF(visibleRect().topLeft()), m_Image);
    }
    m_BackingStore.endPaint();
    m_BackingStore.flush(area);
}

void SeamlessWindow::send(const QJsonObject& input)
{
    if (m_Sink)
        m_Sink(m_RemoteId, input);
}

void SeamlessWindow::sendPointerMove(const QPointF& position)
{
    send(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("pointer-move")},
        {QStringLiteral("x"), position.x() + visibleRect().x()},
        {QStringLiteral("y"), position.y() + visibleRect().y()},
    });
}

void SeamlessWindow::mouseMoveEvent(QMouseEvent* event)
{
    sendPointerMove(event->position());
}

void SeamlessWindow::mousePressEvent(QMouseEvent* event)
{
    sendPointerMove(event->position());
    send(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("pointer-button")},
        {QStringLiteral("button"), static_cast<int>(event->button())},
        {QStringLiteral("pressed"), true},
    });
}

void SeamlessWindow::mouseReleaseEvent(QMouseEvent* event)
{
    sendPointerMove(event->position());
    send(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("pointer-button")},
        {QStringLiteral("button"), static_cast<int>(event->button())},
        {QStringLiteral("pressed"), false},
    });
}

void SeamlessWindow::wheelEvent(QWheelEvent* event)
{
    const QPoint delta = event->angleDelta();
    if (delta.isNull())
        return;
    sendPointerMove(event->position());
    send(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("pointer-axis")},
        {QStringLiteral("dx"), delta.x()},
        {QStringLiteral("dy"), delta.y()},
    });
}

void SeamlessWindow::sendKey(QKeyEvent* event, bool pressed)
{
    if (event->isAutoRepeat())
        return; // The remote Wayland client implements key repeat.
    send(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("key")},
        {QStringLiteral("code"), static_cast<qint64>(SeamlessKeymap::evdevCodeForEvent(event))},
        {QStringLiteral("qtKey"), event->key()},
        {QStringLiteral("modifiers"), static_cast<int>(event->modifiers())},
        {QStringLiteral("text"), event->text().left(16)},
        {QStringLiteral("pressed"), pressed},
    });
}

void SeamlessWindow::keyPressEvent(QKeyEvent* event)
{
    sendKey(event, true);
}

void SeamlessWindow::keyReleaseEvent(QKeyEvent* event)
{
    sendKey(event, false);
}

void SeamlessWindow::focusInEvent(QFocusEvent*)
{
    send(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("focus")},
        {QStringLiteral("focused"), true},
    });
}

void SeamlessWindow::focusOutEvent(QFocusEvent*)
{
    // The server releases any keys and buttons still held for this window.
    send(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("focus")},
        {QStringLiteral("focused"), false},
    });
}
