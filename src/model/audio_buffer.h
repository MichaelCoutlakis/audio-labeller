/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/// Decoded audio, interleaved float samples
struct audio_buffer
{
    uint32_t m_sample_rate_hz{};
    uint32_t m_channels{1U};
    uint32_t m_source_channels{1U}; ///< Channels in the file this was read from
    std::vector<float> m_samples;

    size_t num_frames() const { return m_samples.size() / m_channels; }
    double duration_s() const
    {
        return m_sample_rate_hz ? double(num_frames()) / m_sample_rate_hz : 0.;
    }
};
