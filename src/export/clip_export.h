/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../model/label.h"
#include "../model/project_model.h"

/// The "export" section of the project file. See ARCHITECTURE.md for what each does.
struct export_settings
{
    enum class clip_mode
    {
        padded, ///< Each label plus padding, merging labels whose clips would overlap
        fixed   ///< A window of fixed length centred on each label
    };

    std::filesystem::path m_directory{"dataset"}; ///< Relative to the project file
    clip_mode m_mode{clip_mode::padded};
    double m_padding_s{0.5};
    double m_fixed_length_s{10.};
    double m_max_clip_s{30.};           ///< Merging stops before a clip would exceed this
    unsigned m_background_per_file{0U}; ///< Clips with no labels, from done files. 0 for none.
    double m_background_length_s{5.};

    /// From the project's "export" section, which may be null. Missing keys keep their defaults.
    static export_settings from_json(const nlohmann::json &j);
    nlohmann::json to_json() const;
};

struct clip_event
{
    double m_onset_s{};  ///< Relative to the clip
    double m_offset_s{}; ///< Relative to the clip
    label_defn_id m_class;
    label_id m_label_id{};
    bool m_truncated{}; ///< The label extends beyond the clip
};

struct clip_plan
{
    double m_start_s{}; ///< In the source file
    double m_stop_s{};
    std::vector<clip_event> m_events; ///< Empty for background clips
};

/// Where to cut clips around a file's labels. Clips are in order of start time.
std::vector<clip_plan> plan_clips(
    std::vector<label> labels,
    double duration_s,
    const export_settings &settings);

/// Windows with no labels (or padding) in them, spread evenly through the file
std::vector<clip_plan> plan_background(
    const std::vector<label> &labels,
    double duration_s,
    const export_settings &settings);

struct export_summary
{
    std::filesystem::path m_directory;
    size_t m_files{};
    size_t m_clips{};
    size_t m_unverified_clips{};
    size_t m_todo_with_labels{}; ///< Not exported because they aren't marked done
    std::vector<std::string> m_errors;
};

/// Shared with an export running on another thread
struct export_progress
{
    std::atomic<size_t> m_done{};  ///< Files finished
    std::atomic<size_t> m_total{}; ///< Files to export
    std::atomic<bool> m_cancel{};  ///< Set to stop after the current file, writing nothing more
};

/// Everything an export needs from the project, copied so that it can run on another thread while
/// labelling carries on
struct export_job
{
    struct file
    {
        std::filesystem::path m_path;
        std::string m_display_name;
        project_model::portable_path m_portable;
        std::vector<label> m_labels; ///< Confirmed
        std::vector<label> m_unsure; ///< Confirmed or suggested: no background clips from these
        std::optional<source_id_cache> m_cached_id;
    };

    std::filesystem::path m_directory; ///< Resolved
    export_settings m_settings;
    std::vector<file> m_files; ///< Those marked done
    label_dict m_classes;
    size_t m_todo_with_labels{};
};

struct export_outcome
{
    struct file_result
    {
        std::filesystem::path m_path;
        source_id_cache m_source_id;
        export_record m_record;
    };

    export_summary m_summary;
    std::vector<file_result> m_results;
    bool m_cancelled{}; ///< Nothing was recorded, though some clips may have been written
};

export_job prepare_export(const project_model &project, const export_settings &settings);

/// Cut the clips and write the dataset files, merging into any existing export in the directory:
/// entries for other sources, including those whose originals were deleted, are kept. Uses only
/// the job, so it may run on any thread.
export_outcome run_export(const export_job &job, export_progress *progress = nullptr);

/// Record each exported file's source ID and export in the project
void apply_export(project_model &project, const export_outcome &outcome);

/// Export clips for every file marked done, on this thread
export_summary export_dataset(project_model &project, const export_settings &settings);
