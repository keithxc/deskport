#pragma once
#include <QSize>
#include "../../shared/deskport-core/include/deskport/workspace.h"
#include <QtGlobal>
#include <cmath>

namespace DeskPortDisplay {
constexpr int MaxWidth = DP_WORKSPACE_MAX_WIDTH;
constexpr int MaxHeight = DP_WORKSPACE_MAX_HEIGHT;
struct Workspace {
    QSize pixels;
    int scale = 1;
    // The host's minimum desktop overrode part of a requested enlargement.
    bool limited = false;
};
// Recover compositor scale when XWayland exposes pixels while Qt exposes the
// logical output rectangle. Reject mismatched outputs/orientations.
inline double scaleForOutput(QSize pixels, QSize logical, double fallback) {
    if (pixels.isEmpty() || logical.isEmpty()) return fallback;
    const double x = double(pixels.width()) / logical.width();
    const double y = double(pixels.height()) / logical.height();
    return x >= 0.5 && x <= 8.0 && std::abs(x - y) < 0.01 ? x : fallback;
}
// Match the client's logical desktop with a native 1x/2x Mac backing store.
// Fractional clients receive supersampled frames, never a smaller desktop raster
// enlarged by the viewer. Above 2x, retain drawable resolution rather than
// undersampling text. The size cap remains the explicit exception.
inline Workspace forClient(QSize drawablePixels, double clientScale) {
    if (drawablePixels.isEmpty() || !std::isfinite(clientScale) || clientScale < 0.5 || clientScale > 8.0) return {};
    const auto size = dp_workspace_from_pixels(drawablePixels.width(), drawablePixels.height(), clientScale);
    return {QSize(size.width, size.height), size.scale};
}
// Data saver streams the same logical desktop at a 1x backing store.
inline Workspace forDataSaver(Workspace workspace) {
    const auto size = dp_workspace_for_saver({workspace.pixels.width(), workspace.pixels.height(), workspace.scale});
    return {QSize(size.width, size.height), size.scale};
}
inline Workspace adjusted(Workspace workspace, double factor) {
    const DPWorkspace base{workspace.pixels.width(), workspace.pixels.height(), workspace.scale};
    const auto tuned = dp_workspace_adjust(base, factor);
    const auto result = dp_workspace_for_host(tuned);
    const bool limited = factor > 1.0 && result.width > tuned.width;
    return {QSize(result.width, result.height), result.scale, limited};
}

}
