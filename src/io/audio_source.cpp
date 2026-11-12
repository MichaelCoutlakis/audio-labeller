/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "../model/paths.h"
#include "audio_source.h"

struct audio_source::impl
{
    drwav m_wav{};
    bool m_open{false};
    ~impl()
    {
        if(m_open)
            drwav_uninit(&m_wav);
    }
};

audio_source::audio_source(const std::filesystem::path &path) :
    m_impl(std::make_unique<impl>())
{
#ifdef _WIN32
    m_impl->m_open = drwav_init_file_w(&m_impl->m_wav, path.c_str(), nullptr);
#else
    m_impl->m_open = drwav_init_file(&m_impl->m_wav, path.c_str(), nullptr);
#endif
    if(!m_impl->m_open)
        throw std::runtime_error("Could not open audio file " + generic_utf8(path));

    const auto &w = m_impl->m_wav;
    m_format = audio_format{
        w.sampleRate,
        w.channels,
        w.totalPCMFrameCount,
        w.bitsPerSample,
        w.translatedFormatTag
    };
    if(m_format.m_sample_rate_hz == 0 || m_format.m_channels == 0)
        throw std::runtime_error("Invalid audio format in " + generic_utf8(path));
}

audio_source::~audio_source() = default;

uint64_t audio_source::read_frames(uint64_t start_frame, uint64_t num_frames, float *out)
{
    if(!drwav_seek_to_pcm_frame(&m_impl->m_wav, start_frame))
        throw std::runtime_error("Could not seek in audio file");
    return drwav_read_pcm_frames_f32(&m_impl->m_wav, num_frames, out);
}

uint64_t audio_source::read_raw_frames(uint64_t start_frame, uint64_t num_frames, std::byte *out)
{
    if(!m_format.is_raw_copyable())
        throw std::logic_error("Audio format can't be copied raw");
    if(!drwav_seek_to_pcm_frame(&m_impl->m_wav, start_frame))
        throw std::runtime_error("Could not seek in audio file");
    const auto bytes = drwav_read_raw(&m_impl->m_wav, num_frames * m_format.bytes_per_frame(), out);
    return bytes / m_format.bytes_per_frame();
}

audio_buffer load_mono(const std::filesystem::path &path)
{
    audio_source source(path);
    const auto &fmt = source.format();
    if(fmt.m_frames == 0)
        throw std::runtime_error("No audio in file " + generic_utf8(path));

    audio_buffer buffer;
    buffer.m_sample_rate_hz = fmt.m_sample_rate_hz;
    buffer.m_channels = 1U;
    buffer.m_samples.resize(fmt.m_frames);

    // Mix down to mono so that sounds on any channel can be seen and heard
    constexpr uint64_t chunk_frames = 1U << 16;
    std::vector<float> chunk(chunk_frames * fmt.m_channels);
    const float gain = 1.f / static_cast<float>(fmt.m_channels);

    for(uint64_t start = 0U; start < fmt.m_frames; start += chunk_frames)
    {
        const auto read =
            source.read_frames(start, std::min(chunk_frames, fmt.m_frames - start), chunk.data());
        for(uint64_t frame = 0U; frame != read; ++frame)
        {
            float sum = 0.f;
            for(uint32_t c = 0U; c != fmt.m_channels; ++c)
                sum += chunk[frame * fmt.m_channels + c];
            buffer.m_samples[start + frame] = sum * gain;
        }
        if(read == 0U)
            break;
    }
    return buffer;
}
