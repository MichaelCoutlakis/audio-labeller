/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "dsp/high_pass.h"

namespace
{
constexpr uint32_t rate = 48000U;

std::vector<float> sine(double hz, double seconds, float amplitude = 0.5f)
{
    std::vector<float> out(static_cast<size_t>(seconds * rate));
    for(size_t i = 0U; i != out.size(); ++i)
        out[i] = amplitude *
            static_cast<float>(std::sin(2. * std::numbers::pi * hz * static_cast<double>(i) / rate));
    return out;
}

/// RMS of the middle half, away from the ends
double rms_middle(const std::vector<float> &x)
{
    double sum = 0.;
    const size_t a = x.size() / 4U, b = 3U * x.size() / 4U;
    for(size_t i = a; i != b; ++i)
        sum += double(x[i]) * x[i];
    return std::sqrt(sum / static_cast<double>(b - a));
}

double gain_db(double hz, const high_pass_settings &s)
{
    auto x = sine(hz, 2.);
    const double before = rms_middle(x);
    apply_high_pass(x, rate, s);
    return 20. * std::log10(rms_middle(x) / before);
}
}

TEST_CASE("the high-pass passes above the cutoff and removes rumble below it")
{
    high_pass_settings s;
    s.m_enabled = true;
    s.m_cutoff_hz = 100.;
    s.m_order = 4;

    CHECK(std::abs(gain_db(1000., s)) < 0.05);
    // Forwards and backwards: twice the Butterworth -3 dB at the cutoff
    CHECK(gain_db(100., s) == doctest::Approx(-6.02).epsilon(0.02));
    // Two octaves down, a 4th order run twice is far below -40 dB
    CHECK(gain_db(25., s) < -40.);

    s.m_order = 8;
    CHECK(gain_db(50., s) < gain_db(50., high_pass_settings{true, 100., 4}));

    s.m_enabled = false;
    CHECK(std::abs(gain_db(25., s)) < 1e-6);
}

TEST_CASE("the high-pass doesn't move sounds in time, and removes a DC offset without ringing")
{
    // A click on a DC offset
    std::vector<float> x(rate, 0.3f);
    const size_t click = rate / 2U;
    for(size_t i = click; i != click + 48U; ++i)
        x[i] += 0.5f *
            static_cast<float>(std::sin(2. * std::numbers::pi * 2000. * double(i - click) / rate));
    apply_high_pass(x, rate, high_pass_settings{true, 100., 4});

    const auto peak = std::ranges::max_element(x, {}, [](float v) { return std::abs(v); });
    const auto at = static_cast<size_t>(peak - x.begin());
    CHECK(at >= click);
    CHECK(at < click + 48U);
    // The offset is gone, including at the ends, where a pass starting from zero would ring
    CHECK(std::abs(x.front()) < 1e-3f);
    CHECK(std::abs(x.back()) < 1e-3f);
    CHECK(std::abs(x[rate / 4U]) < 1e-3f);
}

TEST_CASE("high-pass settings")
{
    CHECK(high_pass_settings::from_json(nullptr) == high_pass_settings{});
    const auto s = high_pass_settings::from_json({{"enabled", true}, {"cutoff_hz", 80}});
    CHECK(s.m_enabled);
    CHECK(s.m_cutoff_hz == 80.);
    CHECK(s.m_order == 4);
    CHECK(s.to_json() == nlohmann::json{{"enabled", true}, {"cutoff_hz", 80.}});
    CHECK(high_pass_settings{}.to_json().empty());
    CHECK_THROWS(high_pass_settings::from_json({{"order", 3}}));
    CHECK_THROWS(high_pass_settings::from_json({{"cutoff_hz", 0}}));

    // A cutoff near Nyquist does nothing rather than blow up
    auto x = sine(1000., 0.1);
    const auto before = x;
    apply_high_pass(x, rate, high_pass_settings{true, 23000., 4});
    CHECK(x == before);
}
