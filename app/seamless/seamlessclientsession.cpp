// SPDX-License-Identifier: GPL-3.0-or-later
#include "seamlessclientsession.h"

#include "seamlesswindow.h"

#include <QJsonDocument>
#include <QLoggingCategory>
#include <QtEndian>

#include <cstring>

Q_LOGGING_CATEGORY(lcSeamless, "deskport.seamless")

namespace {
constexpr int MaxTitleLength = 1024;
constexpr int MaxWindows = 64;
constexpr quint32 MaxDimension = 16384;
constexpr qint64 MaxPixels = 64LL * 1024 * 1024;
} // namespace

SeamlessWindowRegistry::SeamlessWindowRegistry(InputSink sink) : m_Sink(std::move(sink)) {}

SeamlessWindowRegistry::~SeamlessWindowRegistry()
{
    clear();
}

bool SeamlessWindowRegistry::create(quint32 id, const QString& title)
{
    if (id == 0 || id <= m_HighestId || m_Windows.size() >= MaxWindows)
        return false;
    m_HighestId = id;
    auto window = std::make_unique<SeamlessWindow>(id, m_Sink);
    window->setTitle(title.left(MaxTitleLength));
    m_Windows.emplace(id, std::move(window));
    return true;
}

bool SeamlessWindowRegistry::createPopup(quint32 id, quint32 parentId)
{
    SeamlessWindow* parent = find(parentId);
    if (!parent || id == 0 || id <= m_HighestId || m_Windows.size() >= MaxWindows)
        return false;
    m_HighestId = id;
    m_Windows.emplace(id, std::make_unique<SeamlessWindow>(id, m_Sink, parent));
    return true;
}

void SeamlessWindowRegistry::setPopupPosition(quint32 id, const QPoint& position)
{
    if (SeamlessWindow* window = find(id))
        window->setPopupPosition(position);
}

SeamlessWindow* SeamlessWindowRegistry::find(quint32 id) const
{
    const auto found = m_Windows.find(id);
    return found == m_Windows.end() ? nullptr : found->second.get();
}

void SeamlessWindowRegistry::setTitle(quint32 id, const QString& title)
{
    if (SeamlessWindow* window = find(id))
        window->setTitle(title.left(MaxTitleLength));
}

void SeamlessWindowRegistry::setWindowGeometry(quint32 id, const QRect& geometry)
{
    if (SeamlessWindow* window = find(id))
        window->setWindowGeometry(geometry);
}

void SeamlessWindowRegistry::unmap(quint32 id)
{
    if (SeamlessWindow* window = find(id))
        window->unmap();
}

void SeamlessWindowRegistry::destroy(quint32 id)
{
    const auto found = m_Windows.find(id);
    if (found == m_Windows.end())
        return;
    std::unique_ptr<SeamlessWindow> window = std::move(found->second);
    m_Windows.erase(found);
    window->hide();
    window.release()->deleteLater();
}

void SeamlessWindowRegistry::clear()
{
    for (auto& [id, window] : m_Windows) {
        Q_UNUSED(id);
        window->hide();
        window.release()->deleteLater();
    }
    m_Windows.clear();
}

SeamlessClientSession::SeamlessClientSession(QObject* parent)
    : QObject(parent),
      m_Registry([this](quint32 id, const QJsonObject& input) { sendInput(id, input); })
{
    m_Transport.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_Transport, &QProcess::readyReadStandardOutput, this,
            &SeamlessClientSession::readTransport);
    connect(&m_Transport, &QProcess::readyReadStandardError, this, [this] {
        const QByteArray text = m_Transport.readAllStandardError();
        for (const QByteArray& line : text.split('\n')) {
            if (!line.trimmed().isEmpty())
                qCInfo(lcSeamless).noquote() << "remote:" << QString::fromUtf8(line.left(2048));
        }
    });
    connect(&m_Transport, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus status) {
                fail(QStringLiteral("transport exited (%1, %2)")
                         .arg(code)
                         .arg(status == QProcess::NormalExit ? QStringLiteral("normal")
                                                             : QStringLiteral("crashed")));
            });
    connect(&m_Transport, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            fail(QStringLiteral("transport failed to start: %1").arg(m_Transport.errorString()));
    });
}

SeamlessClientSession::~SeamlessClientSession()
{
    m_Finished = true;
    stop();
}

void SeamlessClientSession::start(const QString& program, const QStringList& arguments)
{
    qCInfo(lcSeamless).noquote() << "starting transport:" << program << arguments.join(QLatin1Char(' '));
    m_Transport.start(program, arguments);
}

void SeamlessClientSession::stop()
{
    m_Registry.clear();
    if (m_Transport.state() == QProcess::NotRunning)
        return;
    // Closing stdin makes the sidecar shut down its application cleanly; the
    // SSH client then exits on its own.
    m_Transport.closeWriteChannel();
    if (!m_Transport.waitForFinished(3000)) {
        m_Transport.terminate();
        if (!m_Transport.waitForFinished(2000))
            m_Transport.kill();
    }
}

