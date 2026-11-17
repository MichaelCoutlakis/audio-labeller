/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

/// What the app's New project dialog asks for. The data and the project can be in different
/// places: the data root is only read, and the project's own files go beside the project file.
struct new_project_spec
{
    std::filesystem::path m_project_file; ///< A .json extension is added if it has none
    std::filesystem::path m_data_root;    ///< Searched recursively for audio files
    /// A palette to share with other projects. Without one, a palette is made from m_classes.
    std::optional<std::filesystem::path> m_palette;
    std::vector<std::string> m_classes;
};

/// Class names from comma separated text, trimmed, without empty ones. Throws on a duplicate.
std::vector<std::string> split_class_names(const std::string &text);

/// The project file a spec writes, with its extension
std::filesystem::path new_project_file(const new_project_spec &spec);

/// The files a new project creates beside its project file: <name>-palette.json (unless an
/// existing palette is used) and the label file <name>-labels.json, written at the first save
struct new_project_files
{
    std::filesystem::path m_palette;
    std::filesystem::path m_labels;
};
new_project_files new_project_files_for(const std::filesystem::path &project_file);

/// A path as written in a project file: relative to the project's folder if it's inside it, so
/// that the two can move together, else absolute. UTF-8 with forward slashes.
std::string path_for_project(
    const std::filesystem::path &target,
    const std::filesystem::path &project_dir);

/// Write a project (and its palette, if it needs one). Nothing is overwritten: throws, having
/// written nothing, if the project, palette or label file already exists, the data root isn't a
/// folder, or there are no classes. @return the project file written
std::filesystem::path create_project(const new_project_spec &spec);
