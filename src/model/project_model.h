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

struct audio_buffer
{
    uint32_t m_sample_rate_hz{};
    uint32_t m_channels{1U};
    std::vector<float> m_samples;

    size_t num_frames() const { return m_samples.size() / m_channels; }
    double duration_s() const { return double(num_frames()) / m_sample_rate_hz; }
};

using label_id = uint64_t;

struct label
{
    label_id m_id;
    double m_start_s{};
    double m_stop_s{};
    std::string m_label;
    uint32_t m_color_rgba{};
};

struct waveform_envelope
{
    std::vector<float> m_max_vals;
    std::vector<float> m_min_vals;
    size_t m_samples_per_bin{0U};
};

using time_span = std::pair<double, double>;

struct waveform_interaction
{
    void begin_drag(double time_s);
    void update_drag(double time_s);
    void end_drag();

    bool is_dragging() const { return m_dragging; }

private:
    bool m_dragging{false};
    double m_drag_start{};
    double m_drag_current{};
};

struct project_config
{
    static project_config parse_from_file(std::string filename);

    std::vector<std::filesystem::path> m_wav_paths{
        std::filesystem::current_path() / "../../../../"}; //!< Directories to scan for wav files
    std::filesystem::path m_label_path;   //!< Path for pre-saved labels / labelling directory
};

struct audio_file_entry
{
    std::filesystem::path m_path;
};

class project_model
{
public:
    project_model(const project_config &cfg);

    // Rescans files - will reset the active file
    void rescan_files();

    const std::vector<audio_file_entry> &get_files() const { return m_files; }

    label_id add_label(
        const std::filesystem::path &file, double start_s, double stop_s, std::string name);
    void remove_label(const std::filesystem::path &file, label_id id);

private:
    project_config m_cfg;
    std::vector<audio_file_entry> m_files; //!< Available files for labelling
    std::optional<size_t> m_active_file;

    std::map<std::filesystem::path, std::list<label>> m_file_labels; //!< Labels per file
};
