#pragma once

#include "../../decoder.h"
#include "../renderer.h"

#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>
#include <thread>

class IVsyncSource {
public:
    virtual ~IVsyncSource() {}
    virtual bool initialize(SDL_Window* window, int displayFps) = 0;

    // Asynchronous sources produce callbacks on their own, while synchronous
    // sources require calls to waitForVsync().
    virtual bool isAsync() = 0;

    virtual void waitForVsync() {
        // Synchronous sources must implement waitForVsync()!
        SDL_assert(false);
    }
};

class Pacer
{
public:
    Pacer(IFFmpegRenderer* renderer, PVIDEO_STATS videoStats,
          VideoPresentationCallbacks callbacks = {});

    ~Pacer();

    void submitFrame(AVFrame* frame);

    bool initialize(SDL_Window* window, int maxVideoFps, bool enablePacing, QWindow* presentationWindow = nullptr);

    void signalVsync();

    void renderOnMainThread();

private:
    static int vsyncThread(void* context);

    static int renderThread(void* context);

    bool handleVsync(int timeUntilNextVsyncMillis);

    void enqueueFrameForRenderingAndUnlock(AVFrame* frame);

    void renderFrame(AVFrame* frame);

    void dropFrameForEnqueue(QQueue<AVFrame*>& queue);

    void captureSnapshot(AVFrame* frame, bool report);

    void snapshotThread();

    QQueue<AVFrame*> m_RenderQueue;
    QQueue<AVFrame*> m_PacingQueue;
    QQueue<int> m_PacingQueueHistory;
    QQueue<int> m_RenderQueueHistory;
    QMutex m_FrameQueueLock;
    QWaitCondition m_RenderQueueNotEmpty;
    QWaitCondition m_PacingQueueNotEmpty;
    QWaitCondition m_VsyncSignalled;
    bool m_VsyncPending = false; // Protected by m_FrameQueueLock.
    SDL_Thread* m_RenderThread;
    SDL_Thread* m_VsyncThread;
    std::atomic<bool> m_Stopping;
    bool m_FirstPresentation = true;
    // Converts an occasional frame for transition backdrops off the render path.
    // Joined before the decoder and its hardware device are released.
    std::thread m_SnapshotWorker;
    // Never nest this lock with m_FrameQueueLock or hold it during conversion,
    // callbacks, frame release, or join. Busy includes the pending frame.
    QMutex m_SnapshotLock;
    QWaitCondition m_SnapshotAvailable;
    AVFrame* m_SnapshotFrame = nullptr;
    bool m_SnapshotReport = false;
    bool m_SnapshotStopping = false;
    std::atomic<bool> m_SnapshotBusy{false};
    bool m_SnapshotReported = false;
    Uint32 m_LastPresentationSnapshot = 0;

    IVsyncSource* m_VsyncSource;
    IFFmpegRenderer* m_VsyncRenderer;
    int m_MaxVideoFps;
    int m_DisplayFps;
    PVIDEO_STATS m_VideoStats;
    int m_RendererAttributes;
    bool m_ClockPacing = false;
    VideoPresentationCallbacks m_Callbacks;
};