void SeamlessClientSession::readTransport()
{
    m_Reader.append(m_Transport.readAllStandardOutput());
    SeamlessStream::RecordReader::Record record;
    while (!m_Finished && m_Reader.next(&record))
        handleRecord(record);
    if (m_Reader.failed())
        fail(QStringLiteral("protocol error: %1").arg(QString::fromLatin1(m_Reader.error())));
}

void SeamlessClientSession::handleRecord(const SeamlessStream::RecordReader::Record& record)
{
    if (!m_HelloReceived && record.type != SeamlessStream::ServerHello) {
        fail(QStringLiteral("server did not start with a hello record"));
        return;
    }
    switch (record.type) {
    case SeamlessStream::ServerHello: {
        const QJsonDocument document = QJsonDocument::fromJson(record.payload);
        const QJsonObject hello = document.object();
        if (m_HelloReceived || !document.isObject() ||
            hello.value(QStringLiteral("protocol")).toString() !=
                QStringLiteral("deskport-seamless-stream") ||
            hello.value(QStringLiteral("version")).toInt() != SeamlessStream::Version) {
            fail(QStringLiteral("unsupported server hello"));
            return;
        }
        m_HelloReceived = true;
        qCInfo(lcSeamless).noquote() << "server hello:" << QString::fromUtf8(record.payload);
        sendRecord(SeamlessStream::ClientHello, 0,
                   QJsonDocument(QJsonObject{
                                     {QStringLiteral("client"), QStringLiteral("seamless-lab")},
                                     {QStringLiteral("version"), SeamlessStream::Version},
                                 }).toJson(QJsonDocument::Compact));
        emit ready(hello);
        break;
    }
    case SeamlessStream::ServerEvent: {
        if (record.payload.size() > static_cast<qsizetype>(SeamlessStream::MaxJsonPayload))
            return;
        const QJsonDocument document = QJsonDocument::fromJson(record.payload);
        if (document.isObject())
            handleEvent(document.object());
        break;
    }
    case SeamlessStream::Frame:
        if (!handleFrame(record.windowId, record.payload)) {
            fail(QStringLiteral("invalid frame for window %1").arg(record.windowId));
            return;
        }
        // One acknowledgement per frame record, including stale ones, keeps
        // the server's frame credit accounting exact.
        sendRecord(SeamlessStream::ClientFrameAck, record.windowId, QByteArray());
        break;
    default:
        // Unknown record types are ignored for forward compatibility.
        break;
    }
}

void SeamlessClientSession::handleEvent(const QJsonObject& event)
{
    const QString type = event.value(QStringLiteral("type")).toString();
    const qint64 id = event.value(QStringLiteral("id")).toInteger();
    const bool validId = id > 0 && id <= 0x7fffffff;
    const qint64 parent = event.value(QStringLiteral("parent")).toInteger();
    if (type == QStringLiteral("window-create") && validId &&
        event.value(QStringLiteral("popup")).toBool()) {
        if (parent <= 0 || parent > 0x7fffffff ||
            !m_Registry.createPopup(static_cast<quint32>(id), static_cast<quint32>(parent)))
            qCWarning(lcSeamless) << "rejected popup" << id << "for parent" << parent;
    } else if (type == QStringLiteral("window-create") && validId) {
        if (!m_Registry.create(static_cast<quint32>(id),
                               event.value(QStringLiteral("title")).toString()))
            qCWarning(lcSeamless) << "rejected window-create for id" << id;
    } else if (type == QStringLiteral("popup-position") && validId) {
        const int x = event.value(QStringLiteral("x")).toInt();
        const int y = event.value(QStringLiteral("y")).toInt();
        if (qAbs(x) <= static_cast<int>(MaxDimension) && qAbs(y) <= static_cast<int>(MaxDimension))
            m_Registry.setPopupPosition(static_cast<quint32>(id), QPoint(x, y));
    } else if (type == QStringLiteral("app-start") || type == QStringLiteral("app-exit") ||
               type == QStringLiteral("launch-rejected")) {
        emit appEvent(event);
    } else if (type == QStringLiteral("window-title") && validId) {
        m_Registry.setTitle(static_cast<quint32>(id), event.value(QStringLiteral("title")).toString());
    } else if (type == QStringLiteral("window-geometry") && validId) {
        m_Registry.setWindowGeometry(static_cast<quint32>(id),
                                     QRect(event.value(QStringLiteral("x")).toInt(),
                                           event.value(QStringLiteral("y")).toInt(),
                                           event.value(QStringLiteral("width")).toInt(),
                                           event.value(QStringLiteral("height")).toInt()));
    } else if (type == QStringLiteral("window-unmap") && validId) {
        m_Registry.unmap(static_cast<quint32>(id));
    } else if (type == QStringLiteral("window-destroy") && validId) {
        m_Registry.destroy(static_cast<quint32>(id));
    }
    if (type != QStringLiteral("window-title"))
        qCInfo(lcSeamless).noquote() << "event:" << QJsonDocument(event).toJson(QJsonDocument::Compact);
}

