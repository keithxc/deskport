#pragma once
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSslCertificate>
#include <QUuid>

namespace PeerStore {
inline bool write(const QString& path, const QJsonObject& object) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    const auto bytes = QJsonDocument(object).toJson();
    return file.write(bytes) == bytes.size() && file.commit();
}
inline QJsonObject read(const QString& path, bool* ok = nullptr) {
    QFile file(path);
    if (!file.exists()) { if (ok) *ok = true; return {}; }
    if (!file.open(QIODevice::ReadOnly)) { if (ok) *ok = false; return {}; }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (ok) *ok = error.error == QJsonParseError::NoError && doc.isObject();
    return doc.object();
}
// The caller holds the host instance lock and has stopped its Sunshine process.
// Preserve unrelated clients and all unrelated top-level state.
inline bool trust(const QString& path, const QString& id, const QString& name,
                  const QSslCertificate& certificate, bool remove = false) {
    if (id.isEmpty() || (!remove && certificate.isNull())) return false;
    bool ok;
    auto state = read(path, &ok);
    if (!ok) return false;
    auto root = state["root"].toObject();
    if (root["uniqueid"].toString().isEmpty())
        root["uniqueid"] = QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper();
    const auto old = root["named_devices"];
    if (!old.isUndefined() && !old.isArray() && old.toString() != "") return false;
    QJsonArray devices;
    for (const auto& item : old.toArray()) {
        const auto device = item.toObject();
        const bool sameCertificate = !remove && QSslCertificate(device["cert"].toString().toUtf8()) == certificate;
        if (device["uuid"].toString() != id && !sameCertificate) devices.append(item);
    }
    if (!remove) devices.append(QJsonObject{{"uuid", id}, {"name", name},
        {"cert", QString::fromUtf8(certificate.toPem())}, {"enabled", "true"}});
    root["named_devices"] = devices;
    state["root"] = root;
    return write(path, state);
}
}
