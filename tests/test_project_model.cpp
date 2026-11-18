/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "model/project_model.h"
#include "test_utils.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
constexpr auto palette = R"({"label-palette":[{"class-id":0,"class":"Dog","colour":"#ccb42aab"}]})";

/// A project in a temp dir with the given dataset roots, and empty wav files
struct test_project
{
    temp_dir dir;

    explicit test_project(const std::string &roots_json, std::initializer_list<const char *> wavs)
    {
        write_text(dir / "pal.json", palette);
        write_roots(roots_json);
        for(auto wav : wavs)
            write_text(dir / wav, "");
    }

    void write_roots(const std::string &roots_json)
    {
        write_text(
            dir / "proj.json",
            R"({"dataset-roots":)" +
                roots_json +
                R"(,"export-prefix":"out","label-palette":"pal.json","label-file":"labels.json"})");
    }

    project_config config() const
    {
        return project_config::parse_from_file((dir / "proj.json").string());
    }
    void write_labels(const std::string &text) const { write_text(dir / "labels.json", text); }
    json saved() const { return json::parse(read_text(dir / "labels.json")); }
};

std::string labels_v2(const std::string &records)
{
    return R"({"version":2,"labels":[)" + records + "]}";
}
}

TEST_CASE("labels round trip with portable paths")
{
    test_project p(R"(["a","b/"])", {"a/x.wav", "a/sub/y.wav", "b/x.wav"});
    const auto ax = p.dir / "a/x.wav";
    const auto bx = p.dir / "b/x.wav";
    {
        project_model m(p.config());
        CHECK(m.get_files().size() == 3);
        m.add_label(ax, label{get_next_label_id(), label_defn_id{0}, 1., 2.});
        m.add_label(bx, label{get_next_label_id(), label_defn_id{0}, 3., 4.});
        m.save_if_dirty();
    }
    const auto j = p.saved();
    CHECK(j["version"] == project_model::label_file_version);
    REQUIRE(j["labels"].size() == 2);
    CHECK(j["labels"][0]["filename"] == "x.wav");
    CHECK(j["labels"][0]["dataset-root"] == "a");
    CHECK(j["labels"][1]["dataset-root"] == "b/");
    CHECK(j["labels"][0]["start_s"] == 1.);

    const auto text = read_text(p.dir / "labels.json");
    {
        project_model m(p.config());
        CHECK(m.get_labels(ax).size() == 1);
        CHECK(m.get_labels(bx).size() == 1);
        CHECK(!m.is_dirty());
    }
    CHECK(read_text(p.dir / "labels.json") == text);

    // Same project from another working directory
    const auto old = fs::current_path();
    fs::current_path(p.dir / "a");
    {
        project_model m(project_config::parse_from_file("../proj.json"));
        CHECK(m.get_labels(ax).size() == 1);
    }
    fs::current_path(old);
}

TEST_CASE("loading repairs IDs and keeps labels it can't place")
{
    test_project p(R"(["a"])", {"a/x.wav"});
    p.write_labels(labels_v2(R"(
        {"filename":"x.wav","dataset-root":"a","label-id":7,"start_s":-0.5,"stop_s":1,"class-id":0,"class-name":"Dog"},
        {"filename":"x.wav","dataset-root":"a","label-id":7,"start_s":2,"stop_s":3,"class-id":0,"class-name":"Dog"},
        {"filename":"x.wav","dataset-root":"a","label-id":8,"start_s":4,"stop_s":5,"class-id":99,"class-name":"Removed"},
        {"filename":"lost.wav","dataset-root":"gone","label-id":50,"start_s":1,"stop_s":2,"class-id":0,"class-name":"Dog"})"));

    {
        project_model m(p.config());
        const auto &ls = m.get_labels(p.dir / "a/x.wav");
        REQUIRE(ls.size() == 3);
        CHECK(ls.begin()->m_start_s == 0.); // Clamped
        CHECK(m.m_label_dict.find_ptr(label_defn_id{99}) != nullptr);
        CHECK(m.num_unresolved() == 1);
        CHECK(get_next_label_id() > 50);
        CHECK(m.is_dirty()); // The repaired duplicate ID needs writing
    }
    const auto j = p.saved();
    CHECK(j["labels"].size() == 4);
    CHECK(j["labels"].dump().find("lost.wav") != std::string::npos);
    CHECK(j["labels"].dump().find("Removed") != std::string::npos);
}

TEST_CASE("version 1 label files are migrated")
{
    test_project p(R"(["data"])", {"data/Dog goes woof.wav"});
    p.write_labels(R"({"labels":[
        {"filename":"..\\..\\..\\..\\Dog goes woof.wav","label-id":1,"start [s]":0.5,"stop [s]":1.5,"class-id":0,"class-name":"Dog"},
        {"filename":"..\\nowhere.wav","label-id":2,"start [s]":2,"stop [s]":3,"class-id":0,"class-name":"Dog"}]})");
    {
        project_model m(p.config());
        const auto &ls = m.get_labels(p.dir / "data/Dog goes woof.wav");
        REQUIRE(ls.size() == 1);
        CHECK(ls.begin()->m_stop_s == 1.5);
    }
    const auto j = p.saved();
    CHECK(j["version"] == project_model::label_file_version);
    CHECK(j["labels"][0]["filename"] == "Dog goes woof.wav");
    CHECK(j["labels"][1]["start_s"] == 2.); // Unresolved, but its keys are upgraded
    CHECK(!j["labels"][1].contains("start [s]"));
}

