/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <cmath>
#include <numbers>

#include <doctest/doctest.h>

#include "dsp/onset_snap.h"
#include "dsp/spectrogram.h"
#include "review/review.h"
#include "test_utils.h"

namespace fs = std::filesystem;

namespace
{
/// Silence, then a tone from onset_s
audio_buffer tone_burst(uint32_t rate, double length_s, double onset_s, double hz, float amp = 0.5f)
{
    audio_buffer b;
    b.m_sample_rate_hz = rate;
    b.m_samples.resize(static_cast<size_t>(length_s * rate));
    for(size_t n = 0U; n != b.m_samples.size(); ++n)
    {
        const double t = double(n) / rate;
        if(t >= onset_s)
            b.m_samples[n] = amp * float(std::sin(2. * std::numbers::pi * hz * t));
    }
    return b;
}

struct review_project
{
    temp_dir dir;
    review_project()
    {
        write_text(
            dir / "pal.json",
            R"({"label-palette":[{"class-id":0,"class":"Dog","colour":"#ccb42aab"}]})");
        write_text(
            dir / "proj.json",
            R"({"dataset-roots":["d"],"export-prefix":"out","label-palette":"pal.json","label-file":"l.json"})");
        for(auto name : {"d/a.wav", "d/b.wav", "d/c.wav"})
            write_text(dir / name, "");
    }
    project_config config() const
    {
        return project_config::parse_from_file((dir / "proj.json").string());
    }
};

label suggestion(double t, std::optional<float> score = std::nullopt)
{
    return label{get_next_label_id(), label_defn_id{0}, t, t, label_state::suggested, score};
}
}

TEST_CASE("playback windows give onsets context and regions pre-roll")
{
    review_settings s;
    s.m_pre_roll_s = 0.2;
    const auto onset = playback_window(label{1, label_defn_id{0}, 1., 1.}, s, 10.);
    CHECK(onset.first == doctest::Approx(0.9));
    CHECK(onset.second == doctest::Approx(1.5));
    const auto region = playback_window(label{1, label_defn_id{0}, 0.1, 2.}, s, 10.);
    CHECK(region.first == 0.); // Clamped to the file
    CHECK(region.second == 2.);

    const auto view = review_view(label{1, label_defn_id{0}, 1., 1.}, s);
    CHECK(view.first == doctest::Approx(0.9));
    CHECK(view.second == doctest::Approx(1.65));
}

TEST_CASE("the review queue runs across files, by score if there is one")
{
    review_project p;
    project_model m(p.config());
    const auto a = p.dir / "d/a.wav", b = p.dir / "d/b.wav", c = p.dir / "d/c.wav";
    const auto a2 = suggestion(2.), a1 = suggestion(1.), b1 = suggestion(1.);
    m.add_label(a, a2);
    m.add_label(a, a1);
    m.add_label(b, b1);
    m.add_label(c, suggestion(1.));
    m.add_label(a, label{get_next_label_id(), label_defn_id{0}, 0.5, 0.5}); // Confirmed
    m.set_status(c, file_status::skip);

    review_settings s;
    auto queue = review_queue(m, s);
    REQUIRE(queue.size() == 3);
    CHECK(queue[0].m_id == a1.m_id);
    CHECK(queue[1].m_id == a2.m_id);
    CHECK(queue[2].m_id == b1.m_id);

    // Stepping from an item that's gone (accepted) continues from where it was, and wraps
    auto gone = queue[1];
    queue.erase(queue.begin() + 1);
    CHECK(step_review(queue, gone, true, s)->m_id == b1.m_id);
    CHECK(step_review(queue, gone, false, s)->m_id == a1.m_id);
    CHECK(step_review(queue, queue.back(), true, s)->m_id == a1.m_id);
    CHECK(step_review(queue, std::nullopt, false, s)->m_id == b1.m_id);

    const auto doubtful = suggestion(3., 0.1f);
    m.add_label(b, doubtful);
    queue = review_queue(m, s);
    CHECK(queue.front().m_id == doubtful.m_id);
    s.m_by_score = false;
    CHECK(review_queue(m, s).back().m_id == doubtful.m_id);
}

TEST_CASE("onsets snap to the sharpest rise nearby")
{
    const auto audio = tone_burst(48000, 1., 0.5, 2000.);
    CHECK(snap_onset(audio, 0.503, 0.005) == doctest::Approx(0.5).epsilon(0.0005));
    CHECK(snap_onset(audio, 0.4975, 0.005) == doctest::Approx(0.5).epsilon(0.0005));
    // Nothing to snap to in silence, or a steady tone
    CHECK(snap_onset(audio, 0.2, 0.005) == 0.2);
    CHECK(snap_onset(audio, 0.8, 0.005) == 0.8);
}

TEST_CASE("FFT and spectrogram find a tone")
{
    std::vector<std::complex<float>> data(8, {0.f, 0.f});
    data[0] = {1.f, 0.f};
    fft(data);
    for(const auto &v : data)
        CHECK(std::abs(v) == doctest::Approx(1.f));

    const auto audio = tone_burst(16000, 1., 0., 1000., 1.f);
    spectrogram_settings s;
    s.m_scale = frequency_scale::linear;
    const auto image = compute_spectrogram(audio, 0.2, 0.8, 4, 80, s);
    REQUIRE(image.m_db.size() == 320);
    // 1 kHz of 8 kHz is row 10 of 80
    size_t loudest = 0U;
    for(size_t r = 0U; r != image.m_rows; ++r)
        if(image.at(r, 1) > image.at(loudest, 1))
            loudest = r;
    CHECK(loudest >= 9U);
    CHECK(loudest <= 10U);
    CHECK(std::abs(image.at(loudest, 1)) < 1.f); // Full scale is 0 dB
    CHECK(image.at(60, 1) < -60.f);

    CHECK(row_frequency(0., 10, 20., 8000., frequency_scale::mel) == doctest::Approx(20.));
    CHECK(row_frequency(10., 10, 20., 8000., frequency_scale::log) == doctest::Approx(8000.));
}
