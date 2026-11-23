// HostManager members for login start and macOS unattended access.
// Moved unchanged from hostmanager.cpp.
#include "diagnostics.h"
#include <QSysInfo>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#ifdef Q_OS_LINUX
#include <cerrno>
#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>
#endif
#include "hostmanager.h"
#include "hostcaretmonitor.h"
#include <QScreen>
#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#endif
#include "workspaceresolution.h"
#include "peerstore.h"
#include "serviceconfig.h"
#include <QElapsedTimer>
#include <algorithm>
#include <limits>
#include <QSslError>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QNetworkProxy>
#include <QApplication>
#include <QPalette>
#include <QWindow>
#include <QMenu>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QDesktopServices>
#include <QUuid>
#include <QTimer>
#include <QThread>
#include <QMessageBox>
#include <QPushButton>
#include <QDateTime>
#include "../../host/macos/recovery-policy.h"
#include <QSettings>
#include <QPointer>
#include <QActionGroup>
#include <QRegularExpression>
#include <QHostInfo>
#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>
#include "macpermissions.h"
#include "macdock.h"
#include "macunattended.h"
#endif

bool HostManager::loginStartManaged() const {
#ifdef Q_OS_LINUX
    return DeskPortService::startupManagedElsewhere(DeskPortService::unitPath(), DeskPortService::autostartPath());
#else
    return false;
#endif
}

bool HostManager::loginStart() const {
#ifdef Q_OS_LINUX
    if (loginStartManaged()) return DeskPortService::startupEnabled();
#endif
    return QSettings().value("host/startAtLogin", false).toBool();
}

QString HostManager::unattendedDirectory() const {
    return m_Isolated ? m_Directory + "/unattended" : QDir::homePath() + QString::fromLatin1(DeskPortRecovery::Directory);
}

bool HostManager::unattendedMarker(const QString& name, bool present) {
    const auto directory = unattendedDirectory();
    const auto path = directory + "/" + name;
    bool ok = true;
    if (present) {
        ok = QDir().mkpath(directory);
        QSaveFile file(path);
        ok = ok && file.open(QIODevice::WriteOnly);
        if (ok) {
            file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
            ok = file.write("1\n") == 2 && file.commit();
        }
    } else ok = !QFile::exists(path) || QFile::remove(path);
    if (!ok) m_UnattendedError = tr("Cannot save unattended preferences.");
    emit changed();
    return ok;
}

