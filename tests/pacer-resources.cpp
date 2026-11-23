// Synthetic resource/lifecycle tests against the production Pacer implementation.
#include "streaming/video/decoder.h"
#include "streaming/video/ffmpeg-renderers/renderer.h"
#include <QMutex>
#include <QQueue>
#include <QWaitCondition>
#include <QImage>
#include <atomic>
#include <thread>
#define private public
#include "streaming/video/ffmpeg-renderers/pacer/pacer.h"
#undef private
#include "streaming/video/ffmpeg-renderers/pacer/pacingclock.h"
#include "streaming/streamutils.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <vector>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

static void check(bool value, const char* message)
{
    if (!value) {
        std::cerr << "FAIL: " << message << std::endl;
        std::abort();
    }
}

template<class Predicate> static void eventually(Predicate predicate, const char* message)
{
    const auto deadline = Clock::now() + 3s;
    while (!predicate()) {
        check(Clock::now() < deadline, message);
        std::this_thread::sleep_for(1ms);
    }
}

// No window, display query, decoder, clipboard, or connection is created.
int StreamUtils::getDisplayRefreshRate(SDL_Window*) { return 60; }

class Renderer final : public IFFmpegRenderer {
public:
    bool initialize(PDECODER_PARAMETERS) override { return true; }
    bool prepareDecoderContext(AVCodecContext*, AVDictionary**) override { return true; }
    bool isRenderThreadSupported() override { return false; }
    void renderFrame(AVFrame*) override { ++rendered; }
    int rendered = 0;
};

struct Gate {
    void wait() {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return open; });
    }
    void release() {
        std::lock_guard<std::mutex> lock(mutex);
        open = true;
        condition.notify_all();
    }
    std::mutex mutex;
    std::condition_variable condition;
    bool open = false;
};

static AVBufferRef* countedBuffer(int bytes, std::atomic<int>& released)
{
    auto* data = static_cast<uint8_t*>(av_mallocz(bytes));
    check(data, "allocate synthetic buffer");
    AVBufferRef* buffer = av_buffer_create(data, bytes, [](void* opaque, uint8_t* pixels) {
        av_free(pixels);
        ++*static_cast<std::atomic<int>*>(opaque);
    }, &released, 0);
    check(buffer, "create counted buffer");
    return buffer;
}

static AVFrame* frame(std::atomic<int>& released, int width = 16, int height = 8)
{
    AVFrame* result = av_frame_alloc();
    check(result, "allocate synthetic frame");
    result->width = width;
    result->height = height;
    result->format = AV_PIX_FMT_BGRA;
    result->pkt_dts = SDL_GetTicks();
    result->buf[0] = countedBuffer(width * height * 4 + AV_INPUT_BUFFER_PADDING_SIZE, released);
    result->data[0] = result->buf[0]->data;
    result->linesize[0] = width * 4;
    return result;
}

static void snapshots()
{
    Renderer renderer;
    VIDEO_STATS stats{};
    Gate gate;
    std::atomic<int> delivered{0}, released{0}, busyReleased{0}, workerStarts{0};
    std::thread::id callbackThread;
    VideoPresentationCallbacks callbacks;
    callbacks.snapshotReady = [&](QImage image) {
        // Thread IDs can be recycled after join; TLS also detects a replacement
        // thread that happens to receive the same OS/thread-library identity.
        thread_local const int incarnation = ++workerStarts;
        check(incarnation == 1 && workerStarts == 1, "one persistent worker incarnation");
        const int capture = delivered.fetch_add(1);
        check(image.size() == (capture == 0 ? QSize(16, 8) : QSize(32, 12)), "capture uses current frame dimensions");
        if (capture == 0) {
            callbackThread = std::this_thread::get_id();
            gate.wait();
        }
        else check(callbackThread == std::this_thread::get_id(), "worker thread reused");
    };
    auto pacer = std::make_unique<Pacer>(&renderer, &stats, callbacks);
    check(!pacer->m_SnapshotWorker.joinable(), "snapshot worker starts lazily");
    AVFrame* first = frame(released);
    pacer->captureSnapshot(first, false);
    const auto worker = pacer->m_SnapshotWorker.get_id();
    eventually([&] { return delivered.load() == 1; }, "first snapshot callback");
    check(av_buffer_get_ref_count(first->buf[0]) == 2, "one cloned pixel reference in flight");

    AVFrame* busy = frame(busyReleased);
    const auto start = Clock::now();
    pacer->captureSnapshot(busy, true);
    check(Clock::now() - start < 100ms, "busy capture never joins active worker");
    check(av_buffer_get_ref_count(busy->buf[0]) == 1, "busy frame rejected before cloning");
    check(delivered == 1, "busy capture is not queued");
    av_frame_free(&busy);
    av_frame_free(&first);
    check(released == 0 && busyReleased == 1, "worker retains only its accepted frame");
    gate.release();
    eventually([&] { return !pacer->m_SnapshotBusy.load(); }, "first capture releases busy state");
    check(released == 1, "busy clears after pixel reference release");

    AVFrame* second = frame(released, 32, 12);
    pacer->captureSnapshot(second, false);
    av_frame_free(&second);
    eventually([&] { return !pacer->m_SnapshotBusy.load(); }, "second capture completed");
    check(delivered == 2 && released == 2, "reused worker completes second capture");
    check(pacer->m_SnapshotWorker.get_id() == worker, "persistent worker identity");
    pacer.reset(); // Also verifies wake/join of the sleeping snapshot worker.
}

