/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <cstdint>
#include <filesystem>

#include "audio_source.h"

struct wav_copy_result
{
    uint64_t m_frames{}; ///< Frames written
    bool m_verified{};   ///< The file was read back and matched what was written
};

/// Copy a range of frames from a source into a new WAV file. PCM and float sources are copied
/// byte for byte, keeping their sample rate, channels and sample format. Other (compressed)
/// formats are decoded and written as 32-bit float. Throws if the file can't be written.
wav_copy_result copy_wav_range(
    audio_source &source,
    uint64_t start_frame,
    uint64_t num_frames,
    const std::filesystem::path &out);
