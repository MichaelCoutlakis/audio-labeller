/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <chrono>
#include <cmath>
#include <numbers>

#include <doctest/doctest.h>

#include "assist/assisted_labeller.h"
#include "dsp/loudness.h"
#include "io/process.h"
#include "model/paths.h"
#include "test_utils.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
/// Quiet noise with tone bursts at the given times
std::vector<int16_t> bursts(uint32_t rate, double length_s, std::initializer_list<double> starts, double burst_s)
{
    std::vector<int16_t> samples(static_cast<size_t>(rate * length_s));
    uint32_t seed = 1U;
    for(size_t n = 0U; n != samples.size(); ++n)
    {
        seed = seed * 1664525U + 1013904223U;
        double x = 0.001 * (double(seed >> 16) / 32768. - 1.);
        const double t = double(n) / rate;
        for(double s : starts)
            if(t >= s && t < s + burst_s)
                x += 0.5 * std::sin(2. * std::numbers::pi * 2000. * t);
        samples[n] = static_cast<int16_t>(x * 32767.);
    }
    return samples;
}

/// A command that copies a file to {output}, in the platform's shell
std::vector<std::string> copy_command(const fs::path &from)
{
#ifdef _WIN32
    return {"cmd.exe", "/d", "/c", "copy", "/y", native_utf8(from), "{output}"};
#else
    return {"cp", native_utf8(from), "{output}"};
#endif
}
}

TEST_CASE("processes run without a shell, with exit codes, timeouts and output")
{
    temp_dir dir;
    const auto out = dir / "out.txt";
#ifdef _WIN32
    const std::vector<std::string> fail = {"cmd.exe", "/d", "/c", "exit 3"};
    const std::vector<std::string> slow = {"ping", "-n", "20", "127.0.0.1"};
    const auto echo = shell_command("echo a b \"c d\"");
#else
    const std::vector<std::string> fail = {"/bin/sh", "-c", "exit 3"};
    const std::vector<std::string> slow = {"sleep", "20"};
    const auto echo = shell_command("echo a b \"c d\"");
#endif
    CHECK(run_process(fail, dir.path(), out, out).m_exit_code == 3);

    const auto r = run_process(echo, dir.path(), out, out);
    CHECK(r.ok());
#ifdef _WIN32
    CHECK(read_text(out).starts_with("a b \"c d\"")); // cmd's echo keeps quotes
#else
    CHECK(read_text(out).starts_with("a b c d"));
#endif

    const auto start = std::chrono::steady_clock::now();
    const auto timed = run_process(slow, dir.path(), out, out, nullptr, 0.3);
    CHECK(timed.m_timed_out);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(10));

    std::atomic<bool> cancel{true};
    CHECK(run_process(slow, dir.path(), out, out, &cancel).m_cancelled);

    CHECK_THROWS(run_process({"audio_labeller_no_such_program"}, dir.path(), out, out));
}

TEST_CASE("loud regions stand out from the noise floor")
{
    audio_buffer audio;
    audio.m_sample_rate_hz = 16000;
    for(auto s : bursts(16000, 5., {1., 1.15, 3.}, 0.1))
        audio.m_samples.push_back(s / 32768.f);

    loudness_settings s;
    const auto regions = find_loud_regions(audio, s);
    REQUIRE(regions.size() == 2); // The first two are joined across a 50 ms gap
    CHECK(regions[0].m_start_s == doctest::Approx(0.99).epsilon(0.02));
    CHECK(regions[0].m_stop_s == doctest::Approx(1.26).epsilon(0.02));
    CHECK(regions[1].m_start_s == doctest::Approx(2.99).epsilon(0.02));
    CHECK(regions[1].m_above_db > 20.);

    s.m_onsets = true;
    s.m_merge_gap_s = 0.;
    const auto onsets = find_loud_regions(audio, s);
    REQUIRE(onsets.size() == 3);
    CHECK(onsets[1].m_start_s == doctest::Approx(1.15).epsilon(0.002));
    CHECK(onsets[1].m_stop_s == onsets[1].m_start_s);

    // Silence has nothing above the absolute minimum
    audio_buffer silent;
    silent.m_sample_rate_hz = 16000;
    silent.m_samples.assign(16000, 0.f);
    CHECK(find_loud_regions(silent, loudness_settings{}).empty());
}

