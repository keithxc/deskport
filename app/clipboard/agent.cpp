#include "agent.h"
#include "backend/clipboardprotocol.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QUrl>
#include <QUuid>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QSocketNotifier>
#include <QStorageInfo>
#include <QSet>
#include <QImageReader>
#include <QBuffer>
#include <QDateTime>
#include <QScopedValueRollback>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif
#ifdef Q_OS_UNIX
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#endif

namespace {
const QString Text = QStringLiteral("text/plain;charset=utf-8");
const QString Uri = QStringLiteral("text/uri-list");
const QString Marker = QStringLiteral("application/x-deskport-clipboard");
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
qint64 number(const QJsonValue& v) { bool ok = false; const auto n = v.toString().toLongLong(&ok); return ok ? n : -1; }
}
bool ClipboardV2::safeRelativePath(const QString& path) {
    if (path.isEmpty() || path.size() > 2048 || path.contains('\\') || path.contains(':') || path.contains(QChar(0)) || path.startsWith('/')) return false;
    for (const auto& part : path.split('/'))
        if (part.isEmpty() || part == "." || part == ".." || part.endsWith(' ') || part.endsWith('.')) return false;
    return true;
}
bool ClipboardV2::validManifest(const QJsonArray& files) {
    if (files.isEmpty() || files.size() > MaxEntries) return false;
    QSet<QString> seen, directories;
    qint64 total = 0;
    for (const auto& value : files) {
        const auto f = value.toObject(); const auto path = f["path"].toString(); const auto key = path.normalized(QString::NormalizationForm_C).toCaseFolded();
        const auto size = number(f["size"]);
        if (!safeRelativePath(path) || seen.contains(key) || size < 0 || size > MaxFiles || !f["directory"].isBool()) return false;
        const int slash = key.lastIndexOf('/');
        if (slash >= 0 && !directories.contains(key.left(slash))) return false;
        if (f["directory"].toBool()) { if (size) return false; directories.insert(key); }
        total += size; if (total > MaxFiles) return false;
        seen.insert(key);
    }
    return true;
}
ClipboardAgent::ClipboardAgent(std::unique_ptr<ClipboardNative> native, Sender sender, bool authoritative)
    : m_Native(std::move(native)), m_Send(std::move(sender)), m_Authoritative(authoritative) {
    m_Seen = m_Native->revision(); // Never publish the pre-connection clipboard.
    connect(&m_Timer, &QTimer::timeout, this, &ClipboardAgent::observe);
    // ClipboardNative exposes no reliable background-change callback. Cocoa
    // needs changeCount polling, and Wayland/portal notifications only update
    // the backend's revision/state. Focus-based backoff would lose timely copies
    // in the windowless helper, which may never become active.
    m_Timer.start(250);
    if (auto app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        connect(app, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive) observe();
        }, Qt::QueuedConnection);
    }
}
ClipboardAgent::~ClipboardAgent() { stop(); }
void ClipboardAgent::stop() {
    if (m_Stopped) return;
    m_Stopped = true; m_Timer.stop();
    if (m_WaitLoop) m_WaitLoop->quit();
    // A native provider callback can be waiting inside request(). Do not replace
    // its pasteboard item from that nested event loop; let it unwind before the
    // native backend is destroyed on helper exit.
    if (!m_Busy && !m_Serving && !m_Observing && !m_RemoteId.isEmpty() && m_Native->formats().contains(Marker))
        m_Native->publish({}, [](const QString&) { return QByteArray(); });
    m_Native->cancel();
    m_RemoteId.clear();
}
void ClipboardAgent::status(const QString& text) { m_Send({{"type", "clipboard-v2-status"}, {"message", text}}); }
bool ClipboardAgent::snapshotFiles(const QByteArray& data) {
    m_Files = {}; m_Paths.clear(); m_FileStamps.clear();
    qint64 total = 0;
    auto add = [&](const QString& absolute, const QString& relative) {
        QFileInfo f(absolute);
        if (f.isSymLink() || (!f.isFile() && !f.isDir()) || !f.isReadable() || !ClipboardV2::safeRelativePath(relative) || m_Files.size() >= ClipboardV2::MaxEntries) return false;
        const qint64 size = f.isDir() ? 0 : f.size(); total += size;
        if (total > ClipboardV2::MaxFiles) return false;
        m_Files.append(QJsonObject{{"path", relative}, {"size", QString::number(size)}, {"directory", f.isDir()}, {"modified", QString::number(f.lastModified().toMSecsSinceEpoch())}});
        m_Paths.append(f.absoluteFilePath());
#ifdef Q_OS_UNIX
        struct stat stamp;
        if (::lstat(QFile::encodeName(f.absoluteFilePath()).constData(), &stamp) != 0) return false;
        m_FileStamps.append({quint64(stamp.st_dev), quint64(stamp.st_ino)});
#elif defined(Q_OS_WIN)
        const auto path = QDir::toNativeSeparators(f.absoluteFilePath());
        HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        BY_HANDLE_FILE_INFORMATION stamp = {};
        const bool safe = GetFileInformationByHandle(handle, &stamp) && !(stamp.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
        CloseHandle(handle);
        if (!safe) return false;
        m_FileStamps.append({stamp.dwVolumeSerialNumber, (quint64(stamp.nFileIndexHigh) << 32) | stamp.nFileIndexLow});
#else
        m_FileStamps.append({0, 0});
#endif
        return true;
    };
    // Input paths originate only from the local OS. They are never sent to the peer.
    for (auto line : data.split('\n')) {
        line = line.trimmed(); if (line.isEmpty() || line.startsWith('#')) continue;
        auto url = QUrl::fromEncoded(line);
        if (!url.isLocalFile() || (!url.host().isEmpty() && url.host() != "localhost")) return false;
        QFileInfo root(url.toLocalFile());
        if (!add(root.absoluteFilePath(), root.fileName())) return false;
        if (root.isDir()) {
            // Explicit recursion keeps parents before children and rejects links.
            QStringList dirs{root.absoluteFilePath()};
            for (int i = 0; i < dirs.size(); ++i) {
                const auto entries = QDir(dirs[i]).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::DirsFirst | QDir::Name);
                for (const auto& f : entries) {
                    if (!add(f.absoluteFilePath(), root.fileName() + '/' + QDir(root.absoluteFilePath()).relativeFilePath(f.absoluteFilePath()))) return false;
                    if (f.isDir()) dirs.append(f.absoluteFilePath());
                }
            }
        }
    }
    return ClipboardV2::validManifest(m_Files);
}
void ClipboardAgent::observe() {
    if (m_Stopped) return;
    const auto notice = m_Native->stateMessage();
    if (notice != m_NativeNotice) { m_NativeNotice = notice; status(notice); }
    if (!m_Native->valid()) { stop(); return; }
    if (m_Busy || m_Serving || m_Observing) return;
    QScopedValueRollback<bool> observing(m_Observing, true);
    if (!m_PendingOffer.isEmpty()) { auto pending = m_PendingOffer; m_PendingOffer = {}; applyOffer(pending); }
    const auto revision = m_Native->revision(); if (revision == m_Seen) return;
    m_Seen = revision;
    const auto formats = m_Native->formats();
    if (formats.contains(Marker)) return;
    m_LocalId = uuid(); m_LocalRevision = revision; m_RemoteId.clear(); m_Cache.clear(); m_Image.clear(); m_Paths.clear(); m_Files = {}; m_Kind.clear();
    QJsonObject offer{{"type", "clipboard-v2-offer"}, {"id", m_LocalId}};
    if (formats.contains(Uri)) {
        if (snapshotFiles(m_Native->read(Uri))) {
            m_Kind = "files"; int roots = 0;
            for (const auto& value : m_Files) if (!value.toObject()["path"].toString().contains('/')) ++roots;
            offer["items"] = roots;
        }
        else status("File copy skipped: unsupported paths, links, duplicates, or the 16 GiB / 4096 entry limit.");
    } else if (formats.contains("image/png") || formats.contains("image/tiff")) {
        m_Kind = "image"; m_ImageMime = formats.contains("image/png") ? "image/png" : "image/tiff";
        offer["mime"] = "image/png";
    } else {
        const auto mime = formats.contains(Text) ? Text : QStringLiteral("text/plain");
        if (formats.contains(mime)) {
            const auto bytes = m_Native->read(mime); QString encoded;
            const auto text = QString::fromUtf8(bytes);
            if (m_Native->failedRead(bytes) || m_Native->revision() != revision) status("Clipboard content changed or could not be read.");
            else if (text.toUtf8() == bytes && DeskPortClipboard::encode(text, encoded)) { m_Kind = "text"; offer["text"] = encoded; }
            else status("Text copy exceeds the 128 MiB limit or is not valid UTF-8.");
        } else if (!formats.isEmpty()) status("Clipboard format is not supported.");
    }
    // Native reads dispatch events. A changed selection, disconnect or a newer
    // remote offer must not publish this obsolete snapshot as a successful copy.
    if (m_Stopped || !m_Native->valid() || m_Native->revision() != revision) return;
    if (!m_Kind.isEmpty() && m_Native->stateMessage().isEmpty()) status({});
    // Revoke the previous remote offer even when the new local copy is unsupported.
    offer["kind"] = m_Kind;
    if (m_Authoritative) offer["rev"] = ++m_Revision;
    else offer["base"] = m_Revision;
    m_LastOffer = offer;
    if (!m_Authoritative && m_OfferInFlight) { m_QueuedOffer = true; return; }
    m_OfferInFlight = !m_Authoritative; m_Send(offer);
}
QJsonObject ClipboardAgent::serve(const QJsonObject& message) {
    QJsonObject reply{{"type", "clipboard-v2-data"}, {"request", message["request"]}};
    auto fail = [&] { reply["error"] = "Clipboard content changed or could not be read."; return reply; };
    if (m_Serving || message["id"].toString() != m_LocalId || m_Kind.isEmpty()) return fail();
    QScopedValueRollback<bool> serving(m_Serving, true);
    const auto operation = message["operation"].toString();
    if (operation == "manifest" && m_Kind == "files") { reply["files"] = m_Files; return reply; }
    const auto offset = number(message["offset"]); const int count = message["count"].toInt();
    if (offset < 0 || count <= 0 || count > ClipboardV2::Chunk) return fail();
    QByteArray bytes;
    qint64 size = 0;
    if (m_Kind == "image" && operation == "image") {
        if (m_Image.isEmpty()) {
            if (m_Native->revision() != m_LocalRevision) return fail();
            m_Image = m_Native->read(m_ImageMime);
            if (m_ImageMime == "image/tiff") {
                QBuffer input(&m_Image); input.open(QIODevice::ReadOnly); QImageReader reader(&input, "tiff");
                const auto size = reader.size();
                if (!size.isValid() || qint64(size.width()) * size.height() > 32LL * 1024 * 1024) { m_Image.clear(); return fail(); }
                const auto image = reader.read(); QByteArray png; QBuffer output(&png); output.open(QIODevice::WriteOnly);
                if (image.isNull() || !image.save(&output, "PNG")) { m_Image.clear(); return fail(); }
                m_Image = png;
            }
            if (m_Image.size() > ClipboardV2::MaxImage) { m_Image.clear(); return fail(); }
        }
        size = m_Image.size(); if (!size || offset > size) return fail();
        bytes = m_Image.mid(offset, count);
    } else if (m_Kind == "files" && operation == "file") {
        const int index = message["index"].toInt(-1);
        if (index < 0 || index >= m_Paths.size()) return fail();
        const auto f = m_Files[index].toObject(); QFileInfo info(m_Paths[index]); size = number(f["size"]);
        if (f["directory"].toBool() || !info.isFile() || info.isSymLink() || info.size() != size ||
            info.lastModified().toMSecsSinceEpoch() != number(f["modified"]) || offset > size) return fail();
        QFile file;
#ifdef Q_OS_UNIX
        const int fd = ::open(QFile::encodeName(m_Paths[index]).constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0) return fail();
        struct stat stamp;
        if (::fstat(fd, &stamp) != 0 || !S_ISREG(stamp.st_mode) || quint64(stamp.st_dev) != m_FileStamps[index].device ||
            quint64(stamp.st_ino) != m_FileStamps[index].inode || stamp.st_size != size) { ::close(fd); return fail(); }
        if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) { ::close(fd); return fail(); }
#elif defined(Q_OS_WIN)
        const auto path = QDir::toNativeSeparators(m_Paths[index]);
        HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
                                    FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return fail();
        BY_HANDLE_FILE_INFORMATION stamp = {};
        const bool haveStamp = GetFileInformationByHandle(handle, &stamp);
        const quint64 fileTime = (quint64(stamp.ftLastWriteTime.dwHighDateTime) << 32) | stamp.ftLastWriteTime.dwLowDateTime;
        const qint64 modified = qint64(fileTime / 10000) - 11644473600000LL;
        if (!haveStamp || (stamp.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
            stamp.dwVolumeSerialNumber != m_FileStamps[index].device ||
            ((quint64(stamp.nFileIndexHigh) << 32) | stamp.nFileIndexLow) != m_FileStamps[index].inode ||
            ((quint64(stamp.nFileSizeHigh) << 32) | stamp.nFileSizeLow) != quint64(size) || modified != number(f["modified"])) {
            CloseHandle(handle); return fail();
        }
        const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDONLY | _O_BINARY);
        if (fd < 0) { CloseHandle(handle); return fail(); }
        if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) { _close(fd); return fail(); }
