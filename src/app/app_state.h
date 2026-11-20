/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once
#include <filesystem>
#include <future>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../dsp/high_pass.h"
#include "../dsp/spectrogram.h"
#include "../export/clip_export.h"
#include "../interchange/annotation_tree.h"
#include "../manifest/clip_manifest.h"
#include "../model/audio_buffer.h"
#include "../model/file_finder.h"
#include "../model/history.h"
#include "../model/project_model.h"
#include "../review/review.h"
#include "actions.h"
#include "lanes.h"

/// How a review list was shown, remembered per list
struct review_list_view
{
    std::string m_filter;
    std::string m_sort_column; ///< By name, empty for the list's own order
    bool m_descending{false};
};

/// A review list opened recently, and the project it was opened in
struct recent_review_list
{
    std::string m_list;    ///< UTF-8
    std::string m_project; ///< UTF-8

    bool operator==(const recent_review_list &) const = default;
};

struct app_settings
{
    static inline std::string m_filename{"audio-labelling-app-settings.json"};
    static app_settings load();
    void save();

    std::string m_device_name; ///< Previously used playback device, empty for the default
    std::string m_project_file;

    /// Font file (UTF-8), empty for a system font with wide coverage, or "builtin" for ImGui's
    /// own (crisp, but ASCII only). Takes effect at the next start.
    std::string m_font;
    float m_font_size{16.f};

    /* Remembered between runs: */
    label_layout m_layout{label_layout::stacked};
    file_list_settings m_file_list; ///< Filters and order of the file list
    double m_nudge_s{0.01};       ///< Arrow keys move labels by this
    double m_fine_nudge_s{0.001}; ///< And by this with Alt
    float m_playback_speed{1.f};
    bool m_auto_gain{false};         ///< Raise quiet audio automatically when playing
    float m_playback_gain_db{0.f};   ///< Fixed playback gain, when not automatic
    float m_auto_gain_max_db{40.f};  ///< The most the automatic gain raises the level
    bool m_follow_playhead{true};    ///< Scroll the waveform to keep the playhead in view
    float m_initial_view_s{0.f};     ///< Seconds shown when a file is opened, 0 for all of it
    bool m_show_rejected{false}; ///< Show rejected candidates, as a check on what was missed
    bool m_auto_advance{true};   ///< In review mode, go to the next suggestion after a verdict
    bool m_band_shift{false};          ///< Move high bands down when playing them
    float m_band_shift_to_hz{1000.f};  ///< To start here
    float m_waveform_gain_db{0.f}; ///< Vertical zoom of the waveform, for quiet recordings
    bool m_show_spectrogram{false};
    frequency_scale m_spectrogram_scale{frequency_scale::mel};
    float m_spectrogram_gain_db{0.f};   ///< Added to the levels before colouring
    float m_spectrogram_range_db{80.f}; ///< Levels this far below full scale are black
    float m_spectrogram_share{0.5f};    ///< Of the height, below the waveform
    /// Palette classes pinned to the top of the palette, by name, in the order they were pinned.
    /// Names the project doesn't have are kept, in case another project has them.
    std::vector<std::string> m_favourite_classes;
    std::vector<recent_review_list> m_recent_lists; ///< Most recent first
    std::map<std::string, review_list_view> m_list_views; ///< By list path, for the recent ones

    /// Put a list first in the recent ones, keeping at most ten
    void add_recent_list(const recent_review_list &list);

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

/// Labels copied with Ctrl+C, to paste into the same or another recording
struct label_clipboard
{
    std::filesystem::path m_source;
    std::vector<label> m_labels;
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

    void set_selected_file(const std::filesystem::path &path, std::optional<uint32_t> channel);

    bool has_active_file() const { return m_active_file.has_value(); }

    /// Load a file's audio for viewing and playing, as one channel or a mix, with the high-pass
    void load_audio(const std::filesystem::path &path, std::optional<uint32_t> channel);
    /// The channel the active file's audio was loaded with, nullopt for a mix
    std::optional<uint32_t> active_channel() const { return m_active_channel; }

    /// Applied to audio as it's read. From the project, and switched off or changed for this
    /// session in Options.
    high_pass_settings m_high_pass;

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
    /// The project's files that pass the filters, in list order
    std::vector<file_row> filtered_files(const project_model &project) const;

