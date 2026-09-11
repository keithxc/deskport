#pragma once
#include <QSize>
#include <QtGlobal>
#include <cmath>

namespace DeskPortDisplay {
constexpr int MaxWidth = 7680;
constexpr int MaxHeight = 4320;
struct Workspace {
    QSize pixels;
    int scale = 1;
};
// Recover compositor scale when XWayland exposes pixels while Qt exposes the
// logical output rectangle. Reject mismatched outputs/orientations.
inline double scaleForOutput(QSize pixels, QSize logical, double fallback) {
    if (pixels.isEmpty() || logical.isEmpty()) return fallback;
    const double x = double(pixels.width()) / logical.width();
    const double y = double(pixels.height()) / logical.height();
    return x >= 0.5 && x <= 8.0 && std::abs(x - y) < 0.01 ? x : fallback;
}
// Stream the client's drawable pixels 1:1 so the viewer never upscales text.
// HiDPI clients get a 2x host desktop whose logical size is pixels / 2.
inline Workspace forClient(QSize drawablePixels, double clientScale) {
    if (drawablePixels.isEmpty() || !std::isfinite(clientScale) || clientScale < 0.5 || clientScale > 8.0) return {};
    const int scale = clientScale >= 1.5 ? 2 : 1;
    const double width = drawablePixels.width();
    const double height = drawablePixels.height();
    // WindowServer advertises compact modes which it then rejects. Keep a usable
    // minimum logical desktop, preserving aspect ratio, and bound backing-store memory.
    const double minimum = qMax(1.0, qMax(960.0 * scale / width, 540.0 * scale / height));
    const double maximum = qMin(double(MaxWidth) / width, double(MaxHeight) / height);
    const double factor = qMin(minimum, maximum);
    return {QSize(qMin(MaxWidth, int(std::ceil(width * factor / 4)) * 4),
                  qMin(MaxHeight, int(std::ceil(height * factor / 4)) * 4)), scale};
}
}
