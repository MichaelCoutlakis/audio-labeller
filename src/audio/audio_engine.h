/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <portaudio.h>

#include "../model/project_model.h"

class audio_engine
{
public:
    audio_engine();
    ~audio_engine();
    std::vector<audio_dev> get_playback_devices(audio_dev *default_dev = nullptr);

    void set_device(audio_dev device);

    void set_audio_clip(audio_buffer clip);

    void select_region(time_span t, bool loop);
    void play();
    void stop();

    playback_state get_state() const;

private:
    void open_stream();
    void close_stream();

    static int pa_callback(
        const void *input,
        void *output,
        unsigned long num_frames,
        const PaStreamCallbackTimeInfo *,
        PaStreamCallbackFlags,
        void *user_data);

    int render(float *out, unsigned long frames);

    void check_error(PaError c, std::string msg)
    {
        if(c != paNoError)
            throw std::runtime_error(msg + ": " + Pa_GetErrorText(c));
    }

    PaStream *m_stream{nullptr};
    audio_dev m_device{audio_dev{paNoDevice, ""}};
    PaStreamParameters m_stream_params;

    struct buffer_state
    {
        buffer_state(audio_buffer buffer) :
            m_buffer(std::move(buffer))
        {
            m_region_end_frame = m_buffer.num_frames();
        }

        void select_region(time_span t, bool loop)
        {
            double t0 = std::clamp(t.first, 0., m_buffer.duration_s());
            double t1 = std::clamp(t.second, t0, m_buffer.duration_s());
            m_playhead = t0 * m_buffer.m_sample_rate_hz;
            m_region_start_frame = m_playhead;
            m_region_end_frame = std::clamp<uint64_t>(
                t1 * m_buffer.m_sample_rate_hz,
                m_playhead,
                m_buffer.num_frames());
            m_loop = loop;
        }

        playback_state get_state() const
        {
            return playback_state{m_playing, m_playhead * 1.0 / m_buffer.m_sample_rate_hz};
        }
        audio_buffer m_buffer;

        uint64_t m_playhead{};
        uint64_t m_region_start_frame{};
        uint64_t m_region_end_frame{};
        bool m_loop = false;
        bool m_playing{false};
    };

    std::shared_ptr<buffer_state> m_buf;

    mutable std::mutex m_control_mutex; // only for control thread
};