/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <sstream>

#include <doctest/doctest.h>

#include "io/files.h"
#include "manifest/clip_manifest.h"
#include "model/paths.h"
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

namespace
{
/// A project with two dataset roots holding a recording of the same name
struct two_roots
{
    temp_dir dir;
    project_model m{project_config{}};

    explicit two_roots(const std::string &clip_manifest = {})
    {
        write_text(
            dir / "pal.json",
            R"({"label-palette":[{"class-id":0,"class":"Dog","colour":"#ccb42aab"},{"class-id":1,"class":"Fox","colour":"#ccb42aab"}]})");
        write_text(
            dir / "proj.json",
            R"({"dataset-roots":["siteA","siteB"],"export-prefix":"out","label-palette":"pal.json","label-file":"l.json")" +
                clip_manifest + "}");
        write_text(dir / "siteA/rec.wav", "");
        write_text(dir / "siteB/rec.wav", "");
        m = project_model(project_config::parse_from_file((dir / "proj.json").string()));
    }
};
}

TEST_CASE("a review list in our own format")
{
    two_roots p;
    const auto list = p.dir / "runs/run1.csv";
    write_text(
        list,
        "id,path,root,start_s,stop_s,label,channel,score,reason\n"
        "a,rec.wav,siteB,1.0,1.5,Dog,2,0.3,missed\n"
        "b,rec.wav,,4.0,,Fox,,0.9,unsure\n"
        "c,gone.wav,,2.0,3.0,Dog,,0.1,missed\n"
        "d," + generic_utf8(p.dir / "siteB/rec.wav") + ",,6.0,7.0,,,,\n");

    // No clip-manifest section: the defaults are our own format
    const auto settings = clip_manifest_settings::for_list(p.m.config().m_clip_manifest, list);
    auto manifest = load_clip_manifest(p.m, settings);
    REQUIRE(manifest.m_rows.size() == 4);
    CHECK(manifest.m_path == list);
    CHECK(manifest.m_results_path == p.dir / "runs/run1.results.csv");
    CHECK(manifest.m_unresolved == 1);

    const auto &a = manifest.m_rows[0];
    CHECK(a.m_source == p.dir / "siteB/rec.wav"); // Its own root, not the first
    CHECK(a.m_channel == 1U);                      // From 1 in the list, from 0 here
    CHECK(a.m_stop_s == 1.5);
    CHECK_FALSE(a.m_clip); // No cut clips needed
    CHECK(manifest.m_rows[1].m_source == p.dir / "siteA/rec.wav"); // The first root that has it
    CHECK_FALSE(manifest.m_rows[2].m_source);
    CHECK(manifest.m_rows[3].m_source == p.dir / "siteB/rec.wav"); // An absolute path
    CHECK(manifest.m_rows[3].m_label.empty());

    SUBCASE("verdicts go to a results file beside the list, with every column")
    {
        manifest.m_rows[0].m_result = {"relabel", "Fox", "a fox, not a dog"};
        manifest.m_rows[2].m_result = {"not_event", "", ""};
        write_results(manifest);
        CHECK(
            read_text(manifest.m_results_path) ==
            "id,path,root,start_s,stop_s,label,channel,score,reason,verdict,new_label,note\n"
            "a,rec.wav,siteB,1.0,1.5,Dog,2,0.3,missed,relabel,Fox,\"a fox, not a dog\"\n"
            "b,rec.wav,,4.0,,Fox,,0.9,unsure,,,\n"
            "c,gone.wav,,2.0,3.0,Dog,,0.1,missed,not_event,,\n"
            "d," + csv_escape(generic_utf8(p.dir / "siteB/rec.wav")) + ",,6.0,7.0,,,,,,,\n");
        CHECK(read_text(list).find("verdict") == std::string::npos); // The list is left alone

        // Read back with the list, by ID, after the pipeline has written it again in another order
        write_text(
            list,
            "id,path,root,start_s,stop_s,label,channel,score,reason\n"
            "c,gone.wav,,2.0,3.0,Dog,,0.1,missed\n"
            "e,rec.wav,,9.0,9.5,Dog,,0.2,new\n"
            "a,rec.wav,siteB,1.0,1.5,Dog,2,0.3,missed\n");
        const auto again = load_clip_manifest(p.m, settings);
        REQUIRE(again.m_rows.size() == 3);
        CHECK(again.m_rows[0].m_result.m_verdict == "not_event");
        CHECK(again.m_rows[1].m_result == row_verdict{});
        CHECK(again.m_rows[2].m_result == row_verdict{"relabel", "Fox", "a fox, not a dog"});
        CHECK(again.heard() == 2U);
    }

    SUBCASE("without IDs, rows are matched by path, start and label")
    {
        write_text(list, "path,start_s,label\nrec.wav,1.0,Dog\nrec.wav,2.0,Dog\n");
        auto plain = load_clip_manifest(p.m, settings);
        plain.m_rows[1].m_result.m_verdict = "correct";
        write_results(plain);
        write_text(list, "path,start_s,label,score\nrec.wav,2.0,Dog,1\nrec.wav,1.0,Dog,1\n");
        const auto again = load_clip_manifest(p.m, settings);
        CHECK(again.m_rows[0].m_result.m_verdict == "correct");
        CHECK(again.m_rows[1].m_result.m_verdict.empty());
    }

    SUBCASE("a list fed back as its own results doesn't get two verdict columns")
    {
        write_text(list, "path,start_s,verdict\nrec.wav,1.0,correct\n");
        auto fed = load_clip_manifest(p.m, settings);
        fed.m_rows[0].m_result.m_verdict = "not_event";
        CHECK(results_csv(fed) == "path,start_s,verdict,new_label,note\nrec.wav,1.0,not_event,,\n");
    }
}

TEST_CASE("older clip manifests are still read")
{
    two_roots p(R"(,"clip-manifest":{"path":"m.csv","dataset-root":"siteB"})");
    write_text(
        p.dir / "m.csv",
        "clip_path,source_path,onset_s,label\n"
        "c1.wav,rec.wav,1.0,Dog\n");
    const auto settings = clip_manifest_settings::from_json(p.m.config().m_clip_manifest);
    REQUIRE(settings);
    const auto manifest = load_clip_manifest(p.m, *settings);
    REQUIRE(manifest.m_rows.size() == 1);
    CHECK(manifest.m_rows[0].m_onset_s == 1.);
    CHECK(manifest.m_rows[0].m_source == p.dir / "siteB/rec.wav");
    CHECK(manifest.m_rows[0].m_clip == p.dir / "c1.wav");

    // Another list opened in the same project keeps the section's columns and roots, with the
    // clips beside that list
    const auto other = p.dir / "other/list.csv";
    write_text(other, "clip_path,source_path,onset_s\nx.wav,rec.wav,3\n");
    const auto s2 = clip_manifest_settings::for_list(p.m.config().m_clip_manifest, other);
    const auto m2 = load_clip_manifest(p.m, s2);
    CHECK(m2.m_rows[0].m_clip == p.dir / "other/x.wav");
    CHECK(m2.m_rows[0].m_source == p.dir / "siteB/rec.wav");

    // A list without the required columns says which
    write_text(other, "clip_path,label\nx.wav,Dog\n");
    CHECK_THROWS_WITH(load_clip_manifest(p.m, s2), doctest::Contains("\"path\""));
}

TEST_CASE("the results file name")
{
    CHECK(results_path("a/run.csv") == fs::path{"a/run.results.csv"});
    CHECK(results_path("a/run") == fs::path{"a/run.results.csv"});
}
