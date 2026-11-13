/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>
#define DR_FLAC_IMPLEMENTATION
#include <dr_flac.h>
#define DR_MP3_IMPLEMENTATION
#include <dr_mp3.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

#include "../model/paths.h"
#include "audio_source.h"

namespace fs = std::filesystem;

/// One audio file format. Decoders report their format on construction and throw if the file can't
/// be opened.
class audio_source::decoder
{
public:
    virtual ~decoder() = default;
    virtual audio_format format() const = 0;
    virtual bool seek(uint64_t frame) = 0;
    virtual uint64_t read_f32(uint64_t frames, float *out) = 0;
    virtual size_t read_raw(size_t bytes, std::byte *out)
    {
        throw std::logic_error("Audio format can't be copied raw");
    }
};

namespace
{
std::string lower_extension(const fs::path &path)
{
    auto ext = generic_utf8(path.extension());
    std::ranges::transform(
        ext,
        ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

class wav_decoder : public audio_source::decoder
{
public:
    explicit wav_decoder(const fs::path &path)
    {
        // dr_wav reads RIFF, RF64 (over 4 GB) and W64 containers
#ifdef _WIN32
        m_open = drwav_init_file_w(&m_wav, path.c_str(), nullptr);
#else
        m_open = drwav_init_file(&m_wav, path.c_str(), nullptr);
#endif
        if(!m_open)
            throw std::runtime_error("Could not open audio file " + generic_utf8(path));
    }
    ~wav_decoder() override
    {
        if(m_open)
            drwav_uninit(&m_wav);
    }

    audio_format format() const override
    {
        return audio_format{
            m_wav.sampleRate,
            m_wav.channels,
            m_wav.totalPCMFrameCount,
            m_wav.bitsPerSample,
            m_wav.translatedFormatTag
        };
    }
    bool seek(uint64_t frame) override { return drwav_seek_to_pcm_frame(&m_wav, frame); }
    uint64_t read_f32(uint64_t frames, float *out) override
    {
        return drwav_read_pcm_frames_f32(&m_wav, frames, out);
    }
    size_t read_raw(size_t bytes, std::byte *out) override
    {
        return drwav_read_raw(&m_wav, bytes, out);
    }

private:
    drwav m_wav{};
    bool m_open{false};
};

class flac_decoder : public audio_source::decoder
{
public:
    explicit flac_decoder(const fs::path &path)
    {
#ifdef _WIN32
        m_flac = drflac_open_file_w(path.c_str(), nullptr);
#else
        m_flac = drflac_open_file(path.c_str(), nullptr);
#endif
        if(!m_flac)
            throw std::runtime_error("Could not open audio file " + generic_utf8(path));
    }
    ~flac_decoder() override { drflac_close(m_flac); }

    audio_format format() const override
    {
        // Not raw copyable: clips are decoded and written as float WAV
        return audio_format{
            m_flac->sampleRate,
            m_flac->channels,
            m_flac->totalPCMFrameCount,
            m_flac->bitsPerSample,
            0U
        };
    }
    bool seek(uint64_t frame) override { return drflac_seek_to_pcm_frame(m_flac, frame); }
    uint64_t read_f32(uint64_t frames, float *out) override
    {
        return drflac_read_pcm_frames_f32(m_flac, frames, out);
    }

private:
    drflac *m_flac{nullptr};
};

class mp3_decoder : public audio_source::decoder
{
public:
    explicit mp3_decoder(const fs::path &path)
    {
#ifdef _WIN32
        m_open = drmp3_init_file_w(&m_mp3, path.c_str(), nullptr);
#else
        m_open = drmp3_init_file(&m_mp3, path.c_str(), nullptr);
#endif
        if(!m_open)
            throw std::runtime_error("Could not open audio file " + generic_utf8(path));
        m_frames = drmp3_get_pcm_frame_count(&m_mp3); // Decodes the whole file to count
    }
    ~mp3_decoder() override
    {
        if(m_open)
            drmp3_uninit(&m_mp3);
    }

    audio_format format() const override
    {
        return audio_format{m_mp3.sampleRate, m_mp3.channels, m_frames, 32U, 0U};
    }
    bool seek(uint64_t frame) override { return drmp3_seek_to_pcm_frame(&m_mp3, frame); }
    uint64_t read_f32(uint64_t frames, float *out) override
    {
        return drmp3_read_pcm_frames_f32(&m_mp3, frames, out);
    }

private:
    drmp3 m_mp3{};
    bool m_open{false};
    uint64_t m_frames{};
};

/// The decoders by extension. Add new formats here.
std::unique_ptr<audio_source::decoder> make_decoder(const fs::path &path)
{
    const auto ext = lower_extension(path);
    if(ext == ".flac")
        return std::make_unique<flac_decoder>(path);
    if(ext == ".mp3")
        return std::make_unique<mp3_decoder>(path);
    return std::make_unique<wav_decoder>(path); // .wav, .w64, .rf64 and anything else
}
}

bool is_supported_audio(const fs::path &path)
{
    const auto ext = lower_extension(path);
    return ext == ".wav" || ext == ".w64" || ext == ".rf64" || ext == ".flac" || ext == ".mp3";
}

audio_source::audio_source(const fs::path &path) :
    m_decoder(make_decoder(path)),
    m_format(m_decoder->format())
{
    if(m_format.m_sample_rate_hz == 0 || m_format.m_channels == 0)
        throw std::runtime_error("Invalid audio format in " + generic_utf8(path));
}

audio_source::~audio_source() = default;

uint64_t audio_source::read_frames(uint64_t start_frame, uint64_t num_frames, float *out)
{
    if(!m_decoder->seek(start_frame))
        throw std::runtime_error("Could not seek in audio file");
    return m_decoder->read_f32(num_frames, out);
}

uint64_t audio_source::read_raw_frames(uint64_t start_frame, uint64_t num_frames, std::byte *out)
{
    if(!m_format.is_raw_copyable())
        throw std::logic_error("Audio format can't be copied raw");
    if(!m_decoder->seek(start_frame))
        throw std::runtime_error("Could not seek in audio file");
    const auto bytes = m_decoder->read_raw(num_frames * m_format.bytes_per_frame(), out);
    return bytes / m_format.bytes_per_frame();
}

audio_buffer load_mono(const fs::path &path, std::optional<uint32_t> channel)
{
    audio_source source(path);
    const auto &fmt = source.format();
    if(fmt.m_frames == 0)
        throw std::runtime_error("No audio in file " + generic_utf8(path));
    if(channel && *channel >= fmt.m_channels)
        channel.reset();

    audio_buffer buffer;
    buffer.m_sample_rate_hz = fmt.m_sample_rate_hz;
    buffer.m_channels = 1U;
    buffer.m_source_channels = fmt.m_channels;
    buffer.m_samples.resize(fmt.m_frames);

    // A mix lets sounds on any channel be seen and heard
    constexpr uint64_t chunk_frames = 1U << 16;
    std::vector<float> chunk(chunk_frames * fmt.m_channels);
    const float gain = 1.f / static_cast<float>(fmt.m_channels);

    uint64_t total = 0U;
    for(uint64_t start = 0U; start < fmt.m_frames; start += chunk_frames)
    {
        const auto read =
            source.read_frames(start, std::min(chunk_frames, fmt.m_frames - start), chunk.data());
        for(uint64_t frame = 0U; frame != read; ++frame)
        {
            const float *in = chunk.data() + frame * fmt.m_channels;
            if(channel)
                buffer.m_samples[start + frame] = in[*channel];
            else
            {
                float sum = 0.f;
                for(uint32_t c = 0U; c != fmt.m_channels; ++c)
                    sum += in[c];
                buffer.m_samples[start + frame] = sum * gain;
            }
        }
        total += read;
        if(read == 0U)
            break;
    }
    buffer.m_samples.resize(total); // Some decoders' frame counts are estimates
    return buffer;
}
