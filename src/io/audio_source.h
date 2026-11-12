/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

#include "../model/audio_buffer.h"

struct audio_format
{
    uint32_t m_sample_rate_hz{};
    uint32_t m_channels{};
    uint64_t m_frames{};
    uint32_t m_bits_per_sample{};
    uint16_t m_format_tag{}; ///< WAVE format tag: 1 = PCM, 3 = IEEE float, others are compressed

    double duration_s() const
    {
        return m_sample_rate_hz ? double(m_frames) / m_sample_rate_hz : 0.;
    }

    /// Whether frames can be copied as raw bytes into a new file of the same format
    bool is_raw_copyable() const
    {
        return (m_format_tag == 1 || m_format_tag == 3) && m_bits_per_sample % 8 == 0;
    }
    size_t bytes_per_frame() const { return size_t(m_channels) * m_bits_per_sample / 8U; }
};

/// An audio file opened for reading ranges of frames, without reading the whole file into memory
class audio_source
{
public:
    explicit audio_source(const std::filesystem::path &path);
    ~audio_source();
    audio_source(const audio_source &) = delete;
    audio_source &operator=(const audio_source &) = delete;

    const audio_format &format() const { return m_format; }

    /// Read interleaved float frames. @return the number of frames read
    uint64_t read_frames(uint64_t start_frame, uint64_t num_frames, float *out);

    /// Read frames as stored in the file. Only valid if format().is_raw_copyable().
    /// @return the number of frames read
    uint64_t read_raw_frames(uint64_t start_frame, uint64_t num_frames, std::byte *out);

private:
    struct impl;
    std::unique_ptr<impl> m_impl;
    audio_format m_format;
};

/// Read a whole file, mixed down to mono
audio_buffer load_mono(const std::filesystem::path &path);
