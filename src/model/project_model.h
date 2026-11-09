/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <filesystem>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "label.h"
#include "ordered_registry.h"
#include "types.h"

struct audio_buffer
{
    uint32_t m_sample_rate_hz{};
    uint32_t m_channels{1U};
    std::vector<float> m_samples;

    size_t num_frames() const { return m_samples.size() / m_channels; }
    double duration_s() const { return double(num_frames()) / m_sample_rate_hz; }
};

struct waveform_envelope
{
    std::vector<float> m_max_vals;
    std::vector<float> m_min_vals;
    size_t m_samples_per_bin{0U};
};

struct project_config
{
    static project_config parse_from_file(std::string filename);

    /// Resolve a project related path (label, export) relative to the project location, according
    /// to settings
    std::filesystem::path resolve_proj(const std::filesystem::path &p) const;

    /// Resolve one of the dataset roots to a normalised directory path without a trailing separator
    std::filesystem::path resolve_root(const std::filesystem::path &root) const;

    std::filesystem::path m_filename;                 ///< This structure on disk
    std::vector<std::filesystem::path> m_wav_paths{}; ///< Directories to scan for wav files
    std::filesystem::path m_palette;                  ///< Classification dictionary
    std::filesystem::path m_labels;        ///< Path for pre-saved labels / labelling directory
    std::filesystem::path m_export_prefix; ///< Where to save exported artifacts
};

struct audio_file_entry
{
    std::filesystem::path m_path; ///< Absolute and normalised. The key for a file's labels
    std::string m_display_name;   ///< UTF-8 path relative to its dataset root
};

class project_model
{
public:
    /// A default constructed config gives an empty model that never reads or writes any files
    project_model(const project_config &cfg);
    ~project_model();

    // Saves on destruction, so there must only ever be one instance per project
    project_model(const project_model &) = delete;
    project_model &operator=(const project_model &) = delete;

    bool has_project() const { return !m_cfg.m_filename.empty(); }

    // Rescans files - will reset the active file
    void rescan_files();

    /// Export CSV (to the path defined in the project settings)
    void export_csv();

    /// Atomically write the labels to the label file, if they differ from what's already there
    void save_if_changed();

    const std::vector<audio_file_entry> &get_files() const { return m_files; }

    label_dict m_label_dict;

    labels &get_labels(const std::filesystem::path &file) { return m_file_labels[file]; }

private:
    /// How a dataset file is identified on disk, independent of the machine and working directory
    struct portable_path
    {
        std::string m_root;     ///< Dataset root as written in the project, empty if outside them
        std::string m_relative; ///< UTF-8 path relative to the root, with forward slashes
    };

    project_config m_cfg;
    std::vector<audio_file_entry> m_files; //!< Available files for labelling

    std::map<std::filesystem::path, labels> m_file_labels; //!< Labels per file

    /// Records whose audio file couldn't be found, written back verbatim so they aren't lost
    std::vector<nlohmann::json> m_unresolved_labels;

    std::string m_last_saved; //!< Label file contents as last read or written

    portable_path to_portable(const std::filesystem::path &file) const;

    std::string class_name_of(const label &l) const;

    /// Find the scanned file that a label on disk refers to
    std::optional<std::filesystem::path> resolve_stored_path(
        const std::string &filename,
        const std::optional<std::string> &root,
        const std::set<std::filesystem::path> &known_files) const;

    void load_labels();
    std::string serialize_labels() const;
};