#else
        file.setFileName(m_Paths[index]); if (!file.open(QIODevice::ReadOnly)) return fail();
#endif
        if (!file.seek(offset)) return fail();
        bytes = file.read(qMin<qint64>(count, size - offset));
        if (bytes.size() != qMin<qint64>(count, size - offset)) return fail();
    } else return fail();
    reply["size"] = QString::number(size); reply["offset"] = QString::number(offset); reply["data"] = QString::fromLatin1(bytes.toBase64()); return reply;
}
void ClipboardAgent::receive(const QJsonObject& message) {
    if (m_Stopped) return;
    const auto type = message["type"].toString();
    if (type == "clipboard-v2-read") { m_Send(serve(message)); return; }
    if (type == "clipboard-v2-data") {
        const auto request = message["request"].toString();
        if (request == QString::number(m_Request)) {
            m_Replies.insert(request, message); if (m_WaitLoop) m_WaitLoop->quit();
        }
        return;
    }
    if (type != "clipboard-v2-offer") return;
    observe();
    const auto id = message["id"].toString();
    if (id.isEmpty() || id.size() > 64) return;
    auto ordered = message;
    if (m_Authoritative) {
        if (message["base"].toInt(-1) != m_Revision) {
            if (!m_LastOffer.isEmpty()) m_Send(m_LastOffer);
            return;
        }
        ordered["rev"] = ++m_Revision; m_LastOffer = ordered;
        m_Send({{"type", "clipboard-v2-offer"}, {"id", id}, {"kind", "ack"}, {"rev", m_Revision}});
    } else {
        const int revision = message["rev"].toInt(-1);
        if (revision < m_Revision || revision < 0) return;
        m_Revision = revision;
        m_OfferInFlight = false;
        if (m_QueuedOffer) {
            m_QueuedOffer = false; m_LastOffer["base"] = m_Revision;
            m_OfferInFlight = true; m_Send(m_LastOffer); return;
        }
        if (message["kind"] == "ack") return;
    }
    if (m_Busy || m_Serving) { m_RemoteId.clear(); m_PendingOffer = ordered; if (m_WaitLoop) m_WaitLoop->quit(); return; }
    applyOffer(ordered);
}
void ClipboardAgent::applyOffer(const QJsonObject& message) {
    const auto id = message["id"].toString(); const auto kind = message["kind"].toString();
    if (id.isEmpty() || id.size() > 64) return;
    m_RemoteId = id; m_Cache.clear();
    m_LocalId.clear(); m_Kind.clear();
    QStringList formats;
    if (kind == "text") {
        QString text; if (!DeskPortClipboard::decode(message["text"], text)) return;
        m_Cache[Text] = text.toUtf8(); m_Cache["text/plain"] = text.toUtf8(); formats << Text << "text/plain";
    } else if (kind == "image") {
        const auto mime = message["mime"].toString();
        if (mime != "image/png" && mime != "image/tiff") return;
        formats << mime;
    } else if (kind == "files") formats << Uri;
    else { m_RemoteId.clear(); return; }
    formats << Marker;
    m_Native->publish(formats, [this, id](const QString& mime) {
        if (mime == Marker) return id.toUtf8();
        if (m_Stopped || id != m_RemoteId) return QByteArray();
        if (m_Cache.contains(mime)) {
            const auto bytes = m_Cache[mime];
            return bytes.isNull() ? QByteArray("") : bytes; // Valid empty text is not a failed native read.
        }
        return materialize(id, mime);
    }, kind == "files" ? qBound(1, message["items"].toInt(1), ClipboardV2::MaxEntries) : 1);
    m_Seen = m_Native->revision();
}
QJsonObject ClipboardAgent::request(QJsonObject message) {
    const auto key = QString::number(++m_Request); message["type"] = "clipboard-v2-read"; message["request"] = key;
    QEventLoop loop; QTimer deadline;
    deadline.setSingleShot(true); connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    m_WaitLoop = &loop; deadline.start(30000); m_Send(message);
    if (!m_Stopped && !m_Replies.contains(key)) loop.exec();
    m_WaitLoop = nullptr;
    return m_Replies.take(key);
}
QByteArray ClipboardAgent::materialize(const QString& id, const QString& mime) {
    if (m_Busy || m_Stopped) return {};
    QScopedValueRollback<bool> busy(m_Busy, true);
    const auto demandRevision = m_Native->revision();
    status(mime == Uri ? "Receiving clipboard files…" : "Receiving clipboard image…");
    auto fetch = [&](const QString& operation, int index, QFile* file, QByteArray* image, qint64 expected) {
        qint64 offset = 0, total = expected;
        do {
            if (m_Stopped || m_RemoteId != id || m_Native->revision() != demandRevision) return false;
            auto reply = request({{"id", id}, {"operation", operation}, {"index", index}, {"offset", QString::number(offset)}, {"count", ClipboardV2::Chunk}});
            const auto size = number(reply["size"]); const auto encoded = reply["data"].toString().toLatin1(); const auto bytes = QByteArray::fromBase64(encoded);
            if (reply.contains("error") || !reply.contains("data") || number(reply["offset"]) != offset || size < 0 ||
                size > (file ? ClipboardV2::MaxFiles : ClipboardV2::MaxImage) || (total >= 0 && total != size) ||
                bytes.toBase64() != encoded || bytes.size() != qMin<qint64>(ClipboardV2::Chunk, size - offset)) return false;
            total = size;
            if (file && file->write(bytes) != bytes.size()) return false;
            if (image) image->append(bytes);
            offset += bytes.size();
            if (total > 0 && offset % (4 * 1024 * 1024) == 0) status(QString("Receiving clipboard content: %1 / %2 MiB").arg(offset / (1024 * 1024)).arg((total + 1048575) / 1048576));
        } while (offset < total);
        return true;
    };
    QByteArray result;
    if (mime == Uri) {
        const auto reply = request({{"id", id}, {"operation", "manifest"}}); const auto files = reply["files"].toArray();
        if (!ClipboardV2::validManifest(files)) { status("Clipboard file list is unavailable or invalid."); return {}; }
        auto temporary = std::unique_ptr<QTemporaryDir>(new QTemporaryDir(QDir::tempPath() + "/deskport-paste-XXXXXX"));
        if (!temporary->isValid()) return {};
        QFile::setPermissions(temporary->path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qint64 required = 0; for (const auto& value : files) required += number(value.toObject()["size"]);
        if (required + m_CachedBytes > ClipboardV2::MaxFiles || QStorageInfo(temporary->path()).bytesAvailable() < required + 256LL * 1024 * 1024) { status("Not enough temporary disk space for clipboard files."); return {}; }
        for (int i = 0; i < files.size(); ++i) {
            const auto entry = files[i].toObject(); const auto relative = entry["path"].toString(); const auto destination = temporary->path() + '/' + relative;
            if (entry["directory"].toBool()) { if (!QDir().mkpath(destination)) return {}; }
            else {
                QFile file(destination);
                if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || !fetch("file", i, &file, nullptr, number(entry["size"]))) {
                    status("Clipboard file transfer failed or was cancelled; no partial files were published."); return {};
                }
                file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
                file.close();
            }
            if (!relative.contains('/')) result += QUrl::fromLocalFile(destination).toEncoded() + "\r\n";
        }
        // Keep the successful download alive for repeat paste until this session ends.
        // Earlier completed downloads must also survive subsequent clipboard copies.
        if (m_Temporary) {
            const auto old = m_Temporary.release();
            connect(this, &QObject::destroyed, [old] { delete old; });
        }
        m_Temporary = std::move(temporary); m_CachedBytes += required;
    } else {
        if (!fetch("image", 0, nullptr, &result, -1)) { status("Clipboard image transfer failed or the source changed."); return {}; }
    }
    if (m_Stopped || id != m_RemoteId) return {};
    m_Cache[mime] = result; status("Clipboard content ready."); return result;
}
int runClipboardHelper(int argc, char** argv) {
#ifdef Q_OS_UNIX
    signal(SIGPIPE, SIG_IGN);
    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
#ifdef Q_OS_LINUX
    app.setApplicationName(QStringLiteral("DeskPort"));
    app.setDesktopFileName(QStringLiteral("io.github.keithxc.DeskPort"));
#endif
    QFile output; if (!output.open(STDOUT_FILENO, QIODevice::WriteOnly)) return 5;
    auto send = [&](const QJsonObject& message) { output.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n'); output.flush(); };
    ClipboardAgent agent(makeClipboardNative(), send, app.arguments().contains("--clipboard-helper-host"));
    if (!agent.valid()) { agent.observe(); return 2; }
    QTimer nativeHealth;
    QObject::connect(&nativeHealth, &QTimer::timeout, &app, [&] { if (!agent.valid()) { agent.observe(); app.exit(2); } });
    nativeHealth.start(250);
    QByteArray buffer;
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
    QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&input, &QSocketNotifier::activated, &app, [&] {
        char bytes[65536]; ssize_t n;
        while ((n = ::read(STDIN_FILENO, bytes, sizeof(bytes))) > 0) {
            buffer.append(bytes, n);
            if (buffer.size() > ClipboardV2::Frame) { agent.stop(); app.exit(3); return; }
        }
        if (n == 0) { agent.stop(); app.quit(); return; }
        int end;
        while ((end = buffer.indexOf('\n')) >= 0) {
            const auto frame = buffer.left(end); buffer.remove(0, end + 1);
            QJsonParseError error; auto doc = QJsonDocument::fromJson(frame, &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) { agent.stop(); app.exit(4); return; }
            agent.receive(doc.object());
        }
    });
    return app.exec();
#elif defined(Q_OS_WIN)
    auto trace = [](const char* stage) {
        if (!qEnvironmentVariableIsSet("DESKPORT_CLIPBOARD_DIAGNOSTICS")) return;
        DWORD written=0;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE),stage,DWORD(strlen(stage)),&written,nullptr);
    };
    trace("clipboard: application initialization\n");
    QGuiApplication app(argc, argv);
    trace("clipboard: application ready\n");
    app.setQuitOnLastWindowClosed(false);
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!input || input == INVALID_HANDLE_VALUE || !output || output == INVALID_HANDLE_VALUE ||
        GetFileType(input) != FILE_TYPE_PIPE || GetFileType(output) != FILE_TYPE_PIPE) return 5;
    bool broken = false;
    auto send = [&](const QJsonObject& message) {
        const auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
        qsizetype offset = 0;
        while (!broken && offset < bytes.size()) {
            DWORD written = 0;
            if (!WriteFile(output, bytes.constData()+offset, DWORD(bytes.size()-offset), &written, nullptr) || !written) {
                broken = true; app.exit(5); return;
            }
            offset += written;
        }
    };
    ClipboardAgent agent(makeClipboardNative(), send, app.arguments().contains("--clipboard-helper-host"));
    trace("clipboard: native provider ready\n");
    if (!agent.valid()) return 2;
    QByteArray buffer;
    // Anonymous QProcess pipes are not WinSock descriptors. PeekNamedPipe keeps
    // clipboard/OLE dispatch on the GUI thread without a blocking stdin read.
    QTimer inputTimer;
    QObject::connect(&inputTimer, &QTimer::timeout, &app, [&] {
        if (broken) { agent.stop(); app.exit(5); return; }
        DWORD available = 0;
        if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr)) {
            agent.stop(); app.quit(); return;
        }
        DWORD remaining = qMin<DWORD>(available, 1024*1024);
        if (remaining) trace("clipboard: input available\n");
        while (remaining) {
            char bytes[65536]; DWORD received = 0;
            if (!ReadFile(input, bytes, qMin<DWORD>(remaining, sizeof(bytes)), &received, nullptr) || !received) {
                agent.stop(); app.quit(); return;
            }
            remaining -= received; buffer.append(bytes, received);
            if (buffer.size() > ClipboardV2::Frame) { agent.stop(); app.exit(3); return; }
        }
        int end;
        while ((end = buffer.indexOf('\n')) >= 0) {
            const auto frame = buffer.left(end); buffer.remove(0, end+1);
            QJsonParseError error; const auto doc = QJsonDocument::fromJson(frame, &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) { agent.stop(); app.exit(4); return; }
            // OLE may request delayed data while an offer is being published.
            // Leave this timer callback before dispatch so nested request loops
            // can continue draining the pipe.
            const auto message = doc.object();
            QTimer::singleShot(0, &agent, [&agent, message] { agent.receive(message); });
        }
    });
    inputTimer.start(10);
    return app.exec();
#else
    Q_UNUSED(argc); Q_UNUSED(argv); return 2;
#endif
}
