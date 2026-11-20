/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "../io/audio_source.h"
#include "../model/palette.h"
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
constexpr auto finder_names = {"none", "by_class"};
constexpr auto sort_names = {"path", "class_count"};
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
        s.m_project_file = j.value("project", std::string{});
        if(auto dev = j.find("audio_device"); dev != j.end())
            s.m_device_name = dev->value("name", std::string{});
        s.m_font = j.value("font", s.m_font);
        s.m_font_size = std::clamp(j.value("font_size", s.m_font_size), 8.f, 48.f);
        s.m_layout = enum_value(j, "layout", s.m_layout, layout_names);
        auto &list = s.m_file_list;
        list.m_status = enum_value(j, "file_filter", list.m_status, filter_names);
        if(auto f = j.find("file_list"); f != j.end() && f->is_object())
        {
            list.m_search = f->value("search", list.m_search);
            list.m_finder = enum_value(*f, "finder", list.m_finder, finder_names);
            list.m_sort = enum_value(*f, "sort", list.m_sort, sort_names);
            list.m_descending = f->value("descending", list.m_descending);
            if(auto c = f->find("by_class"); c != f->end() && c->is_object())
            {
                list.m_classes.m_classes = c->value("classes", list.m_classes.m_classes);
                list.m_classes.m_suggestions = c->value("suggestions", list.m_classes.m_suggestions);
                list.m_classes.m_rejected = c->value("rejected", list.m_classes.m_rejected);
            }
        }
        s.m_nudge_s = std::max(1e-4, j.value("nudge_ms", s.m_nudge_s * 1000.) / 1000.);
        s.m_fine_nudge_s =
            std::max(1e-5, j.value("fine_nudge_ms", s.m_fine_nudge_s * 1000.) / 1000.);
        s.m_playback_speed = std::clamp(j.value("playback_speed", s.m_playback_speed), 0.1f, 2.f);
        if(auto gain = j.find("playback_gain"); gain != j.end() && gain->is_object())
        {
            s.m_auto_gain = gain->value("automatic", s.m_auto_gain);
            s.m_playback_gain_db =
                std::clamp(gain->value("gain_db", s.m_playback_gain_db), 0.f, 60.f);
            s.m_auto_gain_max_db =
                std::clamp(gain->value("max_gain_db", s.m_auto_gain_max_db), 0.f, 60.f);
        }
        s.m_follow_playhead = j.value("follow_playhead", s.m_follow_playhead);
        s.m_initial_view_s = std::max(0.f, j.value("initial_view_s", s.m_initial_view_s));
        s.m_show_rejected = j.value("show_rejected", s.m_show_rejected);
        s.m_auto_advance = j.value("auto_advance", s.m_auto_advance);
        if(auto band = j.find("band_playback"); band != j.end() && band->is_object())
        {
            s.m_band_shift = band->value("shift", s.m_band_shift);
            s.m_band_shift_to_hz =
                std::clamp(band->value("shift_to_hz", s.m_band_shift_to_hz), 100.f, 8000.f);
        }
        // Earlier versions kept the gain as a factor
        if(auto factor = j.find("waveform_gain"); factor != j.end() && factor->is_number())
            s.m_waveform_gain_db = 20.f * std::log10(std::max(1.f, factor->get<float>()));
        s.m_waveform_gain_db =
            std::clamp(j.value("waveform_gain_db", s.m_waveform_gain_db), 0.f, 60.f);
        if(auto spec = j.find("spectrogram"); spec != j.end() && spec->is_object())
        {
            s.m_show_spectrogram = spec->value("show", s.m_show_spectrogram);
            s.m_spectrogram_scale = enum_value(*spec, "scale", s.m_spectrogram_scale, scale_names);
            s.m_spectrogram_gain_db = spec->value("gain_db", s.m_spectrogram_gain_db);
            s.m_spectrogram_range_db = spec->value("range_db", s.m_spectrogram_range_db);
            s.m_spectrogram_share = std::clamp(spec->value("share", s.m_spectrogram_share), 0.1f, 0.9f);
        }
        s.m_favourite_classes = j.value("favourite_classes", s.m_favourite_classes);
        if(auto lists = j.find("review_lists"); lists != j.end() && lists->is_array())
            for(const auto &l : *lists)
            {
                if(!l.is_object() || !l.contains("list"))
                    continue;
                const auto list = l.value("list", std::string{});
                s.m_recent_lists.push_back({list, l.value("project", std::string{})});
                s.m_list_views[list] = review_list_view{
                    l.value("filter", std::string{}),
                    l.value("sort", std::string{}),
                    l.value("descending", false)};
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
        {"project", m_project_file},
        {"font", m_font},
        {"font_size", m_font_size},
        {"layout", enum_name(m_layout, layout_names)},
        {"file_filter", enum_name(m_file_list.m_status, filter_names)},
        {"file_list",
         {{"search", m_file_list.m_search},
          {"finder", enum_name(m_file_list.m_finder, finder_names)},
          {"sort", enum_name(m_file_list.m_sort, sort_names)},
          {"descending", m_file_list.m_descending},
          {"by_class",
           {{"classes", m_file_list.m_classes.m_classes},
            {"suggestions", m_file_list.m_classes.m_suggestions},
            {"rejected", m_file_list.m_classes.m_rejected}}}}},
        {"nudge_ms", m_nudge_s * 1000.},
        {"fine_nudge_ms", m_fine_nudge_s * 1000.},
        {"playback_speed", m_playback_speed},
        {"playback_gain",
         {{"automatic", m_auto_gain},
          {"gain_db", m_playback_gain_db},
          {"max_gain_db", m_auto_gain_max_db}}},
        {"follow_playhead", m_follow_playhead},
        {"initial_view_s", m_initial_view_s},
        {"show_rejected", m_show_rejected},
        {"auto_advance", m_auto_advance},
        {"band_playback", {{"shift", m_band_shift}, {"shift_to_hz", m_band_shift_to_hz}}},
        {"waveform_gain_db", m_waveform_gain_db},
        {"spectrogram",
         {{"show", m_show_spectrogram},
          {"scale", enum_name(m_spectrogram_scale, scale_names)},
          {"gain_db", m_spectrogram_gain_db},
          {"range_db", m_spectrogram_range_db},
          {"share", m_spectrogram_share}}},
        {"favourite_classes", m_favourite_classes}
    };
    auto lists = nlohmann::json::array();
    for(const auto &recent : m_recent_lists)
    {
        nlohmann::json l{{"list", recent.m_list}, {"project", recent.m_project}};
        if(auto it = m_list_views.find(recent.m_list); it != m_list_views.end())
        {
            l["filter"] = it->second.m_filter;
            l["sort"] = it->second.m_sort_column;
            l["descending"] = it->second.m_descending;
        }
        lists.push_back(std::move(l));
    }
    j["review_lists"] = std::move(lists);
    if(!m_device_name.empty())
        j["audio_device"] = {{"name", m_device_name}};
    std::ofstream f(m_filename);
    f << j.dump(2);
}

