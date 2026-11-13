/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "../io/audio_source.h"
#include "../io/files.h"
#include "../io/source_id.h"
#include "colour.h"
#include "label.h"
#include "paths.h"
#include "project_model.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
std::optional<file_status> status_from_string(const std::string &s)
{
    for(auto status : {file_status::todo, file_status::done, file_status::skip})
        if(s == to_string(status))
            return status;
    return std::nullopt;
}

std::optional<label_state> state_from_string(const std::string &s)
{
    for(auto state : {label_state::confirmed, label_state::suggested, label_state::rejected})
        if(s == to_string(state))
            return state;
    return std::nullopt;
}

std::int64_t mtime_of(const fs::path &p)
{
    const auto t = fs::last_write_time(p).time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(t).count();
}

/// Times are under different keys in version 1 label files
double get_time(const json &record, const char *key, const char *v1_key)
{
    if(auto it = record.find(key); it != record.end())
        return it->get<double>();
    return record.at(v1_key).get<double>();
}

/// Upgrade the keys of a version 1 label record that's being kept verbatim
json upgrade_label_record(json record)
{
    for(auto [v1_key, key] : {std::pair{"start [s]", "start_s"}, std::pair{"stop [s]", "stop_s"}})
    {
        if(auto it = record.find(v1_key); it != record.end())
        {
            record[key] = *it;
            record.erase(v1_key);
        }
    }
    return record;
}

/// The array under a key, or an empty one if absent
const json &array_at(const json &j, const char *key)
{
    static const json empty = json::array();
    auto it = j.find(key);
    if(it == j.end() || it->is_null())
        return empty;
    if(!it->is_array())
        throw std::runtime_error(std::string{"Expected an array for "} + key);
    return *it;
}
}

const char *to_string(label_state state)
{
    switch(state)
    {
    case label_state::suggested: return "suggested";
    case label_state::rejected: return "rejected";
    case label_state::confirmed:
    default: return "confirmed";
    }
}

const char *to_string(file_status status)
{
    switch(status)
    {
    case file_status::done: return "done";
    case file_status::skip: return "skip";
    case file_status::todo:
    default: return "todo";
    }
}

std::string to_hex(uint32_t u)
{
    return fmt::format(
        "#{:02x}{:02x}{:02x}{:02x}",
        colour_channel(u, 0),
        colour_channel(u, 1),
        colour_channel(u, 2),
        colour_channel(u, 3));
}

uint32_t from_hex(std::string col)
{
    unsigned r{}, g{}, b{}, a{};
    std::sscanf(col.c_str(), "#%2x%2x%2x%2x", &r, &g, &b, &a);
    return pack_rgba(r, g, b, a);
}

void to_json(json &j, const label_defn &d)
{
    j = json{{"class-id", d.m_id.m_id}, {"class", d.m_class}, {"colour", to_hex(d.m_color_rgba)}};
    if(d.m_kind == class_kind::onset)
        j["kind"] = "onset";
}

void from_json(const json &j, label_defn &d)
{
    j.at("class-id").get_to(d.m_id.m_id);
    j.at("class").get_to(d.m_class);
    d.m_color_rgba = from_hex(j.at("colour").get<std::string>());
    const auto kind = j.value("kind", std::string{"region"});
    if(kind == "onset")
        d.m_kind = class_kind::onset;
    else if(kind != "region")
        throw std::runtime_error("Unknown class kind " + kind + ", expected region or onset");
}

void to_json(json &j, const label_dict &p)
{
    for(auto &def : p)
        j.push_back(def);
}

void from_json(const json &j, label_dict &p)
{
    for(auto &it : j.items())
        p.add(it.value().get<label_defn>());
}

project_config project_config::parse_from_file(std::string filename)
{
    project_config cfg{};

    if(filename.empty())
        return cfg;

    // Absolute so that resolved paths don't depend on the working directory:
    cfg.m_filename = fs::absolute(filename).lexically_normal();

    std::ifstream f(filename);
    if(!f)
        throw std::runtime_error("Could not open " + filename);
    auto j = json::parse(f);
    j.at("dataset-roots").get_to(cfg.m_wav_paths);
    j.at("label-palette").get_to(cfg.m_palette);
    j.at("export-prefix").get_to(cfg.m_export_prefix);
    j.at("label-file").get_to(cfg.m_labels);
    if(auto it = j.find("export"); it != j.end())
        cfg.m_export = *it;
    if(auto it = j.find("annotations"); it != j.end())
        cfg.m_annotations = *it;
    if(auto it = j.find("review"); it != j.end())
        cfg.m_review = *it;
    if(auto it = j.find("channel"); it != j.end() && !it->is_null())
    {
        // Channels are numbered from 1 in the project file, as in most audio software
        const auto channel = it->get<uint32_t>();
        if(channel == 0U)
            throw std::runtime_error("Channels are numbered from 1");
        cfg.m_channel = channel - 1U;
    }
    return cfg;
}

