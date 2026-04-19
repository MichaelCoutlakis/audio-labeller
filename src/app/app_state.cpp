/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include <AudioFile/AudioFile.h>

#include "app_state.h"

app_settings app_settings::load()
{
    app_settings s{};

    if(!std::filesystem::exists(m_filename))
        return s;

    std::ifstream settings_file(m_filename);
    if(!settings_file)
        return s;
    auto j = nlohmann::json::parse(settings_file);
    s.current_file = j.value("current_file", std::string{});
    if(auto dev = j.find("audio_device"); dev != j.end())
    {
        s.dev = audio_dev{};
        s.dev->index = dev->value("index", 0);
        s.dev->name = dev->value("name", std::string{});
    }
    return s;
}

void app_settings::save()
{
    nlohmann::json j{{"current_file", current_file}};
    if(dev)
        j["audio_device"] = {{"index", dev->index}, {"name", dev->name}};
    std::ofstream f(m_filename);
    f << j.dump(2);
}

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

    m_audio_min_max_map = audio_min_max_pyramid_map(m_active_audio);
}

const audio_min_max_level *app_state::get_audio_min_max_level(size_t samples_per_px)
{
    return m_audio_min_max_map.get_level(samples_per_px);
}

audio_min_max_pyramid_map::audio_min_max_pyramid_map(const audio_buffer &buffer)
{
    for(size_t samples_per_bin = std::min<size_t>(64U, buffer.num_frames());
        samples_per_bin < buffer.num_frames();
        samples_per_bin *= 4)
    {
        audio_min_max_level lvl{samples_per_bin};
        const size_t num_bins = (buffer.num_frames() + samples_per_bin - 1U) / samples_per_bin;

        lvl.m_max_vals.resize(num_bins);
        lvl.m_min_vals.resize(num_bins);
        lvl.m_bin_times.resize(num_bins);

        for(size_t bin = 0U; bin != num_bins; ++bin)
        {
            const size_t start = bin * samples_per_bin;
            const size_t end = std::min(start + samples_per_bin, buffer.num_frames());

            auto it =
                std::minmax_element(buffer.m_samples.data() + start, buffer.m_samples.data() + end);

            lvl.m_min_vals[bin] = *it.first;
            lvl.m_max_vals[bin] = *it.second;
            lvl.m_bin_times[bin] = float(samples_per_bin * bin) / buffer.m_sample_rate_hz;
        }
        m_levels.push_back(std::move(lvl));
    }
}

const audio_min_max_level *audio_min_max_pyramid_map::get_level(size_t samples_per_px) const
{
    if(m_levels.empty())
        return nullptr;
    for(auto &lvl : m_levels)
        if(lvl.m_samples_per_bin >= samples_per_px)
            return &lvl;
    return &m_levels.back();
}
