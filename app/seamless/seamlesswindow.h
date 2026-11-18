// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QBackingStore>
#include <QImage>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QWindow>

#include <functional>

// One parentless native top-level for one remote xdg_toplevel. It owns only
// presentation and local input translation; lifecycle ordering belongs to the
// session's registry. All methods run on the GUI thread.
class SeamlessWindow final : public QWindow {
public:
    using InputSink = std::function<void(quint32 windowId, const QJsonObject& input)>;

    // `popupParent` makes this an xdg_popup: a frameless, non-focusable local
    // window placed relative to the parent and never resized from here.
    SeamlessWindow(quint32 remoteId, InputSink sink, SeamlessWindow* popupParent = nullptr);
    ~SeamlessWindow() override;

    quint32 remoteId() const { return m_RemoteId; }

    // Applies decoded rectangles to the window image. `bufferSize` is the
    // remote buffer size in pixels; `bufferScale` its Wayland buffer scale.
    // Returns false for an inconsistent frame (the caller drops the session).
    bool beginFrame(const QSize& bufferSize, int bufferScale);
    bool applyRect(const QRect& rect, const QByteArray& pixels);
    void endFrame();

    // The xdg_surface window geometry in surface-local logical coordinates.
    // It excludes client-side shadows and is what XDG configure sizes mean;
    // an invalid rectangle means the whole buffer.
    void setWindowGeometry(const QRect& geometry);
    // Popup offset relative to the parent's window geometry.
    void setPopupPosition(const QPoint& position);

    void unmap();

protected:
    bool event(QEvent* event) override;
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    QRect visibleRect() const;
    void placePopup();
    QSize logicalFrameSize() const;
    void render();
    void send(const QJsonObject& input);
    void sendPointerMove(const QPointF& position);
    void sendKey(QKeyEvent* event, bool pressed);

    quint32 m_RemoteId;
    InputSink m_Sink;
    QBackingStore m_BackingStore;
    QImage m_Image;
    int m_BufferScale = 1;
    QRect m_WindowGeometry;
    QPointer<SeamlessWindow> m_PopupParent;
    QPoint m_PopupPosition;
    bool m_Mapped = false;
    // Size the window was programmatically set to; differing local resizes
    // are user requests and are forwarded as configure requests.
    QSize m_ProgrammaticSize;
    QSize m_LastRequestedSize;
    QTimer m_ConfigureTimer;
};