TEST_CASE("assisted labellers suggest labels for review")
{
    temp_dir dir;
    write_text(
        dir / "pal.json",
        R"({"label-palette":[{"class-id":0,"class":"Bird","colour":"#ccb42aab"}]})");
    write_wav16(dir / "data/a.wav", 16000, 1, bursts(16000, 3., {1.}, 0.2));
    write_wav16(dir / "data/b.wav", 16000, 1, bursts(16000, 3., {2.}, 0.2));
    write_text(dir / "fixture.txt", "0.5\t0.5\tBird\t0.3\n1\t2\tx:Bird\n");

    json labellers = json::array();
    labellers.push_back({{"name", "Detector"}, {"command", copy_command(dir / "fixture.txt")}});
    labellers.push_back({{"name", "Broken"}, {"command", json::array({"audio_labeller_no_such_program"})}});
    write_text(
        dir / "proj.json",
        json{
            {"dataset-roots", {"data"}},
            {"export-prefix", "out"},
            {"label-palette", "pal.json"},
            {"label-file", "l.json"},
            {"assisted-labellers", labellers}
        }
            .dump());

    project_model m(project_config::parse_from_file(dir / "proj.json"));
    const auto list = assisted_labeller_settings::list_from_json(m.config().m_assisted_labellers);
    REQUIRE(list.size() == 3);
    CHECK(list[2].m_kind == assisted_labeller_settings::kind::loudness); // Built in, listed last
    CHECK(!list[0].m_from_stdout);

    const auto a = dir / "data/a.wav", b = dir / "data/b.wav";

    // An external detector, on one file
    auto job = prepare_assist(m, list[0], assist_scope::active_file, a, "");
    REQUIRE(job.m_inputs.size() == 1);
    auto outcome = run_assist(job);
    CHECK(outcome.m_errors.empty());
    auto summary = apply_assist(m, outcome);
    CHECK(summary.m_added == 2);
    REQUIRE(m.get_labels(a).size() == 2);
    CHECK(m.get_labels(a).begin()->m_state == label_state::suggested);
    CHECK(m.get_labels(a).begin()->m_score.value() == doctest::Approx(0.3));

    // The built-in, on the files to do that have no suggestions yet: only b
    job = prepare_assist(m, list[2], assist_scope::todo_files, std::nullopt, "Bird");
    REQUIRE(job.m_inputs.size() == 1);
    CHECK(job.m_inputs[0].m_audio == b);
    assist_progress progress;
    outcome = run_assist(job, &progress);
    CHECK(progress.m_done == 1);
    summary = apply_assist(m, outcome);
    CHECK(summary.m_added == 1);
    CHECK(m.get_labels(b).begin()->m_start_s == doctest::Approx(1.98).epsilon(0.02));
    CHECK(apply_assist(m, outcome).m_duplicates == 1); // Running again adds nothing twice

    // Failures are reported per file
    job = prepare_assist(m, list[1], assist_scope::all_files, std::nullopt, "");
    outcome = run_assist(job);
    CHECK(outcome.m_errors.size() == 2);
    CHECK(outcome.m_suggestions.empty());

    // Placeholders
    suggest_input in{a, {"data", "a.wav"}, 1U, "Bird", dir.path(), {}};
    const auto args = expand_command({"x", "--in={audio}", "{relative}", "{channel}", "{class}"}, in, "o.txt");
    CHECK(args[1] == "--in=" + native_utf8(a));
    CHECK(args[2] == "a.wav");
    CHECK(args[3] == "2");
    CHECK(args[4] == "Bird");
}
