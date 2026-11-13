/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <cmath>
#include <vector>

#include <doctest/doctest.h>

#include "dsp/auto_gain.h"

namespace
{
constexpr uint32_t rate = 8000U;

std::vector<float> tone(float amplitude, float seconds)
{
    std::vector<float> v(static_cast<size_t>(seconds * rate));
    for(size_t i = 0; i != v.size(); ++i)
        v[i] = amplitude * std::sin(2.f * 3.14159265f * 440.f * float(i) / rate);
    return v;
}

float peak(const std::vector<float> &v, size_t from = 0U)
{
    float p = 0.f;
    for(size_t i = from; i < v.size(); ++i)
        p = std::max(p, std::abs(v[i]));
    return p;
}
}

TEST_CASE("fixed playback gain is applied and clipped")
{
    auto_gain g;
    auto_gain::settings s;
    s.m_gain_db = 20.f;
    g.configure(s, rate);
    std::vector<float> x{0.01f, -0.02f, 0.5f};
    g.process(x.data(), x.size());
    CHECK(x[0] == doctest::Approx(0.1f));
    CHECK(x[1] == doctest::Approx(-0.2f));
    CHECK(x[2] == 1.f);
    CHECK(g.current_gain_db() == doctest::Approx(20.f));
}

TEST_CASE("automatic gain raises quiet audio to the target")
{
    auto_gain g;
    auto_gain::settings s;
    s.m_automatic = true;
    g.configure(s, rate);
    auto x = tone(0.01f, 1.f); // -40 dBFS
    g.prime(x.data(), 400U);
    g.process(x.data(), x.size());
    CHECK(peak(x, rate / 2U) == doctest::Approx(s.m_target).epsilon(0.05));
    CHECK(g.current_gain_db() == doctest::Approx(34.f).epsilon(0.05));
}

TEST_CASE("automatic gain is limited to its maximum")
{
    auto_gain g;
    auto_gain::settings s;
    s.m_automatic = true;
    s.m_max_gain_db = 20.f;
    g.configure(s, rate);
    auto x = tone(0.001f, 0.5f);
    g.prime(x.data(), 400U);
    g.process(x.data(), x.size());
    CHECK(g.current_gain_db() == doctest::Approx(20.f));
    CHECK(peak(x) == doctest::Approx(0.01f).epsilon(0.05));
}

TEST_CASE("automatic gain backs off quickly when it gets loud, and never clips")
{
    auto_gain g;
    auto_gain::settings s;
    s.m_automatic = true;
    g.configure(s, rate);
    auto quiet = tone(0.001f, 1.f);
    g.prime(quiet.data(), 400U);
    g.process(quiet.data(), quiet.size());

    auto loud = tone(0.8f, 0.5f);
    g.process(loud.data(), loud.size());
    CHECK(peak(loud) <= 1.f);
    CHECK(peak(loud, rate / 20U) == doctest::Approx(s.m_target).epsilon(0.1));
}
