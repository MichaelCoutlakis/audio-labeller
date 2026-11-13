/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../model/project_model.h"
#include "label_format.h"

/// One entry of the project's "annotations" list: a tree of per-recording label files that
/// mirrors the dataset roots, eg. <root>/site1/rec01.txt for <dataset root>/site1/rec01.wav.
/// Trees are how labels are exchanged with other tools: an external pipeline's proposals, the
/// reviewed labels it reads back, or Audacity and Raven.
struct annotation_tree_settings
{
    enum class naming
    {
        replace, ///< rec01.wav -> rec01.txt
        append   ///< rec01.wav -> rec01.wav.txt
    };
    enum class import_mode
    {
        none,        ///< Export only
        suggestions, ///< Read as suggestions, to be reviewed
        labels       ///< Read as labels
    };
    enum class export_files
    {
        done, ///< Only files marked done, so a file's presence means it was reviewed
        all   ///< Also files with labels that aren't marked done
    };

    std::string m_name;
    std::string m_format{"audacity"};
    std::filesystem::path m_root; ///< Relative to the project file, may use ${ENV_VARS}
    naming m_naming{naming::replace};
    std::string m_extension; ///< Empty for the format's own
    import_mode m_import{import_mode::none};
    bool m_export{true};
    export_files m_export_files{export_files::done};
    bool m_include_suggestions{false}; ///< Write suggestions not yet reviewed
    /// An empty file means the recording was checked and has no events, so it's marked done.
    /// Defaults to on, except for trees imported as suggestions.
    bool m_empty_means_done{true};
    nlohmann::json m_format_options; ///< Passed to the format, see make_label_format()

    /// From the project's "annotations" list (which may be null)
    static std::vector<annotation_tree_settings> list_from_json(const nlohmann::json &j);
};

/// How a label's class and state are written as text: the class name, with "x:" in front for
/// rejected candidates
std::string label_text(const label &l, const label_dict &dict);

struct parsed_label_text
{
    std::string m_class;
    bool m_rejected{};
};
parsed_label_text parse_label_text(const std::string &text);

class annotation_tree
{
public:
    annotation_tree(const project_config &cfg, annotation_tree_settings settings);

    const annotation_tree_settings &settings() const { return m_settings; }
    const label_format &format() const { return *m_format; }
    const std::filesystem::path &root() const { return m_root; }

    /// Of the label files, including the leading dot
    std::string extension() const;

    /// Where an audio file's labels are. nullopt for files outside the dataset roots.
    std::optional<std::filesystem::path> path_for(
        const project_model::portable_path &audio) const;

private:
    annotation_tree_settings m_settings;
    std::unique_ptr<label_format> m_format;
    std::filesystem::path m_root; ///< Resolved
};

struct annotation_import_summary
{
    size_t m_files{};       ///< Label files read
    size_t m_added{};       ///< Labels and suggestions added
    size_t m_duplicates{};  ///< Already present, so not added again
    size_t m_marked_done{}; ///< Empty files whose recordings were marked done
    size_t m_unmatched{};   ///< Label files with no audio file in the project
    std::vector<std::string> m_new_classes; ///< Names not in the palette, added for the session
    std::vector<std::string> m_errors;
};

/// Read the tree's label file for every audio file in the project. Labels already present (the
/// same class and times to within a millisecond, in any state) aren't added again, so importing
/// twice changes nothing.
annotation_import_summary import_annotations(project_model &project, const annotation_tree &tree);

struct annotation_export_summary
{
    size_t m_files{};     ///< Label files written or already up to date
    size_t m_unchanged{}; ///< Of those, already up to date
    size_t m_labels{};
    std::vector<std::string> m_errors;
};

/// Write the tree's label file for every file selected by its settings. Confirmed labels are
/// written as their class, rejected ones with "x:" in front, and suggestions only if the settings
/// ask. A file marked done with no labels gets an empty file. Files are written atomically, and
/// only if their contents change.
annotation_export_summary export_annotations(project_model &project, const annotation_tree &tree);

/// The text labels of one file, as export_annotations() writes them, sorted by time
std::vector<text_label> labels_for_export(
    const labels &file_labels,
    const label_dict &dict,
    bool include_suggestions);
