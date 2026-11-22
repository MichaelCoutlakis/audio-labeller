/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <sstream>

#include <doctest/doctest.h>

#include "io/files.h"
#include "manifest/clip_manifest.h"
#include "manifest/review_import.h"
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

namespace
{
/// A project with two dataset roots holding a recording of the same name
struct two_roots
{
    temp_dir dir;
    project_model m{project_config{}};

    explicit two_roots(const std::string &extra = {}, bool scan = true)
    {
        write_text(
            dir / "pal.json",
            R"({"label-palette":[{"class-id":0,"class":"Dog","colour":"#ccb42aab"},{"class-id":1,"class":"Fox","colour":"#ccb42aab"}]})");
        write_text(
            dir / "proj.json",
            R"({"dataset-roots":["siteA","siteB"],"export-prefix":"out","label-palette":"pal.json","label-file":"l.json")" +
                std::string(scan ? "" : R"(,"scan-roots":false)") + extra + "}");
        write_text(dir / "siteA/rec.wav", "");
        write_text(dir / "siteB/rec.wav", "");
        open();
    }

    void open() { m = project_model(project_config::parse_from_file((dir / "proj.json").string())); }

    clip_manifest load(const fs::path &list)
    {
        return load_clip_manifest(m, clip_manifest_settings::for_list(m.config().m_clip_manifest, list));
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

    auto manifest = p.load(list);
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
    CHECK(manifest.m_rows[1].span(1.) == time_span{4., 5.});
    CHECK_FALSE(manifest.m_rows[2].m_source);
    CHECK(manifest.m_rows[3].m_source == p.dir / "siteB/rec.wav"); // An absolute path
    CHECK(manifest.m_rows[3].m_label.empty());

    // A list without the required columns says which
    write_text(list, "path,label\nrec.wav,Dog\n");
    CHECK_THROWS_WITH(p.load(list), doctest::Contains("\"start_s\""));
}

TEST_CASE("review results are a diff of the list")
{
    two_roots p;
    const auto list = p.dir / "runs/run1.csv";
    write_text(
        list,
        "id,path,start_s,stop_s,label,score\n"
        "a,rec.wav,1.0,1.5,Dog,0.3\n"
        "b,rec.wav,4.0,5.0,Fox,0.9\n"
        "c,rec.wav,6.0,7.0,Dog,0.1\n"
        "d,rec.wav,8.0,9.0,Dog,0.2\n");
    auto manifest = p.load(list);

    manifest.m_rows[0].m_result = {"relabel", "Fox", 1.25, std::nullopt, "a fox, not a dog"};
    manifest.m_rows[1].m_result = {"correct", "", std::nullopt, std::nullopt, ""};
    manifest.m_rows[2].m_result = {"correct", "", 6.1, 6.9, ""}; // Right class, moved
    // A second sound near the first, which the list didn't have
    const auto added = manifest.add_row(0U, {2.0, 2.5}, "Dog");
    CHECK(manifest.m_rows[added].m_added);
    CHECK(manifest.heard() == 4U);

    write_results(manifest, true);
    const std::string header = "id,path,start_s,stop_s,label,score,verdict,new_label,new_start_s,"
                               "new_stop_s,note\n";
    CHECK(
        read_text(manifest.m_results_path) ==
        header +
            "a,rec.wav,1.0,1.5,Dog,0.3,relabel,Fox,1.25,,\"a fox, not a dog\"\n"
            "b,rec.wav,4.0,5.0,Fox,0.9,correct,,,,\n"
            "c,rec.wav,6.0,7.0,Dog,0.1,correct,,6.1,6.9,\n"
            "d,rec.wav,8.0,9.0,Dog,0.2,,,,,\n" // Not heard
            ",rec.wav,2,2.5,Dog,,added,Dog,2,2.5,\n");
    // The changes alone: no unchanged or unheard rows
    CHECK(
        read_text(changes_path(list)) ==
        header +
            "a,rec.wav,1.0,1.5,Dog,0.3,relabel,Fox,1.25,,\"a fox, not a dog\"\n"
            "c,rec.wav,6.0,7.0,Dog,0.1,correct,,6.1,6.9,\n"
            ",rec.wav,2,2.5,Dog,,added,Dog,2,2.5,\n");
    CHECK(read_text(list).find("verdict") == std::string::npos); // The list is left alone

    // The sidecar says which list, as it was
    const auto sidecar = json::parse(read_text(results_sidecar_path(list)));
    CHECK(sidecar.at("list") == generic_utf8(list));
    CHECK(sidecar.at("list_hash") == content_hash(read_text(list)));
    CHECK(sidecar.contains("written"));
    const auto hash = content_hash(read_text(list));

    // Read back after the pipeline wrote the list again in another order, with a new row
    write_text(
        list,
        "id,path,start_s,stop_s,label,score\n"
        "c,rec.wav,6.0,7.0,Dog,0.1\n"
        "e,rec.wav,9.5,9.9,Dog,0.2\n"
        "a,rec.wav,1.0,1.5,Dog,0.3\n");
    // Results still being written for the list as it was read name that list, not the new one
    write_results(manifest);
    CHECK(json::parse(read_text(results_sidecar_path(list))).at("list_hash") == hash);
    const auto again = p.load(list);
    REQUIRE(again.m_rows.size() == 4); // Three of the list's, and the one added
    CHECK(again.m_rows[0].m_result.m_new_start_s == 6.1);
    CHECK(again.m_rows[0].reviewed_span(1.) == time_span{6.1, 6.9});
    CHECK(again.m_rows[1].m_result == row_verdict{});
    CHECK(again.m_rows[2].m_result.m_note == "a fox, not a dog");
    CHECK(again.m_rows[2].reviewed_label() == "Fox");
    const auto &back = again.m_rows[3];
    CHECK(back.m_added);
    CHECK(back.m_source == p.dir / "siteA/rec.wav");
    CHECK(back.reviewed_span(1.) == time_span{2.0, 2.5});

    SUBCASE("an added row taken back isn't written")
    {
        auto taken = again;
        taken.m_rows[3].m_result = {};
        CHECK_FALSE(taken.m_rows[3].present());
        CHECK(results_csv(taken).find("added") == std::string::npos);
    }
}

TEST_CASE("without IDs, results are matched by path, start and label")
{
    two_roots p;
    const auto list = p.dir / "l.csv";
    write_text(list, "path,start_s,label\nrec.wav,1.0,Dog\nrec.wav,2.0,Dog\n");
    auto plain = p.load(list);
    plain.m_rows[1].m_result.m_verdict = "correct";
    write_results(plain);
    write_text(list, "path,start_s,label,score\nrec.wav,2.0,Dog,1\nrec.wav,1.0,Dog,1\n");
    const auto again = p.load(list);
    CHECK(again.m_rows[0].m_result.m_verdict == "correct");
    CHECK(again.m_rows[1].m_result.m_verdict.empty());

    // A list fed back as its own results doesn't get two verdict columns
    write_text(list, "path,start_s,verdict\nrec.wav,1.0,correct\n");
    fs::remove(results_path(list));
    auto fed = p.load(list);
    fed.m_rows[0].m_result.m_verdict = "not_event";
    CHECK(
        results_csv(fed) ==
        "path,start_s,verdict,new_label,new_start_s,new_stop_s,note\nrec.wav,1.0,not_event,,,,\n");
}

TEST_CASE("a list's own column named like a result column is kept")
{
    two_roots p;
    const auto list = p.dir / "l.csv";
    write_text(list, "path,start_s,note\nrec.wav,1.0,from the model\n");
    auto manifest = p.load(list);
    manifest.m_rows[0].m_result = {"correct", "", std::nullopt, std::nullopt, "heard it"};
    write_results(manifest);
    CHECK(
        read_text(manifest.m_results_path) ==
        "path,start_s,list_note,verdict,new_label,new_start_s,new_stop_s,note\n"
        "rec.wav,1.0,\"from the model\",correct,,,,\"heard it\"\n");
    const auto again = p.load(list);
    CHECK(again.m_rows[0].m_fields[2] == "from the model");
    CHECK(again.m_rows[0].m_result.m_note == "heard it");
}

TEST_CASE("a clip carries where it starts in its recording")
{
    two_roots p;
    const auto list = p.dir / "l.csv";
    // A second of audio at 1 kHz
    write_wav16(p.dir / "clips/c1.wav", 1000U, 1U, std::vector<int16_t>(1000U));
    write_wav16(p.dir / "clips/c2.wav", 1000U, 1U, std::vector<int16_t>(1000U));

    write_text(
        list,
        "path,start_s,stop_s,label,clip_path,clip_offset_s\n"
        "elsewhere.wav,120.3,120.6,Dog,clips/c1.wav,120.0\n"
        // Cut near the recording's start, with padding before it
        "elsewhere.wav,0.1,0.4,Dog,clips/c2.wav,-0.5\n"
        "elsewhere.wav,120.8,121.4,Dog,clips/c1.wav,120.0\n"
        "rec.wav,1.0,1.5,Dog,,\n"); // Its recording is there, so no clip is needed
    const auto manifest = p.load(list);
    REQUIRE(manifest.m_rows.size() == 4);
    const auto &c1 = manifest.m_rows[0];
    CHECK(c1.on_clip());
    CHECK(c1.m_clip == p.dir / "clips/c1.wav");
    CHECK(c1.m_clip_offset_s == 120.);
    CHECK(c1.m_clip_duration_s == doctest::Approx(1.));
    CHECK_FALSE(c1.partly_outside_clip(1.));
    CHECK(manifest.m_rows[1].m_clip_offset_s == -0.5);
    CHECK_FALSE(manifest.m_rows[1].partly_outside_clip(1.));
    CHECK(manifest.m_rows[2].partly_outside_clip(1.)); // Runs past the clip's end
    CHECK_FALSE(manifest.m_rows[3].on_clip());

    // A clip without its offset isn't opened, and the error says why
    write_text(list, "path,start_s,clip_path\nx.wav,1,clips/c1.wav\n");
    CHECK_THROWS_WITH(p.load(list), doctest::Contains("\"clip_offset_s\""));
    write_text(list, "path,start_s,clip_path,clip_offset_s\nx.wav,1,clips/c1.wav,\n");
    CHECK_THROWS_WITH(p.load(list), doctest::Contains("Row 2"));
    // Other names, through the project's columns
    write_text(
        p.dir / "proj.json",
        R"({"dataset-roots":["siteA"],"export-prefix":"out","label-palette":"pal.json","label-file":"l.json",)"
        R"("clip-manifest":{"columns":{"clip_offset":"source_offset_s"}}})");
    p.open();
    write_text(list, "path,start_s,clip_path,source_offset_s\nx.wav,1,clips/c1.wav,0.5\n");
    CHECK(p.load(list).m_rows[0].m_clip_offset_s == 0.5);
}

TEST_CASE("a row's label is the region that holds its start")
{
    temp_dir dir;
    const label_defn_id dog{0}, fox{1};
    labels ls;
    // A long call whose sharpest onset, as a pipeline finds it, is seconds in
    const label call{get_next_label_id(), dog, 10., 18., label_state::suggested};
    ls.add(call);
    CHECK(find_label_at(ls, 14.2, 0.05, dog) == ls.find_ptr(call.m_id));
    // Its edges, give or take the window
    CHECK(find_label_at(ls, 18.04, 0.05) != nullptr);
    CHECK(find_label_at(ls, 18.1, 0.05) == nullptr);
    CHECK(find_label_at(ls, 9.9, 0.05) == nullptr);

    const label rain{get_next_label_id(), fox, 0., 60.};
    const label late{get_next_label_id(), dog, 14., 16., label_state::suggested};
    ls.add(rain);
    ls.add(late);
    CHECK(find_label_at(ls, 15., 0.05, dog) == ls.find_ptr(late.m_id));
    CHECK(find_label_at(ls, 12., 0.05, dog) == ls.find_ptr(call.m_id));
    CHECK(find_label_at(ls, 15., 0.05, fox) == ls.find_ptr(rain.m_id));
    CHECK(find_label_at(ls, 1., 0.05) == ls.find_ptr(rain.m_id));
}

TEST_CASE("importing a review's edits into the project's labels")
{
    two_roots p;
    const auto rec = p.dir / "siteA/rec.wav";
    const label_defn_id dog{0}, fox{1};
    // The project's own labels in one recording, as a mixed list draws on others too
    const label dog_call{get_next_label_id(), dog, 10., 18., label_state::suggested};
    const label rain{get_next_label_id(), fox, 0., 60.};
    const label bark{get_next_label_id(), dog, 30., 31.};
    p.m.add_label(rec, dog_call);
    p.m.add_label(rec, rain);
    p.m.add_label(rec, bark);

    const auto results = p.dir / "run.results.csv";
    write_text(
        results,
        "id,path,start_s,stop_s,label,verdict,new_label,new_start_s,new_stop_s,note\n"
        "a,rec.wav,14.2,15,Dog,correct,,,,\n"           // Confirms the call that holds it
        "b,rec.wav,30.2,30.8,Dog,relabel,Fox,30.1,,\n"  // Relabels and moves the bark
        "c,rec.wav,45,46,Dog,not_event,,,,\n"           // Only a Fox (rain) there: no change
        "d,rec.wav,50,51,Dog,correct,,,,\n"             // No Dog there: made from its span
        "e,rec.wav,52,,Dog,correct,,,,\n"               // No Dog there and no stop
        "f,gone.wav,1,2,Dog,correct,,,,\n"              // Recording not in the project
        "g,rec.wav,5,6,Owl,correct,,,,\n"               // Class not in the palette
        ",rec.wav,40,41,Dog,added,Dog,40,41,\n"         // Added by the reviewer
        "h,rec.wav,55,56,Dog,,,,,\n");                  // Not heard
    const auto settings = clip_manifest_settings::for_list({}, results);
    auto plan = plan_review_import(p.m, load_results_file(p.m, settings, results), 0.05);

    CHECK(plan.m_changed == 2U); // Confirmed, relabelled
    CHECK(plan.m_added == 2U);   // Made from a span, added
    CHECK(plan.m_rejected == 0U);
    CHECK(plan.m_unchanged == 1U); // Nothing of its class to reject
    CHECK(plan.unapplied() == 3U);
    CHECK(plan.m_unapplied.at("its recording isn't in the project") == 1U);
    CHECK(plan.m_unapplied.at("its class isn't in the palette") == 1U);
    CHECK(plan.m_unapplied.at("no label of its class there, and no stop to make one") == 1U);
    CHECK(plan.m_unknown_classes == std::set<std::string>{"Owl"});
    REQUIRE(plan.m_files.size() == 1U);

    const auto &after = plan.m_files.at(rec);
    CHECK(after.find_ptr(dog_call.m_id)->m_state == label_state::confirmed);
    CHECK(after.find_ptr(dog_call.m_id)->m_start_s == 10.); // Its span is kept
    CHECK(after.find_ptr(rain.m_id)->m_defn_id == fox);    // Another class's label is left alone
    CHECK(after.find_ptr(rain.m_id)->m_state == label_state::confirmed);
    const auto *moved = after.find_ptr(bark.m_id);
    CHECK(moved->m_defn_id == fox);
    CHECK(moved->m_start_s == 30.1);
    CHECK(moved->m_stop_s == 31.);
    CHECK(after.size() == 5U);
    CHECK(p.m.get_labels(rec).size() == 3U); // Planning changes nothing

    // Applied, then planned again: nothing more to do, the relabelled bark included
    p.m.set_labels(rec, after);
    plan = plan_review_import(p.m, load_results_file(p.m, settings, results), 0.05);
    CHECK(plan.m_files.empty());
    CHECK(plan.m_changed == 0U);
    CHECK(plan.m_added == 0U);
    CHECK(plan.m_unchanged == 5U);

    // Not an event rejects the label of its class
    write_text(
        results,
        "path,start_s,label,verdict\n"
        "rec.wav,30.5,Fox,not_event\n");
    plan = plan_review_import(p.m, load_results_file(p.m, settings, results), 0.05);
    CHECK(plan.m_rejected == 1U);
    CHECK(plan.m_files.at(rec).find_ptr(bark.m_id)->m_state == label_state::rejected);

    // Classes the palette lacks can be added to it, so their rows apply
    p.m.add_palette_classes({"Owl"});
    CHECK(class_named(p.m.m_label_dict, "Owl"));
    CHECK(read_text(p.dir / "pal.json").find("\"Owl\"") != std::string::npos);
    p.open();
    CHECK(class_named(p.m.m_label_dict, "Owl"));
}

TEST_CASE("a project that doesn't scan its roots lists the recordings named")
{
    two_roots p({}, false);
    CHECK(p.m.get_files().empty());

    // A review list's rows are found on disk, under the roots
    const auto list = p.dir / "l.csv";
    write_text(list, "path,root,start_s\nrec.wav,siteB,1\nmissing.wav,,1\n");
    const auto manifest = p.load(list);
    CHECK(manifest.m_rows[0].m_source == p.dir / "siteB/rec.wav");
    CHECK_FALSE(manifest.m_rows[1].m_source);

    p.m.add_files({*manifest.m_rows[0].m_source});
    REQUIRE(p.m.get_files().size() == 1U);
    CHECK(p.m.get_files()[0].m_display_name == "rec.wav");
    CHECK(p.m.get_files()[0].m_search_text == "siteB/rec.wav");

    // The label file names recordings too, so they're listed when it's opened again
    p.m.add_label(p.dir / "siteB/rec.wav", label{get_next_label_id(), label_defn_id{0}, 1., 2.});
    p.m.save_if_dirty();
    p.open();
    REQUIRE(p.m.get_files().size() == 1U);
    CHECK(p.m.get_labels(p.dir / "siteB/rec.wav").size() == 1U);
}

TEST_CASE("a missing column says whether the project renamed it")
{
    two_roots p(R"(,"clip-manifest":{"columns":{"source":"recording"}})");
    const auto list = p.dir / "l.csv";
    write_text(list, "path,start_s\nrec.wav,1\n");
    try
    {
        p.load(list);
        FAIL("opened a list without its renamed column");
    }
    catch(const std::exception &e)
    {
        const std::string message = e.what();
        CHECK(message.find("no \"recording\" column") != std::string::npos);
        CHECK(message.find("renames the \"path\" column to \"recording\"") != std::string::npos);
        CHECK(message.find("columns are: path, start_s") != std::string::npos);
    }

    // A column not renamed only says what the list lacks
    write_text(list, "recording,begin\nrec.wav,1\n");
    try
    {
        p.load(list);
        FAIL("opened a list without a start");
    }
    catch(const std::exception &e)
    {
        const std::string message = e.what();
        CHECK(message.find("no \"start_s\" column") != std::string::npos);
        CHECK(message.find("renames") == std::string::npos);
    }
}

TEST_CASE("a project file's mistakes say which key and what it's for")
{
    temp_dir dir;
    write_text(dir / "proj.json", R"({"dataset-roots":["data"],"export-prefix":"out","label-file":"l.json"})");
    CHECK_THROWS_WITH(
        project_config::parse_from_file(dir / "proj.json"),
        doctest::Contains("has no \"label-palette\": the path of the palette file"));
    write_text(dir / "proj.json", R"({"dataset-roots":"data","export-prefix":"out","label-palette":"p.json","label-file":"l.json"})");
    CHECK_THROWS_WITH(
        project_config::parse_from_file(dir / "proj.json"),
        doctest::Contains("\"dataset-roots\" should be a list of the folders of audio to label, not \"data\""));
    write_text(dir / "proj.json", "{ not json");
    CHECK_THROWS_WITH(project_config::parse_from_file(dir / "proj.json"), doctest::Contains("isn't valid JSON"));
}
