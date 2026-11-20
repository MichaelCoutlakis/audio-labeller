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
/// section, which renames the columns and may name a list to open with the project.
struct clip_manifest_settings
{
    std::filesystem::path m_path;      ///< Relative to the project, may use ${ENV_VARS}
    std::filesystem::path m_clip_root; ///< Clip paths are relative to this. Default: the CSV's folder.
    /* Columns. Where a column isn't named in the project, an older name is read too: source_path
       for path, onset_s for start_s. Only path and start_s are required. */
    std::string m_source_column{"path"}; ///< The recording, relative to a dataset root, or absolute
    std::string m_onset_column{"start_s"};
    /// The event's end: its stop time in the recording, or its length from the start. Used to
    /// play the event, and for a label a verdict has to create.
    std::string m_stop_column{"stop_s"};
    std::string m_duration_column{"duration_s"};
    std::string m_label_column{"label"}; ///< The class the pipeline gave it
    std::string m_root_column{"root"};   ///< The row's dataset root, as written in the project
    std::string m_channel_column{"channel"}; ///< From 1, as in the project
    std::string m_clip_column{"clip_path"};  ///< A cut clip, played if the recording isn't there
    std::string m_id_column{"id"};           ///< A key to join the results on
    std::string m_dataset_root; ///< The root source paths are relative to; empty to try each
    /// A label whose span holds the event's start, give or take this, is the event's label
    double m_match_s{0.05};
    /// The length of a label a verdict creates, when the list gives no stop or duration
    double m_region_s{1.};

    /// nullopt if the project names no clip manifest
    static std::optional<clip_manifest_settings> from_json(const nlohmann::json &j);

    /// The settings to read another list with: the project's section, if it has one, with that
    /// list's path
    static clip_manifest_settings for_list(const nlohmann::json &j, const std::filesystem::path &list);
};

/// Read CSV records, with quoted fields (which may hold commas, quotes and line breaks)
std::vector<std::vector<std::string>> read_csv(std::istream &in);

struct clip_manifest
{
    struct row
    {
        std::vector<std::string> m_fields; ///< As in the CSV, one per column
        std::optional<std::filesystem::path> m_clip;
        std::optional<std::filesystem::path> m_source; ///< nullopt if not in the project
        double m_onset_s{};
        std::optional<double> m_stop_s; ///< From the stop or duration column, if there's one
        std::string m_label;
        std::optional<uint32_t> m_channel; ///< From 0
        std::string m_key;                 ///< Its ID, else its path, start and label
        row_verdict m_result;

        /// The span a label created for this clip gets
        time_span span(double default_length_s) const;
    };

    std::filesystem::path m_path;         ///< The list
    std::filesystem::path m_results_path; ///< Beside it, see results_path
    std::vector<std::string> m_columns;
    std::vector<row> m_rows;
    size_t m_unresolved{}; ///< Rows whose source recording isn't in the project

    /// The rows with a verdict
    size_t heard() const;
};

/// Where a list's verdicts go: beside it, "run.csv" giving "run.results.csv"
std::filesystem::path results_path(const std::filesystem::path &list);

/// The results file: every column of the list, then verdict, new_label and note
std::string results_csv(const clip_manifest &manifest);

/// Write the results file, atomically. Throws if it can't be written.
void write_results(const clip_manifest &manifest);

/// Read the verdicts back from the results file, if there is one, into the rows with the same
/// key. The list may have been written again since, with rows added, removed or moved.
/// @param settings For the column names
void read_results(clip_manifest &manifest, const clip_manifest_settings &settings);

/// Read a list, and its results if there are any. Throws if the file can't be read or lacks a
/// required column.
clip_manifest load_clip_manifest(const project_model &project, const clip_manifest_settings &settings);

/// The label whose span holds a clip's onset, give or take the window, preferring one of the
/// given class, then the one whose start is nearest the onset. A region can start well before the
/// onset a pipeline finds in it; a zero-length label has to be within the window of it.
const label *find_label_at(
    const labels &file_labels,
    double onset_s,
    double window_s,
    std::optional<label_defn_id> preferred = std::nullopt);

enum class clip_verdict
{
    correct,  ///< The label is right: confirmed
    relabel,  ///< The event is of another class
    not_event ///< Not an event: rejected
};

/// As written in the results: correct, relabel or not_event
const char *verdict_name(clip_verdict v);

/// Record a verdict on the label at a clip's onset in its source recording, creating the label if
/// there's none there, so that it lands in the annotations and survives the next re-cut.
/// @param event The clip's onset, and the stop a label created for it gets
/// @return the label's ID
label_id apply_clip_verdict(
    project_model &project,
    const std::filesystem::path &source,
    time_span event,
    double window_s,
    clip_verdict verdict,
    label_defn_id defn_id);

/// The class named by a clip's label text, if the palette has it
std::optional<label_defn_id> class_named(const label_dict &dict, const std::string &name);
