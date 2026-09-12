#pragma once
#include <QString>
#include <QFileInfo>
#include <QStandardPaths>

namespace DeskPortService {
#ifdef Q_OS_LINUX
inline QString autostartPath() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/io.github.keithxc.DeskPort.desktop";
}
inline QString unitPath() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/systemd/user/io.github.keithxc.DeskPort.service";
}
#endif
// 声明式系统配置 (Nix home-manager) 把自启文件链接进 /nix/store。那份配置才是
// 事实源: 我们既不能覆盖也不能删除, 否则下次 switch 会把改动悄悄还原回去。
inline bool storeManaged(const QString& path) {
    const QFileInfo info(path);
    return info.isSymLink() && info.symLinkTarget().startsWith("/nix/store/");
}
inline QString launchAgent() {
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?><plist version=\"1.0\"><dict>"
        "<key>Label</key><string>io.github.keithxc.DeskPort</string>"
        "<key>ProgramArguments</key><array><string>/Applications/DeskPort.app/Contents/MacOS/DeskPort</string>"
        "<string>--background</string></array>"
        "<key>RunAtLoad</key><true/>"
        "<key>KeepAlive</key><dict><key>SuccessfulExit</key><false/></dict>"
        "<key>ThrottleInterval</key><integer>10</integer>"
        "<key>LimitLoadToSessionType</key><string>Aqua</string>"
        "<key>ProcessType</key><string>Interactive</string></dict></plist>");
}
inline QString systemdUnit(QString executable) {
    executable.replace("\\", "\\\\").replace("\"", "\\\"").replace("%", "%%").replace("$", "$$");
    return QStringLiteral("[Unit]\nDescription=DeskPort desktop sharing\nAfter=graphical-session.target\n"
        "PartOf=graphical-session.target\nStartLimitIntervalSec=0\n\n[Service]\nType=simple\n"
        "ExecStart=\"%1\" --background\nRestart=on-failure\nRestartSec=10\n"
        "KillMode=control-group\nTimeoutStopSec=10\n\n[Install]\nWantedBy=graphical-session.target\n").arg(executable);
}
inline QString desktopEntry() {
    return QStringLiteral("[Desktop Entry]\nType=Application\nName=DeskPort\n"
        "Exec=systemctl --user start io.github.keithxc.DeskPort.service\nTerminal=false\n");
}
}
