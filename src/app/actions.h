/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <variant>

#include <nlohmann/json.hpp>

#include "../manifest/clip_manifest.h"
#include "../model/label.h"
#include "../model/project_model.h"
#include "../model/types.h"

/// Everything that changes the model or app state. Views push these and never change state
/// themselves.
namespace actions
{

struct load_file
{
    std::filesystem::path file_path;
};

struct add_label
{
    /// The time span. Equal start and stop make an onset.
    time_span m_t;
    std::filesystem::path m_file;           //!< Active file to which label applied
    std::optional<label_defn_id> m_defn_id; //!< The selected label definition id at the time
};

/// Toggle selection of the labels under a time in the waveform
struct select_labels
{
    double m_x;
    double m_y;               ///< Only labels whose lane contains this are hit
    double m_tolerance_s{0.}; ///< How near an onset counts as on it
};

struct toggle_label_selection
{
    label_id m_id;
    bool m_clear_others{false}; ///< Unselect everything else if this becomes selected
};

struct clear_selection
{
};

/// Choose the palette class for new labels, also applying it to the selected labels
struct set_active_class
{
    std::optional<label_defn_id> m_defn_id;
};

/// Give labels a class, confirming any suggestions among them
struct assign_class
{
    std::set<label_id> m_ids;
    label_defn_id m_defn_id;
};

/// Accept suggestions (or restore rejected labels) as labels
struct accept_labels
{
    std::set<label_id> m_ids;
};

/// Mark labels as not an event
struct reject_labels
{
    std::set<label_id> m_ids;
};

struct delete_labels
{
    std::set<label_id> m_ids;
};

/// Move a label's edges. Moves with the same drag sequence number merge into one undo step.
struct move_label
{
    label_id m_id;
    double m_start_s;
    double m_stop_s;
    uint64_t m_drag_seq;
};

/// Move the onsets (and region starts) to the sharpest rise nearby, as one undo step
struct snap_labels
{
    std::set<label_id> m_ids;
};

/// Select, show and play the next (or previous) suggestion waiting for review, in any file
struct review_step
{
    bool m_forward{true};
};

struct undo
{
};

struct redo
{
};

struct set_file_status
{
    std::filesystem::path m_file;
    file_status m_status;
};

/// The channel of a file to view and play, nullopt for a mix
struct set_channel
{
    std::filesystem::path m_file;
    std::optional<uint32_t> m_channel;
};

struct select_playback_device
{
    audio_dev dev;
};

struct select_playback_region
{
    time_span t;
    bool loop{false};
};

struct toggle_playback
{
};

struct playback_loop
{
    bool loop{false};
};

/// 1 is normal speed, 0.5 half speed and an octave lower
struct set_playback_speed
{
    float m_speed{1.f};
};

struct rescan_files
{
};

struct export_csv
{
};

/// Export clips of the files marked done in the background, see export/clip_export.h
struct export_dataset
{
};

struct cancel_export
{
};

/// Save the export settings to the project file
struct set_export_settings
{
    nlohmann::json m_section;
};

/// Read or write one of the project's annotation trees, by index
struct import_annotations
{
    size_t m_tree{};
};

struct export_annotations
{
    size_t m_tree{};
};

/// Play a clip file from a clip manifest, rather than the active file
struct play_clip
{
    std::filesystem::path m_clip;
};

/// Record a verdict on a clip, on the label at its onset in its source recording
struct judge_clip
{
    std::filesystem::path m_source;
    double m_onset_s{};
    double m_window_s{};
    clip_verdict m_verdict{};
    label_defn_id m_defn_id;
};

/// Open a file and show a time in it, with context either side
struct jump_to
{
    std::filesystem::path m_file;
    double m_time_s{};
};

using app_action = std::variant<
    load_file,
    add_label,
    select_labels,
    toggle_label_selection,
    clear_selection,
    set_active_class,
    assign_class,
    accept_labels,
    reject_labels,
    delete_labels,
    move_label,
    snap_labels,
    review_step,
    undo,
    redo,
    set_file_status,
    set_channel,
    select_playback_device,
    select_playback_region,
    toggle_playback,
    playback_loop,
    set_playback_speed,
    rescan_files,
    export_csv,
    export_dataset,
    cancel_export,
    set_export_settings,
    import_annotations,
    export_annotations,
    play_clip,
    judge_clip,
    jump_to>;

}
