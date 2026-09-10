#pragma once
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QStringList>

// Reproducible packages may retain the same resource timestamps and app version
// across updates. Key the disk cache by QML content, not those timestamps.
inline QString qmlCacheKey(const QString& directory) {
    QStringList paths;
    QDirIterator files(directory, {"*.qml", "*.js"}, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) paths.append(files.next());
    paths.sort();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const auto& path : paths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        hash.addData(path.mid(directory.size()).toUtf8());
        hash.addData(QByteArray(1, '\0'));
        hash.addData(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256));
    }
    return QString::fromLatin1(hash.result().toHex());
}
