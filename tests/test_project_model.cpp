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
    CHECK(j["version"] == 2);
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
    CHECK(j["version"] == 2);
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
