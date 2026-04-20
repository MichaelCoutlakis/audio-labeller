/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <nlohmann/json_fwd.hpp>
#include <stdexcept>

#include <AudioFile/AudioFile.h>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <string>

#include "label.h"
#include "project_model.h"

std::string to_hex(uint32_t u)
{
    uint8_t r = (u >> IM_COL32_R_SHIFT) & 0xFF;
    uint8_t g = (u >> IM_COL32_G_SHIFT) & 0xFF;
    uint8_t b = (u >> IM_COL32_B_SHIFT) & 0xFF;
    uint8_t a = (u >> IM_COL32_A_SHIFT) & 0xFF;
    return fmt::format("\"#{:02x}{:02x}{:02x}{:02x}\"", r, g, b, a);
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
    cfg.m_filename = filename;

    if(filename.empty())
        return cfg;

    std::ifstream f(filename);
    if(!f)
        throw std::runtime_error("Could not open" + filename);
    auto j = nlohmann::json::parse(f);
    j.at("dataset-roots").get_to(cfg.m_wav_paths);
    j.at("label-palette").get_to(cfg.m_palette);
    j.at("export-prefix").get_to(cfg.m_export_prefix);
    j.at("label-file").get_to(cfg.m_labels);
    return cfg;
}

std::filesystem::path project_config::resolve_proj(std::filesystem::path &p)
{
    auto out = p;
    if(out.is_relative())
        out = m_filename.parent_path() / out;

    return out.lexically_normal();
}

project_model::project_model(const project_config &cfg) :
    m_cfg(cfg)
{
    rescan_files();

    auto palette_path = m_cfg.resolve_proj(m_cfg.m_palette);
    spdlog::info("Loading label palette from {}", palette_path.string());

    std::ifstream f(palette_path);
    if(!f)
        throw std::runtime_error("Could not load " + palette_path.string());

    auto j = nlohmann::json::parse(f);
    j.at("label-palette").get_to(m_label_dict);

    load_labels();
}

project_model::~project_model() { save_labels(); }

void project_model::rescan_files()
{
    spdlog::info("Scanning dataset...");
    m_files.clear();

    for(const auto &wav_path : m_cfg.m_wav_paths)
    {
        auto search_path = wav_path;
        if(search_path.is_relative())
            search_path = (m_cfg.m_filename.parent_path() / search_path).lexically_normal();

        spdlog::info("Scanning {}", search_path.string());
        for(const auto &entry : std::filesystem::recursive_directory_iterator(search_path))
        {
            if(entry.path().extension() == ".wav")
                m_files.push_back(audio_file_entry{entry.path()});
        }
    }
}

static std::string csv_escape(const std::string &s)
{
    if(s.find_first_of(", \"\n") == std::string::npos)
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

void project_model::export_csv()
{
    auto export_path = m_cfg.resolve_proj(m_cfg.m_export_prefix);
    export_path += ".csv";

    std::ofstream f(export_path);
    if(!f)
        throw std::runtime_error("Failed to open CSV for export");

    f << "filename,start_s,stop_s,class_label,class_id\n";
    f << std::fixed << std::setprecision(6);

    for(const auto &[src_path, labels] : m_file_labels)
    {
        for(const auto &l : labels)
        {
            auto defn = m_label_dict.find_ptr(l.m_defn_id);
            f << csv_escape(src_path.string()) << "," << l.m_start_s << "," << l.m_stop_s << ","
              << csv_escape(defn->m_class) << "," << defn->m_id << "\n";
        }
    }
}
void project_model::load_labels()
{
    auto label_file = m_cfg.resolve_proj(m_cfg.m_labels);
    if(!std::filesystem::exists(label_file))
    {
        spdlog::warn("Label file {} does not exist, skipping label load", label_file.string());
        return;
    }
    spdlog::info("Loading labels from {}", label_file.string());

    std::ifstream f(label_file);
    if(!f)
        throw std::runtime_error("Could not load labels from " + label_file.string());

    auto j = nlohmann::json::parse(f);

    for(auto &it : j.at("labels").items())
    {
        auto jl = it.value();
        auto filename = jl.at("filename").get<std::filesystem::path>();
        auto label_id = jl.at("label-id").get<uint64_t>();
        auto start_s = jl.at("start [s]").get<double>();
        auto stop_s = jl.at("stop [s]").get<double>();
        auto class_id = jl.at("class-id").get<uint32_t>();
        auto class_name = jl.at("class-name").get<std::string>();

        label_defn defn{class_id, class_name, 0};

        auto defn_lookup = m_label_dict.find_ptr(label_defn_id(class_id));
        if(!defn_lookup)
            spdlog::error("Loading label without class definition");
        else if(defn_lookup->m_class != class_name)
            spdlog::error("Label class name does not match definition class name");

        label l{label_id, class_id, start_s, stop_s};

        m_file_labels[filename].add(l);
    }
}
void project_model::save_labels()
{
    auto label_file = m_cfg.resolve_proj(m_cfg.m_labels);
    spdlog::info("Saving labels to {}", label_file.string());
    std::ofstream f(label_file);
    if(!f)
        throw std::runtime_error("Could not create file to save labels " + label_file.string());

    nlohmann::json j{{"$schema", "../schemas/labels.schema.json"}};

    for(const auto &[src_path, labels] : m_file_labels)
    {
        for(const auto &l : labels)
        {
            auto defn = m_label_dict.find_ptr(l.m_defn_id);
            nlohmann::json r{
                {"filename", src_path},
                {"label-id", l.m_id},
                {"start [s]", l.m_start_s},
                {"stop [s]", l.m_stop_s},
                {"class-id", l.m_defn_id.m_id},
                {"class-name", defn->m_class}
            };
            j["labels"].push_back(r);
        }
    }

    f << j.dump(2);
}
