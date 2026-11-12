/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <vector>

#include <doctest/doctest.h>

#include "audio/playback_cursor.h"

namespace
{
audio_buffer ramp(size_t frames)
{
    audio_buffer b;
    b.m_sample_rate_hz = 10;
    for(size_t i = 0; i != frames; ++i)
        b.m_samples.push_back(float(i));
    return b;
}
}

TEST_CASE("playback_cursor plays a region once")
{
    const auto clip = ramp(100);
    playback_cursor c;
    c.reset(clip);
    c.select_region(clip, {1.0, 1.5}, false);
    REQUIRE(c.play());

    std::vector<float> out(8, -1.f);
    CHECK(c.read(clip, out.data(), out.size()) == 5);
    CHECK(out[0] == 10.f);
    CHECK(out[4] == 14.f);
    CHECK(!c.m_playing);
    CHECK(c.read(clip, out.data(), out.size()) == 0);
}

TEST_CASE("playback_cursor loops")
{
    const auto clip = ramp(100);
    playback_cursor c;
    c.select_region(clip, {0.0, 0.3}, true);
    c.play();

    std::vector<float> out(7);
    CHECK(c.read(clip, out.data(), out.size()) == 7);
    CHECK(out == std::vector<float>{0, 1, 2, 0, 1, 2, 0});
    CHECK(c.m_playing);
}

TEST_CASE("playback_cursor clamps regions to the clip")
{
    const auto clip = ramp(100);
    playback_cursor c;
    c.select_region(clip, {-5.0, 1e9}, false);
    CHECK(c.m_region_start == 0);
    CHECK(c.m_region_end == 100);

    c.select_region(clip, {20.0, 30.0}, false); // Entirely past the end
    CHECK(!c.play());
}
