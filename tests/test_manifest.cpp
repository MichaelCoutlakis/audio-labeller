/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <sstream>

#include <doctest/doctest.h>

#include "manifest/clip_manifest.h"
#include "test_utils.h"

namespace fs = std::filesystem;
using nlohmann::json;

TEST_CASE("CSV records with quotes, commas and line breaks")
{
    std::istringstream in("\xEF\xBB\xBF" "a,b,c\r\n\"x, y\",\"say \"\"hi\"\"\",\"two\nlines\"\n,,\n");
    const auto records = read_csv(in);
    REQUIRE(records.size() == 3);
    CHECK(records[0] == std::vector<std::string>{"a", "b", "c"});
    CHECK(records[1] == std::vector<std::string>{"x, y", "say \"hi\"", "two\nlines"});
    CHECK(records[2] == std::vector<std::string>{"", "", ""});
}

TEST_CASE("clip verdicts edit the label at the onset in the source recording")
{
    temp_dir dir;
    write_text(
        dir / "pal.json",
        R"({"label-palette":[{"class-id":0,"class":"Dog","colour":"#ccb42aab"},{"class-id":1,"class":"Fox","colour":"#ccb42aab"}]})");
    write_text(
        dir / "proj.json",
        R"({"dataset-roots":["data"],"export-prefix":"out","label-palette":"pal.json","label-file":"l.json",)"
        R"("clip-manifest":{"path":"clips/manifest.csv","columns":{"onset":"onset"}}})");
    write_text(dir / "data/site/rec.wav", "");
    write_text(
        dir / "clips/manifest.csv",
        "clip_path,source_path,onset,label,split\n"
        "c1.wav,site/rec.wav,1.0,Dog,train\n"
        "c2.wav,site/rec.wav,2.0,Dog,test\n"
        "c3.wav,missing.wav,3.0,Fox,train\n");

    project_model m(project_config::parse_from_file((dir / "proj.json").string()));
    const auto settings = clip_manifest_settings::from_json(m.config().m_clip_manifest);
    REQUIRE(settings);
    const auto manifest = load_clip_manifest(m, *settings);
    REQUIRE(manifest.m_rows.size() == 3);
    CHECK(manifest.m_columns.back() == "split");
    CHECK(manifest.m_unresolved == 1);
    const auto rec = dir / "data/site/rec.wav";
    CHECK(manifest.m_rows[0].m_source == rec);
    CHECK(manifest.m_rows[0].m_clip == dir / "clips/c1.wav");
    CHECK(manifest.m_rows[1].m_onset_s == 2.);

    // The pipeline's label is there, a little off the clip's onset
    const label dog{get_next_label_id(), label_defn_id{0}, 1.01, 1.01, label_state::suggested};
    m.add_label(rec, dog);
    const auto dog_id = class_named(m.m_label_dict, "dog").value();

    CHECK(apply_clip_verdict(m, rec, {1., 1.5}, 0.05, clip_verdict::correct, dog_id) == dog.m_id);
    CHECK(m.get_labels(rec).find_ptr(dog.m_id)->m_state == label_state::confirmed);

    CHECK(
        apply_clip_verdict(m, rec, {1., 1.5}, 0.05, clip_verdict::relabel, label_defn_id{1}) ==
        dog.m_id);
    CHECK(m.get_labels(rec).find_ptr(dog.m_id)->m_defn_id == label_defn_id{1});

    CHECK(apply_clip_verdict(m, rec, {1., 1.5}, 0.05, clip_verdict::not_event, dog_id) == dog.m_id);
    CHECK(m.get_labels(rec).find_ptr(dog.m_id)->m_state == label_state::rejected);

    // No label at the second clip's onset, so the verdict makes one: a region, not a point
    const auto span = manifest.m_rows[1].span(settings->m_region_s);
    const auto made = apply_clip_verdict(m, rec, span, 0.05, clip_verdict::not_event, dog_id);
    const auto *l = m.get_labels(rec).find_ptr(made);
    REQUIRE(l);
    CHECK_FALSE(l->is_onset());
    CHECK(l->m_start_s == 2.);
    CHECK(l->m_stop_s == doctest::Approx(2. + settings->m_region_s));
    CHECK(l->m_state == label_state::rejected);
    CHECK(m.get_labels(rec).size() == 2);

    CHECK(find_label_at(m.get_labels(rec), 1.5, 0.05) == nullptr);
    CHECK(find_label_at(m.get_labels(rec), 2.5, 0.05) == m.get_labels(rec).find_ptr(made));

    auto bad = *settings;
    bad.m_onset_column = "nope";
    CHECK_THROWS(load_clip_manifest(m, bad));
}

TEST_CASE("a clip manifest's stop or duration column gives the span of a created label")
{
    temp_dir dir;
    write_text(
        dir / "pal.json",
        R"({"label-palette":[{"class-id":0,"class":"Dog","colour":"#ccb42aab"}]})");
    write_text(
        dir / "proj.json",
        R"({"dataset-roots":["data"],"export-prefix":"out","label-palette":"pal.json","label-file":"l.json",)"
        R"("clip-manifest":{"path":"m.csv","region_s":0.25,"columns":{"onset":"onset","duration":"len"}}})");
    write_text(dir / "data/rec.wav", "");
    write_text(
        dir / "m.csv",
        "clip_path,source_path,onset,label,len\n"
        "c1.wav,rec.wav,1.0,Dog,2.5\n"
        "c2.wav,rec.wav,5.0,Dog,\n"
        "c3.wav,rec.wav,7.0,Dog,-1\n");

    project_model m(project_config::parse_from_file((dir / "proj.json").string()));
    const auto settings = clip_manifest_settings::from_json(m.config().m_clip_manifest);
    REQUIRE(settings);
    CHECK(settings->m_region_s == 0.25);
    const auto manifest = load_clip_manifest(m, *settings);
    REQUIRE(manifest.m_rows.size() == 3);
    CHECK(manifest.m_rows[0].span(settings->m_region_s) == time_span{1., 3.5});
    // No duration, or one that makes no sense: the default length
    CHECK(manifest.m_rows[1].span(settings->m_region_s) == time_span{5., 5.25});
    CHECK(manifest.m_rows[2].span(settings->m_region_s) == time_span{7., 7.25});
}

TEST_CASE("a clip's label is the region that holds its onset")
{
    temp_dir dir;
    write_text(
        dir / "pal.json",
        R"({"label-palette":[{"class-id":0,"class":"Dog","colour":"#ccb42aab"},{"class-id":1,"class":"Fox","colour":"#ccb42aab"}]})");
    write_text(
        dir / "proj.json",
        R"({"dataset-roots":["data"],"export-prefix":"out","label-palette":"pal.json","label-file":"l.json"})");
    write_text(dir / "data/rec.wav", "");
    project_model m(project_config::parse_from_file((dir / "proj.json").string()));
    const auto rec = dir / "data/rec.wav";
    const label_defn_id dog{0}, fox{1};

    SUBCASE("a region that starts well before the onset")
    {
        // A long call whose sharpest onset, as a pipeline finds it, is seconds in
        const label call{get_next_label_id(), dog, 10., 18., label_state::suggested};
        m.add_label(rec, call);
        const auto &ls = m.get_labels(rec);
        CHECK(find_label_at(ls, 14.2, 0.05, dog) == ls.find_ptr(call.m_id));
        // Its edges, give or take the window
        CHECK(find_label_at(ls, 18.04, 0.05) != nullptr);
        CHECK(find_label_at(ls, 18.1, 0.05) == nullptr);
        CHECK(find_label_at(ls, 9.9, 0.05) == nullptr);

        CHECK(
            apply_clip_verdict(m, rec, {14.2, 15.2}, 0.05, clip_verdict::correct, dog) ==
            call.m_id);
        CHECK(m.get_labels(rec).size() == 1);
        const auto *l = m.get_labels(rec).find_ptr(call.m_id);
        CHECK(l->m_state == label_state::confirmed);
        // The verdict keeps the region's span
        CHECK(l->m_start_s == 10.);
        CHECK(l->m_stop_s == 18.);
    }

    SUBCASE("overlapping regions: the clip's class first, then the nearest start")
    {
        const label rain{get_next_label_id(), fox, 0., 60.};
        const label early{get_next_label_id(), dog, 20., 30., label_state::suggested};
        const label late{get_next_label_id(), dog, 24., 26., label_state::suggested};
        m.add_label(rec, rain);
        m.add_label(rec, early);
        m.add_label(rec, late);
        const auto &ls = m.get_labels(rec);

        CHECK(find_label_at(ls, 25., 0.05, dog) == ls.find_ptr(late.m_id));
        CHECK(find_label_at(ls, 22., 0.05, dog) == ls.find_ptr(early.m_id));
        CHECK(find_label_at(ls, 25., 0.05, fox) == ls.find_ptr(rain.m_id));
        // Without a class, the nearest start whatever its class
        CHECK(find_label_at(ls, 1., 0.05) == ls.find_ptr(rain.m_id));
        CHECK(find_label_at(ls, 25., 0.05) == ls.find_ptr(late.m_id));
        // Where no region of the class is, another class's region is still the label there
        CHECK(find_label_at(ls, 40., 0.05, dog) == ls.find_ptr(rain.m_id));

        // A relabel takes the one at the onset whatever its class
        CHECK(
            apply_clip_verdict(m, rec, {25., 26.}, 0.05, clip_verdict::relabel, fox) == late.m_id);
        CHECK(m.get_labels(rec).find_ptr(late.m_id)->m_defn_id == fox);
        CHECK(m.get_labels(rec).size() == 3);
    }
}