    /* Display: */
    /// Labels of the active file that are drawn and can be clicked: its labels, including
    /// unclassified selections, and rejected ones only if they're shown
    std::vector<const label *> visible_labels(const project_model &project) const;

    /// The classes that the number keys 1-9 choose, in order: the favourites if any are pinned,
    /// otherwise the first nine of the palette
    std::vector<label_defn_id> numbered_classes(const project_model &project) const;
    bool is_favourite(const label_defn &defn) const;
    void toggle_favourite(const label_defn &defn);

    /// The band of each visible label in the waveform
    std::map<label_id, lane> label_bands(const project_model &project) const;

    /// A time range for the waveform to zoom to, taken by the waveform on its next frame
    std::optional<time_span> m_view_request;

    /// The time under the mouse in the waveform, if it's there
    std::optional<double> m_hover_time_s;

    /* Review: */
    review_settings m_review;
    bool m_review_mode{false};               ///< Tab steps through suggestions in all files
    std::optional<review_item> m_review_pos; ///< The suggestion last stepped to

    /// The time range to play for a label, by the review settings
    time_span playback_window(const label &l) const;

    /* Annotation trees and background export: */
    std::vector<annotation_tree_settings> m_annotation_trees;
    std::shared_ptr<export_progress> m_export_progress;
    std::future<export_outcome> m_export_result;
    bool is_exporting() const { return m_export_result.valid(); }
    bool m_show_export_settings{false};

    /* Copy and paste of labels, eg. into another recording of the same scene: */
    label_clipboard m_label_clipboard;
    bool m_show_paste_dialog{false}; ///< Ctrl+Shift+V: choose the shift and state, then paste
    double m_paste_offset_s{};       ///< Remembered between pastes
    bool m_paste_as_labels{false};   ///< Else as suggestions

    /* Assisted labelling: */
    std::vector<assisted_labeller_settings> m_assisted_labellers;
    std::shared_ptr<assist_progress> m_assist_progress;
    std::future<assist_outcome> m_assist_result;
    bool is_assisting() const { return m_assist_result.valid(); }
    std::string m_assist_report; ///< What the last run did, shown in its window

    /* Review list (clip manifest): */
    std::optional<clip_manifest> m_review_list; ///< The open list, if any
    clip_manifest_settings m_review_list_settings;
    std::string m_review_list_error; ///< Why the last list couldn't be opened
    std::optional<size_t> m_review_list_row; ///< The row last shown, in the list's own order
    bool m_focus_review_list{false};        ///< Bring the list's window to the front
    uint64_t m_review_list_generation{};    ///< Counts the lists opened, so views can tell

    bool m_keys_captured{false}; ///< A view with focus handles the keys, eg. the clip list
    bool m_focus_class_filter{false}; ///< Ctrl+F: put the keyboard in the palette's filter box
    bool m_playing_clip{false};  ///< The audio engine has a manifest clip, not the active file

    /* Projects: */
    /// A project to switch to, taken by the app once the frame's actions are done
    std::optional<std::filesystem::path> m_project_request;
    bool m_browse_for_project{false}; ///< Show a dialog to choose a project to open
    bool m_show_new_project{false};   ///< Show the New project dialog
    bool m_show_project_settings{false};

    /// Forget everything about the current project (the active file, selections, undo history,
    /// review position and the project's own settings), ready for another
    void reset_project_state();

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
        double stop_time = std::numeric_limits<double>::max(),
        frequency_band band = {});

    // Settings between program runs
    app_settings m_settings;

    /* Errors: */
    /// Log an error and queue it to be shown to the user
    void report_error(const std::string &message);
    std::string m_error_message; ///< Errors waiting to be acknowledged, empty if none

    /// Queue a message to be shown to the user
    void report_info(const std::string &message);
    std::string m_info_message;

private:
    std::set<label_id> m_selected_labels;
    uint64_t m_edit_seq{};

    std::shared_ptr<const audio_buffer> m_active_audio{std::make_shared<audio_buffer>()};
    std::optional<uint32_t> m_active_channel;

    audio_min_max_pyramid_map m_audio_min_max_map;
};
