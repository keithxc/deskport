#include "hostmanager.h"
#include "peerstore.h"
#include <QElapsedTimer>
#include <algorithm>
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
#include <QSettings>
#include <QHostInfo>
#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#endif

HostManager::HostManager(QObject *parent, const QString &directory) : QObject(parent) {
    m_Isolated = !directory.isEmpty();
    if (!m_Isolated) {
        const int savedPort = QSettings().value("host/port", DeskPortNetwork::DefaultBasePort).toInt();
        if (DeskPortNetwork::isPrivateBase(savedPort)) m_BasePort = savedPort;
    }
    m_Network.setProxy(QNetworkProxy::NoProxy);
    m_Directory = directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/host" : directory;
    m_Status = available() ? tr("Sharing is off") : tr("Hosting is available in the macOS all-in-one package");
    connect(&m_Display, &QProcess::readyReadStandardOutput, this, [this] {
        m_Buffer += m_Display.readAllStandardOutput();
        while (m_Buffer.contains('\n')) {
            auto index = m_Buffer.indexOf('\n');
            auto object = QJsonDocument::fromJson(m_Buffer.left(index)).object();
            m_Buffer.remove(0, index + 1);
            if (m_Stopping) continue;
            if (object.contains("error")) { beginStop(object["error"].toString()); return; }
            if (object["displayId"].toInt() > 0 && m_Starting && !m_ServerRequested) startServer(object["displayId"].toInt());
        }
    });
    connect(&m_Server, &QProcess::started, this, [this] {
        if (m_Stopping) { m_Server.terminate(); return; }
        m_Starting = false;
        if (!m_Isolated) QSettings().setValue("host/port", m_BasePort);
        setStatus(tr("Host process running on port %1. Screen capture and remote input still need verification.").arg(m_BasePort));
        const auto generation = m_Generation;
        QTimer::singleShot(3000, this, [this, generation] {
            if (generation != m_Generation || m_Server.state() != QProcess::Running) return;
            QFile log(m_Directory + "/host.log");
            if (!log.open(QIODevice::ReadOnly)) return;
            log.seek(m_LogOffset);
            const auto output = log.readAll();
            if (output.contains("No screen capture permission"))
                setStatus(tr("Screen recording permission is required. Click Screen recording, authorize DeskPort in macOS, then stop and start sharing."));
            else if (output.contains("Video failed to find working encoder"))
                setStatus(tr("Screen capture could not start. Check host logs, then stop and start sharing."));
        });
    });
    const auto watchProcess = [this](QProcess *process, const QString &label) {
        connect(process, &QProcess::errorOccurred, this, [this, process, label](QProcess::ProcessError) {
            if (m_Stopping) { finishStop(); return; }
            beginStop(tr("%1 failed: %2. You can retry after cleanup.").arg(label, process->errorString()));
        });
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, label](int code, QProcess::ExitStatus) {
            if (m_Stopping) { finishStop(); return; }
            beginStop(tr("%1 stopped (%2). See host logs, then retry.").arg(label).arg(code));
        });
    };
    watchProcess(&m_Server, tr("Host"));
    watchProcess(&m_Display, tr("Virtual display"));
    connect(&m_Credentials, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_Stopping) { finishStop(); return; }
        beginStop(tr("Could not initialize host authentication"));
    });
    connect(&m_Credentials, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus exitStatus) {
        if (m_Stopping) { finishStop(); return; }
        if (!m_Starting) return;
        if (code != 0 || exitStatus != QProcess::NormalExit) {
            beginStop(tr("Could not initialize host authentication")); return;
        }
        m_Server.setWorkingDirectory(m_Directory);
        m_LogOffset = QFileInfo(m_Directory + "/host.log").size();
        m_Server.setStandardOutputFile(m_Directory + "/host.log", QIODevice::Append);
        m_Server.setStandardErrorFile(m_Directory + "/host.log", QIODevice::Append);
        // The child cannot inherit these listeners. Release immediately before
        // starting it; bind failures still clean up only our own processes.
        m_Ports.release();
        m_Server.start(serverPath(), {m_Directory + "/sunshine.conf"});
    });
    auto menu = new QMenu;
    auto show = menu->addAction(tr("Open DeskPort"));
    connect(show, &QAction::triggered, this, [] {
        for (auto window : QGuiApplication::topLevelWindows()) {
            if (window->type() == Qt::Window) { window->show(); window->raise(); window->requestActivate(); break; }
        }
    });
    connect(menu->addAction(tr("Stop sharing")), &QAction::triggered, this, &HostManager::stop);
    connect(menu->addAction(tr("Quit DeskPort")), &QAction::triggered, this, [] { qApp->quit(); });
    m_Tray.setContextMenu(menu);
    const auto updateTrayIcon = [this] {
#ifdef Q_OS_MACOS
        // AppKit renders a template image with the menu bar's current contrast,
        // including wallpaper-dependent appearance and selected menu items.
        QIcon icon(":/res/deskport-tray-black.svg");
        icon.setIsMask(true);
#else
        const bool lightForeground = qApp->palette().color(QPalette::WindowText).lightness() > 127;
        QIcon icon(lightForeground ? ":/res/deskport-tray-white.svg" : ":/res/deskport-tray-black.svg");
#endif
        m_Tray.setIcon(icon);
    };
    updateTrayIcon();
    connect(qApp, &QGuiApplication::paletteChanged, this, updateTrayIcon);
    m_Tray.setToolTip("DeskPort");
    if (available() && !m_Isolated) m_Tray.show();
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { beginStop(tr("Sharing is off")); });
    if (directory.isEmpty() && available() && (loginStart() || QSettings().value("host/shareOnLaunch", false).toBool()) &&
            !QCoreApplication::arguments().contains("--no-host-autostart")) {
        QTimer::singleShot(0, this, [this] {
            QSettings settings;
            start(settings.value("host/width", 2560).toInt(), settings.value("host/height", 1440).toInt());
        });
    }
}
HostManager::~HostManager() {
    // The event loop may already be gone during application shutdown. Normal UI
    // stops are asynchronous; only destruction waits for our own child processes.
    beginStop(tr("Sharing is off"));
    for (auto process : {&m_Credentials, &m_Server, &m_Display}) {
        process->disconnect(this);
        if (process == &m_Display) process->closeWriteChannel();
        if (process->state() != QProcess::NotRunning && !process->waitForFinished(2500)) {
            process->kill(); process->waitForFinished(1000);
        }
    }
    delete m_Tray.contextMenu();
}
QString HostManager::helperPath() const { return QCoreApplication::applicationDirPath() + "/../Helpers/deskport-display"; }
QString HostManager::serverPath() const {
#ifdef Q_OS_LINUX
    return QCoreApplication::applicationDirPath() + "/../libexec/deskport-host";
#else
    return QCoreApplication::applicationDirPath() + "/../Helpers/Sunshine.app/Contents/MacOS/Sunshine";
#endif
}
bool HostManager::available() const {
#ifdef Q_OS_MACOS
    return QFile::exists(helperPath()) && QFile::exists(serverPath());
#elif defined(Q_OS_LINUX)
    return QFile::exists(serverPath());
#else
    return false;
#endif
}
bool HostManager::running() const {
    return m_Starting || m_Stopping || m_Credentials.state() != QProcess::NotRunning ||
        m_Server.state() != QProcess::NotRunning || m_Display.state() != QProcess::NotRunning;
}
bool HostManager::canPair() const { return !m_Starting && !m_Stopping && m_Server.state() == QProcess::Running; }
void HostManager::setStatus(const QString &value) { m_Status = value; emit changed(); }
void HostManager::start(int width, int height) {
    if (!available() || running()) return;
    if (width < 640 || width > 3840 || height < 360 || height > 2160 || width % 2 || height % 2) {
        setStatus(tr("Unsupported display size")); return;
    }
    if (!QDir().mkpath(m_Directory + "/credentials")) {
        setStatus(tr("Cannot create host state directory")); return;
    }
    m_HostLock.reset(new QLockFile(m_Directory + "/instance.lock"));
    // Hosting is long-lived. Age alone must never let another instance take over.
    m_HostLock->setStaleLockTime(0);
    if (!m_HostLock->tryLock(0)) {
        m_HostLock.reset();
        setStatus(tr("Another DeskPort instance is already using this host state. Open that instance to manage sharing.")); return;
    }
    const int selectedPort = m_Ports.reserve(m_BasePort,
        m_Isolated ? QHostAddress::LocalHost : QHostAddress::AnyIPv4);
    if (!selectedPort) {
        beginStop(tr("No free DeskPort port group is available. Existing services were left unchanged.")); return;
    }
    m_BasePort = selectedPort;
    QFile::setPermissions(m_Directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QFile credentials(m_Directory + "/control-secret");
    if (credentials.open(QIODevice::ReadOnly)) m_Password = QString::fromUtf8(credentials.readAll()).trimmed();
    credentials.close();
    if (m_Password.isEmpty()) {
        m_Password = QUuid::createUuid().toString(QUuid::WithoutBraces) + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QSaveFile save(credentials.fileName());
        if (!save.open(QIODevice::WriteOnly)) { beginStop(tr("Cannot save host credentials")); return; }
        save.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        save.write(m_Password.toUtf8());
        if (!save.commit()) { beginStop(tr("Cannot save host credentials")); return; }
    }
    if (!m_Isolated) {
        QSettings settings;
        settings.setValue("host/width", width); settings.setValue("host/height", height);
        settings.setValue("host/shareOnLaunch", true);
    }
    m_Buffer.clear(); m_Starting = true; m_ServerRequested = false;
    const auto generation = ++m_Generation;
    m_Display.setStandardErrorFile(m_Directory + "/display.log", QIODevice::Append);
#ifdef Q_OS_LINUX
    setStatus(tr("Starting desktop sharing…"));
    startServer(0);
#else
    m_Display.start(helperPath(), {QString::number(width), QString::number(height)});
    setStatus(tr("Creating a private virtual display…"));
#endif
    QTimer::singleShot(15000, this, [this, generation] {
        if (m_Starting && m_Generation == generation) { beginStop(tr("Host startup timed out; see logs")); }
    });
}
void HostManager::startServer(int displayId) {
    m_ServerRequested = true;
    QSaveFile config(m_Directory + "/sunshine.conf");
    if (!config.open(QIODevice::WriteOnly)) { beginStop(tr("Cannot write host configuration")); return; }
    config.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    config.write(QString("file_apps = %1/apps.json\nfile_state = %1/state.json\npkey = %1/credentials/key.pem\ncert = %1/credentials/cert.pem\ncredentials_file = %1/control.json\nlog_path = %1/sunshine.log\n").arg(m_Directory).toUtf8());
    QString deviceName = QHostInfo::localHostName().left(64);
    deviceName.replace('\n', ' '); deviceName.replace('\r', ' ');
    if (deviceName.trimmed().isEmpty()) deviceName = "DeskPort";
    config.write(QString("sunshine_name = %2\nport = %1\naddress_family = ipv4\nupnp = disabled\nsystem_tray = disabled\nmin_log_level = 2\norigin_web_ui_allowed = pc\n").arg(m_BasePort).arg(deviceName).toUtf8());
#ifdef Q_OS_MACOS
    config.write(QString("output_name = %1\n").arg(displayId).toUtf8());
#else
    Q_UNUSED(displayId);
#endif
#ifdef Q_OS_LINUX
    // Capture the existing desktop; Linux virtual displays are a separate milestone.
    config.write("output_name = \n");
    config.write(qgetenv("XDG_CURRENT_DESKTOP").contains("KDE") ? "capture = kwin\n" : "capture = portal\n");
#endif
    if (!config.commit()) { beginStop(tr("Cannot save host configuration")); return; }
    m_Credentials.setWorkingDirectory(m_Directory);
    m_Credentials.setStandardOutputFile(QProcess::nullDevice());
    m_Credentials.setStandardErrorFile(QProcess::nullDevice());
    m_Credentials.start(serverPath(), {m_Directory + "/sunshine.conf", "--creds", "deskport", m_Password});
    const auto generation = m_Generation;
    QTimer::singleShot(5000, this, [this, generation] {
        if (generation == m_Generation && m_Credentials.state() != QProcess::NotRunning)
            beginStop(tr("Host authentication timed out; see logs"));
    });
}
void HostManager::stop() {
    if (!m_Isolated) QSettings().setValue("host/shareOnLaunch", false);
    beginStop(available() ? tr("Sharing is off") : tr("Hosting is available in the macOS all-in-one package"));
}
void HostManager::beginStop(const QString &status) {
    if (m_Stopping) return;
    m_Stopping = true;
    const auto generation = ++m_Generation;
    m_Starting = false;
    m_StopStatus = status;
    setStatus(tr("Stopping sharing…"));
    for (auto process : {&m_Credentials, &m_Server}) {
        if (process->state() != QProcess::NotRunning) process->terminate();
    }
    finishStop();
    QTimer::singleShot(2500, this, [this, generation] {
        if (!m_Stopping || generation != m_Generation) return;
        for (auto process : {&m_Credentials, &m_Server}) {
            if (process->state() != QProcess::NotRunning) process->kill();
        }
        finishStop();
    });
    QTimer::singleShot(3500, this, [this, generation] {
        if (!m_Stopping || generation != m_Generation) return;
        if (m_Display.state() != QProcess::NotRunning) m_Display.kill();
        finishStop();
    });
}
void HostManager::finishStop() {
    if (!m_Stopping) return;
    if (m_Credentials.state() != QProcess::NotRunning || m_Server.state() != QProcess::NotRunning) return;
    // Release the display only after the host has stopped using it.
    if (m_Display.state() != QProcess::NotRunning) { m_Display.closeWriteChannel(); return; }
    // QProcess may emit errorOccurred and finished together. Keep cleanup active
    // through both signals so a terminated sibling cannot replace the root error.
    const auto generation = m_Generation;
    QTimer::singleShot(0, this, [this, generation] {
        if (!m_Stopping || generation != m_Generation) return;
        if (m_Credentials.state() != QProcess::NotRunning ||
            m_Server.state() != QProcess::NotRunning || m_Display.state() != QProcess::NotRunning) return;
        m_Ports.release();
        m_HostLock.reset();
        m_Stopping = false;
        setStatus(m_StopStatus);
    });
}
void HostManager::pair(const QString &pin, const QString &name) {
    if (!canPair()) return;
    const auto generation = m_Generation;
    if (pin.size() != 4 || !std::all_of(pin.begin(), pin.end(), [](QChar c) { return c >= '0' && c <= '9'; })) {
        setStatus(tr("Enter the four-digit PIN shown on the connecting device")); return;
    }
    const auto certificates = QSslCertificate::fromPath(m_Directory + "/credentials/cert.pem");
    if (certificates.isEmpty()) { setStatus(tr("Host certificate is not ready")); return; }
    const auto expectedCertificate = certificates.first();
    const auto pinCertificate = [expectedCertificate](QNetworkReply *reply) {
        connect(reply, qOverload<const QList<QSslError>&>(&QNetworkReply::sslErrors), reply,
                [reply, expectedCertificate](const QList<QSslError>&) {
            if (reply->sslConfiguration().peerCertificate() == expectedCertificate) reply->ignoreSslErrors();
        });
    };
    QNetworkRequest request(QUrl(QString("https://127.0.0.1:%1/api/pin").arg(m_BasePort + 1)));
    request.setTransferTimeout(10000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Basic " + ("deskport:" + m_Password).toUtf8().toBase64());
    // Current Sunshine requires an explicit pending-request ID, not just a PIN.
    auto pending = m_Network.get(request);
    pinCertificate(pending);
    connect(pending, &QNetworkReply::finished, this, [this, pending, request, pinCertificate, pin, name, generation] {
        if (generation != m_Generation || !canPair()) { pending->deleteLater(); return; }
        const auto pairings = QJsonDocument::fromJson(pending->readAll()).object()["pairings"].toArray();
        const bool ok = pending->error() == QNetworkReply::NoError;
        pending->deleteLater();
        if (!ok || pairings.size() != 1) {
            setStatus(tr("Keep exactly one connecting device's PIN dialog open, then retry pairing.")); return;
        }
        const auto id = pairings.first().toObject()["id"].toString();
        const auto clientName = name.trimmed().isEmpty() ? QStringLiteral("DeskPort client") : name.trimmed().left(32);
        auto reply = m_Network.post(request, QJsonDocument(QJsonObject{{"pairing_id", id}, {"pin", pin}, {"name", clientName}}).toJson());
        pinCertificate(reply);
        connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
            if (generation != m_Generation || !canPair()) { reply->deleteLater(); return; }
            const auto object = QJsonDocument::fromJson(reply->readAll()).object();
            setStatus(reply->error() == QNetworkReply::NoError && object["status"].toBool()
                      ? tr("Pairing accepted") : tr("Pairing failed. Keep the PIN dialog open on the connecting device and retry."));
            reply->deleteLater();
        });
    });
}
void HostManager::permission(const QString &kind) {
#ifdef Q_OS_MACOS
    if (kind == "screen") CGRequestScreenCaptureAccess();
    else if (kind == "input") CGRequestPostEventAccess();
    if (kind == "screen" || kind == "input") QDesktopServices::openUrl(QUrl(
        "x-apple.systempreferences:com.apple.preference.security?Privacy_" +
        QString(kind == "screen" ? "ScreenCapture" : "Accessibility")));
#endif
}
void HostManager::openLogs() { QDesktopServices::openUrl(QUrl::fromLocalFile(m_Directory)); }

