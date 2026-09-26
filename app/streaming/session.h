#include <atomic>
#include <QMap>
#pragma once
#include "clipboardsync.h"
#include "sessionlifetime.h"
#include "resizesettler.h"

#include <QSemaphore>
#include <QWindow>
#include "backend/adaptivedisplay.h"
#include "transitionwindow.h"
#include <QTimer>
#include "backend/workspaceresolution.h"

#include <Limelight.h>
#include <opus_multistream.h>
#include "settings/streamingpreferences.h"
#include "input/input.h"
#include "video/decoder.h"
#include "audio/renderers/renderer.h"
#include "video/overlaymanager.h"

class SupportedVideoFormatList : public QList<int>
{
public:
    operator int() const
    {
        int value = 0;

        for (const int & v : *this) {
            value |= v;
        }

        return value;
    }

    void
    removeByMask(int mask)
    {
        int i = 0;
        while (i < this->length()) {
            if (this->value(i) & mask) {
                this->removeAt(i);
            }
            else {
                i++;
            }
        }
    }

    void
    deprioritizeByMask(int mask)
    {
        QList<int> deprioritizedList;

        int i = 0;
        while (i < this->length()) {
            if (this->value(i) & mask) {
                deprioritizedList.append(this->takeAt(i));
            }
            else {
                i++;
            }
        }

        this->append(std::move(deprioritizedList));
    }

    int maskByServerCodecModes(int serverCodecModes)
    {
        int mask = 0;

        const QMap<int, int> mapping = {
            {SCM_H264, VIDEO_FORMAT_H264},
            {SCM_H264_HIGH8_444, VIDEO_FORMAT_H264_HIGH8_444},
            {SCM_HEVC, VIDEO_FORMAT_H265},
            {SCM_HEVC_MAIN10, VIDEO_FORMAT_H265_MAIN10},
            {SCM_HEVC_REXT8_444, VIDEO_FORMAT_H265_REXT8_444},
            {SCM_HEVC_REXT10_444, VIDEO_FORMAT_H265_REXT10_444},
            {SCM_AV1_MAIN8, VIDEO_FORMAT_AV1_MAIN8},
            {SCM_AV1_MAIN10, VIDEO_FORMAT_AV1_MAIN10},
            {SCM_AV1_HIGH8_444, VIDEO_FORMAT_AV1_HIGH8_444},
            {SCM_AV1_HIGH10_444, VIDEO_FORMAT_AV1_HIGH10_444},
        };

        for (QMap<int, int>::const_iterator it = mapping.cbegin(); it != mapping.cend(); ++it) {
            if (serverCodecModes & it.key()) {
                mask |= it.value();
                serverCodecModes &= ~it.key();
            }
        }

        // Make sure nobody forgets to update this for new SCM values
        SDL_assert(serverCodecModes == 0);

        int val = *this;
        return val & mask;
    }
};

class Session : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool viewerReady READ viewerReady NOTIFY viewerReadyChanged)
    Q_PROPERTY(QString hostId READ hostId CONSTANT)
    Q_PROPERTY(QString hostName READ hostName CONSTANT)
    Q_PROPERTY(double desktopAdjustment READ desktopAdjustment CONSTANT)

    friend class SdlInputHandler;
    friend class DeferredSessionCleanupTask;
    friend class AsyncConnectionStartThread;
    friend class ExecThread;

public:
    explicit Session(NvComputer* computer, NvApp& app, StreamingPreferences *preferences = nullptr);

    // NB: This may not get destroyed for a long time! Don't put any cleanup here.
    // Use Session::exec() or DeferredSessionCleanupTask instead.
    virtual ~Session() {};

    bool viewerReady() const { return m_ViewerReady.load(); }
    Q_INVOKABLE void setViewerRequested(bool visible) { m_ViewerRequested = visible; }
    QString hostId() const;
    QString hostName() const;
    Q_INVOKABLE QVariantMap traffic() const;
private:
    quint64 m_TrafficReceivedBase = 0, m_TrafficSentBase = 0;
