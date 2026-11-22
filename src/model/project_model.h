/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
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
    /// An empty path gives an empty config, which loads no project
    static project_config parse_from_file(const std::filesystem::path &filename);

    /// Resolve a project related path (label, export) relative to the project location, after
    /// expanding any ${ENV_VARS} in it
    std::filesystem::path resolve_proj(const std::filesystem::path &p) const;

    /// Resolve one of the dataset roots to a normalised directory path without a trailing separator
    std::filesystem::path resolve_root(const std::filesystem::path &root) const;

    std::filesystem::path m_filename;                 ///< This structure on disk
    /// Directories to scan for audio files, as written in the project (before expanding any
    /// environment variables), as that text is part of each file's identity
    std::vector<std::filesystem::path> m_wav_paths{};
    std::filesystem::path m_palette;                  ///< Classification dictionary
    std::filesystem::path m_labels;        ///< Path for pre-saved labels / labelling directory
    std::filesystem::path m_export_prefix; ///< Where to save exported artifacts
    nlohmann::json m_export;               ///< Clip export settings, see export_settings
    nlohmann::json m_annotations;          ///< Per-recording label files, see annotation_tree
    nlohmann::json m_review;               ///< Playback and review settings, see review_settings
    nlohmann::json m_clip_manifest;        ///< Clips cut elsewhere, see clip_manifest_settings
    nlohmann::json m_assisted_labellers;   ///< Detectors, see assisted_labeller_settings
    nlohmann::json m_high_pass; ///< Filter applied when reading audio, see high_pass_settings
    std::optional<uint32_t> m_channel;     ///< Channel to view and play by default, else a mix
    /// Whether the dataset roots are walked to fill the file list. Without, the list holds only
    /// the recordings something names: the label file, a review list. For reviewing lists that
    /// draw on large datasets.
    bool m_scan_roots{true};
};

/// The settings of a project that the app edits. The rest of the project file (annotation trees,
/// assisted labellers, clip manifest, export) is left as it is.
struct project_settings
{
    std::vector<std::string> m_dataset_roots; ///< UTF-8, as written in the project
    std::string m_palette;                    ///< UTF-8, as written
    std::string m_export_prefix;              ///< UTF-8, as written
    std::optional<uint32_t> m_channel;        ///< Default channel from 0, nullopt for a mix
    nlohmann::json m_review;                  ///< See review_settings
    nlohmann::json m_high_pass;               ///< See high_pass_settings

    bool operator==(const project_settings &) const = default;
};

/// A channel number meaning a mix of all channels
inline constexpr uint32_t mixed_channels = std::numeric_limits<uint32_t>::max();

struct audio_file_entry
{
    std::filesystem::path m_path; ///< Absolute and normalised. The key for a file's labels
    std::string m_display_name;   ///< UTF-8 path relative to its dataset root
    std::string m_search_text;    ///< UTF-8 dataset root as written, then the path below it
};

enum class file_status
{
    todo, ///< Not yet fully labelled
    done, ///< Fully labelled. With no labels, it contains none of the classes.
    skip  ///< Not usable, eg. a corrupt or irrelevant recording
};

const char *to_string(file_status status);
const char *to_string(label_state state);

struct source_id_cache
{
    std::string m_id;
    std::uintmax_t m_size{};
    std::int64_t m_mtime{};
};

/// The source ID of a file (see source_id.h): the cached one if the file's size and modification
/// time still match, else freshly computed. Doesn't touch any project, so it's safe on any thread.
source_id_cache current_source_id(
    const std::filesystem::path &file,
    const std::optional<source_id_cache> &cached);

struct export_record
{
    std::string m_time;      ///< UTC, ISO 8601
    std::string m_directory; ///< UTF-8, as resolved at the time
    uint64_t m_clips{};
    bool m_verified{};
    /// Of the file as exported, which names its clips. Empty in records from before it was kept.
    std::string m_source_id;
};

/// A label file written into an annotation tree, so that it can be removed when the recording
/// stops being done
struct annotation_export_record
{
    std::string m_path; ///< UTF-8, relative to the tree's root, with forward slashes
    std::string m_hash; ///< Of the contents written, to tell whether anyone changed it since

    bool operator==(const annotation_export_record &) const = default;
};

/// Per-file state other than labels
struct file_state
{
    file_status m_status{file_status::todo};
    std::optional<source_id_cache> m_source_id;
    std::optional<export_record> m_export;
    /// Channel to view and play, overriding the project's. mixed_channels for a mix.
    std::optional<uint32_t> m_channel;
    /// By the tree's root as written in the project
    std::map<std::string, annotation_export_record> m_annotation_exports;

    bool is_default() const
    {
        return m_status == file_status::todo &&
            !m_source_id &&
            !m_export &&
            !m_channel &&
            m_annotation_exports.empty();
    }
};

class project_model
{
public:
    /// Version 3 added label states (suggestions), scores and per-file channels
    static constexpr int label_file_version = 3;

    /// A default constructed config gives an empty model that never reads or writes any files
    project_model(const project_config &cfg);
    ~project_model();

    // Saves on destruction, so there must only ever be one instance per project
    project_model(const project_model &) = delete;
    project_model &operator=(const project_model &) = delete;

