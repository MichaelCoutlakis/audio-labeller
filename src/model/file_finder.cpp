/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <cctype>
#include <sstream>

#include "file_finder.h"

namespace
{
std::string lower(std::string s)
{
    std::ranges::transform(
        s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
}

bool class_filter::has(const std::string &name) const
{
    return std::ranges::find(m_classes, name) != m_classes.end();
}

void class_filter::toggle(const std::string &name)
{
    if(auto it = std::ranges::find(m_classes, name); it != m_classes.end())
        m_classes.erase(it);
    else
        m_classes.push_back(name);
}

std::set<label_defn_id> class_filter::ids(const label_dict &dict) const
{
    std::set<label_defn_id> out;
    for(const auto &defn : dict)
        if(has(defn.m_class))
            out.insert(defn.m_id);
    return out;
}

bool class_filter::matches(const label &l, const std::set<label_defn_id> &ids) const
{
    if(!ids.contains(l.m_defn_id))
        return false;
    switch(l.m_state)
    {
    case label_state::confirmed: return true;
    case label_state::suggested: return m_suggestions;
    case label_state::rejected: return m_rejected;
    }
    return false;
}

bool contains_words(const std::string &text, const std::string &query)
{
    const auto haystack = lower(text);
    std::istringstream in(lower(query));
    for(std::string w; in >> w;)
        if(haystack.find(w) == std::string::npos)
            return false;
    return true;
}

std::vector<file_row> list_files(const project_model &project, const file_list_settings &settings)
{
    const bool by_class = settings.m_finder == file_finder::by_class;
    const auto ids = by_class ? settings.m_classes.ids(project.m_label_dict)
                              : std::set<label_defn_id>{};

    std::vector<file_row> rows;
    for(const auto &file : project.get_files())
    {
        const auto status = project.get_status(file.m_path);
        const auto &labels = project.get_labels(file.m_path);
        const auto has_suggestions = [&]
        {
            return std::ranges::any_of(
                labels, [](const label &l) { return l.m_state == label_state::suggested; });
        };
        const auto filter = settings.m_status;
        const bool pass = filter == file_filter::all ||
            (filter == file_filter::todo && status == file_status::todo) ||
            (filter == file_filter::done && status == file_status::done) ||
            (filter == file_filter::skip && status == file_status::skip) ||
            (filter == file_filter::suggested && has_suggestions());
        if(!pass)
            continue;

        if(!settings.m_search.empty())
        {
            const auto portable = project.to_portable(file.m_path);
            const auto path = portable.m_root.empty() ? file.m_display_name
                                                      : portable.m_root + "/" + portable.m_relative;
            if(!contains_words(path, settings.m_search))
                continue;
        }

        file_row row{&file};
        if(by_class && !ids.empty())
        {
            row.m_class_count = static_cast<size_t>(std::ranges::count_if(
                labels, [&](const label &l) { return settings.m_classes.matches(l, ids); }));
            if(row.m_class_count == 0U)
                continue;
        }
        rows.push_back(row);
    }

    if(settings.m_sort == file_sort::class_count && by_class)
        std::ranges::stable_sort(
            rows,
            [&](const file_row &a, const file_row &b)
            {
                return settings.m_descending ? a.m_class_count > b.m_class_count
                                             : a.m_class_count < b.m_class_count;
            });
    else if(settings.m_descending)
        std::ranges::reverse(rows);
    return rows;
}
