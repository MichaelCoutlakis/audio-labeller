/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../model/label.h"
#include "../model/project_model.h"

/// The project's "clip-manifest" section: a CSV listing clips cut by an external pipeline, each
/// with the recording it came from and the onset time in it, so the clips can be checked here
struct clip_manifest_settings
{
    std::filesystem::path m_path;      ///< Relative to the project, may use ${ENV_VARS}
    std::filesystem::path m_clip_root; ///< Clip paths are relative to this. Default: the CSV's folder.
    std::string m_clip_column{"clip_path"};
    std::string m_source_column{"source_path"}; ///< Relative to a dataset root
    std::string m_onset_column{"onset_s"};
    std::string m_label_column{"label"};
    std::string m_dataset_root; ///< The root source paths are relative to; empty to try each
    double m_match_s{0.05};     ///< A label this close to a clip's onset is the clip's label

    /// nullopt if the project has no clip manifest
    static std::optional<clip_manifest_settings> from_json(const nlohmann::json &j);
};

/// Read CSV records, with quoted fields (which may hold commas, quotes and line breaks)
std::vector<std::vector<std::string>> read_csv(std::istream &in);

struct clip_manifest
{
    struct row
    {
        std::vector<std::string> m_fields; ///< As in the CSV, one per column
        std::filesystem::path m_clip;
        std::optional<std::filesystem::path> m_source; ///< nullopt if not in the project
        double m_onset_s{};
        std::string m_label;
    };

    std::vector<std::string> m_columns;
    std::vector<row> m_rows;
    size_t m_unresolved{}; ///< Rows whose source recording isn't in the project
};

/// Throws if the file can't be read or lacks a required column
clip_manifest load_clip_manifest(const project_model &project, const clip_manifest_settings &settings);

/// The label nearest a clip's onset within the window, preferring one of the given class
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

/// Record a verdict on the label at a clip's onset in its source recording, creating the label if
/// there's none there, so that it lands in the annotations and survives the next re-cut.
/// @return the label's ID
label_id apply_clip_verdict(
    project_model &project,
    const std::filesystem::path &source,
    double onset_s,
    double window_s,
    clip_verdict verdict,
    label_defn_id defn_id);

/// The class named by a clip's label text, if the palette has it
std::optional<label_defn_id> class_named(const label_dict &dict, const std::string &name);
