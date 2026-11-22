/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../model/history.h"
#include "../model/label.h"
#include "../model/project_model.h"

/// A review list (the clip manifest): a CSV of events to hear and judge, eg. the ones a model gets
/// wrong, each with the recording it's in and where. The format is our own (see
/// examples/review-list.csv); another tool's CSV is read through the project's "clip-manifest"
/// section, which renames the columns and may name a list to open with the project. Reviewing
/// writes a results file and never changes the project's labels; see review_import.h for that.
struct clip_manifest_settings
{
    std::filesystem::path m_path;      ///< Relative to the project, may use ${ENV_VARS}
    std::filesystem::path m_clip_root; ///< Clip paths are relative to this. Default: the CSV's folder.
    /* Columns. Only path and start_s are required, and clip_offset_s with clip_path. */
    std::string m_source_column{"path"}; ///< The recording, relative to a dataset root, or absolute
    std::string m_onset_column{"start_s"};
    /// The event's end: its stop time in the recording, or its length from the start
    std::string m_stop_column{"stop_s"};
    std::string m_duration_column{"duration_s"};
    std::string m_label_column{"label"}; ///< The class the pipeline gave it
    std::string m_root_column{"root"};   ///< The row's dataset root, as written in the project
    std::string m_channel_column{"channel"}; ///< From 1, as in the project
    std::string m_clip_column{"clip_path"};  ///< A cut clip, shown if the recording isn't there
    /// Where the clip starts in its recording, in seconds. Negative if the clip starts with
    /// padding before the recording's start.
    std::string m_clip_offset_column{"clip_offset_s"};
    std::string m_id_column{"id"}; ///< A key to join the results on
    std::string m_dataset_root;     ///< The root paths are relative to; empty to try each
    /// A label whose span holds the event's start, give or take this, is the event's label
    double m_match_s{0.05};
    /// The length a row is drawn and played with when the list gives no stop or duration
    double m_region_s{1.};

    /// nullopt if the project names no review list
    static std::optional<clip_manifest_settings> from_json(const nlohmann::json &j);

    /// The settings to read another list with: the project's section, if it has one, with that
    /// list's path
    static clip_manifest_settings for_list(const nlohmann::json &j, const std::filesystem::path &list);
};

/// Read CSV records, with quoted fields (which may hold commas, quotes and line breaks)
std::vector<std::vector<std::string>> read_csv(std::istream &in);

/// Where a list's columns are, by the settings' names
struct list_columns
{
    std::optional<size_t> m_source, m_onset, m_stop, m_duration, m_label, m_root, m_channel,
        m_clip, m_clip_offset, m_id;

    list_columns() = default;
    list_columns(const std::vector<std::string> &header, const clip_manifest_settings &s);

    /// A row's field, empty if the column isn't there
    static const std::string &field(const std::vector<std::string> &fields, std::optional<size_t> i);
    /// Its ID if the list has them, else what says which event it is
    std::string key(const std::vector<std::string> &fields) const;
};

struct clip_manifest
{
    struct row
    {
        std::vector<std::string> m_fields; ///< As in the CSV, one per column
        std::optional<std::filesystem::path> m_source; ///< nullopt if not in the project
        double m_start_s{};             ///< In the recording
        std::optional<double> m_stop_s; ///< From the stop or duration column, if there's one
        std::string m_label;
        std::optional<uint32_t> m_channel; ///< From 0
        std::optional<std::filesystem::path> m_clip;
        double m_clip_offset_s{}; ///< Where the clip starts in the recording
        std::optional<double> m_clip_duration_s; ///< Read from the clip, when it's needed
        std::string m_key;                       ///< Its ID, else its path, start and label
        bool m_added{false};                     ///< Added by the reviewer
        row_verdict m_result;

        /// The event as the list gives it, with the default length if there's no stop
        time_span span(double default_length_s) const;
        /// The event as reviewed: moved, if it was
        time_span reviewed_span(double default_length_s) const;
        /// Its class as reviewed: relabelled, if it was
        const std::string &reviewed_label() const;
        /// Whether it's part of the list: an added row taken back (by undo or delete) isn't
        bool present() const { return !m_added || !m_result.m_verdict.empty(); }
        /// Shown on its clip, since its recording isn't in the project
        bool on_clip() const { return !m_source && m_clip; }
        /// On its clip, and not all of the event is in the clip
        bool partly_outside_clip(double default_length_s) const;
        /// Changed by the review: relabelled, not an event, moved or added
        bool changed() const;
    };

    std::filesystem::path m_path;         ///< The list
    std::filesystem::path m_results_path; ///< Beside it, see results_path
    std::vector<std::string> m_columns;
    list_columns m_index;
    std::vector<row> m_rows;
    size_t m_unresolved{}; ///< Rows whose recording isn't in the project

    /// The rows with a verdict
    size_t heard() const;

    /// Add a row for a sound the reviewer found near another row: in the same recording (or
    /// clip), with that row's path, root, channel and clip, and the given span and class.
    /// @return its index
    size_t add_row(size_t near_row, time_span span, const std::string &label);
};

/// Where a list's verdicts go: beside it, "run.csv" giving "run.results.csv"
std::filesystem::path results_path(const std::filesystem::path &list);
/// The changed rows alone: "run.csv" giving "run.changes.csv"
std::filesystem::path changes_path(const std::filesystem::path &list);
/// What the results belong to: "run.csv" giving "run.results.json"
std::filesystem::path results_sidecar_path(const std::filesystem::path &list);

/// The results file: every column of the list, then verdict, new_label, new_start_s, new_stop_s
/// and note, for every row
/// @param changed_only Only the rows the review changed, for the changes file
std::string results_csv(const clip_manifest &manifest, bool changed_only = false);

/// Write the results file and its sidecar, and the changes file if asked, atomically. Throws if
/// they can't be written.
void write_results(const clip_manifest &manifest, bool with_changes = false);

/// Read the verdicts back from the results file, if there is one, into the rows with the same
/// key, and the rows the reviewer added. The list may have been written again since, with rows
/// added, removed or moved.
void read_results(
    const project_model &project,
    clip_manifest &manifest,
    const clip_manifest_settings &settings);

/// Read a list, and its results if there are any. Throws if the file can't be read, lacks a
/// required column, or has a clip without its offset.
/// @param with_results Whether to read the results file beside it
clip_manifest load_clip_manifest(
    const project_model &project,
    const clip_manifest_settings &settings,
    bool with_results = true);

/// Read a results file as a list, with each row's verdict from its own columns
clip_manifest load_results_file(
    const project_model &project,
    const clip_manifest_settings &settings,
    const std::filesystem::path &results);

/// The label whose span holds a time, give or take the window, preferring one of the given class,
/// then the one whose start is nearest. A region can start well before the onset a pipeline finds
/// in it; a zero-length label has to be within the window of it.
const label *find_label_at(
    const labels &file_labels,
    double t_s,
    double window_s,
    std::optional<label_defn_id> preferred = std::nullopt);

enum class clip_verdict
{
    correct,   ///< The event is what the row says
    relabel,   ///< The event is of another class
    not_event, ///< Not an event
};

/// As written in the results: correct, relabel or not_event
const char *verdict_name(clip_verdict v);

/// The class named by a row's label text, if the palette has it
std::optional<label_defn_id> class_named(const label_dict &dict, const std::string &name);
