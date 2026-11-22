/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <istream>
#include <iterator>
#include <sstream>
#include <map>
#include <set>
#include <stdexcept>

#include <spdlog/spdlog.h> // fmt

#include "../io/audio_source.h"
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

clip_manifest_settings read_settings(const json &j)
{
    clip_manifest_settings s;
    if(auto it = j.find("path"); it != j.end() && it->is_string())
        s.m_path = path_from_utf8(it->get<std::string>());
    if(auto it = j.find("clip-root"); it != j.end())
        s.m_clip_root = path_from_utf8(it->get<std::string>());
    if(auto it = j.find("columns"); it != j.end() && it->is_object())
    {
        s.m_source_column = it->value("source", s.m_source_column);
        s.m_onset_column = it->value("start", s.m_onset_column);
        s.m_stop_column = it->value("stop", s.m_stop_column);
        s.m_duration_column = it->value("duration", s.m_duration_column);
        s.m_label_column = it->value("label", s.m_label_column);
        s.m_root_column = it->value("root", s.m_root_column);
        s.m_channel_column = it->value("channel", s.m_channel_column);
        s.m_clip_column = it->value("clip", s.m_clip_column);
        s.m_clip_offset_column = it->value("clip_offset", s.m_clip_offset_column);
        s.m_id_column = it->value("id", s.m_id_column);
    }
    s.m_dataset_root = j.value("dataset-root", s.m_dataset_root);
    s.m_match_s = j.value("match_s", s.m_match_s);
    s.m_region_s = j.value("region_s", s.m_region_s);
    return s;
}

constexpr const char *result_columns[] =
    {"verdict", "new_label", "new_start_s", "new_stop_s", "note"};
constexpr size_t result_count = std::size(result_columns);

bool is_result_column(const std::string &name)
{
    return std::ranges::any_of(result_columns, [&](const char *c) { return lower(name) == c; });
}

std::optional<double> number(const std::string &field)
{
    char *end{};
    const double value = std::strtod(field.c_str(), &end);
    if(field.empty() || end == field.c_str() || !std::isfinite(value))
        return std::nullopt;
    return value;
}

/// Seconds as written to the results: enough digits for a sample at 192 kHz, no trailing zeros
std::string seconds(double t)
{
    auto text = fmt::format("{:.6f}", t);
    while(text.ends_with('0'))
        text.pop_back();
    if(text.ends_with('.'))
        text.pop_back();
    return text == "-0" ? "0" : text;
}

std::string seconds(const std::optional<double> &t) { return t ? seconds(*t) : std::string{}; }
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

