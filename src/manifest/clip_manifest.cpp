/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <istream>
#include <set>
#include <stdexcept>

#include "../model/paths.h"
#include "clip_manifest.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
std::string lower(std::string s)
{
    std::ranges::transform(
        s,
        s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
}

std::optional<clip_manifest_settings> clip_manifest_settings::from_json(const json &j)
{
    if(!j.is_object())
        return std::nullopt;
    clip_manifest_settings s;
    s.m_path = path_from_utf8(j.at("path").get<std::string>());
    if(auto it = j.find("clip-root"); it != j.end())
        s.m_clip_root = path_from_utf8(it->get<std::string>());
    if(auto it = j.find("columns"); it != j.end() && it->is_object())
    {
        s.m_clip_column = it->value("clip", s.m_clip_column);
        s.m_source_column = it->value("source", s.m_source_column);
        s.m_onset_column = it->value("onset", s.m_onset_column);
        s.m_label_column = it->value("label", s.m_label_column);
    }
    s.m_dataset_root = j.value("dataset-root", s.m_dataset_root);
    s.m_match_s = j.value("match_s", s.m_match_s);
    return s;
}

std::vector<std::vector<std::string>> read_csv(std::istream &in)
{
    std::vector<std::vector<std::string>> records;
    std::vector<std::string> record;
    std::string field;
    bool quoted = false, any = false;
    char c{};

    const auto end_record = [&]
    {
        if(any || !field.empty() || !record.empty())
        {
            record.push_back(std::move(field));
            records.push_back(std::move(record));
        }
        record.clear();
        field.clear();
        any = false;
    };

    // Skip a UTF-8 byte order mark
    if(in.peek() == 0xEF)
    {
        char bom[3]{};
        in.read(bom, 3);
    }
    while(in.get(c))
    {
        if(quoted)
        {
            if(c == '"')
            {
                if(in.peek() == '"')
                {
                    field += '"';
                    in.get(c);
                }
                else
                    quoted = false;
            }
            else
                field += c;
            continue;
        }
        switch(c)
        {
        case '"':
            quoted = true;
            any = true;
            break;
        case ',':
            record.push_back(std::move(field));
            field.clear();
            any = true;
            break;
        case '\r':
            break;
        case '\n':
            end_record();
            break;
        default:
            field += c;
        }
    }
    end_record();
    return records;
}

std::optional<label_defn_id> class_named(const label_dict &dict, const std::string &name)
{
    for(const auto &defn : dict)
        if(defn.m_class == name)
            return defn.m_id;
    for(const auto &defn : dict)
        if(lower(defn.m_class) == lower(name))
            return defn.m_id;
    return std::nullopt;
}

clip_manifest load_clip_manifest(const project_model &project, const clip_manifest_settings &settings)
{
    const auto &cfg = project.config();
    const auto path = cfg.resolve_proj(settings.m_path);
    std::ifstream in(path, std::ios::binary);
    if(!in)
        throw std::runtime_error("Could not open the clip manifest " + generic_utf8(path));

    auto records = read_csv(in);
    if(records.empty())
        throw std::runtime_error("The clip manifest " + generic_utf8(path) + " is empty");

    clip_manifest manifest;
    manifest.m_columns = records.front();
    const auto column = [&](const std::string &name)
    {
        for(size_t i = 0U; i != manifest.m_columns.size(); ++i)
            if(lower(manifest.m_columns[i]) == lower(name))
                return i;
        throw std::runtime_error("The clip manifest has no \"" + name + "\" column");
    };
    const auto clip_col = column(settings.m_clip_column);
    const auto source_col = column(settings.m_source_column);
    const auto onset_col = column(settings.m_onset_column);
    const auto label_col = column(settings.m_label_column);

    const auto clip_root = settings.m_clip_root.empty() ? path.parent_path()
                                                        : cfg.resolve_root(settings.m_clip_root);

    std::set<fs::path> known;
    for(const auto &file : project.get_files())
        known.insert(file.m_path);

    for(size_t r = 1U; r < records.size(); ++r)
    {
        auto &fields = records[r];
        fields.resize(manifest.m_columns.size());

        clip_manifest::row row;
        row.m_clip = (clip_root / path_from_utf8(fields[clip_col])).lexically_normal();
        row.m_onset_s = std::strtod(fields[onset_col].c_str(), nullptr);
        row.m_label = fields[label_col];

        const auto rel = path_from_utf8(fields[source_col]);
        for(const auto &root : cfg.m_wav_paths)
        {
            if(!settings.m_dataset_root.empty() && generic_utf8(root) != settings.m_dataset_root)
                continue;
            if(auto candidate = (cfg.resolve_root(root) / rel).lexically_normal();
               known.contains(candidate))
            {
                row.m_source = candidate;
                break;
            }
        }
        manifest.m_unresolved += !row.m_source;
        row.m_fields = std::move(fields);
        manifest.m_rows.push_back(std::move(row));
    }
    return manifest;
}

const label *find_label_at(
    const labels &file_labels,
    double onset_s,
    double window_s,
    std::optional<label_defn_id> preferred)
{
    const label *best = nullptr;
    auto score = [&](const label &l)
    {
        // Same class first, then the nearest
        return std::pair{preferred && l.m_defn_id != *preferred, std::abs(l.m_start_s - onset_s)};
    };
    for(const auto &l : file_labels)
    {
        if(std::abs(l.m_start_s - onset_s) > window_s)
            continue;
        if(!best || score(l) < score(*best))
            best = &l;
    }
    return best;
}

label_id apply_clip_verdict(
    project_model &project,
    const fs::path &source,
    double onset_s,
    double window_s,
    clip_verdict verdict,
    label_defn_id defn_id)
{
    const auto state =
        verdict == clip_verdict::not_event ? label_state::rejected : label_state::confirmed;
    // For a relabel, the existing label is the one at the onset whatever its class
    const auto preferred =
        verdict == clip_verdict::relabel ? std::nullopt : std::optional<label_defn_id>{defn_id};

    if(const auto *existing = find_label_at(project.get_labels(source), onset_s, window_s, preferred))
    {
        auto l = *existing;
        l.m_state = state;
        if(verdict != clip_verdict::not_event)
            l.m_defn_id = defn_id;
        project.update_label(source, l);
        return l.m_id;
    }

    const label l{get_next_label_id(), defn_id, onset_s, onset_s, state};
    project.add_label(source, l);
    return l.m_id;
}
