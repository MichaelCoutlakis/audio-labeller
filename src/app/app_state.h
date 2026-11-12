/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../model/audio_buffer.h"
#include "../model/history.h"
#include "../model/project_model.h"
#include "actions.h"

struct app_settings
{
    static inline std::string m_filename{"audio-labelling-app-settings.json"};
    static app_settings load();
    void save();

    std::string m_device_name; ///< Previously used playback device, empty for the default
    std::string project_file;

    /// Set if the settings file exists but is unreadable, in which case it's left as is on save
    std::string m_load_error;
};

class coord_transform
{
public:
    double m_view_start_s{};
    double m_view_end_s{};

    double time_to_px(double t_s, float px_min, float px_max) const
    {
        double norm = (t_s - m_view_start_s) / (m_view_end_s - m_view_start_s);
        return px_min + norm * (px_max - px_min);
    }

    double px_to_time(float px, float px_min, float px_max) const
    {
        double norm = (px - px_min) / (px_max - px_min);
        return m_view_start_s + norm * (m_view_end_s - m_view_start_s);
    }

    double view_duration() const { return m_view_end_s - m_view_start_s; }
};

/// Represent the audio buffer at multiple zoom levels
struct audio_min_max_level
{
    size_t m_samples_per_bin{};
    std::vector<float> m_min_vals;
    std::vector<float> m_max_vals;
    std::vector<float> m_bin_times;
};

/// Container for audio buffer representations at the different zoom levels
class audio_min_max_pyramid_map
{
public:
    audio_min_max_pyramid_map() = default;
    audio_min_max_pyramid_map(const audio_buffer &buffer);

    const audio_min_max_level *get_level(size_t samples_per_px) const;

private:
    std::vector<audio_min_max_level> m_levels;
};

struct waveform_interaction
{
    void begin_drag(double time_s)
    {
        m_dragging = true;
        m_drag_start = time_s;
        m_drag_current = time_s;
    }
    void update_drag(double time_s) { m_drag_current = time_s; }
    void end_drag() { m_dragging = false; }

    bool is_dragging() const { return m_dragging; }

    time_span get_drag_range() const
    {
        return {std::min(m_drag_start, m_drag_current), std::max(m_drag_start, m_drag_current)};
    }

private:
    bool m_dragging{false};
    double m_drag_start{};
    double m_drag_current{};
};

class app_state
{
public:
    app_state();
    /* Active file: */
    /// Currently loaded file active in the waveform veiwer
    std::optional<std::filesystem::path> m_active_file;

    /// Check if the specified file is the same as the currently active file
    bool is_selected_file(const std::filesystem::path &path) const;

    void set_selected_file(const std::filesystem::path &path);

    bool has_active_file() const { return m_active_file.has_value(); }

    const audio_buffer &get_audio_buffer() const { return *m_active_audio; }
    std::shared_ptr<const audio_buffer> get_audio_buffer_ptr() const { return m_active_audio; }

    const audio_min_max_level *get_audio_min_max_level(size_t samples_per_px) const;

    /* Waveform drag and selections: */
    waveform_interaction m_drag;

    labels m_unlabelled; ///< Selections not labelled yet - use same format for id

    std::optional<label_defn_id> m_active_label_defn;

    bool is_label_selected(label_id id) const { return m_selected_labels.contains(id); }

    /// Toggle whether the specified label is selected
    /// @param clear_others if true and the label becomes selected, unselect other previously
    /// selected labels
    /// @return the resultant selection state
    bool toggle_label_selection(label_id id, bool clear_others = false)
    {
        if(!m_selected_labels.erase(id))
        {
            if(clear_others)
                m_selected_labels.clear();
            m_selected_labels.insert(id);
            return true;
        }
        return false;
    }

    void clear_selections() { m_selected_labels.clear(); }
    void set_selection(std::set<label_id> ids) { m_selected_labels = std::move(ids); }
    void deselect(label_id id) { m_selected_labels.erase(id); }
    const std::set<label_id> &selected_labels() const { return m_selected_labels; }

    /* Undo: */
    history m_history;

    /// A new number for each edit gesture (a drag, a burst of nudges), so that its changes merge
    /// into one undo step
    uint64_t next_edit_seq() { return ++m_edit_seq; }

    /* File list: */
    enum class file_filter
    {
        all,
        todo,
        done,
        skip
    };
    file_filter m_file_filter{file_filter::all};

    /// The project's files that pass the filter, in list order
    std::vector<const audio_file_entry *> filtered_files(const project_model &project) const;

    /* Display: */
    bool m_class_lanes{true}; ///< Each class in its own band of the waveform

    /* Actions: */
    void add_action(actions::app_action action) { m_actions.push_back(std::move(action)); }
    std::vector<actions::app_action> m_actions;

    /* Audio related: */
    bool m_loop{false};                  ///< Whether to loop the playback segment
    bool m_auto_start{true};             ///< Start playing a segment as soon as it's created
    std::vector<audio_dev> m_audio_devs; ///< Available playback audio devices
    playback_state m_playback_state;

    void select_playback_region(
        double start_time,
        double stop_time = std::numeric_limits<double>::max());

    // Settings between program runs
    app_settings settings;

    /* Errors: */
    /// Log an error and queue it to be shown to the user
    void report_error(const std::string &message);
    std::string m_error_message; ///< Errors waiting to be acknowledged, empty if none

private:
    std::set<label_id> m_selected_labels;
    uint64_t m_edit_seq{};

    std::shared_ptr<const audio_buffer> m_active_audio{std::make_shared<audio_buffer>()};

    audio_min_max_pyramid_map m_audio_min_max_map;
};
