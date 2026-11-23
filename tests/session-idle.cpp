// Synthetic control state, loopback IPC and dummy SDL only. No host or clipboard.
// Build session-idle.pro and run this fixture in an external test directory.
#include "streaming/canvasbridge.h"
#include "streaming/video/queuedframesource.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

namespace {
QJsonObject viewport() {
    return {{"width",640},{"height",480},{"scale",1},
            {"visible",true},{"focused",true},{"hovered",true}};
}
void drain() { SDL_Event event; while (SDL_PollEvent(&event)) {} }
void deliver() { QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); }
void push(int code) {
    SDL_Event event{}; event.type = SDL_USEREVENT; event.user.code = code;
    assert(SDL_PushEvent(&event) == 1);
}
int SDLCALL delayInsertion(void*, SDL_Event* event) {
    if (event->type == SDL_USEREVENT && event->user.code == 73)
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    return 1;
}
void descriptions() {
    QObject receiver;
    int calls = 0;
    QJsonObject latest;
    CanvasBridge::setDescriptionCallback(&receiver, [&] {
        ++calls; latest = CanvasBridge::description();
    });
    CanvasBridge::configure(viewport());
    CanvasBridge::capture(true, true, true);
    CanvasBridge::pointerRegion(true, 1, 2, 30, 40);
    CanvasBridge::textInput(true);
    CanvasBridge::overlay(2, true, "synthetic");
    CanvasBridge::label("connecting");
    CanvasBridge::waiting(true);
    CanvasBridge::question("fixture", "synthetic", "cancel", "accept");
    assert(calls == 0);
    deliver();
    assert(calls == 1 && latest == CanvasBridge::description());
    assert(latest["relative"].toBool() && latest["textInput"].toBool());
    assert(latest["pointerRegion"].toObject()["width"].toInt() == 30);
    for (int i = 0; i < 1000; ++i) {
        CanvasBridge::waiting(true);
        CanvasBridge::capture(true, true, true);
        CanvasBridge::pointerRegion(true, 1, 2, 30, 40);
        CanvasBridge::overlay(2, true, "synthetic");
    }
    deliver(); assert(calls == 1);
    std::thread writer([] {
        for (int i = 0; i < 1000; ++i) CanvasBridge::label(QString::number(i));
    });
    writer.join(); deliver();
    assert(calls == 2 && latest["label"] == "999");
    CanvasBridge::dismissQuestion();
    CanvasBridge::inputEnabled(false);
    deliver();
    assert(calls == 3 && !latest.contains("question") && !latest["grab"].toBool());
    CanvasBridge::label("pending teardown");
    CanvasBridge::setDescriptionCallback(nullptr, {});
    deliver(); assert(calls == 3);
    int replacementCalls = 0;
    CanvasBridge::setDescriptionCallback(&receiver, [&] { ++calls; });
    CanvasBridge::setDescriptionCallback(&receiver, [&] { ++replacementCalls; });
    deliver(); assert(calls == 3 && replacementCalls == 1);
    CanvasBridge::setDescriptionCallback(nullptr, {});
    // Queued callbacks are cancelled before their QObject context is destroyed.
    auto transient = new QObject;
    CanvasBridge::setDescriptionCallback(transient, [] { assert(false); });
    CanvasBridge::setDescriptionCallback(nullptr, {});
    delete transient;
    deliver();
}
void waits(SDL_Window* window) {
    for (bool canvas : {false, true}) {
        assert(CanvasBridge::idleWaitTimeout(true, false, canvas) == (canvas ? 2 : 20));
        assert(CanvasBridge::idleWaitTimeout(false, false, canvas) == 200);
    }
    assert(CanvasBridge::idleWaitTimeout(true, true, true) == 2);
    assert(CanvasBridge::idleWaitTimeout(false, true, true) == 33);
    assert(CanvasBridge::idleWaitTimeout(true, true, false) == 20);
    assert(CanvasBridge::idleWaitTimeout(false, true, false) == 20);
    drain();
    QTimer qtWork;
    int ticks = 0;
    QObject::connect(&qtWork, &QTimer::timeout, [&] { ++ticks; });
    qtWork.start(5);
    QElapsedTimer clock;
    SDL_Event event{};
    clock.start();
    assert(!CanvasBridge::waitForEvent(&event, 200));
    assert(clock.elapsed() >= 140 && clock.elapsed() < 600 && ticks >= 10);
    qtWork.stop();
    for (int i = 0; i < 32; ++i) {
        drain(); clock.restart();
        std::thread producer([i] {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            push(1000 + i);
        });
        const bool received = CanvasBridge::waitForEvent(&event, 200);
        producer.join();
        assert(received && event.type == SDL_USEREVENT && event.user.code == 1000 + i);
        assert(clock.elapsed() < 120);
    }
    // Install a later watch while the unified wait is running. The Qt wake
    // arrives 30 ms before SDL finishes inserting this pushed event.
    drain();
    std::thread delayedProducer;
    QTimer::singleShot(2, [&] {
        SDL_AddEventWatch(delayInsertion, nullptr);
        delayedProducer = std::thread([] { push(73); });
    });
    clock.restart();
    assert(CanvasBridge::waitForEvent(&event, 200));
    delayedProducer.join();
    SDL_DelEventWatch(delayInsertion, nullptr);
    assert(event.type == SDL_USEREVENT && event.user.code == 73 && clock.elapsed() < 120);

    // Qt socket readiness shares the same dispatcher as QLocalSocket. The
    // loopback fixture avoids long Unix socket paths and filesystem artifacts.
    QTcpServer server;
    assert(server.listen(QHostAddress::LocalHost, 0));
    QTcpSocket sender;
    sender.connectToHost(QHostAddress::LocalHost, server.serverPort());
    assert(sender.waitForConnected(1000) && server.waitForNewConnection(1000));
    auto receiver = server.nextPendingConnection();
    assert(receiver);
    bool ipcReceived = false;
    QByteArray payload;
    QObject::connect(receiver, &QTcpSocket::readyRead, [&] {
        payload += receiver->readAll();
        if (payload.size() < 9) return;
        assert(payload == "synthetic");
        ipcReceived = true;
        CanvasBridge::state(viewport());
    });
    CanvasBridge::synchronize(window); drain();
    QTimer::singleShot(5, [&] { sender.write("synthetic"); sender.flush(); });
    clock.restart();
    assert(CanvasBridge::waitForEvent(&event, 200));
    assert(ipcReceived && CanvasBridge::isWakeEvent(event) && clock.elapsed() < 120);
    CanvasBridge::synchronize(window); drain();
    delete receiver;
    // Removed SDL watches and queued QObject events must not outlive a wait.
    push(99);
    assert(CanvasBridge::waitForEvent(&event, 200) && event.user.code == 99);
    drain();
}

