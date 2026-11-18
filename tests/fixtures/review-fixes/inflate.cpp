// SPDX-License-Identifier: GPL-3.0-or-later
#include "seamlessinflate.h"
#include <QCoreApplication>
#include <QImage>
#include <QCryptographicHash>
#include <cassert>
#include <cstdio>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    using namespace SeamlessStream;
    QByteArray output;
    int cases = 0;
    const auto reject = [&](const QByteArray& encoded, qsizetype expected) {
        output = "sentinel";
        assert(!inflateRect(encoded, expected, &output));
        assert(output.isEmpty()); ++cases;
    };
    QByteArray normal(4, '\x42');
    assert(inflateRect(qCompress(normal), 4, &output) && output == normal); ++cases;
    reject(qCompress(normal), 0);
    reject(qCompress(normal), -1);
    reject(qCompress(normal), MaxDecodedRectBytes + 1);
    reject(qCompress(normal), 8); // forged/incorrect size prefix
    auto wrongPrefix = qCompress(normal); qToBigEndian<quint32>(8, wrongPrefix.data());
    reject(wrongPrefix, 8); // short actual output
    auto bomb = qCompress(QByteArray(8 * 1024 * 1024, 'x'), 9);
    qToBigEndian<quint32>(4, bomb.data());
    assert(bomb.size() < 40000); reject(bomb, 4);
    for (int n = 0; n < qCompress(normal).size(); ++n) reject(qCompress(normal).left(n), 4);
    reject(qCompress(normal) + "junk", 4);
    reject(qCompress(normal) + qCompress(normal).mid(4), 4); // concatenated streams
    auto corrupt = qCompress(normal); corrupt[corrupt.size()-1] ^= 1; reject(corrupt, 4);
    // Exact full output capacity, high-entropy normal image and every Qt level.
    QImage image(127, 65, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
        image.setPixel(x,y,qRgb(x*19%256,y*31%256,(x*y)%256));
    QByteArray raw(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes());
    for (int level = -1; level <= 9; ++level) {
        assert(inflateRect(qCompress(raw, level), raw.size(), &output) && output == raw); ++cases;
    }
    qsizetype bytes = -1;
    assert(rectByteCount(1,1,&bytes) && bytes == 4);
    assert(rectByteCount(16384,4096,&bytes) && bytes == qsizetype(MaxDecodedRectBytes));
    assert(!rectByteCount(16384,4097,&bytes));
    assert(!rectByteCount(0,1,&bytes));
    assert(!rectByteCount(0xffffffffu,0xffffffffu,&bytes));
    assert(!rectByteCount(1,16385,&bytes));
    // Max legal decoded rectangle (256 MiB), compressed input remains tiny.
    const QByteArray boundary(qsizetype(MaxDecodedRectBytes), '\0');
    assert(inflateRect(qCompress(boundary, 1), boundary.size(), &output) && output == boundary); ++cases;
    std::printf("PASS: %d fixed-capacity inflate cases + 6 geometry boundaries; bomb=%lld bytes, output capacity=4\n",cases,static_cast<long long>(bomb.size()));
}
