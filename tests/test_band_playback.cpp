/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <doctest/doctest.h>

#include <cmath>
#include <numbers>
#include <vector>

#include "dsp/band_playback.h"
#include "dsp/high_pass.h"

namespace
{
constexpr uint32_t rate = 48000U;

audio_buffer tones(std::initializer_list<double> hz, double seconds)
{
    audio_buffer a;
    a.m_sample_rate_hz = rate;
    a.m_samples.assign(static_cast<size_t>(seconds * rate), 0.f);
    for(const double f : hz)
        for(size_t i = 0U; i != a.m_samples.size(); ++i)
            a.m_samples[i] += 0.2f *
                static_cast<float>(std::sin(2. * std::numbers::pi * f * double(i) / rate));
    return a;
}

/// Amplitude of one frequency in the middle half, by correlation with a sine and cosine
double level_at(const std::vector<float> &x, double hz)
{
    double re = 0., im = 0.;
    const size_t a = x.size() / 4U, b = 3U * x.size() / 4U;
    for(size_t i = a; i != b; ++i)
    {
        const double w = 2. * std::numbers::pi * hz * double(i) / rate;
        re += x[i] * std::cos(w);
        im += x[i] * std::sin(w);
    }
    return 2. * std::hypot(re, im) / double(b - a);
}

double db(double ratio) { return 20. * std::log10(ratio); }
}

TEST_CASE("the low-pass passes below the cutoff and removes what's above")
{
    auto a = tones({500., 8000.}, 1.);
    filter_zero_phase(a.m_samples, butterworth_low_pass(2000., rate, 6));
    CHECK(db(level_at(a.m_samples, 500.) / 0.2) > -0.5);
    CHECK(db(level_at(a.m_samples, 8000.) / 0.2) < -60.);
}

TEST_CASE("a band plans the filters it needs")
{
    const band_playback_settings plain;
    CHECK(!plan_band({}, rate, plain).filters()); // Every frequency: as recorded

    const auto both = plan_band({6000., 20000.}, rate, plain);
    CHECK(both.m_high_pass_hz == 6000.);
    CHECK(both.m_low_pass_hz == 20000.);

    // Reaching 0 Hz or Nyquist leaves out that filter
    const auto low = plan_band({0., 3000.}, rate, plain);
    CHECK(!low.m_high_pass_hz);
    CHECK(low.m_low_pass_hz == 3000.);
    const auto high = plan_band({10000., 24000.}, rate, plain);
    CHECK(high.m_high_pass_hz == 10000.);
    CHECK(!high.m_low_pass_hz);
    CHECK(high.m_shift_hz == 0.);

    band_playback_settings shift;
    shift.m_shift = true;
    shift.m_shift_to_hz = 1000.;
    CHECK(plan_band({8000., 9000.}, rate, shift).m_shift_hz == doctest::Approx(7000.));
    CHECK(plan_band({500., 900.}, rate, shift).m_shift_hz == 0.); // Never up
    CHECK(plan_band({0., 9000.}, rate, shift).m_shift_hz == 0.);  // Nothing to move
}

TEST_CASE("only the band of the selection is rendered")
{
    const auto a = tones({300., 8500., 15000.}, 3.);
    const auto e = render_band(a, {1., 2.}, {8000., 9000.}, {});
    CHECK(e.m_start_s == doctest::Approx(1.));
    CHECK(e.m_audio.m_samples.size() == rate); // Just the selection, not the recording
    CHECK(e.m_audio.m_sample_rate_hz == rate);
    CHECK(db(level_at(e.m_audio.m_samples, 8500.) / 0.2) > -1.);
    CHECK(db(level_at(e.m_audio.m_samples, 300.) / 0.2) < -60.);
    CHECK(db(level_at(e.m_audio.m_samples, 15000.) / 0.2) < -60.);
    // Faded in and out
    CHECK(e.m_audio.m_samples.front() == 0.f);
    CHECK(std::abs(e.m_audio.m_samples.back()) < 1e-3f);
}

TEST_CASE("a high band can be shifted down to where it's heard")
{
    const auto a = tones({300., 10000.}, 2.);
    band_playback_settings s;
    s.m_shift = true;
    s.m_shift_to_hz = 1000.;
    // 9 to 11 kHz moves to 1 to 3 kHz, so the 10 kHz tone is heard at 2 kHz
    const auto e = render_band(a, {0.5, 1.5}, {9000., 11000.}, s);
    CHECK(db(level_at(e.m_audio.m_samples, 2000.) / 0.2) > -1.);
    CHECK(db(level_at(e.m_audio.m_samples, 10000.) / 0.2) < -50.);
    CHECK(db(level_at(e.m_audio.m_samples, 300.) / 0.2) < -50.);
    // Not mirrored: the image of the shift would be at 18 kHz - 10 kHz below the centre
    CHECK(db(level_at(e.m_audio.m_samples, 1000.) / 0.2) < -40.);
}

TEST_CASE("a wide band keeps its middle and loses what's outside")
{
    const auto a = tones({200., 3000., 12000.}, 2.);
    const auto e = render_band(a, {0.5, 1.5}, {1000., 6000.}, {});
    CHECK(db(level_at(e.m_audio.m_samples, 3000.) / 0.2) > -0.5);
    CHECK(db(level_at(e.m_audio.m_samples, 200.) / 0.2) < -60.);
    CHECK(db(level_at(e.m_audio.m_samples, 12000.) / 0.2) < -60.);

    // Reaching 0 Hz: a low-pass alone
    const auto low = render_band(a, {0.5, 1.5}, {0., 1000.}, {});
    CHECK(db(level_at(low.m_audio.m_samples, 200.) / 0.2) > -0.5);
    CHECK(db(level_at(low.m_audio.m_samples, 3000.) / 0.2) < -50.);
}