struct InputEvents {
    int lost = 0, gained = 0, keys = 0;
    Uint8 firstFocus = 0;
    void take(const SDL_Event& event) {
        if (event.type == SDL_WINDOWEVENT) {
            lost += event.window.event == SDL_WINDOWEVENT_FOCUS_LOST;
            gained += event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED;
            if (!firstFocus && (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST || event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED))
                firstFocus = event.window.event;
        }
        keys += event.type == SDL_KEYDOWN || event.type == SDL_KEYUP;
    }
    void poll() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) take(event);
    }
};
QJsonObject key() {
    return {{"type","key"},{"scan",int(SDL_SCANCODE_A)},
            {"symbol",int(SDLK_a)},{"modifiers",0},{"down",true}};
}
struct BatchBoundary {
    std::atomic<bool> seen{false}, released{false}, timedOut{false};
};
int SDLCALL pauseBatch(void* opaque, SDL_Event* event) {
    auto& boundary = *static_cast<BatchBoundary*>(opaque);
    if (event->type != SDL_KEYDOWN || boundary.seen.exchange(true, std::memory_order_acq_rel)) return 1;
    // Pause after synchronize took its snapshot but before SDL enqueues the
    // first key. The IPC thread can hide while this event is still in flight.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (CanvasBridge::requested()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            boundary.timedOut.store(true); break;
        }
        std::this_thread::yield();
    }
    // The requested flag publishes before the rest of the locked hide state.
    // Acquiring that lock through a read waits for invalidation to finish.
    if (!boundary.timedOut.load()) (void) CanvasBridge::description();
    boundary.released.store(true, std::memory_order_release);
    return 1;
}
void batchRace(SDL_Window* window) {
    BatchBoundary boundary;
    for (int i = 0; i < 3; ++i) CanvasBridge::input(key());
    SDL_AddEventWatch(pauseBatch, &boundary);
    std::thread lifecycle([&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (!boundary.seen.load(std::memory_order_acquire)) {
            assert(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        CanvasBridge::requested(false);
        while (!boundary.released.load(std::memory_order_acquire)) std::this_thread::yield();
        CanvasBridge::requested(true);
        CanvasBridge::inputEnabled(true);
    });
    CanvasBridge::synchronize(window);
    lifecycle.join(); SDL_DelEventWatch(pauseBatch, &boundary);
    assert(!boundary.timedOut.load());
    InputEvents inFlight; inFlight.poll();
    assert(inFlight.keys == 1);
    CanvasBridge::synchronize(window);
    InputEvents fence; fence.poll();
    assert(fence.lost == 1 && fence.gained == 1 && fence.keys == 0 && fence.firstFocus == SDL_WINDOWEVENT_FOCUS_LOST);
}
void hiddenLoop(SDL_Window* window) {
    CanvasBridge::state(viewport());
    CanvasBridge::requested(true);
    CanvasBridge::inputEnabled(true);
    CanvasBridge::synchronize(window); drain();
    CanvasBridge::input(key());
    CanvasBridge::synchronize(window);
    InputEvents active; active.poll(); assert(active.keys == 1);

    auto hidden = viewport(); hidden["visible"] = false; hidden["focused"] = false;
    CanvasBridge::requested(false);
    CanvasBridge::state(hidden);
    InputEvents idle;
    int timeouts = 0;
    QElapsedTimer clock; clock.start();
    // Mirror execInternal exactly: synchronize, set admission, wait for ONE
    // event, dispatch, repeat. Draining or disabling only once masks the bug.
    for (int iteration = 0; iteration < 16 && timeouts < 2; ++iteration) {
        CanvasBridge::synchronize(window);
        CanvasBridge::inputEnabled(false);
        SDL_Event event;
        if (CanvasBridge::waitForEvent(&event, CanvasBridge::idleWaitTimeout(false, false, true))) idle.take(event);
        else ++timeouts;
    }
    assert(timeouts == 2 && clock.elapsed() >= 280 && clock.elapsed() < 1500);
    assert(idle.lost == 1 && idle.gained == 0 && idle.keys == 0);
    // Unchanged hidden state also must not recreate a release boundary.
    for (int i = 0; i < 10; ++i) CanvasBridge::state(hidden);
    CanvasBridge::synchronize(window);
    InputEvents unchanged; unchanged.poll(); assert(unchanged.lost == 0);

    CanvasBridge::state(viewport());
    CanvasBridge::requested(true);
    CanvasBridge::inputEnabled(true);
    CanvasBridge::synchronize(window);
    InputEvents restored; restored.poll();
    assert(restored.lost == 0 && restored.gained == 1 && restored.keys == 0);
    CanvasBridge::input(key()); CanvasBridge::synchronize(window);
    InputEvents resumed; resumed.poll(); assert(resumed.keys == 1 && resumed.lost == 0);

    // Every transient boundary survives a return before the next SDL turn.
    // Discard keys from both sides of the boundary before delivering the gain.
    for (int boundary = 0; boundary < 3; ++boundary) {
        CanvasBridge::input(key());
        std::thread lifecycle([boundary] {
            if (boundary == 0) {
                CanvasBridge::inputEnabled(false); CanvasBridge::inputEnabled(true);
            } else if (boundary == 1) {
                auto unfocused = viewport(); unfocused["focused"] = false;
                CanvasBridge::state(unfocused); CanvasBridge::state(viewport());
            } else {
                CanvasBridge::requested(false); CanvasBridge::requested(true);
                CanvasBridge::inputEnabled(true);
            }
            CanvasBridge::input(key());
        });
        lifecycle.join();
        CanvasBridge::synchronize(window);
        InputEvents fence; fence.poll();
        assert(fence.lost == 1 && fence.gained == 1 && fence.keys == 0 && fence.firstFocus == SDL_WINDOWEVENT_FOCUS_LOST);
        CanvasBridge::synchronize(window);
        InputEvents settled; settled.poll(); assert(settled.lost == 0 && settled.gained == 0);
    }
}
void inputRaces(SDL_Window* window) {
    // Keep a producer running across disable, then restore without a second
    // clear. Nothing admitted before/after disable may leak into the recall.
    for (int iteration = 0; iteration < 64; ++iteration) {
        std::atomic<bool> started{false}, disabled{false};
        std::thread producer([&] {
            started.store(true, std::memory_order_release);
            while (!disabled.load(std::memory_order_acquire)) CanvasBridge::input(key());
            for (int i = 0; i < 32; ++i) CanvasBridge::input(key());
        });
        while (!started.load(std::memory_order_acquire)) std::this_thread::yield();
        CanvasBridge::inputEnabled(false);
        disabled.store(true, std::memory_order_release);
        producer.join();
        CanvasBridge::synchronize(window);
        InputEvents released; released.poll(); assert(released.lost == 1 && released.keys == 0);
        CanvasBridge::inputEnabled(true);
        CanvasBridge::synchronize(window);
        InputEvents recall; recall.poll(); assert(recall.keys == 0 && recall.lost == 0);
        CanvasBridge::input(key()); CanvasBridge::synchronize(window);
        InputEvents fresh; fresh.poll(); assert(fresh.keys == 1);
    }
    // A late enable from the SDL owner must not undo an IPC hide. It cannot
    // arm admission for a later recall before the new viewer enables input.
    CanvasBridge::requested(false);
    std::thread lateEnable([] { CanvasBridge::inputEnabled(true); CanvasBridge::input(key()); });
    lateEnable.join();
    CanvasBridge::synchronize(window);
    InputEvents hidden; hidden.poll(); assert(hidden.lost == 1 && hidden.keys == 0);
    CanvasBridge::requested(true); CanvasBridge::synchronize(window); drain();
    CanvasBridge::input(key()); CanvasBridge::synchronize(window);
    InputEvents premature; premature.poll(); assert(premature.keys == 0);
    CanvasBridge::inputEnabled(true);
    CanvasBridge::input(key()); CanvasBridge::synchronize(window);
    InputEvents enabled; enabled.poll(); assert(enabled.keys == 1);
}
void nativeHiddenWait() {
    auto window = SDL_CreateWindow("native synthetic", 0, 0, 16, 16, SDL_WINDOW_HIDDEN);
    assert(window && !CanvasBridge::owns(window));
    drain();
    const int timeout = CanvasBridge::idleWaitTimeout(false, false, false);
    QElapsedTimer clock; clock.start();
    SDL_Event event;
    assert(!CanvasBridge::waitForEvent(&event, timeout));
    assert(clock.elapsed() >= 140 && clock.elapsed() < 600);

    QTcpServer server;
    assert(server.listen(QHostAddress::LocalHost, 0));
    QTcpSocket sender;
    sender.connectToHost(QHostAddress::LocalHost, server.serverPort());
    assert(sender.waitForConnected(1000) && server.waitForNewConnection(1000));
    auto receiver = server.nextPendingConnection(); assert(receiver);
    bool ipcReceived = false;
    QByteArray payload;
    QObject::connect(receiver, &QTcpSocket::readyRead, [&] {
        payload += receiver->readAll();
        if (payload.size() < 9) return;
        assert(payload == "synthetic");
        ipcReceived = true; push(81);
    });
    QTimer::singleShot(5, [&] { sender.write("synthetic"); sender.flush(); });
    clock.restart();
    assert(CanvasBridge::waitForEvent(&event, timeout));
    assert(ipcReceived && event.type == SDL_USEREVENT && event.user.code == 81 && clock.elapsed() < 120);
    delete receiver;

    std::thread producer([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); push(82);
    });
    clock.restart();
    const bool received = CanvasBridge::waitForEvent(&event, timeout);
    producer.join();
    assert(received && event.type == SDL_USEREVENT && event.user.code == 82 && clock.elapsed() < 120);
    SDL_DestroyWindow(window); drain();
}
void videoConsumers() {
    QObject receiver;
    int descriptions = 0;
    CanvasBridge::setDescriptionCallback(&receiver, [&] { ++descriptions; });
    deliver(); const int initial = descriptions;
    QueuedVideoSource source(7, [] {});
    char data[] = "synthetic";
    LENTRY entry{}; entry.data = data; entry.length = sizeof(data); entry.bufferType = BUFFER_TYPE_PICDATA;
    DECODE_UNIT unit{};
    unit.bufferList = &entry; unit.fullLength = sizeof(data); unit.frameType = FRAME_TYPE_IDR;
    unit.receiveTimeMs = 100; unit.enqueueTimeMs = 115; unit.presentationTimeMs = 1234;
    unit.frameHostProcessingLatency = 42; unit.hdrActive = true; unit.colorspace = COLORSPACE_REC_2020;
    for (int i = 0; i < 120; ++i) {
        unit.frameNumber = i;
        auto frame = EncodedVideoFrame::decode(EncodedVideoFrame::encode(unit, 7), 2000);
        assert(frame && source.push(std::move(frame)));
        VIDEO_FRAME_HANDLE handle{}; PDECODE_UNIT received{};
        assert(source.poll(&handle, &received));
        assert(received->frameNumber == i && received->presentationTimeMs == 1234);
        assert(received->enqueueTimeMs - received->receiveTimeMs == 15);
        assert(received->frameHostProcessingLatency == 42 && received->hdrActive);
        assert(received->colorspace == COLORSPACE_REC_2020);
        source.complete(handle, DR_OK);
    }
    assert(!source.push(EncodedVideoFrame::decode(EncodedVideoFrame::encode(unit, 8), 2000)));
    SS_HDR_METADATA hdr{}; hdr.maxDisplayLuminance = 1000;
    source.updateMetadata(true, &hdr, 11, 3);
    SS_HDR_METADATA observed{}; uint32_t rtt = 0, variance = 0;
    assert(source.hdrActive() && source.hdrMetadata(&observed) && observed.maxDisplayLuminance == 1000);
    assert(source.rtt(&rtt, &variance) && rtt == 11 && variance == 3);
    hdr.maxDisplayLuminance = 600;
    source.updateMetadata(false, &hdr, 21, 7);
    assert(!source.hdrActive() && source.hdrMetadata(&observed) && observed.maxDisplayLuminance == 600);
    assert(source.rtt(&rtt, &variance) && rtt == 21 && variance == 7);
    source.stop(); deliver(); assert(descriptions == initial);
    CanvasBridge::setDescriptionCallback(nullptr, {});
}
}
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0);
    descriptions();
    auto window = SDL_CreateWindow("synthetic", 0, 0, 16, 16, SDL_WINDOW_HIDDEN);
    assert(window);
    CanvasBridge::registerWindow(window);
    CanvasBridge::requested(true);
    CanvasBridge::inputEnabled(true);
    CanvasBridge::synchronize(window);
    waits(window);
    hiddenLoop(window);
    inputRaces(window);
    batchRace(window);
    nativeHiddenWait();
    videoConsumers();
    CanvasBridge::setDescriptionCallback(nullptr, {});
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::cout << "PASS: control descriptions, callback teardown, hidden loop deadlines, release fences, input races, native/Canvas Qt/SDL wakeups and video metadata/timing\n";
}
