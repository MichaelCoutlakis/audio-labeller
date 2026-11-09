/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <spdlog/spdlog.h>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include <AudioFile/AudioFile.h>

#include "actions.h"
#include "app_state.h"

app_settings app_settings::load()
{
    app_settings s{};

    if(!std::filesystem::exists(m_filename))
        return s;

    std::ifstream settings_file(m_filename);
    if(!settings_file)
        return s;
    try
    {
        auto j = nlohmann::json::parse(settings_file);
        s.project_file = j.value("project", std::string{});
        if(auto dev = j.find("audio_device"); dev != j.end())
        {
            s.dev = audio_dev{};
            s.dev->index = dev->value("index", 0);
            s.dev->name = dev->value("name", std::string{});
        }
    }
    catch(const nlohmann::json::exception &e)
    {
        s = app_settings{};
        s.m_load_error = "Could not read " + m_filename + ":\n" + e.what();
    }
    return s;
}

void app_settings::save()
{
    if(!m_load_error.empty())
        return;

    nlohmann::json j{{"project", project_file}};
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

    const size_t num_frames = audio_file.getNumSamplesPerChannel();
    if(audio_file.getSampleRate() == 0 || num_frames == 0)
        throw std::runtime_error("No audio in file " + path.string());

    // Mix down to mono so that sounds on any channel can be seen and heard:
    audio_buffer buffer;
    buffer.m_channels = 1;
    buffer.m_sample_rate_hz = audio_file.getSampleRate();
    buffer.m_samples.assign(num_frames, 0.f);

    const auto num_channels = static_cast<float>(audio_file.getNumChannels());
    for(const auto &channel : audio_file.samples)
        for(size_t frame = 0U; frame != std::min(num_frames, channel.size()); ++frame)
            buffer.m_samples[frame] += channel[frame] / num_channels;

    return buffer;
}

void app_state::report_error(const std::string &message)
{
    spdlog::error("{}", message);
    if(!m_error_message.empty())
        m_error_message += "\n\n";
    m_error_message += message;
}

app_state::app_state() :
    settings(app_settings::load())
{
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
    spdlog::info("Loaded {}, SR={} Hz", path.string(), m_active_audio.m_sample_rate_hz);

    m_audio_min_max_map = audio_min_max_pyramid_map(m_active_audio);
    m_selected_labels.clear();
    m_unlabelled.clear();
}

const audio_min_max_level *app_state::get_audio_min_max_level(size_t samples_per_px)
{
    return m_audio_min_max_map.get_level(samples_per_px);
}
void app_state::select_playback_region(double start_time, double stop_time)
{
    add_action(actions::select_playback_region({start_time, stop_time}, m_loop));
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
