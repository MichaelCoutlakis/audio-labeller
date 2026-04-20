/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <filesystem>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "label.h"
#include "ordered_registry.h"
#include "types.h"

struct audio_buffer
{
    uint32_t m_sample_rate_hz{};
    uint32_t m_channels{1U};
    std::vector<float> m_samples;

    size_t num_frames() const { return m_samples.size() / m_channels; }
    double duration_s() const { return double(num_frames()) / m_sample_rate_hz; }
};

struct waveform_envelope
{
    std::vector<float> m_max_vals;
    std::vector<float> m_min_vals;
    size_t m_samples_per_bin{0U};
};

struct project_config
{
    static project_config parse_from_file(std::string filename);

    /// Resolve a project related path (label, export) relative to the project location, according
    /// to settings
    std::filesystem::path resolve_proj(std::filesystem::path &p);
    std::filesystem::path m_filename;                 ///< This structure on disk
    std::vector<std::filesystem::path> m_wav_paths{}; ///< Directories to scan for wav files
    std::filesystem::path m_palette;                  ///< Classification dictionary
    std::filesystem::path m_labels;        ///< Path for pre-saved labels / labelling directory
    std::filesystem::path m_export_prefix; ///< Where to save exported artifacts
};

struct audio_file_entry
{
    std::filesystem::path m_path;
};

class project_model
{
public:
    project_model(const project_config &cfg);
    ~project_model();

    // Rescans files - will reset the active file
    void rescan_files();

    /// Export CSV (to the path defined in the project settings)
    void export_csv();

    const std::vector<audio_file_entry> &get_files() const { return m_files; }

    label_dict m_label_dict;

    labels &get_labels(const std::filesystem::path &file) { return m_file_labels[file]; }

private:
    project_config m_cfg;
    std::vector<audio_file_entry> m_files; //!< Available files for labelling

    std::map<std::filesystem::path, labels> m_file_labels; //!< Labels per file

    void load_labels();
    void save_labels();
};
