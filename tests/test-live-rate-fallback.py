#!/usr/bin/env python3
"""Exercise production viewer rate-result, startup check and reconnect budget.

No Qt window, transport, host connection or hardware is used. Only the mutex and
reply-value adapters are stubbed; the actual production statements are compiled.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
adaptive = (root / 'app/backend/adaptivedisplay.cpp').read_text()
session = (root / 'app/streaming/session.cpp').read_text()

def block(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth = 0
    for i in range(opening, len(source)):
        if source[i] == '{': depth += 1
        if source[i] == '}': depth -= 1
        if depth == 0: return source[start:i + 1]
    raise AssertionError(marker)

result = block(adaptive, 'if (!reply["live"].toBool())')
live = block(adaptive, 'bool AdaptiveDisplay::liveRateSupported()')
fallback = block(adaptive, 'bool AdaptiveDisplay::takeRateFallback()')
initial = block(session, 'if (!initialRateChecked)')
begin = block(adaptive, 'void AdaptiveDisplay::beginRateStream(')
ready = block(adaptive, 'void AdaptiveDisplay::markRateStreamReady(')
set_rate = block(adaptive, 'void AdaptiveDisplay::setRateCeiling(')
pending = block(adaptive, '[&] { QMutexLocker lock(&m_Mutex); return m_RateSupported && m_RateReady')
assert 'if (!m_RateSupported || !m_RateReady || m_RateCeiling <= 0) continue;' in adaptive
assert 'm_AdaptiveDisplay->beginRateStream(m_RateCapKbps, m_Preferences->dataSaver);' in session
start = session.index('    m_StreamConfig.bitrate = DeskPortStream::sessionBitrate(')
end = session.index(');', start) + 2
budget = session[start:end]
assert session.index('if (m_AdaptiveDisplay && LiHasVideoKeyFrame())') < session.index('if (!initialRateChecked)')

harness = r'''
#include "app/backend/streambudget.h"
#include "shared/deskport-core/include/deskport/protocol.h"
#include <thread>
#include <cassert>
#include <mutex>
#include <iostream>
struct QMutexLocker {
    std::unique_lock<std::mutex> held;
    explicit QMutexLocker(std::mutex* mutex): held(*mutex) {}
};
using quint64 = std::uint64_t;
template<typename T> T qBound(T low, T value, T high) { return std::clamp(value, low, high); }
struct Wake { int calls = 0; void wakeAll() { ++calls; } };
struct Value { bool value; bool toBool() const { return value; } };
struct Reply { bool value; Value operator[](const char*) const { return {value}; } };
struct Preferences {
    bool smartStreaming = false, dataSaver = true, enableYUV444 = false;
    int bitrateKbps = 15000;
};
struct Stream { int width = 2560, height = 1440, fps = 60, bitrate = 0; };
struct AdaptiveDisplay {
    std::mutex m_Mutex;
    bool m_RateSupported = true, m_RateFallback = false, m_RateReady = false, m_RateSaver = false;
    int m_RateCeiling = 0;
    quint64 m_RateGeneration = 0, sentGeneration = 0;
    Wake m_Wake;
    int requests = 0, lastCap = 0;
    bool lastSaver = false;
    bool liveRateSupported();
    bool takeRateFallback();
    void beginRateStream(int initialKbps, bool saver);
    void markRateStreamReady();
    void setRateCeiling(int kbps, bool saver);
    bool pending(int rateSent, bool rateSaverSent, quint64 rateGenerationSent) {
        return (''' + pending + r''')();
    }
    bool pending() { return pending(lastCap, lastSaver, sentGeneration); }
    void pump() {
        if (!pending()) return;
        QMutexLocker lock(&m_Mutex);
        ++requests; lastCap = m_RateCeiling; lastSaver = m_RateSaver; sentGeneration = m_RateGeneration;
    }
    void receive(bool accepted) {
        const Reply reply {accepted};
''' + result + r'''
    }
};
''' + live + '\n' + fallback + '\n' + begin + '\n' + ready + '\n' + set_rate + r'''
void startCheck(AdaptiveDisplay& controller, bool& initialRateChecked) {
    auto* m_AdaptiveDisplay = &controller;
''' + initial + r'''
}
void negotiate(AdaptiveDisplay& controller, const Preferences& preferences, Stream& stream) {
    const bool m_LiveRateNegotiated = controller.liveRateSupported();
    const auto* m_Preferences = &preferences;
    auto& m_StreamConfig = stream;
''' + budget + r'''
}
int main() {
    Preferences preferences;
    Stream stream;
    AdaptiveDisplay retained;
    negotiate(retained, preferences, stream);
    assert(stream.bitrate == 15000);
    retained.beginRateStream(3750, true);
    assert(!retained.pending()); // never query an encoder before it starts
    bool checked = false;
    startCheck(retained, checked);
    assert(checked && retained.m_Wake.calls == 1 && retained.pending());
    startCheck(retained, checked);
    assert(retained.m_Wake.calls == 1); // exactly one readiness signal
    retained.pump();
    assert(retained.requests == 1 && retained.lastCap == 3750 && retained.lastSaver && !retained.pending());
    retained.receive(false);
    assert(!retained.liveRateSupported());
    assert(retained.takeRateFallback() && !retained.takeRateFallback());
    negotiate(retained, preferences, stream);
    assert(stream.bitrate == DeskPortStream::dataSaverBitrate(stream.width, stream.height));
    assert(stream.width == 2560 && stream.height == 1440 && stream.fps == 60);
    assert(preferences.bitrateKbps == 15000);
    retained.beginRateStream(0, true);
    checked = false; startCheck(retained, checked); retained.pump();
    assert(retained.requests == 1); // fixed reconnect cannot loop another request
    for (int i = 0; i < 10; ++i) {
        preferences.dataSaver = false; negotiate(retained, preferences, stream);
        assert(stream.bitrate == 15000);
        preferences.dataSaver = true; negotiate(retained, preferences, stream);
        assert(stream.bitrate == DeskPortStream::dataSaverBitrate(stream.width, stream.height) && preferences.bitrateKbps == 15000);
    }
    AdaptiveDisplay supported;
    supported.beginRateStream(3750, true);
    checked = false; startCheck(supported, checked); supported.pump(); supported.receive(true);
    assert(supported.liveRateSupported() && !supported.takeRateFallback());
    supported.beginRateStream(3750, true); // same cap, new encoder needs a new ack
    assert(!supported.pending());
    checked = false; startCheck(supported, checked);
    assert(supported.pending()); supported.pump(); assert(supported.requests == 2);
    AdaptiveDisplay late;
    late.beginRateStream(3750, true);
    std::thread userToggle([&] { late.setRateCeiling(15000, false); }); userToggle.join();
    assert(!late.pending());
    checked = false; startCheck(late, checked); late.pump();
    assert(late.requests == 1 && late.lastCap == 15000 && !late.lastSaver); // no stale saver snapshot
    AdaptiveDisplay uncapped;
    uncapped.beginRateStream(0, false);
    checked = false; startCheck(uncapped, checked); uncapped.pump();
    assert(uncapped.requests == 0);
    uncapped.setRateCeiling(3750, true); uncapped.pump();
    assert(uncapped.requests == 1);
    std::cout << "PASS: startup readiness/latest toggle, per-encoder confirmation, retained fixed fallback and capped reconnect budget\n";
}
'''
with tempfile.TemporaryDirectory(prefix='deskport-rate-fallback-') as tmp:
    cpp, binary = Path(tmp) / 'test.cpp', Path(tmp) / 'test'
    cpp.write_text(harness)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread',
                    '-I', str(root), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