bool HostManager::loginStart() const { return QSettings().value("host/startAtLogin", false).toBool(); }
void HostManager::setLoginStart(bool enabled) {
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
        file.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?><plist version=\"1.0\"><dict>"
                   "<key>Label</key><string>io.github.keithxc.DeskPort</string>"
                   "<key>ProgramArguments</key><array><string>/usr/bin/open</string><string>-gj</string>"
                   "<string>/Applications/DeskPort.app</string></array><key>RunAtLoad</key><true/>"
                   "<key>LimitLoadToSessionType</key><string>Aqua</string></dict></plist>");
        if (!file.commit()) { setStatus(tr("Cannot save login startup")); return; }
    } else if (QFile::exists(path) && !QFile::remove(path)) {
        setStatus(tr("Cannot remove login startup")); return;
    }
    QSettings().setValue("host/startAtLogin", enabled);
    emit changed();
#elif defined(Q_OS_LINUX)
    const QString path = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/io.github.keithxc.DeskPort.desktop";
    if (enabled) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QString executable = QCoreApplication::applicationDirPath() + "/deskport";
        executable.replace("\\", "\\\\").replace("\"", "\\\"").replace("`", "\\`").replace("$", "\\$");
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) { setStatus(tr("Cannot install login startup")); return; }
        file.write(QString("[Desktop Entry]\nType=Application\nName=DeskPort\nExec=\"%1\"\nTerminal=false\n").arg(executable).toUtf8());
        if (!file.commit()) { setStatus(tr("Cannot save login startup")); return; }
    } else if (QFile::exists(path) && !QFile::remove(path)) {
        setStatus(tr("Cannot remove login startup")); return;
    }
    QSettings().setValue("host/startAtLogin", enabled); emit changed();