TEST_CASE("labels follow an unambiguously moved dataset root only")
{
    test_project p(R"(["a"])", {"moved/x.wav", "other/x.wav"});
    const auto record =
        R"({"filename":"x.wav","dataset-root":"a","label-id":1,"start_s":1,"stop_s":2,"class-id":0,"class-name":"Dog"})";

    p.write_roots(R"(["moved"])");
    p.write_labels(labels_v2(record));
    {
        project_model m(p.config());
        CHECK(m.get_labels(p.dir / "moved/x.wav").size() == 1);
    }

    p.write_roots(R"(["moved","other"])");
    p.write_labels(labels_v2(record));
    {
        project_model m(p.config());
        CHECK(m.get_labels(p.dir / "moved/x.wav").size() == 0);
        CHECK(m.num_unresolved() == 1);
    }
}

TEST_CASE("label changes track the dirty flag")
{
    test_project p(R"(["a"])", {"a/x.wav"});
    const auto x = p.dir / "a/x.wav";
    project_model m(p.config());
    m.save_if_dirty();
    CHECK(!m.is_dirty());

    const label l{get_next_label_id(), label_defn_id{0}, 1., 2.};
    m.add_label(x, l);
    CHECK(m.is_dirty());
    m.save_if_dirty();

    CHECK(m.update_label(x, l)); // Unchanged
    CHECK(!m.is_dirty());

    auto moved = l;
    moved.m_stop_s = 3.;
    CHECK(m.update_label(x, moved));
    CHECK(m.is_dirty());
    m.save_if_dirty();

    CHECK(m.erase_label(x, l.m_id));
    CHECK(!m.erase_label(x, l.m_id));
    CHECK(m.get_labels(x).empty());
}

TEST_CASE("file status and source IDs persist")
{
    test_project p(R"(["a"])", {"a/x.wav", "a/y.wav"});
    write_text(p.dir / "a/x.wav", "some audio");
    const auto x = p.dir / "a/x.wav";
    std::string id;
    {
        project_model m(p.config());
        CHECK(m.get_status(x) == file_status::todo);
        m.set_status(x, file_status::done);
        id = m.get_source_id(x);
        CHECK(id.size() == 12);
        m.record_export(x, export_record{"2026-11-12T10:00:00Z", "out", 3, true});
    }
    {
        project_model m(p.config());
        CHECK(m.get_status(x) == file_status::done);
        CHECK(m.get_status(p.dir / "a/y.wav") == file_status::todo);
        const auto *state = m.find_file_state(x);
        REQUIRE(state);
        REQUIRE(state->m_export);
        CHECK(state->m_export->m_clips == 3);
        CHECK(m.get_source_id(x) == id);
        CHECK(!m.is_dirty()); // The cached ID was still valid
    }
    CHECK(p.saved()["files"].size() == 1);
}

