/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <stdexcept>
#include <string>

#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

#include "audio_engine.h"

namespace
{
constexpr int default_sample_rate_hz = 48000; ///< Before any clip is loaded
constexpr size_t scratch_frames = 1U << 16;   ///< More than SDL asks for at a time
}

class audio_engine::stream_lock
{
public:
    explicit stream_lock(SDL_AudioStream *stream) :
        m_stream(stream)
    {
        if(m_stream)
            SDL_LockAudioStream(m_stream);
    }
    ~stream_lock()
    {
        if(m_stream)
            SDL_UnlockAudioStream(m_stream);
    }
    stream_lock(const stream_lock &) = delete;
    stream_lock &operator=(const stream_lock &) = delete;

private:
    SDL_AudioStream *m_stream;
};

audio_engine::audio_engine() :
    m_scratch(scratch_frames)
{
}

audio_engine::~audio_engine() { close(); }

std::vector<audio_dev> audio_engine::get_playback_devices() const
{
    std::vector<audio_dev> devs{audio_dev{SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, "System default"}};

    int count = 0;
    SDL_AudioDeviceID *ids = SDL_GetAudioPlaybackDevices(&count);
    if(!ids)
    {
        spdlog::error("audio: could not list devices: {}", SDL_GetError());
        return devs;
    }
    for(int i = 0; i != count; ++i)
        if(const char *name = SDL_GetAudioDeviceName(ids[i]))
            devs.push_back(audio_dev{ids[i], name});
    SDL_free(ids);
    return devs;
}

SDL_AudioSpec audio_engine::clip_spec() const
{
    const int rate = m_clip->m_sample_rate_hz ? static_cast<int>(m_clip->m_sample_rate_hz)
                                              : default_sample_rate_hz;
    return SDL_AudioSpec{SDL_AUDIO_F32, 1, rate};
}

void audio_engine::set_device(const audio_dev &device)
{
    close();

    const auto spec = clip_spec();
    m_stream = SDL_OpenAudioDeviceStream(device.id, &spec, &audio_engine::feed, this);
    if(!m_stream)
        throw std::runtime_error(
            "Could not open audio device " + device.name + ": " + SDL_GetError());

    if(!SDL_ResumeAudioStreamDevice(m_stream))
        spdlog::error("audio: could not start device: {}", SDL_GetError());
    spdlog::info("audio: set device to {}", device.name);
}

void audio_engine::set_audio_clip(std::shared_ptr<const audio_buffer> clip)
{
    stream_lock lock(m_stream);
    m_clip = clip ? std::move(clip) : std::make_shared<audio_buffer>();
    m_cursor.reset(*m_clip);

    if(m_stream)
    {
        SDL_ClearAudioStream(m_stream);
        const auto spec = clip_spec();
        if(!SDL_SetAudioStreamFormat(m_stream, &spec, nullptr))
            throw std::runtime_error(std::string{"Could not set audio format: "} + SDL_GetError());
    }
}

void audio_engine::select_region(time_span t, bool loop)
{
    stream_lock lock(m_stream);
    m_cursor.select_region(*m_clip, t, loop);
    if(m_stream)
        SDL_ClearAudioStream(m_stream); // Jump straight to the new region
}

void audio_engine::set_playback_loop(bool loop)
{
    stream_lock lock(m_stream);
    m_cursor.m_loop = loop;
}

void audio_engine::play()
{
    stream_lock lock(m_stream);
    if(m_stream)
        SDL_ClearAudioStream(m_stream);
    if(!m_cursor.play())
        spdlog::info("audio: nothing to play");
}

void audio_engine::stop()
{
    stream_lock lock(m_stream);
    m_cursor.m_playing = false;
    if(m_stream)
        SDL_ClearAudioStream(m_stream);
}

playback_state audio_engine::get_state() const
{
    stream_lock lock(m_stream);
    if(m_clip->m_sample_rate_hz == 0)
        return {};

    // Audio handed to SDL but not yet sent to the device hasn't been heard yet:
    uint64_t heard = m_cursor.m_playhead;
    if(m_stream && m_cursor.m_playing)
    {
        const int queued_bytes = SDL_GetAudioStreamQueued(m_stream);
        const uint64_t queued = queued_bytes > 0 ? uint64_t(queued_bytes) / sizeof(float) : 0U;
        heard = heard > m_cursor.m_region_start + queued ? heard - queued : m_cursor.m_region_start;
    }
    return playback_state{m_cursor.m_playing, double(heard) / m_clip->m_sample_rate_hz};
}

void audio_engine::close()
{
    if(m_stream)
    {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }
}

void SDLCALL audio_engine::feed(void *user_data, SDL_AudioStream *stream, int additional_bytes, int)
{
    // Called on SDL's audio thread, with the stream locked
    auto &self = *static_cast<audio_engine *>(user_data);
    if(additional_bytes <= 0 || !self.m_cursor.m_playing)
        return; // SDL plays silence

    const size_t wanted = (size_t(additional_bytes) + sizeof(float) - 1U) / sizeof(float);
    if(self.m_scratch.size() < wanted)
        self.m_scratch.resize(wanted); // Rare: only if SDL asks for more than the usual maximum

    const size_t n = self.m_cursor.read(*self.m_clip, self.m_scratch.data(), wanted);
    if(n)
        SDL_PutAudioStreamData(stream, self.m_scratch.data(), static_cast<int>(n * sizeof(float)));
}
