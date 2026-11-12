/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <cstring>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "export/clip_export.h"
#include "io/audio_source.h"
#include "model/paths.h"
#include "model/project_model.h"
#include "test_utils.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
label make_label(label_id id, double start, double stop, uint32_t cls = 0)
{
    return label{id, label_defn_id{cls}, start, stop};
}

std::vector<json> read_jsonl(const fs::path &p)
{
    std::vector<json> rows;
    std::istringstream lines(read_text(p));
    for(std::string line; std::getline(lines, line);)
        rows.push_back(json::parse(line));
    return rows;
}
}

TEST_CASE("padded clips merge when they'd overlap")
{
    export_settings s; // Padded, 0.5 s
    const auto plans = plan_clips(
        {make_label(1, 1.0, 1.5), make_label(2, 2.2, 2.4), make_label(3, 6.0, 7.0)},
        10.,
        s);
    REQUIRE(plans.size() == 2);
    CHECK(plans[0].m_start_s == doctest::Approx(0.5));
    CHECK(plans[0].m_stop_s == doctest::Approx(2.9));
    REQUIRE(plans[0].m_events.size() == 2);
    CHECK(plans[0].m_events[1].m_onset_s == doctest::Approx(1.7));
    CHECK(plans[1].m_events.size() == 1);
}

TEST_CASE("padded clips stop merging at the maximum length and flag cut labels")
{
    export_settings s;
    s.m_max_clip_s = 2.;
    const auto plans = plan_clips(
        {make_label(1, 1.0, 1.5), make_label(2, 2.2, 2.4), make_label(3, 0.6, 3.0)},
        10.,
        s);
    REQUIRE(plans.size() >= 2);
    bool any_truncated = false;
    for(const auto &plan : plans)
        for(const auto &e : plan.m_events)
            any_truncated |= e.m_truncated;
    CHECK(any_truncated);
}

TEST_CASE("clips are clamped to the file")
{
    export_settings s;
    const auto plans = plan_clips({make_label(1, 0.1, 0.3), make_label(2, 9.8, 10.0)}, 10., s);
    REQUIRE(plans.size() == 2);
    CHECK(plans[0].m_start_s == 0.);
    CHECK(plans[1].m_stop_s == 10.);
}

TEST_CASE("fixed clips keep their length near the ends of the file")
{
    export_settings s;
    s.m_mode = export_settings::clip_mode::fixed;
    s.m_fixed_length_s = 4.;
    const auto plans = plan_clips(
        {make_label(1, 0.5, 1.0), make_label(2, 5.0, 5.5), make_label(3, 6.0, 12.0)},
        20.,
        s);
    REQUIRE(plans.size() == 3);
    CHECK(plans[0].m_start_s == 0.);
    CHECK(plans[0].m_stop_s == doctest::Approx(4.));
    CHECK(plans[1].m_stop_s - plans[1].m_start_s == doctest::Approx(4.)); // Not merged
    CHECK(plans[2].m_start_s == 6.);                                      // Longer than the window
    CHECK(plans[2].m_stop_s == 12.);
}

TEST_CASE("background clips avoid labels")
{
    export_settings s;
    s.m_background_per_file = 2;
    s.m_background_length_s = 2.;
    const auto plans = plan_background({make_label(1, 1.0, 3.0)}, 10., s);
    REQUIRE(plans.size() == 2);
    for(const auto &p : plans)
    {
        CHECK(p.m_events.empty());
        CHECK(p.m_start_s >= 3.5);
    }
    CHECK(plan_background({}, 10., export_settings{}).empty()); // Off by default
}

TEST_CASE("export_settings read from the project")
{
    const auto s = export_settings::from_json(
        json::parse(R"({"directory":"out","mode":"fixed","fixed_length_s":2})"));
    CHECK(s.m_directory == "out");
    CHECK(s.m_mode == export_settings::clip_mode::fixed);
    CHECK(s.m_fixed_length_s == 2.);
    CHECK(s.m_padding_s == 0.5); // Default kept
    CHECK_THROWS(export_settings::from_json(json::parse(R"({"mode":"wrong"})")));
}

