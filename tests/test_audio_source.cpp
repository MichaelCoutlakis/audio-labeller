/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
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