bool HostManager::unattendedEnabled() const {
#ifdef Q_OS_MACOS
    // Staged builds and App Translocation must not resume the installed app's
    // paused recovery or read its opt-in as their own.
    if (!m_Isolated && QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../..") != "/Applications/DeskPort.app") return false;
    return QFileInfo(unattendedDirectory() + "/enabled").isFile();
#else
    return false;
#endif
}

bool HostManager::unattendedNeedsApproval() const {
    return unattendedEnabled() && m_UnattendedServiceStatus == 2;
}

QString HostManager::unattendedStatus() const {
    if (!m_UnattendedError.isEmpty()) return m_UnattendedError;
    if (!unattendedEnabled()) return tr("Off");
    if (m_UnattendedServiceStatus == 2) return tr("Waiting for approval in System Settings");
    if (m_UnattendedServiceStatus != 1) return tr("Recovery service needs setup. Turn it off and on to try again.");
    const auto path = unattendedDirectory() + "/heartbeat";
    QFile heartbeat(path);
    if (!heartbeat.open(QIODevice::ReadOnly)) {
        if (QFileInfo(unattendedDirectory() + "/enabled").lastModified().secsTo(QDateTime::currentDateTime()) <= 90)
            return tr("Enabled; waiting for the recovery service to check in");
        return tr("Recovery has not checked in. Review background permissions.");
    }
    if (QFileInfo(path).lastModified().secsTo(QDateTime::currentDateTime()) > 90)
        return tr("Recovery has not checked in. Review background permissions.");
    const auto result = heartbeat.read(64).trimmed();
    if (result == "running") return tr("Enabled; recovery service is checking this Mac");
    if (result == "waiting-session") return tr("Waiting for a logged-in desktop session");
    return tr("Recovery needs attention. Check login startup and background permissions.");
}

void HostManager::refreshUnattended() {
#ifdef Q_OS_MACOS
    if (!m_Isolated) m_UnattendedServiceStatus = unattendedEnabled() ? deskPortUnattendedServiceStatus() : 0;
#endif
    updateUnattendedMonitor();
    emit changed();
}

void HostManager::updateUnattendedMonitor() {
#ifdef Q_OS_MACOS
    QStringList directories, files;
    const bool monitor = m_UnattendedMonitoring && !m_ShuttingDown &&
        (m_Isolated || QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../..") == "/Applications/DeskPort.app");
    if (monitor) {
        // Watch the nearest existing ancestor until the recovery directory is
        // created. Re-arm after atomic marker replacement or directory removal.
        auto directory = unattendedDirectory();
        while (!QFileInfo(directory).isDir()) {
            const auto parent = QFileInfo(directory).absolutePath();
            if (parent == directory) break;
            directory = parent;
        }
        if (QFileInfo(directory).isDir()) directories.append(directory);
        for (const auto& name : {QStringLiteral("enabled"), QStringLiteral("heartbeat")}) {
            const auto path = unattendedDirectory() + "/" + name;
            if (QFileInfo(path).isFile()) files.append(path);
        }
    }
    const auto reconcile = [this](const QStringList& current, const QStringList& wanted) {
        for (const auto& path : current) if (!wanted.contains(path)) m_UnattendedWatcher.removePath(path);
        for (const auto& path : wanted) if (!current.contains(path)) m_UnattendedWatcher.addPath(path);
    };
    reconcile(m_UnattendedWatcher.directories(), directories);
    reconcile(m_UnattendedWatcher.files(), files);
    // Service approval and heartbeat ageing can change without a file event.
    // Keep their existing ten-second detection bound while recovery is enabled.
    // A failed watch also needs polling to discover a later external opt-in.
    const bool watching = !m_UnattendedWatcher.directories().isEmpty();
    if (monitor && (unattendedEnabled() || !watching)) {
        if (!m_UnattendedMonitor.isActive()) m_UnattendedMonitor.start();
    } else m_UnattendedMonitor.stop();
#endif
}

void HostManager::openUnattendedSettings() {
#ifdef Q_OS_MACOS
    if (!m_Isolated) deskPortOpenBackgroundItems();
#endif
}

void HostManager::setUnattended(bool enabled) {
#ifdef Q_OS_MACOS
    if (m_Isolated) return;
    m_UnattendedError.clear();
    const auto bundle = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../..");
    if (bundle != "/Applications/DeskPort.app" ||
        (enabled && !QFile::exists(bundle + "/Contents/Helpers/deskport-recovery"))) {
        m_UnattendedError = tr("Install DeskPort in Applications before enabling unattended operation.");
        emit changed(); return;
    }
    if (enabled) {
        setLoginStart(true);
        if (!loginStart() || !QFile::exists(QDir::homePath() + "/Library/LaunchAgents/io.github.keithxc.DeskPort.plist")) {
            m_UnattendedError = tr("Cannot enable login startup."); emit changed(); return;
        }
        // Remove stale health evidence before requesting a fresh registration.
        if (!unattendedMarker("paused", false) || !unattendedMarker("heartbeat", false) ||
            !unattendedMarker("enabled", true)) return;
        if (!deskPortSetUnattendedService(true, m_UnattendedError)) unattendedMarker("enabled", false);
    } else {
        // Stop this user's recovery first, even when unregister needs attention.
        if (!unattendedMarker("enabled", false)) return;
        unattendedMarker("paused", false);
        deskPortSetUnattendedService(false, m_UnattendedError);
    }
    refreshUnattended();
    if (unattendedNeedsApproval()) openUnattendedSettings();
#else
    Q_UNUSED(enabled);
#endif
}

void HostManager::setLoginStart(bool enabled) {
    if (!enabled && unattendedEnabled()) {
        setStatus(tr("Turn off unattended operation before disabling login startup.")); emit changed(); return;
    }
#ifdef Q_OS_MACOS
    const QString bundle = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../..");
    if (bundle != "/Applications/DeskPort.app") {
        setStatus(tr("Install DeskPort in Applications before enabling login startup")); emit changed(); return;
    }
    const QString path = QDir::homePath() + "/Library/LaunchAgents/io.github.keithxc.DeskPort.plist";
    if (enabled) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) { setStatus(tr("Cannot install login startup")); return; }
        file.write(DeskPortService::launchAgent().toUtf8());
        if (!file.commit()) { setStatus(tr("Cannot save login startup")); return; }
    } else if (QFile::exists(path) && !QFile::remove(path)) {
        setStatus(tr("Cannot remove login startup")); return;
    }
    QSettings().setValue("host/startAtLogin", enabled);
    emit changed();
#elif defined(Q_OS_LINUX)
    const QString path = DeskPortService::autostartPath();
    if (loginStartManaged()) {
        setStatus(tr("Login startup is managed by an existing service or system configuration")); emit changed(); return;
    }
    if (enabled) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        const QString executable = DeskPortService::persistentExecutable(
            QCoreApplication::applicationDirPath() + "/deskport", qEnvironmentVariable("APPIMAGE"), qEnvironmentVariable("APPDIR"));
        const QString unitPath = DeskPortService::unitPath();
        QDir().mkpath(QFileInfo(unitPath).absolutePath());
        QSaveFile unit(unitPath);
        if (!unit.open(QIODevice::WriteOnly)) { setStatus(tr("Cannot install login startup")); return; }
        unit.write(DeskPortService::systemdUnit(executable).toUtf8());
        if (!unit.commit()) { setStatus(tr("Cannot save login startup")); return; }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) { setStatus(tr("Cannot install login startup")); return; }
        file.write(DeskPortService::desktopEntry().toUtf8());
        if (!file.commit()) { setStatus(tr("Cannot save login startup")); return; }
    } else if (QFile::exists(path) && !QFile::remove(path)) {
        setStatus(tr("Cannot remove login startup")); return;
    }
    QProcess::startDetached("systemctl", {"--user", "daemon-reload"});
    QSettings().setValue("host/startAtLogin", enabled); emit changed();
#elif defined(Q_OS_WIN)
    QSettings startup("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (enabled) startup.setValue("io.github.keithxc.DeskPort", "\"" + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + "\" --background");
    else startup.remove("io.github.keithxc.DeskPort");
    startup.sync();
    if (startup.status() != QSettings::NoError) { setStatus(tr("Cannot update Windows login startup")); return; }
    QSettings().setValue("host/startAtLogin", enabled); emit changed();
#else
    Q_UNUSED(enabled);
#endif
}
