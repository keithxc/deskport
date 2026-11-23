// Real SDL dummy-device coverage; no hardware output or streaming connection.
#include <SDL.h>
#include <QtGlobal>
#include <atomic>
#include <mutex>
#define private public
#include "streaming/audio/renderers/sdl.h"
#undef private
#undef NDEBUG
#include <cassert>
#include <cstring>
#include <iostream>

// The renderer only needs the network queue depth; this fixture has no network.
int LiGetPendingAudioDuration(void) { return 0; }

template<class Predicate>
static void waitFor(Predicate predicate)
{
    const Uint32 start = SDL_GetTicks();
    while (!predicate()) {
        assert(SDL_GetTicks() - start < 1000);
        SDL_Delay(1);
    }
}

static void waitForRequest(SdlAudioRenderer& renderer, unsigned int request)
{
    waitFor([&] {
        std::lock_guard<std::mutex> lock(renderer.m_PlaybackLock);
        return renderer.m_PlaybackGeneration == request;
    });
}

static int fillFrame(SdlAudioRenderer& renderer)
{
    int bytes = renderer.m_FrameSize;
    std::memset(renderer.getAudioBuffer(&bytes), 0, bytes);
    return bytes;
}

int main()
{
    using Action = AudioIdleState::Action;
    AudioIdleState idle;
    assert(idle.update(false, 0) == Action::None);
    assert(idle.timeUntilPause(0) == UINT32_MAX);
    assert(idle.update(true, 0) == Action::Mute);
    assert(idle.timeUntilPause(1499) == 1);
    assert(idle.update(true, 1499) == Action::None);
    assert(idle.timeUntilPause(1500) == 0);
    assert(idle.update(true, 1500) == Action::Pause);
    assert(idle.timeUntilPause(1501) == UINT32_MAX);
    assert(idle.update(false, 1501) == Action::Resume);
    assert(idle.update(true, UINT32_MAX - 500) == Action::Mute);
    assert(idle.update(true, 998) == Action::None);
    assert(idle.update(true, 999) == Action::Pause);
    assert(idle.update(false, 1000) == Action::Resume);

    assert(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);
    OPUS_MULTISTREAM_CONFIGURATION config = {};
    config.sampleRate = 48000;
    config.channelCount = 2;
    config.samplesPerFrame = 240;

    // Acquire and release exactly the renderer's own subsystem reference.
    for (bool shared : {false, true}) {
        if (shared) assert(SDL_InitSubSystem(SDL_INIT_AUDIO) == 0);
        SDL_AudioDeviceID device;
        {
            SdlAudioRenderer renderer;
            assert(renderer.prepareForPlayback(&config));
            assert(renderer.m_PlaybackThread == nullptr); // Capability probes only open the device.
            renderer.startPlaybackControl();
            assert(std::strcmp(SDL_GetCurrentAudioDriver(), "dummy") == 0);
            assert(renderer.getCapabilities() == CAPABILITY_SUPPORTS_ARBITRARY_AUDIO_DURATION);
            device = renderer.m_AudioDevice;
            assert(SDL_GetAudioDeviceStatus(device) == SDL_AUDIO_PLAYING);

            // Seed a real queue deterministically, then restore normal playback.
            SDL_PauseAudioDevice(device, 1);
            assert(renderer.submitAudio(fillFrame(renderer)));
            assert(SDL_GetQueuedAudioSize(device) == Uint32(renderer.m_FrameSize));
            SDL_PauseAudioDevice(device, 0);
            renderer.updatePlaybackState(1);
            waitForRequest(renderer, 1);
            assert(SDL_GetQueuedAudioSize(device) == 0);
            assert(SDL_GetAudioDeviceStatus(device) == SDL_AUDIO_PLAYING);
            SDL_Delay(30); // Short mute, no packets.
            renderer.updatePlaybackState(2);
            waitForRequest(renderer, 2);
            assert(SDL_GetAudioDeviceStatus(device) == SDL_AUDIO_PLAYING);

            // A complete toggle during decoding must not submit an old frame.
            const int oldFrame = fillFrame(renderer);
            {
                std::lock_guard<std::mutex> lock(renderer.m_PlaybackLock);
                renderer.updatePlaybackState(3);
                renderer.updatePlaybackState(4);
            }
            assert(renderer.submitAudio(oldFrame));
            waitForRequest(renderer, 4);
            assert(SDL_GetQueuedAudioSize(device) == 0);

            renderer.updatePlaybackState(5);
            waitForRequest(renderer, 5);
            SDL_Delay(1550); // No packets or further publications during idle.
            waitFor([&] { return SDL_GetAudioDeviceStatus(device) == SDL_AUDIO_PAUSED; });
            assert(SDL_GetQueuedAudioSize(device) == 0);
            assert(renderer.submitAudio(fillFrame(renderer))); // Muted frames stay out.
            assert(SDL_GetQueuedAudioSize(device) == 0);

            renderer.updatePlaybackState(6);
            assert(renderer.submitAudio(fillFrame(renderer))); // First fresh frame resumes.
            assert(SDL_GetAudioDeviceStatus(device) == SDL_AUDIO_PLAYING);
            renderer.updatePlaybackState(7); // Teardown also wakes a pending idle wait.
        }
        assert(SDL_GetAudioDeviceStatus(device) == SDL_AUDIO_STOPPED);
        assert(bool(SDL_WasInit(SDL_INIT_AUDIO)) == shared);
        if (shared) SDL_QuitSubSystem(SDL_INIT_AUDIO);
        assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);
    }
    std::cout << "audio idle: real SDL dummy queue, pause/resume and ownership passed\n";
}