fs::path project_config::resolve_proj(const fs::path &p) const
{
    auto out = expand_env_vars(p);
    if(out.is_relative())
        out = m_filename.parent_path() / out;

    return out.lexically_normal();
}

fs::path project_config::resolve_root(const fs::path &root) const
{
    auto out = resolve_proj(root);
    return out.has_filename() ? out : out.parent_path(); // Drop any trailing separator
}

project_model::project_model(const project_config &cfg) :
    m_cfg(cfg)
{
    if(!has_project())
    {
        spdlog::warn("No project configured");
        return;
    }

    rescan_files();

    auto palette_path = m_cfg.resolve_proj(m_cfg.m_palette);
    spdlog::info("Loading label palette from {}", generic_utf8(palette_path));

    std::ifstream f(palette_path);
    if(!f)
        throw std::runtime_error("Could not load " + generic_utf8(palette_path));

    auto j = json::parse(f);
    j.at("label-palette").get_to(m_label_dict);

    load_labels();
}

project_model::~project_model()
{
    try
    {
        save_if_dirty();
    }
    catch(const std::exception &e)
    {
        spdlog::critical("Could not save labels on exit: {}", e.what());
    }
}

void project_model::rescan_files()
{
    if(!has_project())
        return;

    spdlog::info("Scanning dataset...");
    m_files.clear();

    for(const auto &wav_path : m_cfg.m_wav_paths)
    {
        const auto root = m_cfg.resolve_root(wav_path);

        // A missing root (eg. an unplugged drive) must not stop the rest of the project loading:
        std::error_code ec;
        if(!fs::is_directory(root, ec))
        {
            spdlog::error("Dataset root {} not found, skipping", generic_utf8(root));
            continue;
        }

        spdlog::info("Scanning {}", generic_utf8(root));
        for(const auto &entry :
            fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied))
        {
            if(entry.is_regular_file() && is_supported_audio(entry.path()))
            {
                auto path = entry.path().lexically_normal();
                auto name = to_portable(path).m_relative;
                m_files.push_back(audio_file_entry{std::move(path), std::move(name)});
            }
        }
    }
}

project_model::portable_path project_model::to_portable(const fs::path &file) const
{
    for(const auto &wav_path : m_cfg.m_wav_paths)
    {
        const auto rel = file.lexically_relative(m_cfg.resolve_root(wav_path));
        if(!rel.empty() && *rel.begin() != ".." && rel != ".")
            return portable_path{generic_utf8(wav_path), generic_utf8(rel)};
    }
    return portable_path{{}, generic_utf8(file)};
}

std::string project_model::class_name_of(const label &l) const
{
    auto defn = m_label_dict.find_ptr(l.m_defn_id);
    return defn ? defn->m_class : std::string{};
}

const labels &project_model::get_labels(const fs::path &file) const
{
    static const labels none;
    auto it = m_file_labels.find(file);
    return it != m_file_labels.end() ? it->second : none;
}

void project_model::add_label(const fs::path &file, const label &l)
{
    if(!m_file_labels[file].add(l))
        throw std::logic_error("Label ID already in use");
    ++m_revision;
}

bool project_model::erase_label(const fs::path &file, label_id id)
{
    auto it = m_file_labels.find(file);
    if(it == m_file_labels.end() || !it->second.erase(id))
        return false;
    ++m_revision;
    return true;
}

bool project_model::update_label(const fs::path &file, const label &l)
{
    auto it = m_file_labels.find(file);
    if(it == m_file_labels.end())
        return false;
    auto *existing = it->second.find_ptr(l.m_id);
    if(!existing)
        return false;
    if(!(*existing == l))
    {
        *existing = l;
        ++m_revision;
    }
    return true;
}

void project_model::set_labels(const fs::path &file, labels l)
{
    if(get_labels(file) == l)
        return;
    m_file_labels[file] = std::move(l);
    ++m_revision;
}

file_status project_model::get_status(const fs::path &file) const
{
    const auto *state = find_file_state(file);
    return state ? state->m_status : file_status::todo;
}

void project_model::set_status(const fs::path &file, file_status status)
{
    auto &state = m_file_states[file];
    if(state.m_status == status)
        return;
    state.m_status = status;
    ++m_revision;
}