static void snapshotTeardown()
{
    Renderer renderer;
    VIDEO_STATS stats{};
    Gate gate;
    std::atomic<int> pixels{0}, hardware{0}, callbacks{0};
    std::atomic<bool> deleting{false}, destroyed{false};
    VideoPresentationCallbacks presentation;
    presentation.snapshotReady = [&](QImage image) {
        check(image.isNull(), "HDR synthetic hardware frame skips transfer");
        ++callbacks;
        gate.wait();
    };
    auto* pacer = new Pacer(&renderer, &stats, presentation);
    AVFrame* source = frame(pixels);
    // HDR is rejected before hardware transfer; this counted opaque buffer
    // verifies AVFrame hardware-reference ownership without a real GPU/device.
    source->color_trc = AVCOL_TRC_SMPTE2084;
    source->hw_frames_ctx = countedBuffer(1, hardware);
    pacer->captureSnapshot(source, false);
    av_frame_free(&source);
    eventually([&] { return callbacks.load() == 1; }, "teardown capture entered callback");
    std::thread destroyer([&] {
        deleting = true;
        delete pacer;
        destroyed = true;
    });
    eventually([&] { return deleting.load(); }, "destructor started");
    std::this_thread::sleep_for(30ms);
    check(!destroyed && pixels == 0 && hardware == 0, "destructor waits with cloned references alive");

    // A replacement presentation can capture its own dimensions while the old
    // presentation is draining. Neither worker delivers into the other Pacer.
    std::atomic<int> replacementCallbacks{0}, replacementPixels{0};
    VideoPresentationCallbacks replacement;
    replacement.snapshotReady = [&](QImage image) {
        check(image.size() == QSize(24, 10), "replacement presentation owns its picture");
        ++replacementCallbacks;
    };
    {
        Pacer next(&renderer, &stats, replacement);
        AVFrame* fresh = frame(replacementPixels, 24, 10);
        next.captureSnapshot(fresh, false);
        av_frame_free(&fresh);
        eventually([&] { return !next.m_SnapshotBusy.load(); }, "replacement capture completed");
    }
    check(replacementCallbacks == 1 && replacementPixels == 1, "replacement references released");
    gate.release();
    destroyer.join();
    check(destroyed && pixels == 1 && hardware == 1 && callbacks == 1, "teardown joins delivery and releases all references");
}

class VsyncSource final : public IVsyncSource {
public:
    explicit VsyncSource(bool async) : asynchronous(async) {}
    bool initialize(SDL_Window*, int) override { return true; }
    bool isAsync() override { return asynchronous; }
    void waitForVsync() override { ++waits; std::this_thread::sleep_for(3ms); }
    const bool asynchronous;
    std::atomic<int> waits{0};
};

static void startPacing(Pacer& pacer, int mode)
{
    pacer.m_MaxVideoFps = 60;
    pacer.m_DisplayFps = 20;
    pacer.m_RendererAttributes = 0;
    pacer.m_ClockPacing = mode == 0;
    if (mode != 0) pacer.m_VsyncSource = new VsyncSource(mode == 1);
    pacer.m_VsyncThread = SDL_CreateThread(Pacer::vsyncThread, "PacerFixture", &pacer);
    check(pacer.m_VsyncThread, "create pacing thread");
}

