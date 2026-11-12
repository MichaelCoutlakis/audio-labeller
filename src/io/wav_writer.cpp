/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <cstring>
#include <stdexcept>
#include <vector>

#include <dr_wav.h>

#include "../model/paths.h"
#include "wav_writer.h"

wav_copy_result copy_wav_range(
    audio_source &source,
    uint64_t start_frame,
    uint64_t num_frames,
    const std::filesystem::path &out)
{
    const auto &in = source.format();

    drwav_data_format format{};
    format.container = drwav_container_riff;
    format.channels = in.m_channels;
    format.sampleRate = in.m_sample_rate_hz;

    std::vector<std::byte> data;
    uint64_t frames{};
    if(in.is_raw_copyable())
    {
        format.format = in.m_format_tag;
        format.bitsPerSample = in.m_bits_per_sample;
        data.resize(num_frames * in.bytes_per_frame());
        frames = source.read_raw_frames(start_frame, num_frames, data.data());
        data.resize(frames * in.bytes_per_frame());
    }
    else
    {
        format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
        format.bitsPerSample = 32;
        std::vector<float> samples(num_frames * in.m_channels);
        frames = source.read_frames(start_frame, num_frames, samples.data());
        data.resize(frames * in.m_channels * sizeof(float));
        std::memcpy(data.data(), samples.data(), data.size());
    }

    {
        drwav wav;
#ifdef _WIN32
        const bool ok = drwav_init_file_write_w(&wav, out.c_str(), &format, nullptr);
#else
        const bool ok = drwav_init_file_write(&wav, out.c_str(), &format, nullptr);
#endif
        if(!ok)
            throw std::runtime_error("Could not create " + generic_utf8(out));
        const auto written = drwav_write_raw(&wav, data.size(), data.data());
        drwav_uninit(&wav);
        if(written != data.size())
            throw std::runtime_error("Could not write " + generic_utf8(out));
    }

    // Read it back, as the original may be deleted once its clips are exported
    wav_copy_result result{frames, false};
    try
    {
        audio_source check(out);
        const auto &f = check.format();
        if(f.m_frames == frames &&
           f.m_channels == format.channels &&
           f.m_sample_rate_hz == format.sampleRate &&
           f.m_bits_per_sample == format.bitsPerSample)
        {
            std::vector<std::byte> back(data.size());
            result.m_verified =
                check.read_raw_frames(0, frames, back.data()) == frames && back == data;
        }
    }
    catch(const std::exception &)
    {
        result.m_verified = false;
    }
    return result;
}
