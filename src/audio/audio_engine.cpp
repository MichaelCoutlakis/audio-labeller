/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <memory>
#include <mutex>
#include <utility>

#include <portaudio.h>
#include <spdlog/spdlog.h>

#include "audio_engine.h"

struct audio_engine::buffer_state
{
    buffer_state(audio_buffer buffer) :
        m_buffer(std::move(buffer))
    {
        m_region_end_frame = m_buffer.num_frames();
    }

    bool playable() const
    {
        return m_region_end_frame > m_region_start_frame &&
            m_buffer.m_sample_rate_hz > 0 &&
            !m_buffer.m_samples.empty();
    }

    void select_region(time_span t, bool loop)
    {
        double t0 = std::clamp(t.first, 0., m_buffer.duration_s());
        double t1 = std::clamp(t.second, t0, m_buffer.duration_s());
        m_playhead = t0 * m_buffer.m_sample_rate_hz;
        m_playhead_frac = 0.;
        m_region_start_frame = m_playhead;
        m_region_end_frame =
            std::clamp<uint64_t>(t1 * m_buffer.m_sample_rate_hz, m_playhead, m_buffer.num_frames());
        m_loop = loop;
    }

    void play()
    {
        if(m_region_end_frame > m_region_start_frame)
        {
            m_playhead = m_region_start_frame;
            m_playhead_frac = 0.;
            m_playing = true;
            spdlog::info("audio: play started");
        }
        else
            spdlog::info("audio: cannot play empty buffer");
    }
    void stop()
    {
        m_playing = false;
        spdlog::info("audio: stopped");
    }

    playback_state get_state() const
    {
        return playback_state{m_playing, m_playhead * 1.0 / m_buffer.m_sample_rate_hz};
    }
    audio_buffer m_buffer;

    uint64_t m_playhead{};
    double m_playhead_frac{}; ///< Position between m_playhead and the next frame when resampling
    uint64_t m_region_start_frame{};
    uint64_t m_region_end_frame{};
    bool m_loop = false;
    bool m_playing{false};
};

audio_engine::audio_engine() :
    m_buf(std::make_shared<buffer_state>(audio_buffer{}))
{
    if(const PaError err = Pa_Initialize(); err != paNoError)
        spdlog::error("audio: could not initialise: {}", Pa_GetErrorText(err));
}
audio_engine::~audio_engine() { Pa_Terminate(); }

std::vector<audio_dev> audio_engine::get_playback_devices(audio_dev *default_dev)
{
    std::vector<audio_dev> devs;

    int count = Pa_GetDeviceCount();
    for(int i = 0; i < count; ++i) // Count is negative on error
    {
        const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
        if(!info)
            continue;
        const PaHostApiInfo *host_api = Pa_GetHostApiInfo(info->hostApi);

        if(info->maxOutputChannels > 0)
            devs.push_back(audio_dev{i, std::string{info->name} + ": " + host_api->name});
    }

    if(default_dev)
    {
        // PortAudio's index is into all devices, whereas devs only holds the output devices
        const auto it = std::ranges::find(devs, Pa_GetDefaultOutputDevice(), &audio_dev::index);
        if(it != devs.end())
            *default_dev = *it;
        else
            *default_dev = devs.empty() ? audio_dev{paNoDevice, ""} : devs.front();
    }
    return devs;
}

void audio_engine::set_device(audio_dev device)
{
    close_stream();

    std::scoped_lock lock(m_mx_buf);
    m_device = device;
    spdlog::info("audio: set device to {}", device.name);

    if(m_buf)
        open_stream();
}

void audio_engine::set_audio_clip(audio_buffer clip)
{
    spdlog::info("setting audio buffer");
    close_stream(); // Sample rate could change
    std::scoped_lock lock(m_mx_buf);
    m_buf = std::make_shared<buffer_state>(std::move(clip));
    open_stream();
}

void audio_engine::select_region(time_span t, bool loop)
{
    std::scoped_lock lock(m_mx_buf);
    m_buf->select_region(t, loop);
}
void audio_engine::set_playback_loop(bool loop)
{
    std::scoped_lock lock(m_mx_buf);
    m_buf->m_loop = loop;
}

void audio_engine::play()
{
    std::scoped_lock lock(m_mx_buf);
    m_buf->play();
}

void audio_engine::stop()
{
    std::scoped_lock lock(m_mx_buf);
    m_buf->stop();
}

playback_state audio_engine::get_state() const
{
    std::scoped_lock lock(m_mx_buf);
    return m_buf->get_state();
}