static int history(Pacer& pacer)
{
    QMutexLocker lock(&pacer.m_FrameQueueLock);
    return pacer.m_PacingQueueHistory.size();
}

static void pacing(int mode)
{
    Renderer renderer;
    VIDEO_STATS stats{};
    std::atomic<int> pixels{0};
    std::atomic<bool> signalStop{false};
    std::mutex readyLock;
    std::vector<Clock::time_point> ready;
    int nativeWaitsAtResume = -1;
    Pacer* active = nullptr;
    VideoPresentationCallbacks presentation;
    presentation.firstFrame = [] {}; // Disable the global snapshot path.
    presentation.frameReady = [&] {
        std::lock_guard<std::mutex> lock(readyLock);
        ready.push_back(Clock::now());
        if (ready.size() == 1 && mode == 2)
            nativeWaitsAtResume = static_cast<VsyncSource*>(active->m_VsyncSource)->waits;
    };
    auto pacer = std::make_unique<Pacer>(&renderer, &stats, presentation);
    active = pacer.get();
    startPacing(*pacer, mode);
    std::thread signalThread;
    if (mode == 1) {
        // Callbacks keep arriving during idle, as with an async native source.
        signalThread = std::thread([&] {
            while (!signalStop) {
                pacer->signalVsync();
                std::this_thread::sleep_for(2ms);
            }
        });
    }
    eventually([&] { return history(*pacer) == 2; }, "two empty pacing cycles");
    std::this_thread::sleep_for(160ms);
    check(history(*pacer) == 2, "idle has no timed pacing wakeups");
    if (mode == 2) check(static_cast<VsyncSource*>(pacer->m_VsyncSource)->waits == 2, "native wait stops after two empty cycles");

    const auto submitted = Clock::now();
    pacer->submitFrame(frame(pixels));
    eventually([&] { std::lock_guard<std::mutex> lock(readyLock); return !ready.empty(); }, "frame wakes idle pacing");
    {
        std::lock_guard<std::mutex> lock(readyLock);
        check(ready[0] - submitted < 40ms, "idle frame bypasses the next timer/native wait");
        if (mode == 2) check(nativeWaitsAtResume == 2, "resume precedes next native wait");
    }
    pacer->renderOnMainThread();
    if (mode == 0) {
        pacer->submitFrame(frame(pixels));
        eventually([&] { std::lock_guard<std::mutex> lock(readyLock); return ready.size() == 2; }, "timer resumes following frame");
        std::lock_guard<std::mutex> lock(readyLock);
        check(ready[1] - ready[0] >= 35ms && ready[1] - ready[0] < 90ms, "timer resets without catch-up or skipped period");
        pacer->renderOnMainThread();
    }
    eventually([&] { return history(*pacer) >= 3; }, "pacing becomes idle again");
    std::this_thread::sleep_for(150ms);
    signalStop = true;
    if (signalThread.joinable()) signalThread.join();
    const auto stopped = Clock::now();
    pacer.reset();
    check(Clock::now() - stopped < 100ms, "stop wakes idle pacing without a timeout");
    check(pixels == renderer.rendered, "paced synthetic frames released");
}

int main()
{
    check(SDL_Init(SDL_INIT_TIMER) == 0, "initialize SDL timer only");
    snapshots();
    snapshotTeardown();
    for (int mode = 0; mode != 3; ++mode) {
        pacing(mode);
        // Exercise stop at thread startup as well as the idle stop above.
        for (int i = 0; i != 20; ++i) {
            Renderer renderer;
            VIDEO_STATS stats{};
            Pacer pacer(&renderer, &stats);
            startPacing(pacer, mode);
        }
    }
    PacingClock clock(20, Clock::time_point{});
    clock.reset(Clock::time_point{} + 10s);
    check(clock.deadline() == Clock::time_point{} + 10s + 50ms, "clock reset discards stale deadline");
    SDL_Quit();
    std::cout << "PASS: lazy persistent snapshot worker, busy rejection, frame/hardware teardown, replacement presentation, timer/async/sync idle-resume-stop" << std::endl;
}
