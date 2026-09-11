#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace DeskPortClipboard {
constexpr int MaxText = 1024 * 1024;
constexpr int MaxFrame = 1400000;
inline bool encode(const QString& text, QString& encoded) {
    const auto bytes = text.toUtf8();
    if (bytes.size() > MaxText || text.contains(QChar(0))) return false;
    encoded = QString::fromLatin1(bytes.toBase64());
    return true;
}
inline bool decode(const QJsonValue& value, QString& text) {
    if (!value.isString()) return false;
    const auto encoded = value.toString().toLatin1();
    if (encoded.size() > ((MaxText + 2) / 3) * 4) return false;
    const auto bytes = QByteArray::fromBase64(encoded);
    if (bytes.size() > MaxText || bytes.toBase64() != encoded || bytes.contains('\0')) return false;
    text = QString::fromUtf8(bytes);
    return text.toUtf8() == bytes;
}
}
