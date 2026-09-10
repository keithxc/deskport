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
// Preserve the client's logical workspace. macOS has integral 1x/2x display
// modes: fractional client scaling uses a 2x backing surface and client downscale.
inline Workspace forClient(QSize drawablePixels, double clientScale) {
    if (drawablePixels.isEmpty() || !std::isfinite(clientScale) || clientScale < 0.5 || clientScale > 8.0) return {};
    const int scale = clientScale > 1.05 ? 2 : 1;
    const double width = drawablePixels.width() / clientScale;
    const double height = drawablePixels.height() / clientScale;
    // WindowServer advertises compact modes which it then rejects. Keep a usable
    // minimum desktop, preserving aspect ratio, and bound backing-store memory.
    const double minimum = qMax(1.0, qMax(960.0 / width, 540.0 / height));
    const double maximum = qMin(double(MaxWidth) / (width * scale), double(MaxHeight) / (height * scale));
    const double factor = qMin(minimum, maximum);
    return {QSize(qMin(MaxWidth, int(std::ceil(width * factor * scale / 4)) * 4),
                  qMin(MaxHeight, int(std::ceil(height * factor * scale / 4)) * 4)), scale};
}
}
