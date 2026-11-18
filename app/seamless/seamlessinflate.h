// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "seamlessstream.h"
#include <limits>
#include <utility>
#include <zlib.h>

namespace SeamlessStream {
constexpr quint32 MaxFrameDimension = 16384;
constexpr quint64 MaxFramePixels = 64ULL * 1024 * 1024;
constexpr quint64 MaxDecodedRectBytes = MaxFramePixels * 4;

inline bool rectByteCount(quint32 width, quint32 height, qsizetype* bytes)
{
    // Widen before multiplication; check pixels before multiplying by bpp.
    const quint64 pixels = quint64(width) * height;
    if (!width || !height || width > MaxFrameDimension || height > MaxFrameDimension ||
        pixels > MaxFramePixels || pixels > quint64(std::numeric_limits<qsizetype>::max()) / 4)
        return false;
    *bytes = qsizetype(pixels * 4);
    return true;
}

inline bool inflateRect(const QByteArray& encoded, qsizetype expected, QByteArray* pixels)
{
    pixels->clear();
    if (expected <= 0 || quint64(expected) > MaxDecodedRectBytes ||
        encoded.size() <= 4 || encoded.size() > MaxPayload ||
        qFromBigEndian<quint32>(encoded.constData()) != quint64(expected))
        return false;
    // Exactly one allocation, bounded by validated geometry, never the prefix
    // or zlib's claimed expansion. inflate cannot resize this output buffer.
    QByteArray decoded(expected, Qt::Uninitialized);
    z_stream stream {};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(encoded.constData() + 4));
    stream.avail_in = static_cast<uInt>(encoded.size() - 4);
    stream.next_out = reinterpret_cast<Bytef*>(decoded.data());
    stream.avail_out = static_cast<uInt>(expected);
    if (::inflateInit(&stream) != Z_OK)
        return false;
    const int result = ::inflate(&stream, Z_FINISH);
    const bool valid = result == Z_STREAM_END && stream.total_out == quint64(expected) &&
                       stream.avail_in == 0;
    ::inflateEnd(&stream);
    if (!valid)
        return false;
    *pixels = std::move(decoded);
    return true;
}
} // namespace SeamlessStream