#else
    Q_UNUSED(enabled);
#endif
}

bool HostManager::prepareIdentity(const QByteArray& certificate, const QByteArray& key) {
    if (running() || !QDir().mkpath(m_Directory + "/credentials")) return false;
    QLockFile lock(m_Directory + "/instance.lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return false;
    QFile::setPermissions(m_Directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    if (QFile::exists(m_Directory + "/credentials/cert.pem") != QFile::exists(m_Directory + "/credentials/key.pem")) return false;
    for (const auto& entry : {qMakePair(QString("cert.pem"), certificate), qMakePair(QString("key.pem"), key)}) {
        const QString path = m_Directory + "/credentials/" + entry.first;
        if (QFile::exists(path)) continue;
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        if (file.write(entry.second) != entry.second.size() || !file.commit()) return false;
    }
    bool ok;
    auto state = PeerStore::read(m_Directory + "/state.json", &ok);
    if (!ok) return false;
    auto root = state["root"].toObject();
    if (root["uniqueid"].toString().isEmpty()) {
        root["uniqueid"] = QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper();
        root["named_devices"] = QJsonArray();
        state["root"] = root;
        if (!PeerStore::write(m_Directory + "/state.json", state)) return false;
    }
    return !identity()["hostCert"].toString().isEmpty();
}
QJsonObject HostManager::identity() const {
    const auto certs = QSslCertificate::fromPath(m_Directory + "/credentials/cert.pem");
    return {{"hostId", PeerStore::read(m_Directory + "/state.json")["root"].toObject()["uniqueid"]},
            {"hostPort", m_BasePort}, {"hostCert", certs.isEmpty() ? QString() : QString::fromUtf8(certs.first().toPem())}};
}
void HostManager::updatePeerTrust(const QString& id, const QString& name, const QSslCertificate& certificate, bool remove) {
    if (m_TrustBusy) { emit trustUpdated(false); return; }
    m_TrustBusy = true;
    const bool restart = running();
    stop();
    auto timer = new QTimer(this);
    auto elapsed = std::make_shared<QElapsedTimer>(); elapsed->start();
    connect(timer, &QTimer::timeout, this, [=] {
        if (running() && elapsed->elapsed() < 7000) return;
        timer->stop(); timer->deleteLater();
        bool ok = false;
        if (!running()) {
            QLockFile lock(m_Directory + "/instance.lock"); lock.setStaleLockTime(0);
            if (lock.tryLock(0)) ok = PeerStore::trust(m_Directory + "/state.json", id, name, certificate, remove);
        }
        m_TrustBusy = false;
        if (restart) {
            QSettings settings;
            start(settings.value("host/width", 2560).toInt(), settings.value("host/height", 1440).toInt());
        }
        emit trustUpdated(ok);
    });
    timer->start(50);
}
