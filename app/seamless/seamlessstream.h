// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Experimental Seamless debug stream (version 1).
//
// This framing is transport-agnostic: the first debug transport runs the
// sidecar through an already-authenticated SSH session and uses its stdin and
// stdout. A later admitted DeskPort media channel can carry the same records.
//
// Every record starts with a fixed 16-byte little-endian header:
//
//   u32 magic    'DPSS'
//   u16 version  1
//   u16 type     RecordType
//   u32 windowId 0 for session-scoped records
//   u32 length   payload bytes that follow (bounded by MaxPayload)
//
// Session/lifecycle records carry one compact JSON object; frames carry the
// binary payload described by FrameHeader/FrameRect. No pixel data is ever
// placed inside JSON.

#include <QByteArray>
#include <QtEndian>
#include <QtGlobal>

#include <cstring>

namespace SeamlessStream {

constexpr quint32 Magic = 0x53535044; // "DPSS" in little-endian byte order
constexpr quint16 Version = 1;
constexpr int HeaderSize = 16;
constexpr quint32 MaxPayload = 64u * 1024u * 1024u;
constexpr quint32 MaxJsonPayload = 32u * 1024u;
constexpr int MaxFrameRects = 64;

enum RecordType : quint16 {
    // Server to client
    ServerHello = 1,  // JSON
    ServerEvent = 2,  // JSON lifecycle event, same schema as sidecar stdout
    Frame = 3,        // binary FrameHeader + rectangles
    // Client to server
    ClientHello = 101, // JSON
    ClientInput = 102, // JSON input/configure/close/focus request
    ClientFrameAck = 103, // empty; releases one frame of backpressure credit
};

enum FrameEncoding : quint8 {
    EncodingRaw = 0,
    EncodingZlib = 1,
};

// Pixels are always 32-bit premultiplied ARGB in native little-endian memory
// order (B, G, R, A), matching QImage::Format_ARGB32_Premultiplied.
enum FramePixelFormat : quint8 {
    PixelArgb32Premultiplied = 0,
};

#pragma pack(push, 1)
struct RecordHeader {
    quint32 magic;
    quint16 version;
    quint16 type;
    quint32 windowId;
    quint32 length;
};

struct FrameHeader {
    quint64 sequence;
    quint32 width;      // full buffer width in pixels
    quint32 height;     // full buffer height in pixels
    quint16 bufferScale;
    quint16 rectCount;
    quint8 encoding;
    quint8 pixelFormat;
    quint16 reserved;
};

struct FrameRect {
    quint32 x;
    quint32 y;
    quint32 width;
    quint32 height;
    quint32 dataLength; // encoded bytes that follow this rectangle header
};
#pragma pack(pop)

static_assert(sizeof(RecordHeader) == HeaderSize, "unexpected record header size");
static_assert(sizeof(FrameHeader) == 24, "unexpected frame header size");
static_assert(sizeof(FrameRect) == 20, "unexpected frame rectangle size");

inline QByteArray encodeRecord(quint16 type, quint32 windowId, const QByteArray& payload)
{
    QByteArray record;
    record.resize(HeaderSize + payload.size());
    RecordHeader header;
    header.magic = qToLittleEndian(Magic);
    header.version = qToLittleEndian(Version);
    header.type = qToLittleEndian(type);
    header.windowId = qToLittleEndian(windowId);
    header.length = qToLittleEndian(static_cast<quint32>(payload.size()));
    std::memcpy(record.data(), &header, HeaderSize);
    if (!payload.isEmpty())
        std::memcpy(record.data() + HeaderSize, payload.constData(), payload.size());
    return record;
}

// Incremental decoder. Feed arbitrary byte chunks with append(); next() returns
// complete records in order. Any malformed header poisons the decoder.
class RecordReader {
public:
    struct Record {
        quint16 type = 0;
        quint32 windowId = 0;
        QByteArray payload;
    };

    void append(const QByteArray& bytes) { m_Buffer.append(bytes); }

    bool failed() const { return m_Failed; }
    const char* error() const { return m_Error; }
    qsizetype buffered() const { return m_Buffer.size() - m_Offset; }

    bool next(Record* record)
    {
        if (m_Failed || buffered() < HeaderSize)
            return false;
        RecordHeader header;
        std::memcpy(&header, m_Buffer.constData() + m_Offset, HeaderSize);
        const quint32 magic = qFromLittleEndian(header.magic);
        const quint16 version = qFromLittleEndian(header.version);
        const quint32 length = qFromLittleEndian(header.length);
        if (magic != Magic)
            return fail("bad record magic");
        if (version != Version)
            return fail("unsupported record version");
        if (length > MaxPayload)
            return fail("record payload too large");
        if (buffered() < HeaderSize + static_cast<qsizetype>(length))
            return false;
        record->type = qFromLittleEndian(header.type);
        record->windowId = qFromLittleEndian(header.windowId);
        record->payload = m_Buffer.mid(m_Offset + HeaderSize, length);
        m_Offset += HeaderSize + length;
        if (m_Offset > 1024 * 1024 && m_Offset * 2 > m_Buffer.size()) {
            m_Buffer.remove(0, m_Offset);
            m_Offset = 0;
        }
        return true;
    }

private:
    bool fail(const char* message)
    {
        m_Failed = true;
        m_Error = message;
        m_Buffer.clear();
        m_Offset = 0;
        return false;
    }

    QByteArray m_Buffer;
    qsizetype m_Offset = 0;
    bool m_Failed = false;
    const char* m_Error = "";
};

} // namespace SeamlessStream
