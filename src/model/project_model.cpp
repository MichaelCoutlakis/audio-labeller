/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <AudioFile/AudioFile.h>

#include "project_model.h"

project_model::project_model(const project_config &cfg) :
    m_cfg(cfg)
{
    rescan_files();
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

label_id project_model::add_label(
    const std::filesystem::path &file,
    double start_s,
    double stop_s,
    std::string name)
{
    return label_id();
}

void project_model::remove_label(const std::filesystem::path &file, label_id id) { }