    /// Switch to another project: saves this one's labels first (if that throws, nothing
    /// changes), then takes over other's, leaving other empty so that it never saves.
    project_model &operator=(project_model &&other);

    bool has_project() const { return !m_cfg.m_filename.empty(); }
    const project_config &config() const { return m_cfg; }

    /// Replace the export section, in the project and atomically in its file. The file's other
    /// keys keep their values and order.
    void set_export_section(const nlohmann::json &section);

    project_settings settings() const;

    /// Write the settings to the project file atomically, keeping its other keys and their order.
    /// The default channel, review settings and export prefix apply at once. Throws, writing
    /// nothing, if there are no dataset roots or the palette doesn't exist.
    /// @return true if the project must be reopened for them to apply: the dataset roots or the
    /// palette changed. Until then the model keeps the old ones, so that its labels are saved
    /// against the roots they were read with.
    bool set_settings(const project_settings &s);

    // Rescans files - will reset the active file
    void rescan_files();

    /// Export CSV (to the path defined in the project settings)
    void export_csv();

    const std::vector<audio_file_entry> &get_files() const { return m_files; }

    /// Whether the file list is every audio file under the dataset roots, or only those named
    bool scans_roots() const { return m_cfg.m_scan_roots; }
    /// For a project that doesn't scan its roots: add recordings under them to the file list
    void add_files(const std::vector<std::filesystem::path> &files);

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
    /// The channel to view and play (nullopt for a mix of all), from the file or else the project
    std::optional<uint32_t> get_channel(const std::filesystem::path &file) const;
    void set_channel(const std::filesystem::path &file, std::optional<uint32_t> channel);
    const file_state *find_file_state(const std::filesystem::path &file) const;

    /// The file's source ID (see source_id.h), cached against its size and modification time
    std::string get_source_id(const std::filesystem::path &file);

    /// Store a source ID computed elsewhere, eg. by an export on another thread
    void set_source_id(const std::filesystem::path &file, const source_id_cache &id);

    void record_export(const std::filesystem::path &file, export_record record);
    /// Forget the file's clip export, once its clips are withdrawn from the dataset
    void clear_export(const std::filesystem::path &file);

    /// Record the label file written for this file into the annotation tree with this root, or
    /// forget it (nullopt)
    void record_annotation_export(
        const std::filesystem::path &file,
        const std::string &tree_root,
        std::optional<annotation_export_record> record);

    /// How a dataset file is identified on disk, independent of the machine and working directory
    struct portable_path
    {
        std::string m_root;     ///< Dataset root as written in the project, empty if outside them
        std::string m_relative; ///< UTF-8 path relative to the root, with forward slashes
    };
    portable_path to_portable(const std::filesystem::path &file) const;

    /* Saving: */
    bool is_dirty() const { return m_revision != m_saved_revision; }
    /// Changes with every change to the labels, statuses or settings, so views can keep what
    /// they work out from them until it changes
    uint64_t revision() const { return m_revision; }
    /// Changes whenever the file list is filled again
    uint64_t files_version() const { return m_files_version; }
    /// Atomically write the label file, if anything changed since it was last read or written
    void save_if_dirty();

    /// Add a class that isn't in the palette, for this session, with the next free ID
    label_defn_id add_session_class(const std::string &name);

    /// Add classes to the palette file and to this session, each with the next free ID and a
    /// colour of its own. Throws if the palette can't be written.
    void add_palette_classes(const std::vector<std::string> &names);

    /// Labels that refer to audio that couldn't be found
    size_t num_unresolved() const { return m_unresolved_labels.size(); }

    /// Things worth telling the user that were found while opening the project, eg. palette keys
    /// that are no longer used
    const std::vector<std::string> &notices() const { return m_notices; }

private:
    project_config m_cfg;
    std::vector<audio_file_entry> m_files; //!< Available files for labelling
    std::set<std::filesystem::path> m_named_files; ///< Without a scan, the files named so far
    /// Fill m_files from the named files, for a project that doesn't scan its roots
    void list_named_files();

    std::map<std::filesystem::path, labels> m_file_labels;     //!< Labels per file
    std::map<std::filesystem::path, file_state> m_file_states; //!< Only non-default states

    /// Records whose audio file couldn't be found, written back verbatim so they aren't lost
    std::vector<nlohmann::json> m_unresolved_labels;
    std::vector<nlohmann::json> m_unresolved_files;
    std::vector<std::string> m_notices;

    uint64_t m_revision{};       ///< Incremented by every change
    uint64_t m_files_version{};
    uint64_t m_saved_revision{}; ///< Revision matching the label file on disk

    std::string class_name_of(const label &l) const;

    /// Find the scanned file that a label on disk refers to
    std::optional<std::filesystem::path> resolve_stored_path(
        const std::string &filename,
        const std::optional<std::string> &root,
        const std::set<std::filesystem::path> &known_files) const;

    /// Change the project file atomically, keeping its other keys and their order
    void rewrite_project_file(const std::function<void(nlohmann::ordered_json &)> &change) const;

    void load_labels();
    std::string serialize_labels() const;
};
