#pragma once
#include <QString>

namespace DeskPortService {
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