void audio_engine::open_stream()
{
    spdlog::info("audio: opening stream");
    if(!m_buf->playable() || m_device.index == paNoDevice)
        return;

    const PaDeviceInfo *info = Pa_GetDeviceInfo(m_device.index);
    if(!info)
        throw std::runtime_error("Audio device is no longer available: " + m_device.name);

    m_stream_params = PaStreamParameters{};
    m_stream_params.device = m_device.index;
    m_stream_params.channelCount = 1U;
    m_stream_params.sampleFormat = paFloat32;
    m_stream_params.suggestedLatency = info->defaultHighOutputLatency;

    // Devices such as WASAPI in shared mode only accept their own rate:
    m_stream_rate_hz = m_buf->m_buffer.m_sample_rate_hz;
    if(Pa_IsFormatSupported(nullptr, &m_stream_params, m_stream_rate_hz) != paFormatIsSupported)
    {
        spdlog::info(
            "audio: {} Hz not supported by device, resampling to {} Hz",
            m_stream_rate_hz,
            info->defaultSampleRate);
        m_stream_rate_hz = info->defaultSampleRate;
    }

    PaStream *stream{nullptr};
    check_error(
        Pa_OpenStream(
            &stream,
            nullptr,
            &m_stream_params,
            m_stream_rate_hz,
            paFramesPerBufferUnspecified,
            paNoFlag,
            &audio_engine::pa_callback,
            this),
        "open port audio stream");

    if(const PaError err = Pa_StartStream(stream); err != paNoError)
    {
        Pa_CloseStream(stream);
        check_error(err, "start port audio stream");
    }
    m_stream = stream;
    spdlog::info("stream opened");
}

void audio_engine::close_stream()
{
    if(m_stream)
    {
        spdlog::info("closing stream");

        // Failures are logged only: The stream is gone either way and the next open must proceed
        PaStream *stream = std::exchange(m_stream, nullptr);
        if(const PaError err = Pa_StopStream(stream); err != paNoError)
            spdlog::error("audio: stop stream: {}", Pa_GetErrorText(err));
        if(const PaError err = Pa_CloseStream(stream); err != paNoError)
            spdlog::error("audio: close stream: {}", Pa_GetErrorText(err));
    }
}

int audio_engine::pa_callback(
    const void *input,
    void *output,
    unsigned long num_frames,
    const PaStreamCallbackTimeInfo *,
    PaStreamCallbackFlags,
    void *user_data)
{
    auto *self = static_cast<audio_engine *>(user_data);
    return self->render((float *)output, num_frames);
}

int audio_engine::render(float *out, unsigned long frames)
{
    auto channels = m_stream_params.channelCount;

    // The lock also guards the playback state below. It's only ever held briefly by other threads
    // but the audio thread must not block on it, so output silence in the unlikely event of
    // contention.
    std::unique_lock lock(m_mx_buf, std::try_to_lock);
    if(!lock.owns_lock() || !m_buf->m_playing)
    {
        std::fill(out, out + frames * channels, 0.0f);
        return paContinue;
    }

    auto &state = *m_buf;
    auto &buffer = state.m_buffer;
    const uint64_t last_frame = buffer.num_frames() - 1U;
    const double step = buffer.m_sample_rate_hz / m_stream_rate_hz;

    for(unsigned long frame = 0; frame != frames; ++frame)
    {
        if(state.m_playhead >= state.m_region_end_frame)
        {
            if(state.m_loop)
                state.m_playhead = state.m_region_start_frame;
            else
            {
                state.m_playing = false;
                std::fill(out + frame * channels, out + frames * channels, 0.f);
                break;
            }
        }

        const uint64_t next = std::min(state.m_playhead + 1U, last_frame);
        const auto frac = static_cast<float>(state.m_playhead_frac);
        for(int c = 0; c != channels; ++c)
        {
            const float s0 = buffer.m_samples[state.m_playhead * channels + c];
            const float s1 = buffer.m_samples[next * channels + c];
            out[frame * channels + c] = s0 + frac * (s1 - s0);
        }

        // With matching rates the step is exactly 1 and this is a plain copy
        state.m_playhead_frac += step;
        const auto whole_frames = static_cast<uint64_t>(state.m_playhead_frac);
        state.m_playhead += whole_frames;
        state.m_playhead_frac -= static_cast<double>(whole_frames);
    }
    return paContinue;
}
void audio_engine::check_error(PaError c, std::string msg)
{
    if(c != paNoError)
        throw std::runtime_error(msg + ": " + Pa_GetErrorText(c));
}
