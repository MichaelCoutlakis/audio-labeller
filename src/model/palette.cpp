/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <cctype>
#include <sstream>

#include "palette.h"

namespace
{
std::string lower(std::string s)
{
    std::ranges::transform(
        s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
}

std::vector<const label_defn *> filter_classes(const label_dict &dict, const std::string &query)
{
    std::vector<std::string> words;
    std::istringstream in(lower(query));
    for(std::string w; in >> w;)
        words.push_back(w);

    std::vector<const label_defn *> starts, contains;
    if(words.empty())
        return starts;
    for(const auto &defn : dict)
    {
        const auto name = lower(defn.m_class);
        if(!std::ranges::all_of(
               words, [&](const std::string &w) { return name.find(w) != std::string::npos; }))
            continue;
        (name.starts_with(words.front()) ? starts : contains).push_back(&defn);
    }
    starts.insert(starts.end(), contains.begin(), contains.end());
    return starts;
}

std::vector<label_defn_id> numbered_classes(
    const label_dict &dict,
    const std::vector<std::string> &favourites)
{
    constexpr size_t keys = 9U;
    std::vector<label_defn_id> out;
    for(const auto &name : favourites)
        for(const auto &defn : dict)
            if(defn.m_class == name && out.size() < keys)
                out.push_back(defn.m_id);
    if(!out.empty())
        return out;
    for(const auto &defn : dict)
        if(out.size() < keys)
            out.push_back(defn.m_id);
    return out;
}
