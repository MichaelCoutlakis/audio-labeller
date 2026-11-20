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
#include <map>
#include <set>
#include <stdexcept>

#include "../io/files.h"
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

namespace
{
clip_manifest_settings read_settings(const json &j)
{
    clip_manifest_settings s;
    if(auto it = j.find("path"); it != j.end() && it->is_string())
        s.m_path = path_from_utf8(it->get<std::string>());
    if(auto it = j.find("clip-root"); it != j.end())
        s.m_clip_root = path_from_utf8(it->get<std::string>());
    if(auto it = j.find("columns"); it != j.end() && it->is_object())
    {
        s.m_clip_column = it->value("clip", s.m_clip_column);
        s.m_source_column = it->value("source", s.m_source_column);
        s.m_onset_column = it->value("onset", it->value("start", s.m_onset_column));
        s.m_label_column = it->value("label", s.m_label_column);
        s.m_stop_column = it->value("stop", s.m_stop_column);
        s.m_duration_column = it->value("duration", s.m_duration_column);
        s.m_root_column = it->value("root", s.m_root_column);
        s.m_channel_column = it->value("channel", s.m_channel_column);
        s.m_id_column = it->value("id", s.m_id_column);
    }
    s.m_dataset_root = j.value("dataset-root", s.m_dataset_root);
    s.m_match_s = j.value("match_s", s.m_match_s);
    s.m_region_s = j.value("region_s", s.m_region_s);
    return s;
}

/// The older name a default column had, read where the new one isn't there
std::string older_name(const std::string &column)
{
    if(column == "path")
        return "source_path";
    if(column == "start_s")
        return "onset_s";
    return {};
}

/// The columns of a list's header, by the settings' names
struct column_indices
{
    std::optional<size_t> m_source, m_onset, m_stop, m_duration, m_label, m_root, m_channel,
        m_clip, m_id;

    column_indices(const std::vector<std::string> &header, const clip_manifest_settings &s)
    {
        const auto find = [&](const std::string &name) -> std::optional<size_t>
        {
            for(const auto &candidate : {name, older_name(name)})
                for(size_t i = 0U; !candidate.empty() && i != header.size(); ++i)
                    if(lower(header[i]) == lower(candidate))
                        return i;
            return std::nullopt;
        };
        m_source = find(s.m_source_column);
        m_onset = find(s.m_onset_column);
        m_stop = find(s.m_stop_column);
        m_duration = find(s.m_duration_column);
        m_label = find(s.m_label_column);
        m_root = find(s.m_root_column);
        m_channel = find(s.m_channel_column);
        m_clip = find(s.m_clip_column);
        m_id = find(s.m_id_column);
    }

    static const std::string &field(const std::vector<std::string> &fields, std::optional<size_t> i)
    {
        static const std::string none;
        return i && *i < fields.size() ? fields[*i] : none;
    }

    /// Its ID if the list has them, else what says which event it is
    std::string key(const std::vector<std::string> &fields) const
    {
        if(m_id && !field(fields, m_id).empty())
            return "id\x1f" + field(fields, m_id);
        return field(fields, m_source) + "\x1f" + field(fields, m_onset) + "\x1f" +
            field(fields, m_label);
    }
};

constexpr const char *result_columns[] = {"verdict", "new_label", "note"};

bool is_result_column(const std::string &name)
{
    return std::ranges::any_of(result_columns, [&](const char *c) { return lower(name) == c; });
}

std::optional<double> number(const std::string &field)
{
    char *end{};
    const double value = std::strtod(field.c_str(), &end);
    if(end == field.c_str() || !std::isfinite(value))
        return std::nullopt;
    return value;
}
}

std::optional<clip_manifest_settings> clip_manifest_settings::from_json(const json &j)
{
    // A section may only rename columns, for lists opened from the File menu
    if(!j.is_object() || !j.contains("path"))
        return std::nullopt;
    return read_settings(j);
}

