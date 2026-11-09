/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iterator>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <stdexcept>

#include <AudioFile/AudioFile.h>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <string>

#include "label.h"
#include "project_model.h"

namespace fs = std::filesystem;

namespace
{
std::string to_utf8(const std::u8string &s) { return std::string(s.begin(), s.end()); }

std::string generic_utf8(const fs::path &p) { return to_utf8(p.generic_u8string()); }

fs::path path_from_utf8(const std::string &s) { return fs::path(std::u8string(s.begin(), s.end())); }

/// Replace the target in one step, so that a failure part way through a write can never leave a
/// truncated file in place of the previous good one.
void write_file_atomically(const fs::path &target, const std::string &contents)
{
    auto tmp = target;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if(!f)
            throw std::runtime_error("Could not create " + generic_utf8(tmp));
        f << contents;
        f.flush();
        if(!f)
            throw std::runtime_error("Could not write " + generic_utf8(tmp));
    }
    fs::rename(tmp, target);
}
}

std::string to_hex(uint32_t u)
{
    uint8_t r = (u >> IM_COL32_R_SHIFT) & 0xFF;
    uint8_t g = (u >> IM_COL32_G_SHIFT) & 0xFF;
    uint8_t b = (u >> IM_COL32_B_SHIFT) & 0xFF;
    uint8_t a = (u >> IM_COL32_A_SHIFT) & 0xFF;
    return fmt::format("#{:02x}{:02x}{:02x}{:02x}", r, g, b, a);
}

uint32_t from_hex(std::string col)
{
    unsigned r{}, g{}, b{}, a{};
    std::sscanf(col.c_str(), "#%2x%2x%2x%2x", &r, &g, &b, &a);
    return IM_COL32(r, g, b, a);
}

void to_json(nlohmann::json &j, const label_defn &d)
{
    j = nlohmann::json{
        {"class-id", d.m_id.m_id},
        {"class", d.m_class},
        {"colour", to_hex(d.m_color_rgba)}
    };
}

void from_json(const nlohmann::json &j, label_defn &d)
{
    j.at("class-id").get_to(d.m_id.m_id);
    j.at("class").get_to(d.m_class);
    d.m_color_rgba = from_hex(j.at("colour").get<std::string>());
}

void to_json(nlohmann::json &j, const label_dict &p)
{
    for(auto &def : p)
        j.push_back(def);
}

void from_json(const nlohmann::json &j, label_dict &p)
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
    auto j = nlohmann::json::parse(f);
    j.at("dataset-roots").get_to(cfg.m_wav_paths);
    j.at("label-palette").get_to(cfg.m_palette);
    j.at("export-prefix").get_to(cfg.m_export_prefix);
    j.at("label-file").get_to(cfg.m_labels);
    return cfg;
}

fs::path project_config::resolve_proj(const fs::path &p) const
{
    auto out = p;
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

    auto j = nlohmann::json::parse(f);
    j.at("label-palette").get_to(m_label_dict);

    load_labels();
}

project_model::~project_model()
{
    try
    {
        save_if_changed();
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
            if(entry.path().extension() == ".wav")
            {
                auto path = entry.path().lexically_normal();
                auto name = to_portable(path).m_relative;
                m_files.push_back(audio_file_entry{std::move(path), std::move(name)});
            }
        }
    }
}

