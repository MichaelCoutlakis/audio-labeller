/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include <spdlog/spdlog.h>

#include "../io/audio_source.h"
#include "../io/files.h"
#include "../model/paths.h"
#include "annotation_tree.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
constexpr const char *rejected_prefix = "x:";
constexpr double same_time_s = 0.001; ///< Labels this close are taken to be the same

bool iequals(const std::string &a, const std::string &b)
{
    return std::ranges::equal(
        a,
        b,
        [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
}

template <typename E>
E enum_from(const json &j, const char *key, E fallback, std::initializer_list<std::pair<const char *, E>> values)
{
    auto it = j.find(key);
    if(it == j.end() || it->is_null())
        return fallback;
    const auto text = it->get<std::string>();
    for(const auto &[name, value] : values)
        if(text == name)
            return value;
    std::string expected;
    for(const auto &[name, value] : values)
        expected += (expected.empty() ? "" : ", ") + std::string{name};
    throw std::runtime_error(
        "Unknown " + std::string{key} + " \"" + text + "\" in annotations, expected " + expected);
}

/// The class with this name, matching exactly or else ignoring case
std::optional<label_defn_id> find_class(const label_dict &dict, const std::string &name)
{
    for(const auto &defn : dict)
        if(defn.m_class == name)
            return defn.m_id;
    for(const auto &defn : dict)
        if(iequals(defn.m_class, name))
            return defn.m_id;
    return std::nullopt;
}

bool same_label(const label &a, const label &b)
{
    return a.m_defn_id == b.m_defn_id &&
        std::abs(a.m_start_s - b.m_start_s) < same_time_s &&
        std::abs(a.m_stop_s - b.m_stop_s) < same_time_s;
}
}

std::vector<annotation_tree_settings> annotation_tree_settings::list_from_json(const json &j)
{
    std::vector<annotation_tree_settings> trees;
    if(j.is_null())
        return trees;
    if(!j.is_array())
        throw std::runtime_error("\"annotations\" in the project should be a list");

    for(const auto &entry : j)
    {
        annotation_tree_settings s;
        s.m_root = path_from_utf8(entry.at("root").get<std::string>());
        s.m_format = entry.value("format", s.m_format);
        s.m_name = entry.value("name", s.m_format + " (" + generic_utf8(s.m_root) + ")");
        s.m_naming = enum_from(
            entry,
            "naming",
            naming::replace,
            {{"replace", naming::replace}, {"append", naming::append}});
        s.m_extension = entry.value("extension", std::string{});
        s.m_import = enum_from(
            entry,
            "import",
            import_mode::none,
            {{"none", import_mode::none},
             {"suggestions", import_mode::suggestions},
             {"labels", import_mode::labels}});
        s.m_export = entry.value("export", true);
        s.m_export_files = enum_from(
            entry,
            "export-files",
            export_files::done,
            {{"done", export_files::done}, {"all", export_files::all}});
        s.m_include_suggestions = entry.value("include-suggestions", false);
        s.m_empty_means_done =
            entry.value("empty-means-done", s.m_import != import_mode::suggestions);
        s.m_format_options = entry.value("format-options", json::object());

        make_label_format(s.m_format, s.m_format_options); // Check it early
        trees.push_back(std::move(s));
    }
    return trees;
}

std::string label_text(const label &l, const label_dict &dict)
{
    const auto *defn = dict.find_ptr(l.m_defn_id);
    const std::string name = defn ? defn->m_class : std::string{};
    return l.m_state == label_state::rejected ? rejected_prefix + name : name;
}

parsed_label_text parse_label_text(const std::string &text)
{
    if(text.starts_with(rejected_prefix))
        return {text.substr(2), true};
    return {text, false};
}

annotation_tree::annotation_tree(const project_config &cfg, annotation_tree_settings settings) :
    m_settings(std::move(settings)),
    m_format(make_label_format(m_settings.m_format, m_settings.m_format_options)),
    m_root(cfg.resolve_root(m_settings.m_root))
{
}

std::string annotation_tree::extension() const
{
    return m_settings.m_extension.empty() ? std::string{m_format->default_extension()}
                                          : m_settings.m_extension;
}

std::optional<fs::path> annotation_tree::path_for(const project_model::portable_path &audio) const
{
    if(audio.m_root.empty())
        return std::nullopt;

    auto rel = path_from_utf8(audio.m_relative);
    if(m_settings.m_naming == annotation_tree_settings::naming::replace)
        rel.replace_extension();
    rel += path_from_utf8(extension());
    return (m_root / rel).lexically_normal();
}

std::vector<text_label> labels_for_export(
    const labels &file_labels,
    const label_dict &dict,
    bool include_suggestions)
{
    std::vector<text_label> out;
    for(const auto &l : file_labels)
    {
        if(l.m_state == label_state::suggested && !include_suggestions)
            continue;
        text_label t{l.m_start_s, l.m_stop_s, label_text(l, dict)};
        if(l.m_state != label_state::confirmed)
            t.m_score = l.m_score;
        out.push_back(std::move(t));
    }
    std::ranges::stable_sort(
        out,
        {},
        [](const text_label &t) { return std::pair{t.m_start_s, t.m_stop_s}; });
    return out;
}

void add_text_labels(
    project_model &project,
    const fs::path &file,
    const std::vector<text_label> &text_labels,
    label_state state,
    annotation_import_summary &summary)
{
    for(const auto &t : text_labels)
    {
        const auto parsed = parse_label_text(t.m_text);
        auto cls = find_class(project.m_label_dict, parsed.m_class);
        if(!cls)
        {
            cls = project.add_session_class(parsed.m_class);
            summary.m_new_classes.push_back(parsed.m_class);
        }

        label l{
            get_next_label_id(),
            *cls,
            std::max(0., std::min(t.m_start_s, t.m_stop_s)),
            std::max(0., std::max(t.m_start_s, t.m_stop_s)),
            parsed.m_rejected ? label_state::rejected : state,
            t.m_score
        };
        const auto &existing = project.get_labels(file);
        if(std::ranges::any_of(existing, [&](const label &e) { return same_label(e, l); }))
        {
            ++summary.m_duplicates;
            continue;
        }
        project.add_label(file, l);
        ++summary.m_added;
    }
}

void finish_summary(annotation_import_summary &summary)
{
    std::ranges::sort(summary.m_new_classes);
    const auto [first, last] = std::ranges::unique(summary.m_new_classes);
    summary.m_new_classes.erase(first, last);
}

annotation_import_summary import_annotations(project_model &project, const annotation_tree &tree)
{
    annotation_import_summary summary;
    const auto &settings = tree.settings();
    const auto state = settings.m_import == annotation_tree_settings::import_mode::labels
        ? label_state::confirmed
        : label_state::suggested;

    std::error_code ec;
    if(!fs::is_directory(tree.root(), ec))
        throw std::runtime_error("Annotation folder " + generic_utf8(tree.root()) + " not found");

    std::set<fs::path> expected;
    for(const auto &file : project.get_files())
    {
        const auto path = tree.path_for(project.to_portable(file.m_path));
        if(!path)
            continue;
        expected.insert(*path);
        if(!fs::is_regular_file(*path, ec))
            continue;

        try
        {
            std::ifstream in(*path, std::ios::binary);
            if(!in)
                throw std::runtime_error("Could not open");
            const auto text_labels = tree.format().read(in);
            ++summary.m_files;

            if(text_labels.empty())
            {
                const auto &existing = project.get_labels(file.m_path);
                const bool has_labels = std::ranges::any_of(existing, &label::is_confirmed);
                if(settings.m_empty_means_done &&
                   !has_labels &&
                   project.get_status(file.m_path) == file_status::todo)
                {
                    project.set_status(file.m_path, file_status::done);
                    ++summary.m_marked_done;
                }
                continue;
            }

            add_text_labels(project, file.m_path, text_labels, state, summary);
        }
        catch(const std::exception &e)
        {
            summary.m_errors.push_back(generic_utf8(*path) + ": " + e.what());
        }
    }

    // Label files for recordings the project doesn't have, eg. a dataset root that's missing
    const auto ext = tree.extension();
    for(const auto &entry :
        fs::recursive_directory_iterator(tree.root(), fs::directory_options::skip_permission_denied))
    {
        if(!entry.is_regular_file(ec))
            continue;
        const auto name = generic_utf8(entry.path().filename());
        if(name.ends_with(ext) && !expected.contains(entry.path().lexically_normal()))
            ++summary.m_unmatched;
    }

    finish_summary(summary);
    spdlog::info(
        "Imported {} labels from {} files in {}",
        summary.m_added,
        summary.m_files,
        generic_utf8(tree.root()));
    return summary;
}

annotation_export_summary export_annotations(project_model &project, const annotation_tree &tree)
{
    annotation_export_summary summary;
    const auto &settings = tree.settings();

    for(const auto &file : project.get_files())
    {
        const auto status = project.get_status(file.m_path);
        if(status == file_status::skip)
            continue;

        const auto text_labels = labels_for_export(
            project.get_labels(file.m_path),
            project.m_label_dict,
            settings.m_include_suggestions);

        const bool write = status == file_status::done ||
            (settings.m_export_files == annotation_tree_settings::export_files::all &&
             !text_labels.empty());
        if(!write)
            continue;

        const auto path = tree.path_for(project.to_portable(file.m_path));
        if(!path)
            continue;

        try
        {
            recording_info info;
            if(tree.format().needs_recording_info())
                info.m_sample_rate_hz = audio_source(file.m_path).format().m_sample_rate_hz;

            std::ostringstream out;
            tree.format().write(out, text_labels, info);
            const auto contents = out.str();

            ++summary.m_files;
            summary.m_labels += text_labels.size();

            std::error_code ec;
            if(fs::is_regular_file(*path, ec))
            {
                std::ifstream in(*path, std::ios::binary);
                const std::string existing{
                    std::istreambuf_iterator<char>(in),
                    std::istreambuf_iterator<char>()
                };
                if(existing == contents)
                {
                    ++summary.m_unchanged;
                    continue;
                }
            }
            fs::create_directories(path->parent_path());
            write_file_atomically(*path, contents);
        }
        catch(const std::exception &e)
        {
            summary.m_errors.push_back(file.m_display_name + ": " + e.what());
        }
    }
    spdlog::info("Wrote {} label files to {}", summary.m_files, generic_utf8(tree.root()));
    return summary;
}
