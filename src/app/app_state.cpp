/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "../io/audio_source.h"
#include "../model/paths.h"
#include "actions.h"
#include "app_state.h"

namespace
{
template <typename E>
E enum_value(const nlohmann::json &j, const char *key, E fallback, std::initializer_list<const char *> names)
{
    const auto text = j.value(key, std::string{});
    int i = 0;
    for(auto name : names)
    {
        if(text == name)
            return static_cast<E>(i);
        ++i;
    }
    return fallback;
}

template <typename E>
const char *enum_name(E value, std::initializer_list<const char *> names)
{
    return *(names.begin() + static_cast<int>(value));
}

constexpr auto layout_names = {"stacked", "class_lanes", "full_height"};
constexpr auto filter_names = {"all", "todo", "done", "skip", "suggested"};
constexpr auto scale_names = {"linear", "log", "mel"};
}

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
            s.m_device_name = dev->value("name", std::string{});
        s.m_font = j.value("font", s.m_font);
        s.m_font_size = std::clamp(j.value("font_size", s.m_font_size), 8.f, 48.f);
        s.m_layout = enum_value(j, "layout", s.m_layout, layout_names);
        s.m_file_filter = enum_value(j, "file_filter", s.m_file_filter, filter_names);
        s.m_nudge_s = std::max(1e-4, j.value("nudge_ms", s.m_nudge_s * 1000.) / 1000.);
        s.m_fine_nudge_s =
            std::max(1e-5, j.value("fine_nudge_ms", s.m_fine_nudge_s * 1000.) / 1000.);
        s.m_playback_speed = std::clamp(j.value("playback_speed", s.m_playback_speed), 0.1f, 2.f);
        s.m_show_rejected = j.value("show_rejected", s.m_show_rejected);
        s.m_auto_advance = j.value("auto_advance", s.m_auto_advance);
        s.m_waveform_gain = std::clamp(j.value("waveform_gain", s.m_waveform_gain), 1.f, 1000.f);
        if(auto spec = j.find("spectrogram"); spec != j.end() && spec->is_object())
        {
            s.m_show_spectrogram = spec->value("show", s.m_show_spectrogram);
            s.m_spectrogram_scale = enum_value(*spec, "scale", s.m_spectrogram_scale, scale_names);
            s.m_spectrogram_gain_db = spec->value("gain_db", s.m_spectrogram_gain_db);
            s.m_spectrogram_range_db = spec->value("range_db", s.m_spectrogram_range_db);
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

    nlohmann::json j{
        {"project", project_file},
        {"font", m_font},
        {"font_size", m_font_size},
        {"layout", enum_name(m_layout, layout_names)},
        {"file_filter", enum_name(m_file_filter, filter_names)},
        {"nudge_ms", m_nudge_s * 1000.},
        {"fine_nudge_ms", m_fine_nudge_s * 1000.},
        {"playback_speed", m_playback_speed},
        {"show_rejected", m_show_rejected},
        {"auto_advance", m_auto_advance},
        {"waveform_gain", m_waveform_gain},
        {"spectrogram",
         {{"show", m_show_spectrogram},
          {"scale", enum_name(m_spectrogram_scale, scale_names)},
          {"gain_db", m_spectrogram_gain_db},
          {"range_db", m_spectrogram_range_db}}}
    };
    if(!m_device_name.empty())
        j["audio_device"] = {{"name", m_device_name}};
    std::ofstream f(m_filename);
    f << j.dump(2);
}

void app_state::report_error(const std::string &message)
{
    spdlog::error("{}", message);
    if(!m_error_message.empty())
        m_error_message += "\n\n";
    m_error_message += message;
}

void app_state::report_info(const std::string &message)
{
    spdlog::info("{}", message);
    if(!m_info_message.empty())
        m_info_message += "\n\n";
    m_info_message += message;
}

app_state::app_state() :
    settings(app_settings::load())
{
}

bool app_state::is_selected_file(const std::filesystem::path &path) const
{
    if(path.empty())
        return false;

    return m_active_file.has_value() && m_active_file.value() == path;
}

void app_state::set_selected_file(
    const std::filesystem::path &path,
    std::optional<uint32_t> channel)
{
    if(path.empty() || is_selected_file(path))
        return;

    // File not yet selected - select it and load the audio
    load_audio(path, channel);
    m_active_file = path;
    m_selected_labels.clear();
    m_unlabelled.clear();
}

void app_state::load_audio(const std::filesystem::path &path, std::optional<uint32_t> channel)
{
    m_active_audio = std::make_shared<const audio_buffer>(load_mono(path, channel));
    spdlog::info("Loaded {}, SR={} Hz", generic_utf8(path), m_active_audio->m_sample_rate_hz);
    m_audio_min_max_map = audio_min_max_pyramid_map(*m_active_audio);
}

std::vector<const label *> app_state::visible_labels(const project_model &project) const
{
    std::vector<const label *> out;
    for(const auto &l : m_unlabelled)
        out.push_back(&l);
    if(m_active_file)
        for(const auto &l : project.get_labels(*m_active_file))
            if(l.m_state != label_state::rejected || settings.m_show_rejected)
                out.push_back(&l);
    return out;
}

std::map<label_id, lane> app_state::label_bands(const project_model &project) const
{
    return layout_labels(visible_labels(project), project.m_label_dict, settings.m_layout);
}

time_span app_state::playback_window(const label &l) const
{
    return ::playback_window(l, m_review, m_active_audio->duration_s());
}

std::vector<const audio_file_entry *> app_state::filtered_files(const project_model &project) const
{
    std::vector<const audio_file_entry *> files;
    for(const auto &file : project.get_files())
    {
        const auto status = project.get_status(file.m_path);
        const auto filter = settings.m_file_filter;
        const auto has_suggestions = [&]
        {
            return std::ranges::any_of(
                project.get_labels(file.m_path),
                [](const label &l) { return l.m_state == label_state::suggested; });
        };
        const bool pass = filter == file_filter::all ||
            (filter == file_filter::todo && status == file_status::todo) ||
            (filter == file_filter::done && status == file_status::done) ||
            (filter == file_filter::skip && status == file_status::skip) ||
            (filter == file_filter::suggested && has_suggestions());
        if(pass)
            files.push_back(&file);
    }
    return files;
}

const audio_min_max_level *app_state::get_audio_min_max_level(size_t samples_per_px) const
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