void app_settings::add_recent_list(const recent_review_list &list)
{
    std::erase(m_recent_lists, list);
    m_recent_lists.insert(m_recent_lists.begin(), list);
    if(m_recent_lists.size() > 10U)
        m_recent_lists.resize(10U);
    std::erase_if(
        m_list_views,
        [&](const auto &view)
        { return std::ranges::find(m_recent_lists, view.first, &recent_review_list::m_list) ==
              m_recent_lists.end(); });
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
    m_settings(app_settings::load())
{
}

bool app_state::is_selected_file(const std::filesystem::path &path) const
{
    if(path.empty())
        return false;

    return m_active_file.has_value() && m_active_file.value() == path;
}

void app_state::reset_project_state()
{
    m_active_file.reset();
    m_active_audio = std::make_shared<audio_buffer>();
    m_active_channel.reset();
    m_audio_min_max_map = {};
    m_drag = {};
    m_unlabelled.clear();
    m_selected_labels.clear();
    m_active_label_defn.reset();
    m_history = {};
    m_view_request.reset();
    m_hover_time_s.reset();
    m_review = {};
    m_high_pass = {};
    m_review_mode = false;
    m_review_pos.reset();
    m_annotation_trees.clear();
    m_export_progress.reset();
    m_export_result = {};
    m_show_export_settings = false;
    m_label_clipboard = {}; // Its class IDs are the old palette's
    m_show_paste_dialog = false;
    m_assisted_labellers.clear();
    m_assist_progress.reset();
    m_assist_result = {};
    m_assist_report.clear();
    m_playing_clip = false;
    m_review_list.reset();
    m_review_list_settings = {};
    m_review_list_error.clear();
    m_review_list_row.reset();
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
    auto audio = load_mono(path, channel);
    apply_high_pass(audio.m_samples, audio.m_sample_rate_hz, m_high_pass);
    m_active_audio = std::make_shared<const audio_buffer>(std::move(audio));
    m_active_channel = channel;
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
            if(l.m_state != label_state::rejected || m_settings.m_show_rejected)
                out.push_back(&l);
    return out;
}

std::vector<label_defn_id> app_state::numbered_classes(const project_model &project) const
{
    return ::numbered_classes(project.m_label_dict, m_settings.m_favourite_classes);
}

bool app_state::is_favourite(const label_defn &defn) const
{
    return std::ranges::find(m_settings.m_favourite_classes, defn.m_class) !=
        m_settings.m_favourite_classes.end();
}

void app_state::toggle_favourite(const label_defn &defn)
{
    auto &favourites = m_settings.m_favourite_classes;
    if(auto it = std::ranges::find(favourites, defn.m_class); it != favourites.end())
        favourites.erase(it);
    else
        favourites.push_back(defn.m_class);
}

std::map<label_id, lane> app_state::label_bands(const project_model &project) const
{
    return layout_labels(visible_labels(project), project.m_label_dict, m_settings.m_layout);
}

time_span app_state::playback_window(const label &l) const
{
    return ::playback_window(l, m_review, m_active_audio->duration_s());
}

std::vector<file_row> app_state::filtered_files(const project_model &project) const
{
    return list_files(project, m_settings.m_file_list);
}

const audio_min_max_level *app_state::get_audio_min_max_level(size_t samples_per_px) const
{
    return m_audio_min_max_map.get_level(samples_per_px);
}

void app_state::select_playback_region(double start_time, double stop_time, frequency_band band)
{
    add_action(actions::select_playback_region({start_time, stop_time}, m_loop, band));
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
