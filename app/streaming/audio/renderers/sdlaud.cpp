#include "sdl.h"

#include <Limelight.h>
#include <SDL.h>

SdlAudioRenderer::SdlAudioRenderer()
    : m_AudioDevice(0),
      m_AudioBuffer(nullptr),
      m_FrameSize(0),
      m_AudioInitialized(false)
{
}

bool SdlAudioRenderer::prepareForPlayback(const OPUS_MULTISTREAM_CONFIGURATION* opusConfig)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s",
                     SDL_GetError());
        return false;
    }
    m_AudioInitialized = true;
    SDL_AudioSpec want, have;

    SDL_zero(want);
    want.freq = opusConfig->sampleRate;
    want.format = AUDIO_F32SYS;
    want.channels = opusConfig->channelCount;

    // On PulseAudio systems, setting a value too small can cause underruns for other
    // applications sharing this output device. We impose a floor of 480 samples (10 ms)
    // to mitigate this issue. Otherwise, we will buffer up to 3 frames of audio which
    // is 15 ms at regular 5 ms frames and 30 ms at 10 ms frames for slow connections.
    // The buffering helps avoid audio underruns due to network jitter.
#ifndef Q_OS_DARWIN
    want.samples = SDL_max(480, opusConfig->samplesPerFrame * 3);
#else
    // HACK: Changing the buffer size can lead to Bluetooth HFP
    // audio issues on macOS, so we're leaving this alone.
    // https://github.com/moonlight-stream/moonlight-qt/issues/1071
    want.samples = SDL_max(480, opusConfig->samplesPerFrame);
#endif

    m_FrameSize = opusConfig->samplesPerFrame *
                  opusConfig->channelCount *
                  getAudioBufferSampleSize();

    m_AudioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (m_AudioDevice == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to open audio device: %s",
                     SDL_GetError());
        return false;
    }

    m_AudioBuffer = SDL_malloc(m_FrameSize);
    if (m_AudioBuffer == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to allocate audio buffer");
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Desired audio buffer: %u samples (%u bytes)",
                want.samples,
                want.samples * want.channels * getAudioBufferSampleSize());

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Obtained audio buffer: %u samples (%u bytes)",
                have.samples,
                have.size);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "SDL audio driver: %s",
                SDL_GetCurrentAudioDriver());

    // Start playback
    SDL_PauseAudioDevice(m_AudioDevice, 0);

    return true;
}

void SdlAudioRenderer::startPlaybackControl()
{
    // Device pause/resume can wait for the audio callback. Keep these operations
    // off the viewer's activity/input thread, including during packet silence.
    m_PlaybackWake = SDL_CreateSemaphore(0);
    if (m_PlaybackWake != nullptr) {
        m_PlaybackThread = SDL_CreateThread(playbackThread, "AudioIdle", this);
    }
    if (!m_PlaybackThread) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Unable to start audio idle control: %s", SDL_GetError());
    }
}

SdlAudioRenderer::~SdlAudioRenderer()
{
    if (m_PlaybackThread != nullptr) {
        m_StopPlaybackThread = true;
        SDL_SemPost(m_PlaybackWake);
        SDL_WaitThread(m_PlaybackThread, nullptr);
    }
    if (m_PlaybackWake != nullptr) {
        SDL_DestroySemaphore(m_PlaybackWake);
    }

    if (m_AudioDevice != 0) {
        // Stop playback
        SDL_PauseAudioDevice(m_AudioDevice, 1);
        SDL_CloseAudioDevice(m_AudioDevice);
    }

    if (m_AudioBuffer != nullptr) {
        SDL_free(m_AudioBuffer);
    }

    if (m_AudioInitialized) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

void SdlAudioRenderer::updatePlaybackState(unsigned int request)
{
    if (m_PlaybackRequest.exchange(request) != request && m_PlaybackThread) {
        SDL_SemPost(m_PlaybackWake);
    }
}

int SdlAudioRenderer::playbackThread(void* context)
{
    auto renderer = static_cast<SdlAudioRenderer*>(context);
    while (!renderer->m_StopPlaybackThread.load()) {
        Uint32 delay;
        {
            std::lock_guard<std::mutex> lock(renderer->m_PlaybackLock);
            renderer->syncPlaybackState();
            delay = renderer->m_IdleState.timeUntilPause(SDL_GetTicks());
        }
        SDL_SemWaitTimeout(renderer->m_PlaybackWake, delay);
    }
    return 0;
}

void SdlAudioRenderer::syncPlaybackState()
{
    // Serialized with the final queue submission, never with UI publication.
    const unsigned int request = m_PlaybackRequest.load();
    if (request == m_PlaybackGeneration && !m_IdleState.muted()) return;
    const auto action = m_IdleState.update((request & 1) != 0, SDL_GetTicks());
    if (request != m_PlaybackGeneration || action == AudioIdleState::Action::Pause) {
        // Also flush across a quick mute/unmute that the worker sees as one edge.
        SDL_ClearQueuedAudio(m_AudioDevice);
        m_PlaybackGeneration = request;
    }
    if (action == AudioIdleState::Action::Pause) {
        SDL_PauseAudioDevice(m_AudioDevice, 1);
    }
    else if (action == AudioIdleState::Action::Resume) {
        SDL_PauseAudioDevice(m_AudioDevice, 0);
    }
}

void* SdlAudioRenderer::getAudioBuffer(int*)
{
    m_BufferGeneration = m_PlaybackRequest.load();
    return m_AudioBuffer;
}

bool SdlAudioRenderer::submitAudio(int bytesWritten)
{
    if (bytesWritten == 0 || (m_PlaybackRequest.load() & 1)) {
        // Nothing to do
        return true;
    }

    // Don't queue if there's already more than 30 ms of audio data waiting
    // in Moonlight's audio queue.
    if (LiGetPendingAudioDuration() > 30) {
        return true;
    }

    // Provide backpressure on the queue to ensure too many frames don't build up
    // in SDL's audio queue, but don't wait forever to avoid a deadlock if the
    // audio device fails.
    for (int i = 0; i < 100; i++) {
        if (m_PlaybackRequest.load() & 1) {
            return true;
        }
        // Our device may enter a permanent error status upon removal, so we need
        // to recreate the audio device to pick up the new default audio device.
        if (SDL_GetAudioDeviceStatus(m_AudioDevice) == SDL_AUDIO_STOPPED) {
            return false;
        }

        // Only queue more samples where there are 10 frames or less in SDL's queue
        if (SDL_GetQueuedAudioSize(m_AudioDevice) / m_FrameSize <= 10) {
            break;
        }

        SDL_Delay(1);
    }

    std::lock_guard<std::mutex> lock(m_PlaybackLock);
    // Resume before queuing the first fresh frame, even if the worker has not
    // been scheduled yet. No extra PCM buffer or copy is needed.
    syncPlaybackState();
    if (m_IdleState.muted() || m_BufferGeneration != m_PlaybackGeneration) {
        return true;
    }
    if (SDL_QueueAudio(m_AudioDevice, m_AudioBuffer, bytesWritten) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to queue audio sample: %s",
                     SDL_GetError());
    }

    return true;
}

int SdlAudioRenderer::getCapabilities()
{
    // Direct submit can't be used because we use LiGetPendingAudioDuration()
    return CAPABILITY_SUPPORTS_ARBITRARY_AUDIO_DURATION;
}

IAudioRenderer::AudioFormat SdlAudioRenderer::getAudioBufferFormat()
{
    return AudioFormat::Float32NE;
}
