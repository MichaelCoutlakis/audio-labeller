/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <chrono>
#include <cstddef>
#include <cstring>

#include <doctest/doctest.h>

#include "io/audio_source.h"
#include "io/source_id.h"
#include "test_utils.h"

TEST_CASE("audio_source reads format, ranges and raw frames")
{
    temp_dir dir;
    const auto file = dir / "stereo.wav";
    // Left counts up, right counts down:
    std::vector<int16_t> samples;
    for(int16_t i = 0; i != 100; ++i)
    {
        samples.push_back(i * 100);
        samples.push_back(static_cast<int16_t>(-i * 100));
    }
    write_wav16(file, 16000, 2, samples);

    audio_source source(file);
    const auto &fmt = source.format();
    CHECK(fmt.m_sample_rate_hz == 16000);
    CHECK(fmt.m_channels == 2);
    CHECK(fmt.m_frames == 100);
    CHECK(fmt.m_bits_per_sample == 16);
    CHECK(fmt.is_raw_copyable());
    CHECK(fmt.bytes_per_frame() == 4);

    std::vector<float> frames(2 * 10);
    CHECK(source.read_frames(50, 10, frames.data()) == 10);
    CHECK(frames[0] == doctest::Approx(5000 / 32768.).epsilon(1e-6));
    CHECK(frames[1] == doctest::Approx(-5000 / 32768.).epsilon(1e-6));

    std::vector<std::byte> raw(4 * 3);
    CHECK(source.read_raw_frames(98, 10, raw.data()) == 2); // Clipped at the end of the file
    int16_t first{};
    std::memcpy(&first, raw.data(), sizeof(first));
    CHECK(first == 9800);
}

TEST_CASE("load_mono mixes all channels")
{
    temp_dir dir;
    const auto file = dir / "right_only.wav";
    write_wav16(file, 8000, 2, {0, 16384, 0, 16384, 0, 16384});

    const auto buffer = load_mono(file);
    CHECK(buffer.m_channels == 1);
    CHECK(buffer.m_sample_rate_hz == 8000);
    REQUIRE(buffer.num_frames() == 3);
    CHECK(buffer.m_samples[1] == doctest::Approx(0.25));
}

TEST_CASE("load_mono can read a single channel")
{
    temp_dir dir;
    const auto file = dir / "two.WAV";
    write_wav16(file, 8000, 2, {0, 16384, 0, 16384});
    CHECK(is_supported_audio(file));
    CHECK(is_supported_audio("x.Flac"));
    CHECK(!is_supported_audio("x.txt"));

    CHECK(load_mono(file, 1U).m_samples[0] == doctest::Approx(0.5));
    CHECK(load_mono(file, 0U).m_samples[0] == 0.f);
    CHECK(load_mono(file, 7U).m_samples[0] == doctest::Approx(0.25)); // No such channel: a mix
    CHECK(load_mono(file).m_source_channels == 2);
}

