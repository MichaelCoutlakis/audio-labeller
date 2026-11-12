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
    /// The time span
    time_span m_t;
    std::filesystem::path m_file;           //!< Active file to which label applied
    std::optional<label_defn_id> m_defn_id; //!< The selected label definition id at the time
};

/// Toggle selection of the labels under a time in the waveform
struct select_labels
{
    double m_x;
    double m_y; ///< Only labels whose lane contains this are hit
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

struct rescan_files
{
};

struct export_csv
{
};

using app_action = std::variant<
    load_file,
    add_label,
    select_labels,
    toggle_label_selection,
    clear_selection,
    set_active_class,
    delete_labels,
    move_label,
    undo,
    redo,
    set_file_status,
    select_playback_device,
    select_playback_region,
    toggle_playback,
    playback_loop,
    rescan_files,
    export_csv>;

}
