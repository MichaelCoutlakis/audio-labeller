/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
        {"class-id", d.m_id},
        {"class", d.m_class},
        {"colour", to_hex(d.m_color_rgba)}
    };
}

void from_json(const nlohmann::json &j, label_defn &d)
{
    j.at("class-id").get_to(d.m_id);
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
    j.at("label-palette").get_to(cfg.m_label_path);
    j.at("export-path").get_to(cfg.m_export_path);
    return cfg;
}

project_model::project_model(const project_config &cfg) :
    m_cfg(cfg)
{
    rescan_files();

    auto palette_path = m_cfg.m_label_path;
    if(palette_path.is_relative())
        palette_path = (m_cfg.m_filename.parent_path() / palette_path).lexically_normal();

    spdlog::info("Loading label palette from {}", palette_path.string());

    std::ifstream f(palette_path);
    if(!f)
        throw std::runtime_error("Could not load " + palette_path.string());

    auto j = nlohmann::json::parse(f);
    j.at("label-palette").get_to(m_label_dict);
}

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