list_columns::list_columns(const std::vector<std::string> &header, const clip_manifest_settings &s)
{
    const auto find = [&](const std::string &name) -> std::optional<size_t>
    {
        for(size_t i = 0U; i != header.size(); ++i)
            if(lower(header[i]) == lower(name))
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
    m_clip_offset = find(s.m_clip_offset_column);
    m_id = find(s.m_id_column);
}

const std::string &list_columns::field(const std::vector<std::string> &fields, std::optional<size_t> i)
{
    static const std::string none;
    return i && *i < fields.size() ? fields[*i] : none;
}

std::string list_columns::key(const std::vector<std::string> &fields) const
{
    if(m_id && !field(fields, m_id).empty())
        return "id\x1f" + field(fields, m_id);
    return field(fields, m_source) + "\x1f" + field(fields, m_onset) + "\x1f" +
        field(fields, m_label);
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
    const auto stop = m_stop_s.value_or(m_start_s + default_length_s);
    return {m_start_s, std::max(stop, m_start_s)};
}

time_span clip_manifest::row::reviewed_span(double default_length_s) const
{
    auto t = span(default_length_s);
    if(m_result.m_new_start_s)
        t.first = *m_result.m_new_start_s;
    if(m_result.m_new_stop_s)
        t.second = *m_result.m_new_stop_s;
    t.second = std::max(t.first, t.second);
    return t;
}

const std::string &clip_manifest::row::reviewed_label() const
{
    return m_result.m_new_label.empty() ? m_label : m_result.m_new_label;
}

bool clip_manifest::row::partly_outside_clip(double default_length_s) const
{
    if(!on_clip() || !m_clip_duration_s)
        return false;
    const auto t = reviewed_span(default_length_s);
    return t.first < m_clip_offset_s - 1e-6 ||
        t.second > m_clip_offset_s + *m_clip_duration_s + 1e-6;
}

bool clip_manifest::row::changed() const
{
    const auto &v = m_result.m_verdict;
    return v == "relabel" || v == "not_event" || v == "added" || m_result.m_new_start_s ||
        m_result.m_new_stop_s;
}

size_t clip_manifest::heard() const
{
    return static_cast<size_t>(std::ranges::count_if(
        m_rows,
        [](const row &r) { return r.present() && !r.m_result.m_verdict.empty(); }));
}

size_t clip_manifest::add_row(size_t near_row, time_span span, const std::string &label)
{
    const auto &near = m_rows.at(near_row);
    row r;
    r.m_fields.resize(m_columns.size());
    // The same recording, or clip, as the row it was found near; the rest is the reviewer's
    for(const auto column : {m_index.m_source, m_index.m_root, m_index.m_channel, m_index.m_clip,
                             m_index.m_clip_offset})
        if(column)
            r.m_fields[*column] = near.m_fields[*column];
    const auto set = [&](std::optional<size_t> column, const std::string &value)
    {
        if(column)
            r.m_fields[*column] = value;
    };
    set(m_index.m_onset, seconds(span.first));
    set(m_index.m_stop, seconds(span.second));
    set(m_index.m_duration, seconds(span.second - span.first));
    set(m_index.m_label, label);
    r.m_source = near.m_source;
    r.m_channel = near.m_channel;
    r.m_clip = near.m_clip;
    r.m_clip_offset_s = near.m_clip_offset_s;
    r.m_clip_duration_s = near.m_clip_duration_s;
    r.m_start_s = span.first;
    r.m_stop_s = span.second;
    r.m_label = label;
    r.m_key = m_index.key(r.m_fields);
    r.m_added = true;
    r.m_result = {"added", label, span.first, span.second, {}};
    m_rows.push_back(std::move(r));
    return m_rows.size() - 1U;
}

std::optional<label_defn_id> class_named(const label_dict &dict, const std::string &name)
{
    if(name.empty())
        return std::nullopt;
    for(const auto &defn : dict)
        if(defn.m_class == name)
            return defn.m_id;
    for(const auto &defn : dict)
        if(lower(defn.m_class) == lower(name))
            return defn.m_id;
    return std::nullopt;
}

fs::path results_path(const fs::path &list)
{
    auto name = list.stem();
    name += ".results.csv";
    return list.parent_path() / name;
}

fs::path changes_path(const fs::path &list)
{
    auto name = list.stem();
    name += ".changes.csv";
    return list.parent_path() / name;
}

fs::path results_sidecar_path(const fs::path &list)
{
    auto name = list.stem();
    name += ".results.json";
    return list.parent_path() / name;
}

namespace
{
/// The name a list's column has in its results, if it's written there. A list fed back as its
/// own results (it has a verdict column) loses its result columns, so they aren't there twice;
/// in any other list, a column named like one (eg. a pipeline's "note") is kept as "list_note".
std::optional<std::string> results_column_name(const std::vector<std::string> &columns, size_t c)
{
    const auto &name = columns[c];
    if(!is_result_column(name))
        return name;
    const bool fed_back =
        std::ranges::any_of(columns, [](const std::string &n) { return lower(n) == "verdict"; });
    if(fed_back)
        return std::nullopt;
    return "list_" + name;
}
}

std::string results_csv(const clip_manifest &manifest, bool changed_only)
{
    std::vector<size_t> kept;
    std::vector<std::string> header;
    for(size_t c = 0U; c != manifest.m_columns.size(); ++c)
        if(auto name = results_column_name(manifest.m_columns, c))
        {
            kept.push_back(c);
            header.push_back(std::move(*name));
        }

    std::string out;
    const auto line = [&](const std::vector<std::string> &fields, const std::vector<std::string> &result)
    {
        for(const auto c : kept)
            out += csv_escape(c < fields.size() ? fields[c] : std::string{}) + ",";
        for(size_t i = 0U; i != result.size(); ++i)
            out += csv_escape(result[i]) + (i + 1U == result.size() ? "\n" : ",");
    };
    for(const auto &name : header)
        out += csv_escape(name) + ",";
    for(size_t i = 0U; i != result_count; ++i)
        out += std::string{result_columns[i]} + (i + 1U == result_count ? "\n" : ",");
    for(const auto &r : manifest.m_rows)
    {
        if(!r.present() || (changed_only && !r.changed()))
            continue;
        const auto &v = r.m_result;
        line(
            r.m_fields,
            {v.m_verdict, v.m_new_label, seconds(v.m_new_start_s), seconds(v.m_new_stop_s), v.m_note});
    }
    return out;
}

void write_results(const clip_manifest &manifest, bool with_changes)
{
    write_file_atomically(manifest.m_results_path, results_csv(manifest));
    if(with_changes)
        write_file_atomically(changes_path(manifest.m_path), results_csv(manifest, true));
    const json sidecar{
        {"list", generic_utf8(manifest.m_path)},
        {"list_hash", manifest.m_list_hash},
        {"written", utc_now_iso8601()},
        {"app", "audio-labeller " AUDIOLAB_VERSION}};
    write_file_atomically(results_sidecar_path(manifest.m_path), sidecar.dump(2) + "\n");
}

namespace
{
/// The verdict columns of a results file's header
std::array<std::optional<size_t>, result_count> find_result_columns(
    const std::vector<std::string> &header)
{
    std::array<std::optional<size_t>, result_count> found{};
    for(size_t c = 0U; c != header.size(); ++c)
        for(size_t k = 0U; k != result_count; ++k)
            if(lower(header[c]) == result_columns[k])
                found[k] = c;
    return found;
}

row_verdict verdict_of(
    const std::vector<std::string> &fields,
    const std::array<std::optional<size_t>, result_count> &columns)
{
    const auto f = [&](size_t k) -> const std::string & { return list_columns::field(fields, columns[k]); };
    return {f(0), f(1), number(f(2)), number(f(3)), f(4)};
}

/// What a row's fields say, with its recording found in the project
class row_reader
{
public:
    row_reader(
        const project_model &project,
        const clip_manifest_settings &settings,
        const list_columns &columns,
        const fs::path &list) :
        m_project(project),
        m_settings(settings),
        m_columns(columns),
        m_clip_root(
            settings.m_clip_root.empty() ? list.parent_path()
                                         : project.config().resolve_root(settings.m_clip_root))
    {
        for(const auto &file : project.get_files())
            m_known.insert(file.m_path);
    }

    /// @param line For errors: the row's line in the file
    clip_manifest::row read(std::vector<std::string> fields, size_t line) const
    {
        const auto field = [&](std::optional<size_t> i) -> const std::string &
        { return list_columns::field(fields, i); };

        clip_manifest::row row;
        row.m_start_s = number(field(m_columns.m_onset)).value_or(0.);
        row.m_label = field(m_columns.m_label);
        row.m_stop_s = number(field(m_columns.m_stop));
        if(!row.m_stop_s)
            if(const auto duration = number(field(m_columns.m_duration)))
                row.m_stop_s = row.m_start_s + *duration;
        if(row.m_stop_s && *row.m_stop_s <= row.m_start_s)
            row.m_stop_s.reset();
        if(const auto channel = number(field(m_columns.m_channel)); channel && *channel >= 1.)
            row.m_channel = static_cast<uint32_t>(*channel) - 1U;
        if(const auto &clip = field(m_columns.m_clip); !clip.empty())
        {
            const auto offset = number(field(m_columns.m_clip_offset));
            if(!offset)
                throw std::runtime_error(fmt::format(
                    "Row {} of the review list has a clip ({}) but its \"{}\" is \"{}\", not a "
                    "number of seconds: where the clip starts in its recording, negative if the "
                    "clip starts with padding.",
                    line,
                    clip,
                    m_settings.m_clip_offset_column,
                    field(m_columns.m_clip_offset)));
            row.m_clip = (m_clip_root / path_from_utf8(clip)).lexically_normal();
            row.m_clip_offset_s = *offset;
        }
        row.m_key = m_columns.key(fields);
        row.m_source = find_recording(field(m_columns.m_source), field(m_columns.m_root));
        if(!row.m_source && row.m_clip)
        {
            std::error_code ec;
            if(fs::is_regular_file(*row.m_clip, ec))
                try
                {
                    row.m_clip_duration_s = audio_source(*row.m_clip).format().duration_s();
                }
                catch(const std::exception &)
                {
                }
        }
        row.m_fields = std::move(fields);
        return row;
    }

private:
    std::optional<fs::path> find_recording(const std::string &path, const std::string &row_root) const
    {
        const auto &cfg = m_project.config();
        const auto rel = path_from_utf8(path);
        if(path.empty())
            return std::nullopt;
        if(rel.is_absolute())
        {
            const auto p = rel.lexically_normal();
            const bool under_root = !m_project.to_portable(p).m_root.empty();
            return known(p) && under_root ? std::optional{p} : std::nullopt;
        }
        // The row's own root, else the list's, else each
        const auto &wanted = row_root.empty() ? m_settings.m_dataset_root : row_root;
        for(const auto &root : cfg.m_wav_paths)
        {
            if(!wanted.empty() &&
               generic_utf8(root) != wanted &&
               cfg.resolve_root(root) != cfg.resolve_root(path_from_utf8(wanted)))
                continue;
            if(auto candidate = (cfg.resolve_root(root) / rel).lexically_normal(); known(candidate))
                return candidate;
        }
        return std::nullopt;
    }

    /// In the project's file list, or, for a project that doesn't scan its roots, on disk
    bool known(const fs::path &p) const
    {
        if(m_known.contains(p))
            return true;
        std::error_code ec;
        return !m_project.scans_roots() && fs::is_regular_file(p, ec);
    }

    const project_model &m_project;
    const clip_manifest_settings &m_settings;
    const list_columns &m_columns;
    fs::path m_clip_root;
    std::set<fs::path> m_known;
};

/// The list's header and rows, checked for the columns every list needs
/// @param text Set to the file's contents
std::vector<std::vector<std::string>> read_list_records(
    const fs::path &path,
    const clip_manifest_settings &settings,
    list_columns &columns,
    std::string &text)
{
    std::ifstream file(path, std::ios::binary);
    if(!file)
        throw std::runtime_error(
            "Could not open the review list " + generic_utf8(path) +
            ": it doesn't exist or can't be read.");
    text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    std::istringstream in(text);
    auto records = read_csv(in);
    if(records.empty())
        throw std::runtime_error(
            "The review list " + generic_utf8(path) +
            " is empty: it needs a header line naming its columns, then a line per event.");
    columns = list_columns(records.front(), settings);
    const clip_manifest_settings defaults;
    const auto require = [&](std::optional<size_t> i,
                             const std::string &name,
                             const std::string &default_name,
                             const char *key,
                             const char *why)
    {
        if(i)
            return;
        std::string header;
        for(const auto &column : records.front())
            header += (header.empty() ? "" : ", ") + column;
        auto message = fmt::format(
            "The review list {} has no \"{}\" column, which {}.",
            generic_utf8(path),
            name,
            why);
        // A column the project renames may be missing from the list, or the renaming wrong
        if(name != default_name)
            message += fmt::format(
                " The project's \"clip-manifest\" section renames the \"{}\" column to \"{}\" "
                "(\"columns\": {{\"{}\": \"{}\"}}), so either the list or that setting needs "
                "changing.",
                default_name,
                name,
                key,
                name);
        message += " The list's columns are: " + header + ".";
        throw std::runtime_error(message);
    };
    require(
        columns.m_source,
        settings.m_source_column,
        defaults.m_source_column,
        "source",
        "every list needs: each event's recording");
    require(
        columns.m_onset,
        settings.m_onset_column,
        defaults.m_onset_column,
        "start",
        "every list needs: where each event starts in its recording, in seconds");
    if(columns.m_clip)
        require(
            columns.m_clip_offset,
            settings.m_clip_offset_column,
            defaults.m_clip_offset_column,
            "clip_offset",
            "a list with clips needs: where each clip starts in its recording, in seconds");
    return records;
}
}

void read_results(
    const project_model &project,
    clip_manifest &manifest,
    const clip_manifest_settings &settings)
{
    std::ifstream in(manifest.m_results_path, std::ios::binary);
    if(!in)
        return;
    const auto records = read_csv(in);
    if(records.empty())
        return;
    const auto &header = records.front();
    const list_columns columns(header, settings);
    const auto found = find_result_columns(header);
    if(!found[0])
        throw std::runtime_error(
            "The results file " + generic_utf8(manifest.m_results_path) +
            " beside the review list has no \"verdict\" column, so it isn't results the app "
            "wrote. Move it aside to start the review afresh.");

    // Each of the list's columns in the results file
    std::vector<std::optional<size_t>> from(manifest.m_columns.size());
    for(size_t c = 0U; c != manifest.m_columns.size(); ++c)
    {
        const auto name = results_column_name(manifest.m_columns, c);
        for(size_t h = 0U; h != header.size(); ++h)
            if(lower(header[h]) == lower(name.value_or(manifest.m_columns[c])))
                from[c] = h;
    }

    const row_reader reader(project, settings, manifest.m_index, manifest.m_path);
    std::map<std::string, row_verdict> verdicts;
    for(size_t r = 1U; r < records.size(); ++r)
    {
        const auto &fields = records[r];
        auto v = verdict_of(fields, found);
        if(v.m_verdict == "added")
        {
            std::vector<std::string> list_fields(manifest.m_columns.size());
            for(size_t c = 0U; c != from.size(); ++c)
                list_fields[c] = list_columns::field(fields, from[c]);
            auto row = reader.read(std::move(list_fields), r + 1U);
            row.m_added = true;
            row.m_result = std::move(v);
            manifest.m_unresolved += !row.m_source;
            manifest.m_rows.push_back(std::move(row));
        }
        else if(v != row_verdict{})
            verdicts[columns.key(fields)] = std::move(v);
    }
    for(auto &row : manifest.m_rows)
        if(auto it = verdicts.find(row.m_key); it != verdicts.end() && !row.m_added)
            row.m_result = it->second;
}

clip_manifest load_clip_manifest(
    const project_model &project,
    const clip_manifest_settings &settings,
    bool with_results)
{
    const auto path = project.config().resolve_proj(settings.m_path);
    clip_manifest manifest;
    manifest.m_path = path;
    manifest.m_results_path = results_path(path);
    std::string text;
    auto records = read_list_records(path, settings, manifest.m_index, text);
    manifest.m_list_hash = content_hash(text);
    manifest.m_columns = records.front();

    const row_reader reader(project, settings, manifest.m_index, path);
    for(size_t r = 1U; r < records.size(); ++r)
    {
        auto &fields = records[r];
        fields.resize(manifest.m_columns.size());
        auto row = reader.read(std::move(fields), r + 1U);
        manifest.m_unresolved += !row.m_source;
        manifest.m_rows.push_back(std::move(row));
    }
    if(with_results)
        read_results(project, manifest, settings);
    return manifest;
}

clip_manifest load_results_file(
    const project_model &project,
    const clip_manifest_settings &settings,
    const fs::path &results)
{
    auto s = settings;
    s.m_path = results;
    auto manifest = load_clip_manifest(project, s, false);
    const auto found = find_result_columns(manifest.m_columns);
    if(!found[0])
        throw std::runtime_error(
            "The file " + generic_utf8(results) + " has no \"verdict\" column, so it isn't a "
            "review's results. Results are written beside a review list as <list>.results.csv.");
    for(auto &row : manifest.m_rows)
    {
        row.m_result = verdict_of(row.m_fields, found);
        row.m_added = row.m_result.m_verdict == "added";
    }
    return manifest;
}

const label *find_label_at(
    const labels &file_labels,
    double t_s,
    double window_s,
    std::optional<label_defn_id> preferred)
{
    const label *best = nullptr;
    auto score = [&](const label &l)
    {
        // Same class first, then the nearest start
        return std::pair{preferred && l.m_defn_id != *preferred, std::abs(l.m_start_s - t_s)};
    };
    for(const auto &l : file_labels)
    {
        if(t_s < l.m_start_s - window_s || t_s > std::max(l.m_stop_s, l.m_start_s) + window_s)
            continue;
        if(!best || score(l) < score(*best))
            best = &l;
    }
    return best;
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
