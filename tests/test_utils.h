/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <dr_wav.h>

/// A fresh directory, removed again afterwards
class temp_dir
{
public:
    temp_dir()
    {
        static std::atomic<int> counter{};
        m_path = std::filesystem::temp_directory_path() /
            "audio_labeller_tests" /
            std::to_string(++counter);
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }
    ~temp_dir()
    {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }
    temp_dir(const temp_dir &) = delete;
    temp_dir &operator=(const temp_dir &) = delete;

    const std::filesystem::path &path() const { return m_path; }
    std::filesystem::path operator/(const std::filesystem::path &p) const
    {
        return (m_path / p).lexically_normal();
    }

private:
    std::filesystem::path m_path;
};

inline void write_text(const std::filesystem::path &p, const std::string &s)
{
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}

inline std::string read_text(const std::filesystem::path &p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

/// Write a 16-bit PCM WAV of interleaved samples
inline void write_wav16(
    const std::filesystem::path &p,
    uint32_t sample_rate_hz,
    uint32_t channels,
    const std::vector<int16_t> &samples)
{
    std::filesystem::create_directories(p.parent_path());
    drwav_data_format format{};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_PCM;
    format.channels = channels;
    format.sampleRate = sample_rate_hz;
    format.bitsPerSample = 16;

    drwav wav;
#ifdef _WIN32
    drwav_init_file_write_w(&wav, p.c_str(), &format, nullptr);
#else
    drwav_init_file_write(&wav, p.c_str(), &format, nullptr);
#endif
    drwav_write_pcm_frames(&wav, samples.size() / channels, samples.data());
    drwav_uninit(&wav);
}
