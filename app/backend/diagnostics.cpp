#include "inputtestprofile.h"
#include "diagnostics.h"
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QDateTime>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QUrlQuery>
#include <QUuid>
#include <QRegularExpression>
#include <QDataStream>
#include <QTimer>
#include <Limelight.h>

namespace {
const QStringList sources {"client", "host", "display"};
const QStringList stages {"decoder-setup", "continuation", "mode-request", "mode-ready", "mode-failed", "observed", "stop-begin", "viewer-geometry-ready", "probe-begin", "probe-end", "stop-end", "resume-request", "resume-response", "initialize-begin", "input-init-begin", "input-init-end", "first-render-submit", "snapshot-ready", "snapshot-skipped-hdr", "snapshot-transfer-failed", "snapshot-scale-failed"};
const QStringList failureStages {"session-prepare", "virtual-display", "stream-launch", "capture", "encoder"};
const QStringList failureCodes {"cancelled", "unauthorized", "busy", "stale", "cycle", "topology-unsupported", "topology-unavailable", "topology-invalid", "unavailable", "timeout", "display-failed", "policy-unsupported", "capture-encoder-failed", "capture-permission", "encoder-unavailable"};
const QStringList runtimeBackends {"kwin", "portal", "wlr", "screen-capture-kit", "vaapi", "videotoolbox", "software", "nvenc", "amdvce", "quicksync"};
const QStringList navigationStages {"devices-request", "devices-ready", "devices-frame", "event-loop-delay"};
const QStringList events {"failure","runtime","navigation","started", "stopped", "failed", "timeout", "connection", "display", "encoder", "decoder", "input", "capture", "recovery", "permission", "resize", "enabled", "network", "rate", "encoded"};
// Integer measurements only; each record type keeps its own fixed key set.
const QStringList networkKeys {"rtt_ms", "rtt_var_ms", "received_fps", "rendered_fps", "network_drop_permille", "pacer_drop_permille", "received_kbps"};
const QStringList rateKeys {"target_kbps", "rtt_ms", "baseline_ms", "ceiling_kbps", "floor_kbps", "resume_kbps"};
const QStringList encodedKeys {"encoded_kbps", "frames", "keyframes", "max_frame_bytes"};
bool qualityStrictRange(int minimum, int maximum, bool strict) {
    return minimum == 22 && maximum == (strict ? 26 : 51);
}
QString codecName(int format) {
    switch (format) {
    case VIDEO_FORMAT_H264:
    case VIDEO_FORMAT_H264_HIGH8_444: return "h264";
    case VIDEO_FORMAT_H265:
    case VIDEO_FORMAT_H265_MAIN10:
    case VIDEO_FORMAT_H265_REXT8_444:
    case VIDEO_FORMAT_H265_REXT10_444: return "hevc";
    case VIDEO_FORMAT_AV1_MAIN8:
    case VIDEO_FORMAT_AV1_MAIN10:
    case VIDEO_FORMAT_AV1_HIGH8_444:
    case VIDEO_FORMAT_AV1_HIGH10_444: return "av1";
    default: return {};
    }
}
QStringList names() {
    QStringList result;
    for (const auto& source : sources) for (int i = 0; i < 3; ++i)
        result << source + QString("-%1.jsonl").arg(i);
    return result;
}
QFileInfoList viewerDirectories(const QString& root) {
    const QString path=root+"/viewers";
    if (QFileInfo(path).isSymLink()) return {};
    QFileInfoList result;
    for (const auto& directory : QDir(path).entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks,QDir::Time))
        if (QRegularExpression("^[a-f0-9]{32}$").match(directory.fileName()).hasMatch()) result.append(directory);
    return result;
}
void cleanViewers(const QString& root, bool all=false) {
    const auto cutoff=QDateTime::currentDateTimeUtc().addDays(-7);
    for (const auto& directory : viewerDirectories(root)) {
        for (int i=0;i<3;++i) {
            QFileInfo file(directory.filePath()+QString("/client-%1.jsonl").arg(i));
            if (all || file.isSymLink() || file.lastModified()<cutoff) QFile::remove(file.filePath());
        }
        QDir().rmdir(directory.filePath()); // Remove only an empty known run directory.
    }
}
QString platform() {
#ifdef Q_OS_MACOS
    return "macOS";
#elif defined(Q_OS_WIN)
    return "Windows";
#else
    return "Linux";
#endif
}
// ZIP store method: small bounded text files, no external archiver or directory traversal.
quint32 crc32(const QByteArray& data) {
    quint32 crc = 0xffffffff;
    for (unsigned char c : data) { crc ^= c; for (int i=0; i<8; ++i) crc = (crc >> 1) ^ (0xedb88320 & (0u - (crc & 1))); }
    return ~crc;
}
QByteArray zip(const QMap<QString, QByteArray>& files) {
    QByteArray out, central;
    QDataStream local(&out,QIODevice::WriteOnly), cd(&central,QIODevice::WriteOnly);
    local.setByteOrder(QDataStream::LittleEndian); cd.setByteOrder(QDataStream::LittleEndian);
    for (auto it=files.begin(); it!=files.end(); ++it) {
        const QByteArray name=it.key().toUtf8(), data=it.value();
        const quint32 offset=out.size(), crc=crc32(data), size=data.size();
        local << quint32(0x04034b50) << quint16(20) << quint16(0) << quint16(0) << quint16(0) << quint16(33) << crc << size << size << quint16(name.size()) << quint16(0);
        local.writeRawData(name.data(),name.size()); local.writeRawData(data.data(),data.size());
        cd << quint32(0x02014b50) << quint16(20) << quint16(20) << quint16(0) << quint16(0) << quint16(0) << quint16(33) << crc << size << size << quint16(name.size()) << quint16(0) << quint16(0) << quint16(0) << quint16(0) << quint32(0) << offset;
        cd.writeRawData(name.data(),name.size());
    }
    const quint32 offset=out.size();
    local.writeRawData(central.data(),central.size());
    local << quint32(0x06054b50) << quint16(0) << quint16(0) << quint16(files.size()) << quint16(files.size()) << quint32(central.size()) << offset << quint16(0);
    return out;
}
}
Diagnostics& Diagnostics::instance() { static Diagnostics value; return value; }
Diagnostics::Diagnostics(QObject* parent, const QString& directory) : QObject(parent),
    m_Directory(directory.isEmpty() ? InputTestProfile::data()+"/diagnostics" : directory),
    m_Run(QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-')),
    m_Enabled(directory.isEmpty() && QSettings().value("diagnostics/enabled",true).toBool()) {
    // Each media worker has an independent run; never rotate the shell's log
    // files concurrently from multiple processes.
    if (directory.isEmpty() && qEnvironmentVariableIsSet("DESKPORT_SESSION_ENDPOINT"))
        m_Directory += "/viewers/" + m_Run;
    m_Time.start();
    prune();
    cleanViewers(m_Directory);
    startMaintenance();
}
void Diagnostics::startMaintenance() {
    if (m_MaintenanceStarted || !QCoreApplication::instance()) return;
    m_MaintenanceStarted=true;
    auto timer=new QTimer(this);
    timer->setInterval(60*60*1000);
    connect(timer,&QTimer::timeout,this,[this] { QMutexLocker lock(&m_Mutex); prune(); cleanViewers(m_Directory); });
    timer->start();
}
bool Diagnostics::enabled() const { QMutexLocker lock(&m_Mutex); return m_Enabled; }
void Diagnostics::setEnabled(bool value) {
    { QMutexLocker lock(&m_Mutex); m_Enabled=value; m_Pending.clear();
      QSettings().setValue("diagnostics/enabled",value); }
    if (value) record("client","diagnostics enabled");
    emit changed();
}
// Fail closed: retain fixed event classes and tightly matched numeric timing records,
// never arbitrary log text. Unknown formats lose detail instead of leaking identities.
QJsonObject Diagnostics::project(const QString& message) {
    if (message.size()>16384) return {};
    const auto lower=message.toLower();
    // Do not fingerprint or persist private payloads, even when mixed with an error.
    for (const auto& word : {"clipboard", "keycode", "keystroke", "keydown", "keyup", "key text", "text input", "password", "token", "secret", "private key", "rikey"})
        if (lower.contains(QLatin1String(word))) return {};
    static const QRegularExpression failure("DeskPort failure stage=([a-z-]+) code=([a-z-]+)\\s*$");
    auto structured = failure.match(message);
    if (structured.hasMatch() && failureStages.contains(structured.captured(1)) && failureCodes.contains(structured.captured(2)))
        return {{"event", "failure"}, {"stage", structured.captured(1)}, {"code", structured.captured(2)}};
    static const QRegularExpression runtime("DeskPort runtime kind=(capture|encoder) backend=([a-z-]+)\\s*$");
    structured = runtime.match(message);
    if (structured.hasMatch() && runtimeBackends.contains(structured.captured(2)))
        return {{"event", "runtime"}, {"kind", structured.captured(1)}, {"backend", structured.captured(2)}};
    static const QRegularExpression encoder("Found (?:H\\.264|HEVC|AV1) encoder: [a-z0-9_]{1,32} \\[([a-z-]+)\\]\\s*$");
    structured = encoder.match(message);
    if (structured.hasMatch() && runtimeBackends.contains(structured.captured(1)))
        return {{"event", "runtime"}, {"kind", "encoder"}, {"backend", structured.captured(1)}};
    // Fixed upstream signatures become local codes; no suffix is ever retained.
    if (message.contains("No screen capture permission"))
        return {{"event", "failure"}, {"stage", "capture"}, {"code", "capture-permission"}};
    if (message.contains("Video failed to find working encoder"))
        return {{"event", "failure"}, {"stage", "encoder"}, {"code", "encoder-unavailable"}};
    // This callback reports the selected stream, unlike decoder probing or
    // saved preferences. Preserve only known codec IDs and bounded integers.
    static const QRegularExpression decoder("Video stream is ([0-9]{1,5})x([0-9]{1,5})x([0-9]{1,4}) \\(format 0x([0-9a-fA-F]{1,8})\\)\\s*$");
    auto match = decoder.match(message);
    if (match.hasMatch()) {
        bool validFormat = false;
        const auto format = match.captured(4).toInt(&validFormat, 16);
        const auto codec = validFormat ? codecName(format) : QString();
        const int width = match.captured(1).toInt(), height = match.captured(2).toInt(), fps = match.captured(3).toInt();
        if (!codec.isEmpty() && width > 0 && width <= 65535 && height > 0 && height <= 65535 && fps > 0 && fps <= 1000)
            return {{"event", "decoder"}, {"codec", codec}, {"video_format", format},
                    {"width", width}, {"height", height}, {"fps", fps}};
        return {};
    }
    static const QRegularExpression resize("DeskPort resize stage=([a-z-]+) tick_ms=([0-9]{1,10}) width=([0-9]{1,5}) height=([0-9]{1,5})\\s*$");
    match=resize.match(message);
    if (match.hasMatch() && stages.contains(match.captured(1))) {
        return {{"event","resize"},{"stage",match.captured(1)}, {"tick_ms",match.captured(2).toDouble()},
            {"width",match.captured(3).toInt()},{"height",match.captured(4).toInt()}};
    }
    static const QRegularExpression navigation("DeskPort navigation stage=([a-z-]+) duration_ms=([0-9]{1,10})\\s*$");
    match = navigation.match(message);
    if (match.hasMatch() && navigationStages.contains(match.captured(1)))
        return {{"event", "navigation"}, {"stage", match.captured(1)}, {"duration_ms", match.captured(2).toDouble()}};
    static const QRegularExpression network("DeskPort network rtt_ms=([0-9]{1,6}) rtt_var_ms=([0-9]{1,6}) received_fps=([0-9]{1,4}) rendered_fps=([0-9]{1,4}) network_drop_permille=([0-9]{1,4}) pacer_drop_permille=([0-9]{1,4}) received_kbps=([0-9]{1,8})\\s*$");
    match = network.match(message);
    if (match.hasMatch()) {
        QJsonObject record {{"event", "network"}};
        for (int i = 0; i < networkKeys.size(); ++i) record[networkKeys[i]] = match.captured(i + 1).toDouble();
        return record;
    }
    static const QRegularExpression path("DeskPort network path: (remote|local)\\s*$");
    match = path.match(message);
    if (match.hasMatch()) return {{"event", "network"}, {"path", match.captured(1)}};
    static const QRegularExpression liveQuality("DeskPort live quality: min_qp=([0-9]{1,2}), max_qp=([0-9]{1,2}), strict=(on|off)\\s*$");
    match = liveQuality.match(message);
    if (match.hasMatch()) {
        const int minimum = match.captured(1).toInt(), maximum = match.captured(2).toInt();
        const bool strict = match.captured(3) == "on";
        if (!qualityStrictRange(minimum, maximum, strict)) return {};
        return {{"event", "rate"}, {"min_qp", minimum}, {"max_qp", maximum}, {"quality_strict", strict}};
    }
    static const QRegularExpression liveRate("DeskPort live rate: target_kbps=([0-9]{1,8}), rtt_ms=([0-9]{1,6}), quality_floor=(on|off), saver=(on|off)(?:, min_qp=([0-9]{1,2})(?:, max_qp=([0-9]{1,2}))?)?\\s*$");
    match = liveRate.match(message);
    if (match.hasMatch()) {
        QJsonObject record {{"event", "rate"}, {"target_kbps", match.captured(1).toDouble()}, {"rtt_ms", match.captured(2).toDouble()},
                            {"quality_floor", match.captured(3)}, {"saver", match.captured(4) == "on"}};
        if (!match.captured(5).isEmpty()) {
            const int qp = match.captured(5).toInt();
            if (qp > 51) return {};
            record["min_qp"] = qp;
        }
        if (!match.captured(6).isEmpty()) {
            const int qp = match.captured(6).toInt();
            if (qp > 51 || qp < record.value("min_qp").toInt()) return {};
            record["max_qp"] = qp;
        }
        return record;
    }
    static const QRegularExpression rate("DeskPort remote rate: target_kbps=([0-9]{1,8}), rtt_ms=([0-9]{1,6}), baseline_ms=([0-9]{1,6}), quality_floor=(on|off)\\s*$");
    match = rate.match(message);
    if (match.hasMatch()) {
        return {{"event", "rate"}, {"target_kbps", match.captured(1).toDouble()}, {"rtt_ms", match.captured(2).toDouble()},
                {"baseline_ms", match.captured(3).toDouble()}, {"quality_floor", match.captured(4)}};
    }
    static const QRegularExpression rateStart("DeskPort remote rate control: ceiling_kbps=([0-9]{1,8}), floor_kbps=([0-9]{1,8}), resume_kbps=([0-9]{1,8}), live_encoder=(yes|no)\\s*$");
    match = rateStart.match(message);
    if (match.hasMatch())
        return {{"event", "rate"}, {"ceiling_kbps", match.captured(1).toDouble()}, {"floor_kbps", match.captured(2).toDouble()},
                {"resume_kbps", match.captured(3).toDouble()}, {"live_encoder", match.captured(4)}};
    static const QRegularExpression liveCeiling("DeskPort live ceiling: kbps=([0-9]{1,8}), permille=([0-9]{1,4})\\s*$");
    match = liveCeiling.match(message);
    if (match.hasMatch())
        return {{"event", "rate"}, {"ceiling_kbps", match.captured(1).toDouble()}, {"live_encoder", "yes"}};
    static const QRegularExpression encoded("DeskPort encoded video: Mbps=([0-9]{1,6})(?:\\.([0-9]{1,9}))?, packets=([0-9]{1,9}), keyframes=([0-9]{1,9}), max_packet_bytes=([0-9]{1,10}) \\(before transport/FEC\\)\\s*$");
    match = encoded.match(message);
    if (match.hasMatch()) {
        const double mbps = (match.captured(1) + "." + (match.captured(2).isEmpty() ? QString("0") : match.captured(2))).toDouble();
        return {{"event", "encoded"}, {"encoded_kbps", double(qRound64(mbps * 1000))},
                {"frames", match.captured(3).toDouble()}, {"keyframes", match.captured(4).toDouble()},
                {"max_frame_bytes", match.captured(5).toDouble()}};
    }
    const QList<QPair<QString,QString>> classes {
        {"diagnostics enabled","enabled"},{"permission","permission"},{"timeout","timeout"},
        {"failed","failed"},{"error","failed"},{"recovery","recovery"},{"stopped","stopped"},
        {"started","started"},{"connect","connection"},{"encoder","encoder"},
        {"decoder","decoder"},{"capture","capture"},{"display","display"},{"input-init","input"}};
    for (const auto& entry : classes) if (lower.contains(entry.first)) return {{"event",entry.second}};
    return {};
}
QJsonObject Diagnostics::validate(const QJsonObject& o) {
    if (!events.contains(o.value("event").toString()) || !sources.contains(o.value("source").toString())) return {};
    static const QRegularExpression id("^[a-f0-9]{32}$");
    if (!id.match(o.value("run").toString()).hasMatch()) return {};
    QJsonObject result {{"event",o.value("event")},{"source",o.value("source")},{"run",o.value("run")}};
    for (const auto& key : {"elapsed_ms","tick_ms","width","height","duration_ms"}) {
        auto v=o.value(QLatin1String(key));
        if (v.isDouble() && v.toDouble()>=0 && v.toDouble()<=4294967295.0 && v.toDouble()==double(quint64(v.toDouble()))) result[QLatin1String(key)]=v;
    }
    for (const auto& key : {"phase","error"}) {
        auto v=o.value(QLatin1String(key));
        if (v.isDouble() && v.toDouble()>=-2147483648.0 && v.toDouble()<=2147483647.0 && v.toDouble()==double(v.toInt())) result[QLatin1String(key)]=v;
    }
    if (QStringList{"starting","failed","terminated"}.contains(o.value("state").toString())) result["state"]=o.value("state");
    const auto event = o.value("event").toString();
    const QStringList& measured = event == "network" ? networkKeys : event == "rate" ? rateKeys : event == "encoded" ? encodedKeys : QStringList();
    for (const auto& key : measured) {
        auto v=o.value(key);
        if (v.isDouble() && v.toDouble()>=0 && v.toDouble()<=4294967295.0 && v.toDouble()==double(quint64(v.toDouble()))) result[key]=v;
    }
    if (event == "network" && QStringList{"remote","local"}.contains(o.value("path").toString())) result["path"]=o.value("path");
    if (event == "rate" && QStringList{"on","off"}.contains(o.value("quality_floor").toString())) result["quality_floor"]=o.value("quality_floor");
    if (event == "rate" && o.value("saver").isBool()) result["saver"]=o.value("saver");
    if (event == "rate") {
        for (const auto& key : {"min_qp", "max_qp"}) if (o.contains(key)) {
            const auto qp = o.value(key);
            if (!qp.isDouble() || qp.toDouble() < 0 || qp.toDouble() > 51 || qp.toDouble() != qp.toInt()) return {};
            result[key] = qp;
        }
        if (result.contains("min_qp") && result.contains("max_qp") &&
            result.value("min_qp").toInt() > result.value("max_qp").toInt()) return {};
        if (o.contains("quality_strict")) {
            const auto strict = o.value("quality_strict");
            if (!strict.isBool() || !result.contains("min_qp") || !result.contains("max_qp") ||
                !qualityStrictRange(result.value("min_qp").toInt(), result.value("max_qp").toInt(), strict.toBool())) return {};
            result["quality_strict"] = strict;
        }
    }
    if (event == "rate" && QStringList{"yes","no"}.contains(o.value("live_encoder").toString())) result["live_encoder"]=o.value("live_encoder");
    if (event == "decoder" && (o.contains("codec") || o.contains("video_format") || o.contains("fps"))) {
        const auto format = o.value("video_format"), fps = o.value("fps");
        if (!format.isDouble() || format.toDouble() < 1 || format.toDouble() > 65535 ||
            format.toDouble() != format.toInt() || codecName(format.toInt()).isEmpty() ||
            o.value("codec").toString() != codecName(format.toInt()) ||
            !fps.isDouble() || fps.toDouble() < 1 || fps.toDouble() > 1000 || fps.toDouble() != fps.toInt() ||
            result.value("width").toDouble() < 1 || result.value("width").toDouble() > 65535 ||
            result.value("height").toDouble() < 1 || result.value("height").toDouble() > 65535) return {};
        result["codec"] = o.value("codec"); result["video_format"] = format; result["fps"] = fps;
    }
    if (!result.contains("elapsed_ms")) return {};
    if (stages.contains(o.value("stage").toString()) ||
        (o.value("event") == "navigation" && navigationStages.contains(o.value("stage").toString()))) result["stage"]=o.value("stage");
    if (o.value("event") == "failure") {
        if (!failureStages.contains(o.value("stage").toString()) || !failureCodes.contains(o.value("code").toString())) return {};
        result["stage"] = o.value("stage"); result["code"] = o.value("code");
    }
    if (o.value("event") == "runtime") {
        if (!QStringList{"capture", "encoder"}.contains(o.value("kind").toString()) || !runtimeBackends.contains(o.value("backend").toString())) return {};
        result["kind"] = o.value("kind"); result["backend"] = o.value("backend");
    }
    return result;
}
void Diagnostics::ingest(const QString& source, const QByteArray& bytes) {
    QMutexLocker lock(&m_Mutex);
    if (!m_Enabled || !sources.contains(source)) return;
    auto& pending=m_Pending[source];
    // Bound partial lines; discard oversized records through the next newline.
    for (char c : bytes) {
        if (c=='\n') { if (pending.size()<=16384) write(source,project(QString::fromUtf8(pending))); pending.clear(); }
        else if (pending.size()<=16384) pending.append(c);
    }
}
void Diagnostics::record(const QString& source, const QString& message) {
    QMutexLocker lock(&m_Mutex);
    if (m_Enabled && sources.contains(source)) write(source,project(message));
}
void Diagnostics::connection(int phase, int error, const QString& state) {
    QMutexLocker lock(&m_Mutex);
    if (m_Enabled && phase>=0 && phase<=32 && QStringList{"starting","failed","terminated"}.contains(state))
        write("client",{{"event","connection"},{"phase",phase},{"error",error},{"state",state}});
}
void Diagnostics::prune() {
    if (QFileInfo(m_Directory).isSymLink()) return;
    const auto cutoff=QDateTime::currentDateTimeUtc().addDays(-7);
    for (const auto& name : names() + QStringList{"DeskPort-diagnostics.zip"}) {
        QFileInfo info(m_Directory+"/"+name);
        if (info.isSymLink() || (info.exists() && info.lastModified()<cutoff)) QFile::remove(info.filePath());
    }
}
void Diagnostics::write(const QString& source, QJsonObject event) {
    if (event.isEmpty() || QFileInfo(m_Directory).isSymLink()) return;
    if (!QDir().mkpath(m_Directory)) return;
    QFile::setPermissions(m_Directory,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    prune();
    event["source"]=source; event["run"]=m_Run; event["elapsed_ms"]=double(m_Time.elapsed());
    const auto line=QJsonDocument(validate(event)).toJson(QJsonDocument::Compact)+'\n';
    const auto base=m_Directory+"/"+source;
    const auto path=base+"-0.jsonl";
    if (QFileInfo(path).size()+line.size()>FileLimit) {
        QFile::remove(base+"-2.jsonl"); QFile::rename(base+"-1.jsonl",base+"-2.jsonl"); QFile::rename(path,base+"-1.jsonl");
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly|QIODevice::Append)) {
        file.setPermissions(QFile::ReadOwner|QFile::WriteOwner); file.write(line);
    }
}
QString Diagnostics::createBundle() {
    QMutexLocker lock(&m_Mutex);
    m_Status.clear(); m_Bundle.clear(); prune(); cleanViewers(m_Directory);
    if (QFileInfo(m_Directory).isSymLink()) { m_Status=tr("Cannot use the diagnostics directory."); return {}; }
    QMap<QString,QByteArray> files;
    for (const auto& name : names()) {
        QFileInfo info(m_Directory+"/"+name);
        if (!info.isFile() || info.isSymLink() || info.size()>FileLimit) continue;
        QFile file(info.filePath()); if (!file.open(QIODevice::ReadOnly)) continue;
        QByteArray data;
        const auto snapshot=file.read(FileLimit);
        auto lines=snapshot.split('\n');
        // The final fragment is incomplete (or empty after a trailing newline).
        lines.removeLast();
        for (const auto& line : lines) {
            if (line.size()>16384) continue;
            auto safe=validate(QJsonDocument::fromJson(line).object());
            if (!safe.isEmpty()) data+=QJsonDocument(safe).toJson(QJsonDocument::Compact)+'\n';
        }
        if (!data.isEmpty()) files.insert(name,data);
    }
    const auto directories=viewerDirectories(m_Directory);
    int viewerIndex=0;
    for (const auto& directory : directories) {
        if (viewerIndex>=8) break;
        if (!QRegularExpression("^[a-f0-9]{32}$").match(directory.fileName()).hasMatch()) continue;
        for (int i=0;i<3;++i) {
            QFileInfo info(directory.filePath()+QString("/client-%1.jsonl").arg(i));
            if (!info.isFile() || info.isSymLink() || info.size()>FileLimit) continue;
            QFile file(info.filePath()); if(!file.open(QIODevice::ReadOnly))continue;
            auto lines=file.read(FileLimit).split('\n'); lines.removeLast(); QByteArray data;
            for(const auto& line:lines) {
                if(line.size()>16384)continue;
                auto safe=validate(QJsonDocument::fromJson(line).object());
                if(!safe.isEmpty())data+=QJsonDocument(safe).toJson(QJsonDocument::Compact)+'\n';
            }
            if(!data.isEmpty())files.insert(QString("viewer-%1-client-%2.jsonl").arg(viewerIndex).arg(i),data);
        }
        ++viewerIndex;
    }
    // Build version is compile-time product metadata, not host/environment inventory.
    QString version=QCoreApplication::applicationVersion();
    if (version.size() > 64 || !QRegularExpression("^[0-9]+(?:\\.[0-9]+){1,3}(?:-[A-Za-z0-9]+(?:[.-][A-Za-z0-9]+)*)?\\z").match(version).hasMatch()) version="development";
    files["manifest.json"]=QJsonDocument(QJsonObject{{"schema",1},{"version",version},{"platform",platform()},
        {"diagnostics_enabled",m_Enabled},{"log_files",files.size()}}).toJson();
    files["README.txt"]="DeskPort diagnostics: event classes, relative timing, resize dimensions and random run IDs only. Unknown messages are omitted. No configuration, pairing, keys, clipboard, key text, screen or raw error text is collected. Review before attaching to a PUBLIC GitHub issue. Nothing has been uploaded.\n";
    if (!QDir().mkpath(m_Directory)) { m_Status=tr("Cannot create the diagnostics directory."); return {}; }
    QFile::setPermissions(m_Directory,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    // One bounded export, replaced only by an explicit subsequent export.
    const QString target=m_Directory+"/DeskPort-diagnostics.zip";
    if (QFileInfo(target).isSymLink()) { m_Status=tr("Cannot replace the diagnostics archive."); return {}; }
    QSaveFile output(target);
    if (!output.open(QIODevice::WriteOnly)) { m_Status=tr("Cannot write the diagnostics archive."); return {}; }
    output.setPermissions(QFile::ReadOwner|QFile::WriteOwner);
    const auto archive=zip(files);
    if (output.write(archive)!=archive.size() || !output.commit()) { m_Status=tr("Cannot save the diagnostics archive."); return {}; }
    m_Bundle=target;
    m_Status=files.size()==2 ? tr("Archive contains version information only. Enable logs and reproduce the problem to collect events.") : tr("Archive ready. Review it, then drag the ZIP into your public GitHub issue. Nothing is uploaded automatically.");
    return target;
}
QUrl Diagnostics::issueUrl() {
    QUrl url("https://github.com/keithxc/deskport/issues/new"); QUrlQuery query;
    query.addQueryItem("title","[Feedback] ");
    query.addQueryItem("body","## What happened?\n\n## Steps to reproduce\n\n## Expected behavior\n\n## Diagnostics\nReview the locally generated DeskPort-diagnostics.zip, then drag it here to attach. This issue and its attachments are PUBLIC. Do not include credentials or private content. DeskPort does not upload or attach files automatically.\n");
    url.setQuery(query); return url;
}
bool Diagnostics::feedback() {
    const bool ready=!createBundle().isEmpty();
    if (ready && !QDesktopServices::openUrl(issueUrl())) m_Status+=tr(" Could not open the browser. Open github.com/keithxc/deskport/issues/new manually.");
    emit changed(); return ready;
}
void Diagnostics::showBundle() { if (!m_Bundle.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_Directory)); }
void Diagnostics::clear() {
    { QMutexLocker lock(&m_Mutex);
      if (!QFileInfo(m_Directory).isSymLink()) { for (const auto& name : names()) QFile::remove(m_Directory+"/"+name); QFile::remove(m_Directory+"/DeskPort-diagnostics.zip"); cleanViewers(m_Directory,true); }
      m_Bundle.clear(); m_Pending.clear(); m_Status=tr("Saved diagnostics cleared."); }
    emit changed();
}