public:
    Q_INVOKABLE void exec(QWindow* qtWindow);
    Q_INVOKABLE bool adaptiveRestartPending() const { return m_NetworkRetry || m_ManualReconnect || m_AdaptiveNextSize.isValid(); }
    Q_INVOKABLE Session* adaptiveContinuation();
    // The transport cannot survive client sleep; stop without quitting the host app.
    void endForSystemSleep();
    void requestReconnect();
    bool leaveFullscreen();
    Q_INVOKABLE int retryDelay() const { return m_RecoveryDeadline ? qMin(4000, 1000 << qMin(m_RecoveryAttempt, 2)) : 0; }
    Q_INVOKABLE void cancelRecovery() { m_RecoveryCancelled = true; m_NetworkRetry = false; }
private:
    bool scheduleNetworkRecovery();
    std::atomic<bool> m_NetworkRetry{false};
    std::atomic<bool> m_RecoveryCancelled{false};
    std::atomic<qint64> m_RecoveryDeadline{0}, m_StreamStartedAt{0};
    int m_RecoveryAttempt = 0;
    QString m_ResumeToken;
public:
    double desktopAdjustment() const { return m_Preferences->desktopAdjustment; }
    Q_INVOKABLE void setDesktopAdjustment(double value);

    static
    void getDecoderInfo(SDL_Window* window,
                        bool& isHardwareAccelerated, bool& isFullScreenOnly,
                        bool& isHdrSupported, QSize& maxResolution);

    static Session* get()
    {
        return s_ActiveSession;
    }

    Overlay::OverlayManager& getOverlayManager()
    {
        return m_OverlayManager;
    }

    void flushWindowEvents();

signals:
    void viewerReadyChanged();
    void stageStarting(QString stage);

    void stageFailed(QString stage, int errorCode, QString failingPorts);

    void connectionStarted();

    void displayLaunchError(QString text);

    void displayLaunchWarning(QString text);

    void quitStarting();

    void sessionFinished(int portTestResult);

    // Emitted after sessionFinished() when the session is ready to be destroyed
    void readyForDeletion();
    void transportCleanupFinished();

