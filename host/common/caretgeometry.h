// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QRectF>
#include <cmath>
// All inputs must use the same coordinate system: screen logical units on
// AT-SPI/Wayland, physical pixels for the DPI-aware Windows probe.
inline QJsonObject caretGeometry(const QRectF& caret, const QRectF& screen) {
    const double x=caret.center().x(), y=caret.bottom();
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(caret.height()) ||
        caret.width()<0 || caret.height()<=0 || !screen.isValid() ||
        !std::isfinite(screen.x()) || !std::isfinite(screen.y()) ||
        !std::isfinite(screen.width()) || !std::isfinite(screen.height()) ||
        x<screen.left() || x>screen.right() || y<screen.top() || y>screen.bottom()) return {{"valid",false}};
    return {{"valid",true},{"x",(x-screen.x())/screen.width()},{"y",(y-screen.y())/screen.height()}};
}
