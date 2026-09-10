#include "hostmanager.h"
#include <algorithm>
#include <QSslError>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QNetworkProxy>
#include <QApplication>
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
#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#endif

HostManager::HostManager(QObject *parent, const QString &directory) : QObject(parent) {
    m_Isolated = !directory.isEmpty();
    m_Network.setProxy(QNetworkProxy::NoProxy);
    m_Directory = directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/host" : directory;
    m_Status = available() ? tr("Sharing is off") : tr("Hosting is available in the macOS all-in-one package");
    connect(&m_Display, &QProcess::readyReadStandardOutput, this, [this] {
        m_Buffer += m_Display.readAllStandardOutput();
        while (m_Buffer.contains('\n')) {
            auto index = m_Buffer.indexOf('\n');
            auto object = QJsonDocument::fromJson(m_Buffer.left(index)).object();
            m_Buffer.remove(0, index + 1);
            if (object.contains("error")) { stop(); setStatus(object["error"].toString()); return; }
            if (object["displayId"].toInt() > 0 && m_Starting) startServer(object["displayId"].toInt());
        }
    });
    connect(&m_Server, &QProcess::started, this, [this] {
        m_Starting = false;
        setStatus(tr("Host process started on port 48989. Grant permissions, then connect to this computer:48989."));
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
    connect(&m_Server, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_Stopping) return;
        m_Starting = false; setStatus(tr("Host failed: ") + m_Server.errorString());
    });
    connect(&m_Display, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_Stopping) return;
        m_Starting = false; setStatus(tr("Virtual display failed: ") + m_Display.errorString());
    });
    connect(&m_Server, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
        if (!m_Stopping && m_Display.state() != QProcess::NotRunning) {
            setStatus(tr("Host stopped (%1). See host logs.").arg(code));
        }
        emit changed();
    });
    connect(&m_Display, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
        m_Starting = false;
        if (!m_Stopping && m_Server.state() != QProcess::NotRunning) m_Server.terminate();
        emit changed();
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
    m_Tray.setIcon(QIcon(":/res/deskport.svg"));
    m_Tray.setToolTip("DeskPort");
    if (available()) m_Tray.show();
    connect(qApp, &QCoreApplication::aboutToQuit, this, &HostManager::stop);
    if (directory.isEmpty() && available() && loginStart()) {
        QTimer::singleShot(0, this, [this] {
            QSettings settings;
            start(settings.value("host/width", 2560).toInt(), settings.value("host/height", 1440).toInt());
        });
    }
}
HostManager::~HostManager() { stop(); delete m_Tray.contextMenu(); }
QString HostManager::helperPath() const { return QCoreApplication::applicationDirPath() + "/../Helpers/deskport-display"; }
QString HostManager::serverPath() const { return QCoreApplication::applicationDirPath() + "/../Helpers/Sunshine.app/Contents/MacOS/Sunshine"; }
bool HostManager::available() const {
#ifdef Q_OS_MACOS
    return QFile::exists(helperPath()) && QFile::exists(serverPath());
#else
    return false;
#endif
}
bool HostManager::running() const { return m_Starting || m_Server.state() != QProcess::NotRunning || m_Display.state() != QProcess::NotRunning; }
void HostManager::setStatus(const QString &value) { m_Status = value; emit changed(); }
void HostManager::start(int width, int height) {
    if (!available() || running()) return;
    if (width < 640 || width > 3840 || height < 360 || height > 2160 || width % 2 || height % 2) {
        setStatus(tr("Unsupported display size")); return;
    }
    QDir().mkpath(m_Directory + "/credentials");
    QFile::setPermissions(m_Directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QFile credentials(m_Directory + "/control-secret");
    if (credentials.open(QIODevice::ReadOnly)) m_Password = QString::fromUtf8(credentials.readAll()).trimmed();
    credentials.close();
    if (m_Password.isEmpty()) {
        m_Password = QUuid::createUuid().toString(QUuid::WithoutBraces) + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QSaveFile save(credentials.fileName());
        if (!save.open(QIODevice::WriteOnly)) { setStatus(tr("Cannot save host credentials")); return; }
        save.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        save.write(m_Password.toUtf8());
        if (!save.commit()) { setStatus(tr("Cannot save host credentials")); return; }
    }
    if (!m_Isolated) {
        QSettings settings;
        settings.setValue("host/width", width); settings.setValue("host/height", height);
    }
    m_Buffer.clear(); m_Starting = true;
    const auto generation = ++m_Generation;
    m_Display.setStandardErrorFile(m_Directory + "/display.log", QIODevice::Append);
    m_Display.start(helperPath(), {QString::number(width), QString::number(height)});
    setStatus(tr("Creating a private virtual display…"));
    QTimer::singleShot(15000, this, [this, generation] {
        if (m_Starting && m_Generation == generation) { stop(); setStatus(tr("Host startup timed out; see logs")); }
    });
}
void HostManager::startServer(int displayId) {
    QSaveFile config(m_Directory + "/sunshine.conf");
    if (!config.open(QIODevice::WriteOnly)) { stop(); setStatus(tr("Cannot write host configuration")); return; }
    config.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    config.write(QString("file_apps = %1/apps.json\nfile_state = %1/state.json\npkey = %1/credentials/key.pem\ncert = %1/credentials/cert.pem\ncredentials_file = %1/control.json\nlog_path = %1/sunshine.log\n").arg(m_Directory).toUtf8());
    config.write(QString("sunshine_name = DeskPort\nport = 48989\nupnp = disabled\nmin_log_level = 2\noutput_name = %1\norigin_web_ui_allowed = pc\n").arg(displayId).toUtf8());
    if (!config.commit()) { stop(); setStatus(tr("Cannot save host configuration")); return; }
    QProcess credentials;
    credentials.setWorkingDirectory(m_Directory);
    credentials.start(serverPath(), {m_Directory + "/sunshine.conf", "--creds", "deskport", m_Password});
    if (!credentials.waitForFinished(5000) || credentials.exitCode() != 0) {
        credentials.kill(); credentials.waitForFinished(); stop(); setStatus(tr("Could not initialize host authentication")); return;
    }
    m_Server.setWorkingDirectory(m_Directory);
    m_LogOffset = QFileInfo(m_Directory + "/host.log").size();
    m_Server.setStandardOutputFile(m_Directory + "/host.log", QIODevice::Append);
    m_Server.setStandardErrorFile(m_Directory + "/host.log", QIODevice::Append);
    m_Server.start(serverPath(), {m_Directory + "/sunshine.conf"});
}
void HostManager::stop() {
    if (m_Stopping) return;
    m_Stopping = true;
    ++m_Generation;
    m_Starting = false;
    for (auto process : {&m_Server, &m_Display}) {
        if (process->state() != QProcess::NotRunning) {
            if (process == &m_Display) process->closeWriteChannel();
            else process->terminate();
            if (!process->waitForFinished(2500)) { process->kill(); process->waitForFinished(1000); }
        }
    }
    m_Stopping = false;
    setStatus(available() ? tr("Sharing is off") : tr("Hosting is available in the macOS all-in-one package"));
}
void HostManager::pair(const QString &pin, const QString &name) {
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
    QNetworkRequest request(QUrl("https://127.0.0.1:48990/api/pin"));
    request.setTransferTimeout(10000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Basic " + ("deskport:" + m_Password).toUtf8().toBase64());
    // Current Sunshine requires an explicit pending-request ID, not just a PIN.
    auto pending = m_Network.get(request);
    pinCertificate(pending);
    connect(pending, &QNetworkReply::finished, this, [this, pending, request, pinCertificate, pin, name] {
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
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
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
#else
    Q_UNUSED(enabled);
#endif
}
