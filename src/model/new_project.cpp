/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "../io/files.h"
#include "new_project.h"
#include "paths.h"

namespace fs = std::filesystem;

namespace
{
std::string trim(const std::string &s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if(first == std::string::npos)
        return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1U);
}

/// Distinct colours, as hex RGBA: hues a golden angle apart, so neighbours always differ
std::string class_colour(size_t index)
{
    const double hue = std::fmod(static_cast<double>(index) * 137.508, 360.) / 60.;
    const double s = 0.65, v = 0.85;
    const double c = v * s;
    const double x = c * (1. - std::abs(std::fmod(hue, 2.) - 1.));
    double r = 0., g = 0., b = 0.;
    switch(static_cast<int>(hue))
    {
    case 0: r = c, g = x; break;
    case 1: r = x, g = c; break;
    case 2: g = c, b = x; break;
    case 3: g = x, b = c; break;
    case 4: r = x, b = c; break;
    default: r = c, b = x; break;
    }
    const auto byte = [&](double ch)
    { return static_cast<unsigned>(std::lround((ch + v - c) * 255.)); };
    return fmt::format("#{:02x}{:02x}{:02x}b0", byte(r), byte(g), byte(b));
}

bool is_inside(const fs::path &target, const fs::path &dir)
{
    const auto rel = target.lexically_relative(dir);
    return !rel.empty() && *rel.begin() != "..";
}
}

std::vector<std::string> split_class_names(const std::string &text)
{
    std::vector<std::string> out;
    std::set<std::string> seen;
    size_t pos = 0U;
    while(pos <= text.size())
    {
        const auto comma = std::min(text.find(',', pos), text.size());
        auto name = trim(text.substr(pos, comma - pos));
        pos = comma + 1U;
        if(name.empty())
            continue;
        if(!seen.insert(name).second)
            throw std::runtime_error("The class " + name + " is listed twice");
        out.push_back(std::move(name));
    }
    return out;
}

fs::path new_project_file(const new_project_spec &spec)
{
    auto file = fs::absolute(spec.m_project_file).lexically_normal();
    if(!file.has_extension())
        file += ".json";
    return file;
}

new_project_files new_project_files_for(const fs::path &project_file)
{
    const auto dir = project_file.parent_path();
    const auto stem = project_file.stem().native();
    return {
        dir / (stem + fs::path("-palette.json").native()),
        dir / (stem + fs::path("-labels.json").native())
    };
}

std::string path_for_project(const fs::path &target, const fs::path &project_dir)
{
    const auto t = fs::absolute(target).lexically_normal();
    const auto d = fs::absolute(project_dir).lexically_normal();
    if(t == d)
        return ".";
    if(is_inside(t, d))
        return generic_utf8(t.lexically_relative(d));
    return generic_utf8(t);
}

fs::path create_project(const new_project_spec &spec)
{
    if(spec.m_project_file.empty())
        throw std::runtime_error("Choose where to save the project");
    if(spec.m_data_root.empty())
        throw std::runtime_error("Choose the folder of audio files");

    const auto project = new_project_file(spec);
    const auto dir = project.parent_path();
    const auto files = new_project_files_for(project);
    const auto root = fs::absolute(spec.m_data_root).lexically_normal();

    if(!fs::is_directory(root))
        throw std::runtime_error("The audio folder " + generic_utf8(root) + " doesn't exist");
    if(fs::exists(project))
        throw std::runtime_error(
            generic_utf8(project) +
            " already exists. Open it with File > Open project, or choose another name.");
    if(fs::exists(files.m_labels))
        throw std::runtime_error(
            "The label file " +
            generic_utf8(files.m_labels) +
            " already exists, perhaps from another project. Choose another project name.");

    fs::path palette;
    if(spec.m_palette)
    {
        palette = fs::absolute(*spec.m_palette).lexically_normal();
        if(!fs::is_regular_file(palette))
            throw std::runtime_error("The palette " + generic_utf8(palette) + " doesn't exist");
    }
    else
    {
        if(spec.m_classes.empty())
            throw std::runtime_error("Name at least one class to label, or choose a palette");
        palette = files.m_palette;
        if(fs::exists(palette))
            throw std::runtime_error(
                "The palette " +
                generic_utf8(palette) +
                " already exists. Use it as an existing palette, or choose another project name.");
    }

    fs::create_directories(dir);

    if(!spec.m_palette)
    {
        auto classes = nlohmann::ordered_json::array();
        for(size_t i = 0U; i != spec.m_classes.size(); ++i)
            classes.push_back(
                {{"class-id", i}, {"class", spec.m_classes[i]}, {"colour", class_colour(i)}});
        const nlohmann::ordered_json j{{"label-palette", classes}};
        write_file_atomically(palette, j.dump(4) + "\n");
        spdlog::info("Wrote the palette {}", generic_utf8(palette));
    }

    nlohmann::ordered_json j;
    j["dataset-roots"] = {path_for_project(root, dir)};
    j["label-palette"] = path_for_project(palette, dir);
    j["label-file"] = path_for_project(files.m_labels, dir);
    j["export-prefix"] = generic_utf8(project.stem()) + "-export";
    write_file_atomically(project, j.dump(4) + "\n");
    spdlog::info("Wrote the project {}", generic_utf8(project));
    return project;
}
