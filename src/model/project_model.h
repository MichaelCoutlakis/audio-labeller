/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "label.h"
#include "ordered_registry.h"
#include "types.h"

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

enum class file_status
{
    todo, ///< Not yet fully labelled
    done, ///< Fully labelled. With no labels, it contains none of the classes.
    skip  ///< Not usable, eg. a corrupt or irrelevant recording
};

const char *to_string(file_status status);

struct source_id_cache
{
    std::string m_id;
    std::uintmax_t m_size{};
    std::int64_t m_mtime{};
};

struct export_record
{
    std::string m_time;      ///< UTC, ISO 8601
    std::string m_directory; ///< UTF-8, as resolved at the time
    uint64_t m_clips{};
    bool m_verified{};
};

/// Per-file state other than labels
struct file_state
{
    file_status m_status{file_status::todo};
    std::optional<source_id_cache> m_source_id;
    std::optional<export_record> m_export;

    bool is_default() const { return m_status == file_status::todo && !m_source_id && !m_export; }
};

class project_model
{
public:
    static constexpr int label_file_version = 2;

    /// A default constructed config gives an empty model that never reads or writes any files
    project_model(const project_config &cfg);
    ~project_model();

    // Saves on destruction, so there must only ever be one instance per project
    project_model(const project_model &) = delete;
    project_model &operator=(const project_model &) = delete;

    bool has_project() const { return !m_cfg.m_filename.empty(); }
    const project_config &config() const { return m_cfg; }

    // Rescans files - will reset the active file
    void rescan_files();

    /// Export CSV (to the path defined in the project settings)
    void export_csv();

    const std::vector<audio_file_entry> &get_files() const { return m_files; }

    label_dict m_label_dict;

    /* Labels. All changes go through these, so that the revision tracks them. */
    const labels &get_labels(const std::filesystem::path &file) const;
    void add_label(const std::filesystem::path &file, const label &l);
    bool erase_label(const std::filesystem::path &file, label_id id);
    /// Replace the label with the same id. @return false if there is none
    bool update_label(const std::filesystem::path &file, const label &l);
    void set_labels(const std::filesystem::path &file, labels l);

    /* Per-file state: */
    file_status get_status(const std::filesystem::path &file) const;
    void set_status(const std::filesystem::path &file, file_status status);
    const file_state *find_file_state(const std::filesystem::path &file) const;

    /// The file's source ID (see source_id.h), cached against its size and modification time
    std::string get_source_id(const std::filesystem::path &file);

    void record_export(const std::filesystem::path &file, export_record record);

    /// How a dataset file is identified on disk, independent of the machine and working directory
    struct portable_path
    {
        std::string m_root;     ///< Dataset root as written in the project, empty if outside them
        std::string m_relative; ///< UTF-8 path relative to the root, with forward slashes
    };
    portable_path to_portable(const std::filesystem::path &file) const;

    /* Saving: */
    bool is_dirty() const { return m_revision != m_saved_revision; }
    /// Atomically write the label file, if anything changed since it was last read or written
    void save_if_dirty();

    /// Labels that refer to audio that couldn't be found
    size_t num_unresolved() const { return m_unresolved_labels.size(); }

private:
    project_config m_cfg;
    std::vector<audio_file_entry> m_files; //!< Available files for labelling

    std::map<std::filesystem::path, labels> m_file_labels;     //!< Labels per file
    std::map<std::filesystem::path, file_state> m_file_states; //!< Only non-default states

    /// Records whose audio file couldn't be found, written back verbatim so they aren't lost
    std::vector<nlohmann::json> m_unresolved_labels;
    std::vector<nlohmann::json> m_unresolved_files;

    uint64_t m_revision{};       ///< Incremented by every change
    uint64_t m_saved_revision{}; ///< Revision matching the label file on disk

    std::string class_name_of(const label &l) const;

    /// Find the scanned file that a label on disk refers to
    std::optional<std::filesystem::path> resolve_stored_path(
        const std::string &filename,
        const std::optional<std::string> &root,
        const std::set<std::filesystem::path> &known_files) const;

    void load_labels();
    std::string serialize_labels() const;
};