bool SeamlessClientSession::handleFrame(quint32 windowId, const QByteArray& payload)
{
    if (payload.size() < static_cast<qsizetype>(sizeof(SeamlessStream::FrameHeader)))
        return false;
    SeamlessStream::FrameHeader header;
    std::memcpy(&header, payload.constData(), sizeof(header));
    const quint32 width = qFromLittleEndian(header.width);
    const quint32 height = qFromLittleEndian(header.height);
    const int scale = qFromLittleEndian(header.bufferScale);
    const int rectCount = qFromLittleEndian(header.rectCount);
    if (width == 0 || height == 0 || width > MaxDimension || height > MaxDimension ||
        static_cast<qint64>(width) * height > MaxPixels || rectCount > SeamlessStream::MaxFrameRects ||
        header.encoding > SeamlessStream::EncodingZlib ||
        header.pixelFormat != SeamlessStream::PixelArgb32Premultiplied) {
        return false;
    }

    SeamlessWindow* window = m_Registry.find(windowId);
    if (!window)
        return true; // Destroyed locally before the frame arrived; drop it.
    if (!window->beginFrame(QSize(static_cast<int>(width), static_cast<int>(height)), scale))
        return false;

    qsizetype offset = sizeof(header);
    for (int index = 0; index < rectCount; ++index) {
        if (payload.size() - offset < static_cast<qsizetype>(sizeof(SeamlessStream::FrameRect)))
            return false;
        SeamlessStream::FrameRect rect;
        std::memcpy(&rect, payload.constData() + offset, sizeof(rect));
        offset += sizeof(rect);
        const quint32 x = qFromLittleEndian(rect.x);
        const quint32 y = qFromLittleEndian(rect.y);
        const quint32 w = qFromLittleEndian(rect.width);
        const quint32 h = qFromLittleEndian(rect.height);
        const quint32 length = qFromLittleEndian(rect.dataLength);
        if (w == 0 || h == 0 || x >= width || y >= height || w > width - x || h > height - y ||
            static_cast<qint64>(length) > payload.size() - offset) {
            return false;
        }
        const qsizetype expected = static_cast<qsizetype>(w) * h * 4;
        const QByteArray encoded = QByteArray::fromRawData(payload.constData() + offset, length);
        offset += length;
        QByteArray pixels;
        if (header.encoding == SeamlessStream::EncodingZlib) {
            // qCompress prefixes the big-endian decoded size; validate it
            // before qUncompress allocates.
            if (encoded.size() < 4 ||
                qFromBigEndian<quint32>(encoded.constData()) != static_cast<quint32>(expected))
                return false;
            pixels = qUncompress(encoded);
        } else {
            pixels = QByteArray(encoded.constData(), encoded.size());
        }
        if (pixels.size() != expected ||
            !window->applyRect(QRect(static_cast<int>(x), static_cast<int>(y),
                                     static_cast<int>(w), static_cast<int>(h)), pixels))
            return false;
        ++m_Stats.rects;
        m_Stats.encodedBytes += length;
        m_Stats.decodedBytes += static_cast<quint64>(expected);
    }
    if (offset != payload.size())
        return false;
    ++m_Stats.frames;
    window->endFrame();
    if (m_FramedWindows.insert(windowId).second) {
        qCInfo(lcSeamless).noquote()
            << QStringLiteral("first frame for window %1: %2x%3 scale %4, shown %5 at %6,%7")
                   .arg(windowId).arg(width).arg(height).arg(scale)
                   .arg(window->isVisible() ? QStringLiteral("yes") : QStringLiteral("no"))
                   .arg(window->x()).arg(window->y());
    }
    return true;
}

void SeamlessClientSession::launch(const QString& appId)
{
    sendInput(0, QJsonObject{
        {QStringLiteral("type"), QStringLiteral("launch")},
        {QStringLiteral("app"), appId.left(64)},
    });
}

void SeamlessClientSession::sendInput(quint32 windowId, const QJsonObject& input)
{
    sendRecord(SeamlessStream::ClientInput, windowId,
               QJsonDocument(input).toJson(QJsonDocument::Compact));
}

void SeamlessClientSession::sendRecord(quint16 type, quint32 windowId, const QByteArray& payload)
{
    if (m_Finished || m_Transport.state() != QProcess::Running)
        return;
    m_Transport.write(SeamlessStream::encodeRecord(type, windowId, payload));
}

void SeamlessClientSession::fail(const QString& reason)
{
    if (m_Finished)
        return;
    m_Finished = true;
    qCInfo(lcSeamless).noquote() << "session finished:" << reason;
    m_Registry.clear();
    if (m_Transport.state() != QProcess::NotRunning) {
        m_Transport.closeWriteChannel();
        if (!m_Transport.waitForFinished(2000))
            m_Transport.terminate();
    }
    emit finished(reason);
}