clip_manifest_settings clip_manifest_settings::for_list(const json &j, const fs::path &list)
{
    auto s = j.is_object() ? read_settings(j) : clip_manifest_settings{};
    s.m_path = list;
    // Clips are beside the list unless the project says where, which is for its own list
    if(j.is_object() && !j.contains("clip-root"))
        s.m_clip_root.clear();
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

time_span clip_manifest::row::span(double default_length_s) const
{
    const auto stop = m_stop_s.value_or(m_onset_s + default_length_s);
    return {m_onset_s, std::max(stop, m_onset_s)};
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

size_t clip_manifest::heard() const
{
    return static_cast<size_t>(
        std::ranges::count_if(m_rows, [](const row &r) { return !r.m_result.m_verdict.empty(); }));
}

fs::path results_path(const fs::path &list)
{
    auto name = list.stem();
    name += ".results";
    name += list.extension().empty() ? fs::path{".csv"} : list.extension();
    return list.parent_path() / name;
}

std::string results_csv(const clip_manifest &manifest)
{
    std::vector<size_t> kept;
    for(size_t c = 0U; c != manifest.m_columns.size(); ++c)
        if(!is_result_column(manifest.m_columns[c]))
            kept.push_back(c);

    std::string out;
    const auto line = [&](const std::vector<std::string> &fields, const row_verdict &v)
    {
        for(const auto c : kept)
            out += csv_escape(c < fields.size() ? fields[c] : std::string{}) + ",";
        out += csv_escape(v.m_verdict) + "," + csv_escape(v.m_new_label) + "," +
            csv_escape(v.m_note) + "\n";
    };
    line(manifest.m_columns, {"verdict", "new_label", "note"});
    for(const auto &r : manifest.m_rows)
        line(r.m_fields, r.m_result);
    return out;
}

void write_results(const clip_manifest &manifest)
{
    write_file_atomically(manifest.m_results_path, results_csv(manifest));
}

void read_results(clip_manifest &manifest, const clip_manifest_settings &settings)
{
    std::ifstream in(manifest.m_results_path, std::ios::binary);
    if(!in)
        return;
    const auto records = read_csv(in);
    if(records.empty())
        return;
    const auto &header = records.front();
    const column_indices columns(header, settings);
    std::optional<size_t> found[3];
    for(size_t c = 0U; c != header.size(); ++c)
        for(size_t k = 0U; k != 3U; ++k)
            if(lower(header[c]) == result_columns[k])
                found[k] = c;
    if(!found[0])
        throw std::runtime_error(
            "The results file " + generic_utf8(manifest.m_results_path) + " has no verdict column");

    std::map<std::string, row_verdict> verdicts;
    for(size_t r = 1U; r < records.size(); ++r)
    {
        const auto &fields = records[r];
        row_verdict v{
            column_indices::field(fields, found[0]),
            column_indices::field(fields, found[1]),
            column_indices::field(fields, found[2])};
        if(v != row_verdict{})
            verdicts[columns.key(fields)] = std::move(v);
    }
    for(auto &row : manifest.m_rows)
        if(auto it = verdicts.find(row.m_key); it != verdicts.end())
            row.m_result = it->second;
}

clip_manifest load_clip_manifest(const project_model &project, const clip_manifest_settings &settings)
{
    const auto &cfg = project.config();
    const auto path = cfg.resolve_proj(settings.m_path);
    std::ifstream in(path, std::ios::binary);
    if(!in)
        throw std::runtime_error("Could not open the review list " + generic_utf8(path));

    auto records = read_csv(in);
    if(records.empty())
        throw std::runtime_error("The review list " + generic_utf8(path) + " is empty");

    clip_manifest manifest;
    manifest.m_path = path;
    manifest.m_results_path = results_path(path);
    manifest.m_columns = records.front();
    const column_indices columns(manifest.m_columns, settings);
    const auto require = [&](std::optional<size_t> i, const std::string &name)
    {
        if(!i)
            throw std::runtime_error("The review list has no \"" + name + "\" column");
        return *i;
    };
    const auto source_col = require(columns.m_source, settings.m_source_column);
    const auto onset_col = require(columns.m_onset, settings.m_onset_column);

    const auto clip_root = settings.m_clip_root.empty() ? path.parent_path()
                                                        : cfg.resolve_root(settings.m_clip_root);

    std::set<fs::path> known;
    for(const auto &file : project.get_files())
        known.insert(file.m_path);

    // The roots to look in for a row: the row's own, else the list's, else each
    const auto roots_for = [&](const std::string &row_root)
    {
        const auto &wanted = row_root.empty() ? settings.m_dataset_root : row_root;
        std::vector<fs::path> roots;
        for(const auto &root : cfg.m_wav_paths)
            if(wanted.empty() ||
               generic_utf8(root) == wanted ||
               cfg.resolve_root(root) == cfg.resolve_root(path_from_utf8(wanted)))
                roots.push_back(cfg.resolve_root(root));
        return roots;
    };

    for(size_t r = 1U; r < records.size(); ++r)
    {
        auto &fields = records[r];
        fields.resize(manifest.m_columns.size());
        const auto field = [&](std::optional<size_t> i) -> const std::string &
        { return column_indices::field(fields, i); };

        clip_manifest::row row;
        if(const auto &clip = field(columns.m_clip); !clip.empty())
            row.m_clip = (clip_root / path_from_utf8(clip)).lexically_normal();
        row.m_onset_s = number(fields[onset_col]).value_or(0.);
        row.m_label = field(columns.m_label);
        row.m_stop_s = number(field(columns.m_stop));
        if(!row.m_stop_s)
            if(const auto duration = number(field(columns.m_duration)))
                row.m_stop_s = row.m_onset_s + *duration;
        if(row.m_stop_s && *row.m_stop_s <= row.m_onset_s)
            row.m_stop_s.reset();
        if(const auto channel = number(field(columns.m_channel)); channel && *channel >= 1.)
            row.m_channel = static_cast<uint32_t>(*channel) - 1U;
        row.m_key = columns.key(fields);

        const auto rel = path_from_utf8(fields[source_col]);
        if(rel.is_absolute())
        {
            if(const auto p = rel.lexically_normal(); known.contains(p))
                row.m_source = p;
        }
        else
            for(const auto &root : roots_for(field(columns.m_root)))
                if(auto candidate = (root / rel).lexically_normal(); known.contains(candidate))
                {
                    row.m_source = candidate;
                    break;
                }
        manifest.m_unresolved += !row.m_source;
        row.m_fields = std::move(fields);
        manifest.m_rows.push_back(std::move(row));
    }
    read_results(manifest, settings);
    return manifest;
}

const char *verdict_name(clip_verdict v)
{
    switch(v)
    {
    case clip_verdict::correct: return "correct";
    case clip_verdict::relabel: return "relabel";
    case clip_verdict::not_event:
    default: return "not_event";
    }
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
        // Same class first, then the nearest start
        return std::pair{preferred && l.m_defn_id != *preferred, std::abs(l.m_start_s - onset_s)};
    };
    for(const auto &l : file_labels)
    {
        if(onset_s < l.m_start_s - window_s ||
           onset_s > std::max(l.m_stop_s, l.m_start_s) + window_s)
            continue;
        if(!best || score(l) < score(*best))
            best = &l;
    }
    return best;
}

label_id apply_clip_verdict(
    project_model &project,
    const fs::path &source,
    time_span event,
    double window_s,
    clip_verdict verdict,
    label_defn_id defn_id)
{
    const auto state =
        verdict == clip_verdict::not_event ? label_state::rejected : label_state::confirmed;
    // For a relabel, the existing label is the one at the onset whatever its class
    const auto preferred =
        verdict == clip_verdict::relabel ? std::nullopt : std::optional<label_defn_id>{defn_id};

    if(const auto *existing =
           find_label_at(project.get_labels(source), event.first, window_s, preferred))
    {
        auto l = *existing;
        l.m_state = state;
        if(verdict != clip_verdict::not_event)
            l.m_defn_id = defn_id;
        project.update_label(source, l);
        return l.m_id;
    }

    const label
        l{get_next_label_id(), defn_id, event.first, std::max(event.second, event.first), state};
    project.add_label(source, l);
    return l.m_id;
}