TEST_CASE("RF64 and 32-bit multi-channel files at high sample rates")
{
    temp_dir dir;
    struct variant
    {
        drwav_container container;
        uint32_t format;
        uint32_t bits;
        uint32_t rate;
    };
    for(const auto v : {
            variant{drwav_container_rf64, DR_WAVE_FORMAT_PCM, 24, 96000},
            variant{drwav_container_riff, DR_WAVE_FORMAT_PCM, 32, 192000},
            variant{drwav_container_riff, DR_WAVE_FORMAT_IEEE_FLOAT, 32, 48000},
            variant{drwav_container_rf64, DR_WAVE_FORMAT_IEEE_FLOAT, 32, 192000},
        })
    {
        CAPTURE(v.bits);
        CAPTURE(v.rate);
        const auto file = dir / "multi.wav";
        constexpr uint32_t channels = 4;
        constexpr uint64_t frames = 1000;

        drwav_data_format format{};
        format.container = v.container;
        format.format = v.format;
        format.channels = channels;
        format.sampleRate = v.rate;
        format.bitsPerSample = v.bits;
        std::vector<float> samples(frames * channels);
        for(uint64_t i = 0; i != frames; ++i)
            for(uint32_t c = 0; c != channels; ++c)
                samples[i * channels + c] = 0.1f * float(c + 1U) * (i % 2 ? 1.f : -1.f);

        std::vector<std::byte> data(frames * channels * v.bits / 8U);
        for(size_t s = 0; s != samples.size(); ++s)
        {
            if(v.format == DR_WAVE_FORMAT_IEEE_FLOAT)
                std::memcpy(data.data() + s * 4, &samples[s], 4);
            else
            {
                const auto value = static_cast<int32_t>(samples[s] * 2147483647.f);
                const auto bytes = v.bits / 8U;
                for(uint32_t b = 0; b != bytes; ++b)
                    data[s * bytes + b] = std::byte(uint32_t(value) >> (32U - 8U * bytes + 8U * b));
            }
        }
        drwav wav;
#ifdef _WIN32
        REQUIRE(drwav_init_file_write_w(&wav, file.c_str(), &format, nullptr));
#else
        REQUIRE(drwav_init_file_write(&wav, file.c_str(), &format, nullptr));
#endif
        drwav_write_raw(&wav, data.size(), data.data());
        drwav_uninit(&wav);

        audio_source source(file);
        CHECK(source.format().m_channels == channels);
        CHECK(source.format().m_sample_rate_hz == v.rate);
        CHECK(source.format().m_frames == frames);
        CHECK(source.format().is_raw_copyable());

        const auto third = load_mono(file, 2U);
        REQUIRE(third.num_frames() == frames);
        CHECK(third.m_samples[1] == doctest::Approx(0.3).epsilon(1e-4));
        CHECK(load_mono(file).m_samples[1] == doctest::Approx(0.25).epsilon(1e-4));
    }
}

TEST_CASE("audio_source rejects files that aren't audio")
{
    temp_dir dir;
    write_text(dir / "garbage.wav", "not a wav file");
    CHECK_THROWS(audio_source(dir / "garbage.wav"));
    CHECK_THROWS(audio_source(dir / "missing.wav"));
}

TEST_CASE("source IDs depend only on content")
{
    temp_dir dir;
    write_text(dir / "a.wav", "same bytes");
    write_text(dir / "sub/b.wav", "same bytes");
    write_text(dir / "c.wav", "other bytes");

    const auto id = compute_source_id(dir / "a.wav");
    CHECK(id.size() == 12);
    CHECK(id == compute_source_id(dir / "sub/b.wav"));
    CHECK(id != compute_source_id(dir / "c.wav"));
}

// Run explicitly: audio_labeller_tests -tc="long recording*" --no-skip
TEST_CASE("long recording load time and memory" * doctest::skip())
{
    temp_dir dir;
    const auto file = dir / "long.wav";
    constexpr uint32_t rate = 48000, channels = 4;
    constexpr uint64_t frames = uint64_t(rate) * 600; // 10 minutes
    {
        drwav_data_format format{};
        format.container = drwav_container_riff;
        format.format = DR_WAVE_FORMAT_PCM;
        format.channels = channels;
        format.sampleRate = rate;
        format.bitsPerSample = 24;
        drwav wav;
#ifdef _WIN32
        REQUIRE(drwav_init_file_write_w(&wav, file.c_str(), &format, nullptr));
#else
        REQUIRE(drwav_init_file_write(&wav, file.c_str(), &format, nullptr));
#endif
        std::vector<std::byte> chunk(rate * channels * 3);
        for(size_t i = 0; i != chunk.size(); ++i)
            chunk[i] = std::byte(i * 7919U);
        for(uint64_t s = 0; s != 600; ++s)
            drwav_write_raw(&wav, chunk.size(), chunk.data());
        drwav_uninit(&wav);
    }
    const auto start = std::chrono::steady_clock::now();
    const auto mono = load_mono(file);
    const auto mixed_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto start_one = std::chrono::steady_clock::now();
    const auto one = load_mono(file, 3U);
    const auto one_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_one).count();
    CHECK(mono.num_frames() == frames);
    MESSAGE(
        "10 min, 4 ch, 24-bit, 48 kHz (" << std::filesystem::file_size(file) / 1000000
                                         << " MB): mix in " << mixed_s << " s, one channel in "
                                         << one_s << " s, view buffer "
                                         << mono.m_samples.size() * sizeof(float) / 1000000
                                         << " MB");
}
