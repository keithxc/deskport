#pragma once

#include "renderer.h"
#include "../audioidlestate.h"
#include <SDL.h>
#include <atomic>
#include <mutex>

class SdlAudioRenderer : public IAudioRenderer
{
public:
    SdlAudioRenderer();

    virtual ~SdlAudioRenderer();

    virtual bool prepareForPlayback(const OPUS_MULTISTREAM_CONFIGURATION* opusConfig) override;

    void startPlaybackControl() override;
    virtual void updatePlaybackState(unsigned int request) override;

    virtual void* getAudioBuffer(int* size) override;

    virtual bool submitAudio(int bytesWritten) override;

    virtual int getCapabilities() override;

    virtual AudioFormat getAudioBufferFormat() override;

private:
    static int playbackThread(void* context);
    void syncPlaybackState();

    SDL_AudioDeviceID m_AudioDevice;
    void* m_AudioBuffer;
    int m_FrameSize;
    bool m_AudioInitialized;
    AudioIdleState m_IdleState;
    std::atomic<unsigned int> m_PlaybackRequest{0};
    unsigned int m_PlaybackGeneration = 0;
    unsigned int m_BufferGeneration = 0;
    std::mutex m_PlaybackLock;
    SDL_sem* m_PlaybackWake = nullptr;
    SDL_Thread* m_PlaybackThread = nullptr;
    std::atomic<bool> m_StopPlaybackThread{false};
};
