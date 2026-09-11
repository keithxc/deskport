#pragma once
#include "workspaceresolution.h"
#include <QCryptographicHash>
#include <QRect>
#include <QSettings>

namespace DeskPortDisplay {
struct SessionWindowState {
    QRect geometry;
    QSize streamSize;
    int scale = 1;
    bool maximized = false;
    bool fullscreen = false;
    bool valid() const {
        return geometry.width() >= 100 && geometry.height() >= 100 &&
            geometry.width() <= 16384 && geometry.height() <= 16384 &&
            streamSize.width() >= 640 && streamSize.width() <= MaxWidth &&
            streamSize.height() >= 360 && streamSize.height() <= MaxHeight &&
            streamSize.width() % 4 == 0 && streamSize.height() % 4 == 0 && (scale == 1 || scale == 2);
    }
};
inline QString sessionWindowKey(const QString& hostId) {
    return "sessionWindows/" + QString::fromLatin1(QCryptographicHash::hash(hostId.toUtf8(), QCryptographicHash::Sha256).toHex());
}
inline SessionWindowState readSessionWindow(QSettings& settings, const QString& hostId,
                                           const QByteArray& outputs, int windowMode) {
    if (hostId.isEmpty()) return {};
    settings.beginGroup(sessionWindowKey(hostId));
    SessionWindowState state;
    // Version 1 stored 1x downscaled workspaces; they no longer match the stream.
    if (settings.value("version").toInt() == 2 && settings.value("outputs").toByteArray() == outputs &&
        settings.value("windowMode", -1).toInt() == windowMode) {
        state.geometry = settings.value("geometry").toRect();
        state.streamSize = settings.value("streamSize").toSize();
        state.scale = settings.value("scale", 0).toInt();
        state.maximized = settings.value("maximized").toBool();
        state.fullscreen = settings.value("fullscreen").toBool();
    }
    settings.endGroup();
    return state.valid() ? state : SessionWindowState{};
}
inline void writeSessionWindow(QSettings& settings, const QString& hostId, const QByteArray& outputs,
                              int windowMode, const SessionWindowState& state) {
    if (hostId.isEmpty() || !state.valid()) return;
    settings.beginGroup(sessionWindowKey(hostId));
    settings.setValue("version", 2);
    settings.setValue("outputs", outputs);
    settings.setValue("windowMode", windowMode);
    settings.setValue("geometry", state.geometry);
    settings.setValue("streamSize", state.streamSize);
    settings.setValue("scale", state.scale);
    settings.setValue("maximized", state.maximized);
    settings.setValue("fullscreen", state.fullscreen);
    settings.endGroup();
}
}