std::optional<uint32_t> project_model::get_channel(const fs::path &file) const
{
    const auto *state = find_file_state(file);
    const auto channel = state && state->m_channel ? state->m_channel : m_cfg.m_channel;
    if(!channel || *channel == mixed_channels)
        return std::nullopt;
    return channel;
}

void project_model::set_channel(const fs::path &file, std::optional<uint32_t> channel)
{
    // Stored only where it differs from the project's default
    std::optional<uint32_t> stored = channel.value_or(mixed_channels);
    if(stored.value() == m_cfg.m_channel.value_or(mixed_channels))
        stored.reset();
    auto &state = m_file_states[file];
    if(state.m_channel == stored)
        return;
    state.m_channel = stored;
    ++m_revision;
}

label_defn_id project_model::add_session_class(const std::string &name)
{
    uint32_t next{};
    for(const auto &defn : m_label_dict)
        next = std::max(next, defn.m_id.m_id + 1U);
    m_label_dict.add(label_defn{label_defn_id{next}, name, unknown_class_colour});
    spdlog::warn("Class {} is not in the palette, adding it as {} for this session", name, next);
    return label_defn_id{next};
}

const file_state *project_model::find_file_state(const fs::path &file) const
{
    auto it = m_file_states.find(file);
    return it != m_file_states.end() ? &it->second : nullptr;
}

std::string project_model::get_source_id(const fs::path &file)
{
    const auto size = fs::file_size(file);
    const auto mtime = mtime_of(file);

    auto &state = m_file_states[file];
    if(state.m_source_id &&
       state.m_source_id->m_size == size &&
       state.m_source_id->m_mtime == mtime)
        return state.m_source_id->m_id;

    state.m_source_id = source_id_cache{compute_source_id(file), size, mtime};
    ++m_revision;
    return state.m_source_id->m_id;
}

void project_model::record_export(const fs::path &file, export_record record)
{
    m_file_states[file].m_export = std::move(record);
    ++m_revision;
}

void project_model::export_csv()
{
    if(!has_project())
        throw std::runtime_error("No project loaded, nothing to export");

    auto export_path = m_cfg.resolve_proj(m_cfg.m_export_prefix);
    export_path += ".csv";

    std::ofstream f(export_path);
    if(!f)
        throw std::runtime_error("Failed to open CSV for export: " + generic_utf8(export_path));

    f << "filename,start_s,stop_s,class_label,class_id,dataset_root\n";
    f << std::fixed << std::setprecision(6);

    for(const auto &[src_path, labels] : m_file_labels)
    {
        const auto portable = to_portable(src_path);
        for(const auto &l : labels)
        {
            if(!l.is_confirmed())
                continue;
            f << csv_escape(portable.m_relative) << "," << l.m_start_s << "," << l.m_stop_s << ","
              << csv_escape(class_name_of(l)) << "," << l.m_defn_id.m_id << ","
              << csv_escape(portable.m_root) << "\n";
        }
    }

    if(!m_unresolved_labels.empty())
        spdlog::warn(
            "{} labels were not exported because their audio files were not found",
            m_unresolved_labels.size());
}

std::optional<fs::path> project_model::resolve_stored_path(
    const std::string &filename,
    const std::optional<std::string> &root,
    const std::set<fs::path> &known_files) const
{
    if(root)
    {
        const auto rel = path_from_utf8(filename);
        for(const auto &wav_path : m_cfg.m_wav_paths)
            if(generic_utf8(wav_path) == *root)
                if(auto normal = (m_cfg.resolve_root(wav_path) / rel).lexically_normal();
                   known_files.contains(normal))
                    return normal;
        return std::nullopt;
    }

    // Older label files have no dataset root: Paths were relative to the working directory of
    // whichever run saved them, usually with Windows separators.
    auto stored_text = filename;
    std::ranges::replace(stored_text, '\\', '/');
    const auto stored = path_from_utf8(stored_text);

    for(const auto &candidate : {fs::absolute(stored), m_cfg.m_filename.parent_path() / stored})
        if(auto normal = candidate.lexically_normal(); known_files.contains(normal))
            return normal;

    // That working directory may be long gone, so accept a single file with a matching tail:
    fs::path tail;
    for(const auto &part : stored.relative_path())
        if(part != ".." && part != ".")
            tail /= part;
    const auto suffix = "/" + generic_utf8(tail);

    std::optional<fs::path> match;
    for(const auto &file : m_files)
    {
        if(generic_utf8(file.m_path).ends_with(suffix))
        {
            if(match)
                return std::nullopt; // Ambiguous, so don't guess
            match = file.m_path;
        }
    }
    return match;
}