TEST_CASE("a failed save leaves the previous file intact")
{
    test_project p(R"(["a"])", {"a/x.wav"});
    project_model m(p.config());
    m.add_label(p.dir / "a/x.wav", label{get_next_label_id(), label_defn_id{0}, 1., 2.});
    m.save_if_dirty();
    const auto before = read_text(p.dir / "labels.json");

    fs::create_directories(p.dir / "labels.json.tmp"); // Blocks the temporary file
    m.add_label(p.dir / "a/x.wav", label{get_next_label_id(), label_defn_id{0}, 3., 4.});
    CHECK_THROWS(m.save_if_dirty());
    CHECK(m.is_dirty());
    CHECK(read_text(p.dir / "labels.json") == before);
    fs::remove_all(p.dir / "labels.json.tmp");
}

TEST_CASE("unreadable label files fail to load")
{
    test_project p(R"(["a"])", {"a/x.wav"});
    p.write_labels("{ broken");
    CHECK_THROWS(project_model(p.config()));

    p.write_labels(R"({"version":99,"labels":[]})");
    CHECK_THROWS(project_model(p.config()));

    p.write_labels(R"({"labels":5})");
    CHECK_THROWS(project_model(p.config()));
}

TEST_CASE("suggestions, scores, onsets and channels round trip")
{
    test_project p(R"(["a"])", {"a/x.wav"});
    const auto x = p.dir / "a/x.wav";
    const label onset{get_next_label_id(), label_defn_id{0}, 1., 1., label_state::suggested, 0.25f};
    const label rejected{get_next_label_id(), label_defn_id{0}, 2., 3., label_state::rejected};
    {
        project_model m(p.config());
        CHECK(!m.get_channel(x));
        m.add_label(x, onset);
        m.add_label(x, rejected);
        m.set_channel(x, 1U);
        CHECK(m.get_channel(x) == 1U);
    }
    const auto j = p.saved();
    CHECK(j["labels"][0]["state"] == "suggested");
    CHECK(j["labels"][0]["score"] == doctest::Approx(0.25));
    CHECK(!j["labels"][1].contains("score"));
    CHECK(j["files"][0]["channel"] == "2"); // Numbered from 1 in the file
    {
        project_model m(p.config());
        const auto &ls = m.get_labels(x);
        REQUIRE(ls.size() == 2);
        CHECK(*ls.find_ptr(onset.m_id) == onset);
        CHECK(ls.find_ptr(onset.m_id)->is_onset());
        CHECK(*ls.find_ptr(rejected.m_id) == rejected);
        CHECK(m.get_channel(x) == 1U);
        m.set_channel(x, std::nullopt); // Back to the project's default
        CHECK(!m.get_channel(x));
    }
    CHECK(!p.saved()["files"].dump().contains("channel"));
}

