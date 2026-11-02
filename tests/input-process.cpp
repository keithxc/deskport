// SPDX-License-Identifier: GPL-3.0-or-later
#include "host/common/inputprocess.h"
#include <cassert>
#include <cstdio>
#include <vector>

using namespace deskport::input_process;

static std::vector<std::uint8_t> packet(std::uint32_t magic, std::size_t extra = 4) {
    std::vector<std::uint8_t> bytes {0, 0, 0, std::uint8_t(4 + extra), std::uint8_t(magic), std::uint8_t(magic >> 8),
                                     std::uint8_t(magic >> 16), std::uint8_t(magic >> 24)};
    bytes.resize(bytes.size() + extra, 0x5a);
    return bytes;
}

int main() {
    // Keyboard, text, mouse and scroll go to the helper; gamepad, touch, pen stay.
    for (std::uint32_t magic : {0x03u, 0x04u, 0x17u, 0x05u, 0x07u, 0x08u, 0x09u, 0x0Au, 0x55000001u}) {
        const auto bytes = packet(magic);
        assert(desktopPacket(bytes.data(), bytes.size()));
    }
    for (std::uint32_t magic : {0x0Cu, 0x55000002u, 0x55000003u, 0x55000004u, 0x55000005u, 0x0u}) {
        const auto bytes = packet(magic);
        assert(!desktopPacket(bytes.data(), bytes.size()));
    }
    assert(!desktopPacket(nullptr, 8));
    const auto shortPacket = packet(0x03);
    assert(!desktopPacket(shortPacket.data(), 7));

    // Framing survives arbitrary chunking and keeps message order.
    std::vector<std::uint8_t> stream;
    const auto key = packet(0x03, 6);
    const std::uint8_t port[12] {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    for (auto part : {frame(Message::packet, key.data(), key.size()), frame(Message::touchPort, port, sizeof(port)),
                      frame(Message::reset, nullptr, 0)})
        stream.insert(stream.end(), part.begin(), part.end());
    Reader reader;
    std::vector<std::pair<Message, std::vector<std::uint8_t>>> received;
    for (std::size_t i = 0; i < stream.size(); ++i) {
        reader.feed(&stream[i], 1);
        while (auto message = reader.next()) received.push_back(std::move(*message));
    }
    assert(received.size() == 3 && !reader.broken());
    assert(received[0].first == Message::packet && received[0].second == key);
    assert(received[1].first == Message::touchPort && received[1].second.size() == sizeof(port));
    assert(received[2].first == Message::reset && received[2].second.empty());

    // Oversized payloads are refused when framing and rejected when parsing.
    std::vector<std::uint8_t> huge(kMaxPayload + 1);
    assert(frame(Message::packet, huge.data(), huge.size()).empty());
    const std::uint8_t badLength[] {1, 0xff, 0xff, 0xff, 0x7f};
    Reader oversized;
    oversized.feed(badLength, sizeof(badLength));
    assert(!oversized.next() && oversized.broken());
    const std::uint8_t badType[] {9, 0, 0, 0, 0};
    Reader unknown;
    unknown.feed(badType, sizeof(badType));
    assert(!unknown.next() && unknown.broken());
    std::puts("PASS: desktop packet routing, chunked framing, order and malformed channel");
    return 0;
}