TEST_CASE("export_dataset writes verified lossless clips and merges re-exports")
{
    temp_dir dir;
    write_text(
        dir / "pal.json",
        R"({"label-palette":[{"class-id":0,"class":"Dog","colour":"#ccb42aab"},{"class-id":1,"class":"Fox","colour":"#bd3a3aa6"}]})");
    write_text(
        dir / "proj.json",
        R"({"dataset-roots":["audio"],"export-prefix":"out","label-palette":"pal.json","label-file":"labels.json"})");

    // 10 s of stereo at 1 kHz, each sample unique so a clip's position can be checked
    std::vector<int16_t> samples;
    for(int i = 0; i != 10000; ++i)
    {
        samples.push_back(static_cast<int16_t>(i));
        samples.push_back(static_cast<int16_t>(-i));
    }
    write_wav16(dir / "audio/a.wav", 1000, 2, samples);
    write_wav16(dir / "audio/b.wav", 1000, 2, std::vector<int16_t>(2000, 1));
    const auto a = dir / "audio/a.wav";
    const auto b = dir / "audio/b.wav";
    const auto out = dir / "dataset";

    std::string a_id;
    {
        project_model m(project_config::parse_from_file((dir / "proj.json").string()));
        m.add_label(a, make_label(get_next_label_id(), 2.0, 3.0, 0));
        m.add_label(a, make_label(get_next_label_id(), 2.5, 2.8, 1));
        m.add_label(b, make_label(get_next_label_id(), 0.5, 1.0, 0));
        m.set_status(a, file_status::done); // b stays to do

        const auto summary = export_dataset(m, export_settings{});
        CHECK(summary.m_errors.empty());
        CHECK(summary.m_files == 1);
        CHECK(summary.m_clips == 1);
        CHECK(summary.m_unverified_clips == 0);
        CHECK(summary.m_todo_with_labels == 1);
        a_id = m.get_source_id(a);
        const auto *state = m.find_file_state(a);
        REQUIRE(state);
        REQUIRE(state->m_export);
        CHECK(state->m_export->m_verified);
    }

    const auto rows = read_jsonl(out / "metadata.jsonl");
    REQUIRE(rows.size() == 1);
    const auto &row = rows[0];
    CHECK(row["file_name"] == "clips/" + a_id + "_000001500.wav");
    CHECK(row["source_id"] == a_id);
    CHECK(row["source_offset_s"] == 1.5);
    CHECK(row["duration_s"] == 2.0);
    CHECK(row["channels"] == 2);
    REQUIRE(row["events"].size() == 2);
    CHECK(row["events"][0]["event_label"] == "Dog");
    CHECK(row["events"][0]["onset_s"] == 0.5);
    CHECK(row["events"][1]["event_label"] == "Fox");

    // Lossless: same format, and the samples are the source's from 1.5 s
    audio_source clip(out / path_from_utf8(row["file_name"].get<std::string>()));
    CHECK(clip.format().m_bits_per_sample == 16);
    CHECK(clip.format().m_frames == 2000);
    std::vector<std::byte> raw(4);
    clip.read_raw_frames(0, 1, raw.data());
    int16_t first{};
    std::memcpy(&first, raw.data(), 2);
    CHECK(first == 1500);

    CHECK(read_text(out / "labels.tsv").starts_with("filename\tonset\toffset\tevent_label\n"));
    CHECK(read_text(out / "labels.tsv").find("\t0.500000\t1.500000\tDog") != std::string::npos);
    CHECK(read_text(out / "sources.csv").find(a_id + ",audio,a.wav,") != std::string::npos);
    CHECK(json::parse(read_text(out / "dataset.json"))["classes"].size() == 2);

    // Delete the original of a, then export b: a's clips and entries must survive
    fs::remove(a);
    {
        project_model m(project_config::parse_from_file((dir / "proj.json").string()));
        m.set_status(b, file_status::done);
        const auto summary = export_dataset(m, export_settings{});
        CHECK(summary.m_files == 1);
    }
    CHECK(read_jsonl(out / "metadata.jsonl").size() == 2);
    CHECK(fs::exists(out / path_from_utf8(row["file_name"].get<std::string>())));
    CHECK(read_text(out / "sources.csv").find(a_id) != std::string::npos);

    // Changing b's labels and re-exporting replaces its clips rather than adding to them
    std::string old_b_clip;
    for(const auto &r : read_jsonl(out / "metadata.jsonl"))
        if(r["source_id"] != a_id)
            old_b_clip = r["file_name"];
    {
        project_model m(project_config::parse_from_file((dir / "proj.json").string()));
        auto l = *m.get_labels(b).begin();
        l.m_start_s = 1.2;
        l.m_stop_s = 1.4;
        m.update_label(b, l);
        export_dataset(m, export_settings{});
    }
    CHECK(read_jsonl(out / "metadata.jsonl").size() == 2);
    CHECK(!fs::exists(out / path_from_utf8(old_b_clip)));
}