private:
    std::atomic<bool> m_ViewerReady{false}, m_ViewerRequested{true};
    void setViewerReady(bool ready) { if (m_ViewerReady.exchange(ready) != ready) emit viewerReadyChanged(); }
    ResizeSettler m_ResizeSettler;
    bool m_ExecRequested = false;
    SessionLifetime m_Lifetime{this, [this] { emit readyForDeletion(); }};
    std::unique_ptr<ClipboardSync> m_Clipboard;
    void initializeClipboard();
    std::shared_ptr<AdaptiveDisplay> m_AdaptiveDisplay;
    bool m_SessionAdmissionFailed = false;
    QString m_SessionTopologyError;
    std::shared_ptr<TransitionWindow> m_TransitionWindow;
    QTimer* m_TransitionTimer = nullptr;
    QSize m_AdaptiveNextSize, m_AdaptiveObservedSize;
    QByteArray m_WindowOutputs;
    QString m_LastWindowRecord;
    bool m_RestoredWindow = false;
    QRect m_AdaptiveGeometry;
    int m_AdaptiveScale = 1, m_AdaptiveObservedScale = 1;
    bool m_ManualReconnect = false, m_ManualResume = false;
    bool m_AdaptiveResume = false, m_AdaptiveMaximized = false;
    struct ClientScreen { QString name; QPoint origin; QSize logicalSize; qreal scale; };
    QVector<ClientScreen> m_ClientScreens;
    qreal m_ClientDefaultScale = 1.0;
    bool m_ClientWayland = false;
    DeskPortDisplay::Workspace workspaceForWindow(SDL_Window* window, bool initialFullscreen = false) const;
    void initializeAdaptiveDisplay(SDL_Window* window);
    bool checkAdaptiveResize();
    void restoreAdaptiveWindow();
    void rememberAdaptiveWindow();
    void execInternal();

    bool initialize();

    bool startConnectionAsync();

    bool validateLaunch(SDL_Window* testWindow);

    void emitLaunchWarning(QString text);

    bool populateDecoderProperties(SDL_Window* window);

    IAudioRenderer* createAudioRenderer(const POPUS_MULTISTREAM_CONFIGURATION opusConfig);

    bool initializeAudioRenderer();

    bool testAudio(int audioConfiguration);

    int getAudioRendererCapabilities(int audioConfiguration);

    void getWindowDimensions(int& x, int& y,
                             int& width, int& height);

    void toggleFullscreen();

    void notifyMouseEmulationMode(bool enabled);

    void updateOptimalWindowDisplayMode();

    enum class DecoderAvailability {
        None,
        Software,
        Hardware
    };

    struct DecoderProbe {
        DecoderAvailability availability = DecoderAvailability::None;
        int capabilities = 0, colorSpace = 0, colorRange = 0;
        bool fullScreen = false;
    };
    // Scoped to one initialize() and its hidden window. Never reuse across
    // dimensions, displays, drivers, sessions, or changed decoder preferences.
    QMap<QString, DecoderProbe> m_DecoderProbes;
    DecoderProbe probeDecoder(SDL_Window* window, StreamingPreferences::VideoDecoderSelection vds,
                              int videoFormat, int width, int height, int frameRate);
    DecoderAvailability getDecoderAvailability(SDL_Window* window,
                                               StreamingPreferences::VideoDecoderSelection vds,
                                               int videoFormat, int width, int height, int frameRate);

    static
    bool chooseDecoder(StreamingPreferences::VideoDecoderSelection vds,
                       SDL_Window* window, int videoFormat, int width, int height,
                       int frameRate, bool enableVsync, bool enableFramePacing,
                       bool testOnly,
                       IVideoDecoder*& chosenDecoder);

    static
    void clStageStarting(int stage);

    static
    void clStageFailed(int stage, int errorCode);

    static
    void clConnectionTerminated(int errorCode);

    static
    void clLogMessage(const char* format, ...);

    static
    void clRumble(unsigned short controllerNumber, unsigned short lowFreqMotor, unsigned short highFreqMotor);

    static
    void clConnectionStatusUpdate(int connectionStatus);

    static
    void clSetHdrMode(bool enabled);

    static
    void clRumbleTriggers(uint16_t controllerNumber, uint16_t leftTrigger, uint16_t rightTrigger);

    static
    void clSetMotionEventState(uint16_t controllerNumber, uint8_t motionType, uint16_t reportRateHz);

    static
    void clSetControllerLED(uint16_t controllerNumber, uint8_t r, uint8_t g, uint8_t b);

    static
    int arInit(int audioConfiguration,
               const POPUS_MULTISTREAM_CONFIGURATION opusConfig,
               void* arContext, int arFlags);

    static
    void arCleanup();

    static
    void arDecodeAndPlaySample(char* sampleData, int sampleLength);

    static
    int drSetup(int videoFormat, int width, int height, int frameRate, void*, int);

    static
    void drCleanup();

    static
    int drSubmitDecodeUnit(PDECODE_UNIT du);

    StreamingPreferences* m_Preferences;
    bool m_IsFullScreen;
    SupportedVideoFormatList m_SupportedVideoFormats; // Sorted in order of descending priority
    STREAM_CONFIGURATION m_StreamConfig;
    DECODER_RENDERER_CALLBACKS m_VideoCallbacks;
    AUDIO_RENDERER_CALLBACKS m_AudioCallbacks;
    NvComputer* m_Computer;
    NvApp m_App;
    SDL_Window* m_Window;
    IVideoDecoder* m_VideoDecoder;
    SDL_SpinLock m_DecoderLock;
    bool m_AudioDisabled;
    bool m_AudioMuted;
    Uint32 m_FullScreenFlag;
    QWindow* m_QtWindow;
    bool m_ThreadedExec;
    bool m_UnexpectedTermination;
    std::atomic<bool> m_TerminationReported {false};
    SdlInputHandler* m_InputHandler;
    int m_MouseEmulationRefCount;
    int m_FlushingWindowEventsRef;

    bool m_AsyncConnectionSuccess;
    int m_PortTestResults;

    int m_ActiveVideoFormat;
    int m_ActiveVideoWidth;
    int m_ActiveVideoHeight;
    int m_ActiveVideoFrameRate;

    OpusMSDecoder* m_OpusDecoder;
    IAudioRenderer* m_AudioRenderer;
    OPUS_MULTISTREAM_CONFIGURATION m_ActiveAudioConfig;
    OPUS_MULTISTREAM_CONFIGURATION m_OriginalAudioConfig;
    int m_AudioSampleCount;
    Uint32 m_DropAudioEndTime;

    Overlay::OverlayManager m_OverlayManager;

    static CONNECTION_LISTENER_CALLBACKS k_ConnCallbacks;
    static Session* s_ActiveSession;
    static QSemaphore s_ActiveSessionSemaphore;
};