void project_model::load_labels()
{
    auto label_file = m_cfg.resolve_proj(m_cfg.m_labels);
    if(!fs::exists(label_file))
    {
        spdlog::warn("Label file {} does not exist, it will be created", generic_utf8(label_file));
        ++m_revision;
        return;
    }
    spdlog::info("Loading labels from {}", generic_utf8(label_file));

    std::ifstream f(label_file);
    if(!f)
        throw std::runtime_error("Could not load labels from " + generic_utf8(label_file));

    const std::string text{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    if(text.empty())
    {
        spdlog::warn("Label file {} is empty", generic_utf8(label_file));
        ++m_revision;
        return;
    }

    auto j = json::parse(text);
    if(!j.is_object())
        throw std::runtime_error("Label file is not a JSON object: " + generic_utf8(label_file));

    if(const int version = j.value("version", 1); version > label_file_version)
        throw std::runtime_error(
            fmt::format(
                "Label file {} is version {}, which needs a newer version of this app",
                generic_utf8(label_file),
                version));

    const auto &label_records = array_at(j, "labels");
    const auto &file_records = array_at(j, "files");

    // If exactly one saved root has gone from the project and exactly one project root is unused
    // by the file, then the dataset was moved. Anything less clear-cut risks attaching labels to a
    // same-named file in the wrong root, so those stay unresolved.
    std::set<std::string> stale_roots;
    for(const auto *records : {&label_records, &file_records})
        for(const auto &record : *records)
            if(auto it = record.find("dataset-root"); it != record.end())
                stale_roots.insert(it->get<std::string>());
    std::set<std::string> unused_roots;
    for(const auto &wav_path : m_cfg.m_wav_paths)
        if(!stale_roots.erase(generic_utf8(wav_path)))
            unused_roots.insert(generic_utf8(wav_path));
    const bool dataset_moved = stale_roots.size() == 1 && unused_roots.size() == 1;

    std::set<fs::path> known_files;
    for(const auto &file : m_files)
        known_files.insert(file.m_path);

    std::map<std::string, std::optional<fs::path>> resolved_paths;
    auto resolve = [&](const json &record) -> std::optional<fs::path>
    {
        const auto filename = record.at("filename").get<std::string>();
        std::optional<std::string> root;
        if(auto it = record.find("dataset-root"); it != record.end())
            root = it->get<std::string>();
        if(dataset_moved && root == *stale_roots.begin())
            root = *unused_roots.begin();

        const auto cache_key = root.value_or(std::string{}) + '\n' + filename;
        auto cached = resolved_paths.find(cache_key);
        if(cached == resolved_paths.end())
            cached =
                resolved_paths.emplace(cache_key, resolve_stored_path(filename, root, known_files))
                    .first;
        return cached->second;
    };

    std::set<label_id> used_ids;
    std::vector<std::pair<fs::path, label>> duplicate_ids;
    label_id max_id{};

    for(const auto &record : label_records)
    {
        const auto id = record.at("label-id").get<label_id>();
        const auto start_s = get_time(record, "start_s", "start [s]");
        const auto stop_s = get_time(record, "stop_s", "stop [s]");
        const auto class_id = record.at("class-id").get<uint32_t>();
        const auto class_name = record.at("class-name").get<std::string>();

        max_id = std::max(max_id, id);

        // Audio may only be missing for now, so the label is kept and written back:
        const auto path = resolve(record);
        if(!path)
        {
            used_ids.insert(id);
            m_unresolved_labels.push_back(upgrade_label_record(record));
            continue;
        }

        // Keep labels of a class that was removed from the palette, rather than drop them:
        const auto defn_lookup = m_label_dict.find_ptr(label_defn_id(class_id));
        if(!defn_lookup)
        {
            spdlog::error(
                "Class {} ({}) is not in the palette, adding it for this session",
                class_id,
                class_name);
            m_label_dict.add(label_defn{class_id, class_name, unknown_class_colour});
        }
        else if(defn_lookup->m_class != class_name)
            spdlog::error(
                "Label class name {} does not match definition class name {}",
                class_name,
                defn_lookup->m_class);

        label l{id, class_id, std::max(0., start_s), std::max(0., stop_s)};
        if(auto it = record.find("state"); it != record.end())
        {
            if(auto state = state_from_string(it->get<std::string>()))
                l.m_state = *state;
            else
                spdlog::error("Unknown label state {}", it->dump());
        }
        if(auto it = record.find("score"); it != record.end() && it->is_number())
            l.m_score = it->get<float>();

        if(used_ids.insert(id).second)
            m_file_labels[*path].add(l);
        else
            duplicate_ids.emplace_back(*path, l);
    }

    // New IDs must not collide with those just loaded:
    reserve_label_ids(max_id);

    for(auto &[path, l] : duplicate_ids)
    {
        l.m_id = get_next_label_id();
        m_file_labels[path].add(l);
    }

    for(const auto &record : file_records)
    {
        const auto path = resolve(record);
        if(!path)
        {
            m_unresolved_files.push_back(record);
            continue;
        }

        file_state state;
        const auto status_text = record.value("status", std::string{to_string(file_status::todo)});
        if(auto status = status_from_string(status_text))
            state.m_status = *status;
        else
            spdlog::error("Unknown file status {}", status_text);

        if(auto it = record.find("source-id"); it != record.end())
            state.m_source_id = source_id_cache{
                it->get<std::string>(),
                record.value("size", std::uintmax_t{}),
                record.value("mtime", std::int64_t{})
            };

        if(auto it = record.find("channel"); it != record.end())
        {
            const auto text = it->get<std::string>();
            state.m_channel =
                text == "mix" ? mixed_channels : static_cast<uint32_t>(std::stoul(text) - 1U);
        }

        if(auto it = record.find("export"); it != record.end())
            state.m_export = export_record{
                it->value("time", std::string{}),
                it->value("directory", std::string{}),
                it->value("clips", uint64_t{}),
                it->value("verified", false)
            };

        if(!state.is_default())
            m_file_states[*path] = std::move(state);
    }

    if(!m_unresolved_labels.empty())
        spdlog::warn(
            "{} labels refer to audio files not found in the dataset. They will be preserved.",
            m_unresolved_labels.size());

    // Any migration (older version, moved dataset, repaired IDs) is written at the next save:
    if(serialize_labels() != text)
        ++m_revision;
}

std::string project_model::serialize_labels() const
{
    json j{{"$schema", "../schemas/labels.schema.json"}, {"version", label_file_version}};

    auto &label_records = j["labels"];
    label_records = json::array();
    for(const auto &[src_path, labels] : m_file_labels)
    {
        const auto portable = to_portable(src_path);
        for(const auto &l : labels)
        {
            json r{
                {"filename", portable.m_relative},
                {"label-id", l.m_id},
                {"start_s", l.m_start_s},
                {"stop_s", l.m_stop_s},
                {"class-id", l.m_defn_id.m_id},
                {"class-name", class_name_of(l)}
            };
            if(!portable.m_root.empty())
                r["dataset-root"] = portable.m_root;
            if(l.m_state != label_state::confirmed)
                r["state"] = to_string(l.m_state);
            if(l.m_score)
                r["score"] = *l.m_score;
            label_records.push_back(std::move(r));
        }
    }
    for(const auto &r : m_unresolved_labels)
        label_records.push_back(r);

    auto &file_records = j["files"];
    file_records = json::array();
    for(const auto &[src_path, state] : m_file_states)
    {
        if(state.is_default())
            continue;

        const auto portable = to_portable(src_path);
        json r{{"filename", portable.m_relative}, {"status", to_string(state.m_status)}};
        if(!portable.m_root.empty())
            r["dataset-root"] = portable.m_root;
        if(state.m_source_id)
        {
            r["source-id"] = state.m_source_id->m_id;
            r["size"] = state.m_source_id->m_size;
            r["mtime"] = state.m_source_id->m_mtime;
        }
        if(state.m_channel)
            r["channel"] = *state.m_channel == mixed_channels
                ? std::string{"mix"}
                : std::to_string(*state.m_channel + 1U);
        if(state.m_export)
            r["export"] = json{
                {"time", state.m_export->m_time},
                {"directory", state.m_export->m_directory},
                {"clips", state.m_export->m_clips},
                {"verified", state.m_export->m_verified}
            };
        file_records.push_back(std::move(r));
    }
    for(const auto &r : m_unresolved_files)
        file_records.push_back(r);

    return j.dump(2);
}

void project_model::save_if_dirty()
{
    if(!has_project() || !is_dirty())
        return;

    const auto revision = m_revision;
    const auto label_file = m_cfg.resolve_proj(m_cfg.m_labels);
    spdlog::info("Saving labels to {}", generic_utf8(label_file));
    write_file_atomically(label_file, serialize_labels());
    m_saved_revision = revision;
}
