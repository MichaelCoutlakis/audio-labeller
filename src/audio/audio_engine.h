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
    void set_playback_loop(bool loop);

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

    void check_error(PaError c, std::string msg);

    PaStream *m_stream{nullptr};
    audio_dev m_device{audio_dev{paNoDevice, ""}};
    PaStreamParameters m_stream_params;

    struct buffer_state;
    std::shared_ptr<buffer_state> m_buf; ///< Always valid but may be default
    mutable std::mutex m_mx_buf;
};