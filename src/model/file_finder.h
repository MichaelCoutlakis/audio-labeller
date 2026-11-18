/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <set>
#include <string>
#include <vector>

#include "label.h"
#include "project_model.h"

/// Which files the list shows by their status
enum class file_filter
{
    all,
    todo,
    done,
    skip,
    suggested ///< Files with suggestions waiting for review
};

/// A file filter beyond the status: none, or the built-in one by class. External commands will
/// be listed after these.
enum class file_finder
{
    none,
    by_class ///< Files with labels of the chosen classes
};

/// The built-in filter by class: files with labels of any of the chosen classes
struct class_filter
{
    /// By name, in the order chosen. Names the palette doesn't have are kept, in case another
    /// project has them.
    std::vector<std::string> m_classes;
    bool m_suggestions{false}; ///< Count suggestions as well as confirmed labels
    bool m_rejected{false};    ///< And rejected candidates

    bool has(const std::string &name) const;
    void toggle(const std::string &name);

    /// The palette's ids of the chosen classes
    std::set<label_defn_id> ids(const label_dict &dict) const;

    /// Whether a label is one the filter looks for, given the chosen classes' ids
    bool matches(const label &l, const std::set<label_defn_id> &ids) const;
};

/// The order of the file list
enum class file_sort
{
    path,       ///< The project's order: by root, then path
    class_count ///< By the count of labels of the chosen classes, ties in the project's order
};

/// How the file list is filtered and sorted, remembered between runs
struct file_list_settings
{
    file_filter m_status{file_filter::all};
    std::string m_search; ///< Words every listed file's path contains, case-insensitive
    file_finder m_finder{file_finder::none};
    class_filter m_classes;
    file_sort m_sort{file_sort::path};
    bool m_descending{false};
};

/// Whether a text contains every word of a query, ignoring (ASCII) case. Always for no words.
bool contains_words(const std::string &text, const std::string &query);

struct file_row
{
    const audio_file_entry *m_file{};
    size_t m_class_count{}; ///< Labels of the chosen classes, when filtering by class
};

/// The project's files that pass the filters, in the chosen order. The text search looks at each
/// file's path as the project knows it: the dataset root as written, then the path below it.
/// Filtering by class with no classes chosen (or none the palette has) keeps every file.
std::vector<file_row> list_files(const project_model &project, const file_list_settings &settings);
