/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <memory>
#include <mutex>

#include <portaudio.h>
#include <spdlog/spdlog.h>

#include "audio_engine.h"

audio_engine::audio_engine() { Pa_Initialize(); }
audio_engine::~audio_engine() { Pa_Terminate(); }

std::vector<audio_dev> audio_engine::get_playback_devices(audio_dev *default_dev)
{
    std::vector<audio_dev> devs;

    int count = Pa_GetDeviceCount();
    for(int i = 0; i != count; ++i)
    {
        const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
        const PaHostApiInfo *host_api = Pa_GetHostApiInfo(info->hostApi);

        if(info->maxOutputChannels > 0)
            devs.push_back(audio_dev{i, std::string{info->name} + ": " + host_api->name});
    }

    if(default_dev)
    {
        int default_idx = Pa_GetDefaultOutputDevice();
        *default_dev = devs.at(default_idx);
    }
    return devs;
}

void audio_engine::set_device(audio_dev device)
{
    close_stream();

    std::scoped_lock lock(m_control_mutex);
    m_device = device;
    spdlog::info("audio: set device to {}", device.name);

    if(m_buf)
        open_stream();
}

void audio_engine::set_audio_clip(audio_buffer clip)
{
    spdlog::info("setting audio buffer");
    close_stream(); // Sample rate could change
    std::scoped_lock lock(m_control_mutex);
    m_buf = std::make_shared<buffer_state>(std::move(clip));
    open_stream();
}

void audio_engine::select_region(time_span t, bool loop)
{
    std::scoped_lock lock(m_control_mutex);

    if(m_buf)
        m_buf->select_region(t, loop);
}

void audio_engine::play()
{
    std::scoped_lock lock(m_control_mutex);
    if(m_buf)
        m_buf->m_playing = true;
}

void audio_engine::stop()
{
    std::scoped_lock lock(m_control_mutex);
    if(m_buf)
        m_buf->m_playing = false;
}

playback_state audio_engine::get_state() const
{
    std::scoped_lock lock(m_control_mutex);

    playback_state s{false, 0.};
    if(m_buf)
        s = m_buf->get_state();
    return s;
}

void audio_engine::open_stream()
{
    spdlog::info("audio: opening stream");
    if(!m_buf || m_device.index == paNoDevice)
        return;

    const PaDeviceInfo *info = Pa_GetDeviceInfo(m_device.index);

    m_stream_params = PaStreamParameters{};
    m_stream_params.device = m_device.index;
    m_stream_params.channelCount = 1U;
    m_stream_params.sampleFormat = paFloat32;
    m_stream_params.suggestedLatency = info->defaultHighOutputLatency;

    check_error(
        Pa_OpenStream(
            &m_stream,
            nullptr,
            &m_stream_params,
            m_buf->m_buffer.m_sample_rate_hz,
            paFramesPerBufferUnspecified,
            paNoFlag,
            &audio_engine::pa_callback,
            this),
        "open port audio stream");

    check_error(Pa_StartStream(m_stream), "start port audio stream");
    spdlog::info("stream opened");
}

void audio_engine::close_stream()
{
    if(m_stream)
    {
        spdlog::info("closing stream");
        check_error(Pa_StopStream(m_stream), "stop port audio stream");
        check_error(Pa_CloseStream(m_stream), "close port audio stream");
        m_stream = nullptr;
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
    std::shared_ptr<buffer_state> buf;
    {
        std::scoped_lock lock(m_control_mutex);
        buf = m_buf;
    }
    auto channels = m_stream_params.channelCount;

    if(!buf || !buf->m_playing)
    {
        std::fill(out, out + frames * channels, 0.0f);
        return paContinue;
    }

    auto &state = *buf;
    auto &buffer = buf->m_buffer;

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

        for(int c = 0; c != channels; ++c)
        {
            out[frame * channels + c] = buffer.m_samples[state.m_playhead * channels + c];
        }

        ++state.m_playhead;
    }
    return paContinue;
}
