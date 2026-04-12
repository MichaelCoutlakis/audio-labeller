/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include "app_state.h"
#include <iostream>
#include <stdexcept>

#include <AudioFile/AudioFile.h>

audio_buffer load_audio(const std::filesystem::path &path)
{
    AudioFile<float> audio_file;

    if(!audio_file.load(path.string()))
        throw std::runtime_error("Could not load audio file " + path.string());

    if(audio_file.getNumChannels() < 1)
        throw std::runtime_error("Must have at least one channel");

    // TODO: Only loading first channel for now:

    audio_buffer buffer;
    buffer.m_channels = 1;
    buffer.m_sample_rate_hz = audio_file.getSampleRate();
    buffer.m_samples = audio_file.samples[0];
    return buffer;
}

bool app_state::is_selected_file(const std::filesystem::path &path)
{
    if(path.empty())
        return false;

    return m_active_file.has_value() && m_active_file.value() == path;
}

void app_state::set_selected_file(const std::filesystem::path &path)
{
    if(path.empty() || is_selected_file(path))
        return;

    // File not yet selected - select it and load the audio
    m_active_audio = load_audio(path);
    m_active_file = path;
    std::cout << "Loaded " << path.string() << ", SR = " << m_active_audio.m_sample_rate_hz << "\n";
}