TEST_CASE("project default channel, and palettes that mark classes as onsets")
{
    test_project p(R"(["a"])", {"a/x.wav"});
    write_text(
        p.dir / "pal.json",
        R"({"label-palette":[{"class-id":0,"class":"Dog","colour":"#ccb42aab","kind":"onset"},{"class-id":1,"class":"Rain","colour":"#ccb42aab"}]})");
    write_text(
        p.dir / "proj.json",
        R"({"dataset-roots":["a"],"export-prefix":"out","label-palette":"pal.json","label-file":"labels.json","channel":2})");
    const auto x = p.dir / "a/x.wav";
    project_model m(p.config());
    // Every class marks regions: an old palette's "kind" is ignored, with a notice naming them
    CHECK(m.m_label_dict.size() == 2);
    REQUIRE(m.notices().size() == 1);
    CHECK(m.notices().front().contains("(Dog)"));
    CHECK(m.get_channel(x) == 1U);
    m.set_channel(x, std::nullopt); // An explicit mix overrides the project's channel
    CHECK(!m.get_channel(x));
    CHECK(m.find_file_state(x)->m_channel == mixed_channels);
}

TEST_CASE("files are listed sorted within each root, roots in the project's order")
{
    test_project p(R"(["z","a"])", {"z/b.wav", "z/a/y.wav", "z/a.wav", "a/c.wav", "a/b.wav"});
    project_model m(p.config());
    std::vector<std::string> names;
    for(const auto &f : m.get_files())
        names.push_back(f.m_display_name);
    // Folder "a" sorts before "a.wav", as paths compare by component
    CHECK(names == std::vector<std::string>{"a/y.wav", "a.wav", "b.wav", "b.wav", "c.wav"});
    CHECK(m.get_files()[1].m_path == p.dir / "z/a.wav");
    CHECK(m.get_files()[3].m_path == p.dir / "a/b.wav");
}

TEST_CASE("project settings are saved to the project file, keeping its other keys")
{
    test_project p(R"(["a"])", {"a/x.wav"});
    // A key the settings don't cover, which must survive
    auto file = json::parse(read_text(p.dir / "proj.json"));
    file["annotations"] = json::array({{{"root", "trees"}}});
    write_text(p.dir / "proj.json", file.dump());

    project_model m(p.config());
    auto s = m.settings();
    CHECK(s.m_dataset_roots == std::vector<std::string>{"a"});
    CHECK(s.m_palette == "pal.json");
    CHECK(!s.m_channel);

    s.m_channel = 1U; // Channel 2
    s.m_review = json{{"point_post_s", 0.3}};
    s.m_export_prefix = "out/csv";
    s.m_high_pass = json{{"enabled", true}, {"cutoff_hz", 80.}};
    CHECK(!m.set_settings(s)); // Applies without reopening
    CHECK(m.config().m_high_pass["cutoff_hz"] == 80.);
    CHECK(m.get_channel(p.dir / "a/x.wav") == 1U);
    CHECK(m.config().m_export_prefix == "out/csv");

    auto saved = json::parse(read_text(p.dir / "proj.json"));
    CHECK(saved["channel"] == 2);
    CHECK(saved["review"]["point_post_s"] == 0.3);
    CHECK(saved["annotations"][0]["root"] == "trees");
    CHECK(saved["high-pass"]["enabled"] == true);
    CHECK(project_config::parse_from_file(p.dir / "proj.json").m_high_pass["cutoff_hz"] == 80.);
    CHECK(project_config::parse_from_file(p.dir / "proj.json").m_channel == 1U);

    // Back to a mix, the default, which is written as no key
    s.m_channel.reset();
    CHECK(!m.set_settings(s));
    saved = json::parse(read_text(p.dir / "proj.json"));
    CHECK(!saved.contains("channel"));

    SUBCASE("new roots need the project reopened")
    {
        write_text(p.dir / "b/y.wav", "");
        s.m_dataset_roots.push_back("b");
        CHECK(m.set_settings(s));
        CHECK(m.config().m_wav_paths.size() == 1U); // Until reopened
        CHECK(project_config::parse_from_file(p.dir / "proj.json").m_wav_paths.size() == 2U);
    }
    SUBCASE("a missing palette or no roots are refused")
    {
        const auto before = read_text(p.dir / "proj.json");
        auto bad = s;
        bad.m_palette = "missing.json";
        CHECK_THROWS(m.set_settings(bad));
        bad = s;
        bad.m_dataset_roots.clear();
        CHECK_THROWS(m.set_settings(bad));
        CHECK(read_text(p.dir / "proj.json") == before);
    }
}

TEST_CASE("frequency bands round trip, and labels without one have none in the file")
{
    test_project p(R"(["a"])", {"a/x.wav"});
    const auto x = p.dir / "a/x.wav";
    label banded{get_next_label_id(), label_defn_id{0}, 1., 2.};
    banded.m_band = {8000., 9000.};
    label from_bottom{get_next_label_id(), label_defn_id{0}, 3., 4.};
    from_bottom.m_band.m_high_hz = 500.;
    const label full{get_next_label_id(), label_defn_id{0}, 5., 6.};
    {
        project_model m(p.config());
        m.add_label(x, banded);
        m.add_label(x, from_bottom);
        m.add_label(x, full);
    }
    const auto j = p.saved();
    CHECK(j["labels"][0]["low_hz"] == doctest::Approx(8000.));
    CHECK(j["labels"][0]["high_hz"] == doctest::Approx(9000.));
    CHECK(!j["labels"][1].contains("low_hz"));
    CHECK(j["labels"][1]["high_hz"] == doctest::Approx(500.));
    CHECK(!j["labels"][2].contains("low_hz"));
    CHECK(!j["labels"][2].contains("high_hz"));

    project_model m(p.config());
    const auto &ls = m.get_labels(x);
    CHECK(ls.find_ptr(banded.m_id)->m_band == banded.m_band);
    CHECK(ls.find_ptr(from_bottom.m_id)->m_band == from_bottom.m_band);
    CHECK(ls.find_ptr(full.m_id)->m_band.is_full());
}
