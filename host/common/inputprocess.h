// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace deskport::input_process {
// Desktop input (keyboard, text, mouse and scroll) is injected by a separate
// host process, so encoding and streaming can never delay a key release that
// Sunshine's own repeat timer would otherwise turn into repeated characters.
// Gamepads, touch and pen keep Sunshine's in-process path and feedback.
enum class Message : std::uint8_t { packet = 1, touchPort = 2, reset = 3 };
constexpr std::size_t kHeaderBytes = 5;
constexpr std::size_t kMaxPayload = 64 * 1024;

// Raw Moonlight input packet: big-endian size, then little-endian magic.
inline bool desktopPacket(const std::uint8_t *data, std::size_t size) {
    if (!data || size < 8) return false;
    const std::uint32_t magic = std::uint32_t(data[4]) | std::uint32_t(data[5]) << 8 |
                                std::uint32_t(data[6]) << 16 | std::uint32_t(data[7]) << 24;
    switch (magic) {
    case 0x00000003:  // KEY_DOWN_EVENT_MAGIC
    case 0x00000004:  // KEY_UP_EVENT_MAGIC
    case 0x00000017:  // UTF8_TEXT_EVENT_MAGIC
    case 0x00000005:  // MOUSE_MOVE_ABS_MAGIC
    case 0x00000007:  // MOUSE_MOVE_REL_MAGIC_GEN5
    case 0x00000008:  // MOUSE_BUTTON_DOWN_EVENT_MAGIC_GEN5
    case 0x00000009:  // MOUSE_BUTTON_UP_EVENT_MAGIC_GEN5
    case 0x0000000A:  // SCROLL_MAGIC_GEN5
    case 0x55000001:  // SS_HSCROLL_MAGIC
        return true;
    default:
        return false;
    }
}

// One message: type byte, little-endian 32-bit payload length, payload.
inline std::vector<std::uint8_t> frame(Message type, const void *data, std::size_t size) {
    std::vector<std::uint8_t> out;
    if (size > kMaxPayload || (size && !data)) return out;
    out.reserve(kHeaderBytes + size);
    out.push_back(std::uint8_t(type));
    for (int i = 0; i < 4; ++i) out.push_back(std::uint8_t(size >> (8 * i)));
    if (size) out.insert(out.end(), static_cast<const std::uint8_t *>(data), static_cast<const std::uint8_t *>(data) + size);
    return out;
}

// Incremental decoder for a byte stream. An unknown type or oversized length
// marks the stream broken; the receiver then stops instead of guessing.
class Reader {
public:
    void feed(const std::uint8_t *data, std::size_t size) { buffer.insert(buffer.end(), data, data + size); }
    bool broken() const { return failed; }
    std::optional<std::pair<Message, std::vector<std::uint8_t>>> next() {
        if (failed || buffer.size() - offset < kHeaderBytes) return std::nullopt;
        const std::uint8_t *head = buffer.data() + offset;
        const std::uint8_t type = head[0];
        std::size_t size = 0;
        for (int i = 0; i < 4; ++i) size |= std::size_t(head[1 + i]) << (8 * i);
        if (type < 1 || type > 3 || size > kMaxPayload) {
            failed = true;
            return std::nullopt;
        }
        if (buffer.size() - offset < kHeaderBytes + size) return std::nullopt;
        std::vector<std::uint8_t> payload(head + kHeaderBytes, head + kHeaderBytes + size);
        offset += kHeaderBytes + size;
        if (offset == buffer.size()) {
            buffer.clear();
            offset = 0;
        }
        return std::make_pair(Message(type), std::move(payload));
    }
private:
    std::vector<std::uint8_t> buffer;
    std::size_t offset = 0;
    bool failed = false;
};
}
