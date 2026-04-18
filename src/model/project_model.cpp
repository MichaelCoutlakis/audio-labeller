/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <imgui.h>

#include <AudioFile/AudioFile.h>

#include "project_model.h"

project_model::project_model(const project_config &cfg) :
    m_cfg(cfg)
{
    rescan_files();

    m_label_dict.add(label_defn{get_next_label_defn_id(), "Dog", IM_COL32(255, 0, 0, 100)});
    m_label_dict.add(label_defn{get_next_label_defn_id(), "Cat", IM_COL32(0, 120, 0, 100)});
    m_label_dict.add(label_defn{get_next_label_defn_id(), "Bat", IM_COL32(0, 0, 120, 100)});
    m_label_dict.add(label_defn{get_next_label_defn_id(), "Cow", IM_COL32(255, 0, 0, 100)});
    m_label_dict.add(label_defn{get_next_label_defn_id(), "Ant", IM_COL32(0, 120, 0, 100)});
    m_label_dict.add(label_defn{get_next_label_defn_id(), "Fly", IM_COL32(0, 0, 120, 100)});
}

void project_model::rescan_files()
{
    m_files.clear();

    for(const auto &wav_path : m_cfg.m_wav_paths)
    {
        for(const auto &entry : std::filesystem::recursive_directory_iterator(wav_path))
        {
            if(entry.path().extension() == ".wav")
                m_files.push_back(audio_file_entry{entry.path()});
        }
    }
}

//label_id project_model::add_label(
//    const std::filesystem::path &file,
//    double start_s,
//    double stop_s,
//    std::string name)
//{
//    return label_id();
//}
//
//void project_model::remove_label(const std::filesystem::path &file, label_id id) { }
