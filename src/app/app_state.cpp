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
#include "../io/files.h"
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
        s.m_review_changes_file = j.value("review_changes_file", s.m_review_changes_file);
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
                    l.value("descending", false),
                    l.value("hidden", std::vector<std::string>{})};
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
            l["hidden"] = it->second.m_hidden;
        }
        lists.push_back(std::move(l));
    }
    j["review_lists"] = std::move(lists);
    j["review_changes_file"] = m_review_changes_file;
    if(!m_device_name.empty())
        j["audio_device"] = {{"name", m_device_name}};
    write_file_atomically(m_filename, j.dump(2));
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
    m_active_offset_s = 0.;
    m_active_is_clip = false;
    m_row_labels.clear();
    m_row_labels_key.reset();
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
    m_review_list.reset();
    m_review_list_settings = {};
    m_review_list_error.clear();
    m_review_list_row.reset();
    m_file_list_cache.reset();
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
    m_active_offset_s = 0.;
    m_active_is_clip = false;
    m_selected_labels.clear();
    m_unlabelled.clear();
}

void app_state::set_selected_clip(const std::filesystem::path &clip, double offset_s)
{
    if(!(m_active_is_clip && is_selected_file(clip) && m_active_offset_s == offset_s))
    {
        load_audio(clip, std::nullopt);
        m_active_file = clip;
        m_active_offset_s = offset_s;
        m_active_is_clip = true;
    }
    m_selected_labels.clear();
    m_unlabelled.clear();
}

void app_state::refresh_row_labels(const project_model &project)
{
    const row_labels_key key{
        m_review_list_generation,
        m_review_list_revision,
        m_active_file,
        project.m_label_dict.size()};
    if(m_row_labels_key == key)
        return;
    m_row_labels_key = key;
    m_row_labels.clear();
    if(!m_review_list || !m_active_file)
        return;
    const auto &rows = m_review_list->m_rows;
    const auto region = m_review_list_settings.m_region_s;
    for(size_t r = 0U; r != rows.size(); ++r)
    {
        const auto &row = rows[r];
        const bool here = m_active_is_clip ? row.on_clip() && *row.m_clip == *m_active_file
                                           : row.m_source == m_active_file;
        if(!here || !row.present())
            continue;
        const auto t = row.reviewed_span(region);
        label l{row_label_base + r, label_defn_id{}, t.first, t.second};
        if(const auto cls = class_named(project.m_label_dict, row.reviewed_label()))
            l.m_defn_id = *cls;
        const auto &verdict = row.m_result.m_verdict;
        l.m_state = verdict.empty()      ? label_state::suggested
            : verdict == "not_event" ? label_state::rejected
                                     : label_state::confirmed;
        m_row_labels.add(l);
    }
}

bool app_state::list_mode() const
{
    if(!m_review_list || !m_review_list_row || !m_active_file ||
       *m_review_list_row >= m_review_list->m_rows.size())
        return false;
    const auto &row = m_review_list->m_rows[*m_review_list_row];
    return m_active_is_clip ? row.on_clip() && *row.m_clip == *m_active_file
                            : row.m_source == m_active_file;
}

std::string app_state::row_label_text(label_id id) const
{
    const auto r = row_of(id);
    if(!r || !m_review_list || *r >= m_review_list->m_rows.size())
        return {};
    const auto &row = m_review_list->m_rows[*r];
    const auto &name = row.reviewed_label();
    const auto text = name.empty() ? std::string{"(no class)"} : name;
    if(row.m_added)
        return "list+ " + text;
    if(row.m_result.m_verdict == "not_event")
        return "list x:" + text;
    return "list: " + text;
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
    for(const auto &l : m_row_labels)
        out.push_back(&l);
    if(m_active_file && !m_active_is_clip)
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
    // In the audio's own time, then back in the recording's (they differ for a clip)
    auto shifted = l;
    shifted.m_start_s -= m_active_offset_s;
    shifted.m_stop_s -= m_active_offset_s;
    const auto w = ::playback_window(shifted, m_review, m_active_audio->duration_s());
    return {w.first + m_active_offset_s, w.second + m_active_offset_s};
}

const app_state::file_list_cache &app_state::file_list(const project_model &project) const
{
    auto &c = m_file_list_cache;
    if(!c ||
       c->m_project != &project ||
       c->m_revision != project.revision() ||
       c->m_files_version != project.files_version() ||
       c->m_classes != project.m_label_dict.size() ||
       c->m_settings != m_settings.m_file_list)
    {
        c = file_list_cache{
            &project,
            project.revision(),
            project.files_version(),
            project.m_label_dict.size(),
            m_settings.m_file_list,
            list_files(project, m_settings.m_file_list),
            0U};
        for(const auto &file : project.get_files())
            c->m_finished += project.get_status(file.m_path) != file_status::todo;
    }
    return *c;
}

const std::vector<file_row> &app_state::filtered_files(const project_model &project) const
{
    return file_list(project).m_rows;
}

size_t app_state::finished_files(const project_model &project) const
{
    return file_list(project).m_finished;
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