static std::string csv_escape(const std::string &s)
{
    if(s.find_first_of(", \"\r\n") == std::string::npos)
        return s;

    std::string out = "\"";
    for(char c : s)
    {
        if(c == '"')
            out += "\"\"";
        else
            out += c;
    }
    out += "\"";
    return out;
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
        spdlog::warn("Label file {} does not exist, skipping label load", generic_utf8(label_file));
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
        return;
    }

    auto j = nlohmann::json::parse(text);
    if(!j.is_object())
        throw std::runtime_error("Label file is not a JSON object: " + generic_utf8(label_file));

    m_last_saved = text;

    const auto records = j.find("labels");
    if(records == j.end())
        return;

    std::set<fs::path> known_files;
    for(const auto &file : m_files)
        known_files.insert(file.m_path);

    // If exactly one saved root has gone from the project and exactly one project root is unused
    // by the labels then the dataset was moved. Anything less clear-cut risks attaching labels to
    // a same-named file in the wrong root, so those stay unresolved.
    std::set<std::string> stale_roots;
    for(const auto &jl : *records)
        if(auto it = jl.find("dataset-root"); it != jl.end())
            stale_roots.insert(it->get<std::string>());
    std::set<std::string> unused_roots;
    for(const auto &wav_path : m_cfg.m_wav_paths)
        if(!stale_roots.erase(generic_utf8(wav_path)))
            unused_roots.insert(generic_utf8(wav_path));
    const bool dataset_moved = stale_roots.size() == 1 && unused_roots.size() == 1;

    std::map<std::string, std::optional<fs::path>> resolved_paths;
    std::set<label_id> used_ids;
    std::vector<std::pair<fs::path, label>> duplicate_ids;
    label_id max_id{};

    for(const auto &jl : *records)
    {
        const auto filename = jl.at("filename").get<std::string>();
        const auto id = jl.at("label-id").get<label_id>();
        const auto start_s = jl.at("start [s]").get<double>();
        const auto stop_s = jl.at("stop [s]").get<double>();
        const auto class_id = jl.at("class-id").get<uint32_t>();
        const auto class_name = jl.at("class-name").get<std::string>();

        max_id = std::max(max_id, id);

        std::optional<std::string> root;
        if(auto it = jl.find("dataset-root"); it != jl.end())
            root = it->get<std::string>();
        if(dataset_moved && root == *stale_roots.begin())
            root = *unused_roots.begin();

        const auto cache_key = root.value_or(std::string{}) + '\n' + filename;
        auto cached = resolved_paths.find(cache_key);
        if(cached == resolved_paths.end())
            cached =
                resolved_paths
                    .emplace(cache_key, resolve_stored_path(filename, root, known_files))
                    .first;

        // Audio may only be missing for now, so the label is kept and written back untouched:
        if(!cached->second)
        {
            used_ids.insert(id);
            m_unresolved_labels.push_back(jl);
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
            m_label_dict.add(label_defn{class_id, class_name, IM_COL32(127, 127, 127, 127)});
        }
        else if(defn_lookup->m_class != class_name)
            spdlog::error(
                "Label class name {} does not match definition class name {}",
                class_name,
                defn_lookup->m_class);

        label l{id, class_id, std::max(0., start_s), std::max(0., stop_s)};

        if(used_ids.insert(id).second)
            m_file_labels[*cached->second].add(l);
        else
            duplicate_ids.emplace_back(*cached->second, l);
    }

    // New IDs must not collide with those just loaded:
    reserve_label_ids(max_id);

    for(auto &[path, l] : duplicate_ids)
    {
        l.m_id = get_next_label_id();
        m_file_labels[path].add(l);
    }

    if(!m_unresolved_labels.empty())
        spdlog::warn(
            "{} labels refer to audio files not found in the dataset. They will be preserved.",
            m_unresolved_labels.size());
}

std::string project_model::serialize_labels() const
{
    nlohmann::json j{{"$schema", "../schemas/labels.schema.json"}};
    auto &records = j["labels"];
    records = nlohmann::json::array();

    for(const auto &[src_path, labels] : m_file_labels)
    {
        const auto portable = to_portable(src_path);
        for(const auto &l : labels)
        {
            nlohmann::json r{
                {"filename", portable.m_relative},
                {"label-id", l.m_id},
                {"start [s]", l.m_start_s},
                {"stop [s]", l.m_stop_s},
                {"class-id", l.m_defn_id.m_id},
                {"class-name", class_name_of(l)}
            };
            if(!portable.m_root.empty())
                r["dataset-root"] = portable.m_root;
            records.push_back(std::move(r));
        }
    }

    for(const auto &r : m_unresolved_labels)
        records.push_back(r);

    return j.dump(2);
}

void project_model::save_if_changed()
{
    if(!has_project())
        return;

    auto text = serialize_labels();
    if(text == m_last_saved)
        return;

    const auto label_file = m_cfg.resolve_proj(m_cfg.m_labels);
    spdlog::info("Saving labels to {}", generic_utf8(label_file));
    write_file_atomically(label_file, text);
    m_last_saved = std::move(text);
}
