/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <doctest/doctest.h>

#include "model/file_finder.h"
#include "test_utils.h"

namespace
{
constexpr auto palette = R"({"label-palette":[
    {"class-id":0,"class":"Dog","colour":"#ccb42aab"},
    {"class-id":1,"class":"Clap","colour":"#ccb42aab"},
    {"class-id":2,"class":"Human","colour":"#ccb42aab"}]})";

struct finder_project
{
    temp_dir m_dir;
    std::optional<project_model> m_model;

    finder_project()
    {
        write_text(m_dir / "pal.json", palette);
        write_text(
            m_dir / "proj.json",
            R"({"dataset-roots":["Site A","site b"],"export-prefix":"out",)"
            R"("label-palette":"pal.json","label-file":"labels.json"})");
        for(auto wav : {"Site A/morning/one.wav", "Site A/evening/two.wav", "site b/Morning/three.wav"})
            write_text(m_dir / wav, "");
        m_model.emplace(project_config::parse_from_file(m_dir / "proj.json"));
    }

    project_model &model() { return *m_model; }
    const std::filesystem::path &path(size_t i) const { return m_model->get_files()[i].m_path; }

    void add(size_t file, uint32_t defn, label_state state = label_state::confirmed)
    {
        label l{get_next_label_id(), label_defn_id{defn}, 0., 1.};
        l.m_state = state;
        m_model->add_label(path(file), l);
    }

    std::vector<std::string> names(const file_list_settings &s) const
    {
        std::vector<std::string> out;
        for(const auto &row : list_files(*m_model, s))
            out.push_back(row.m_file->m_display_name);
        return out;
    }
};

using names_t = std::vector<std::string>;
}

TEST_CASE("search words must all be in the text, ignoring case")
{
    CHECK(contains_words("Site A/morning/one.wav", ""));
    CHECK(contains_words("Site A/morning/one.wav", "  "));
    CHECK(contains_words("Site A/morning/one.wav", "MORN one"));
    CHECK_FALSE(contains_words("Site A/morning/one.wav", "morning two"));
}

TEST_CASE("the text search looks at the root as written and the path below it")
{
    finder_project p;
    // Listed by root, then path
    CHECK(p.names({}) == names_t{"evening/two.wav", "morning/one.wav", "Morning/three.wav"});

    file_list_settings s;
    s.m_search = "morning";
    CHECK(p.names(s) == names_t{"morning/one.wav", "Morning/three.wav"});
    s.m_search = "site a/"; // "a" alone is in ".wav"
    CHECK(p.names(s) == names_t{"evening/two.wav", "morning/one.wav"});
    s.m_search = "SITE B wav";
    CHECK(p.names(s) == names_t{"Morning/three.wav"});

    // With the status filter
    p.model().set_status(p.path(1), file_status::done);
    s.m_search = "morning";
    s.m_status = file_filter::done;
    CHECK(p.names(s) == names_t{"morning/one.wav"});
}

TEST_CASE("filtering by class keeps files with labels of any chosen class, and counts them")
{
    finder_project p;
    p.add(0, 0);                            // evening/two: a dog
    p.add(1, 1);                            // morning/one: two claps and a suggested one
    p.add(1, 1);
    p.add(1, 1, label_state::suggested);
    p.add(2, 2, label_state::suggested);    // Morning/three: a suggested human, a rejected clap
    p.add(2, 1, label_state::rejected);

    file_list_settings s;
    s.m_finder = file_finder::by_class;
    // No classes chosen: every file
    CHECK(p.names(s).size() == 3U);
    s.m_classes.m_classes = {"Not in this palette"};
    CHECK(p.names(s).size() == 3U);

    s.m_classes.toggle("Clap");
    CHECK(p.names(s) == names_t{"morning/one.wav"});
    CHECK(list_files(p.model(), s)[0].m_class_count == 2U);

    s.m_classes.m_suggestions = true;
    CHECK(list_files(p.model(), s)[0].m_class_count == 3U);
    s.m_classes.m_rejected = true;
    CHECK(p.names(s) == names_t{"morning/one.wav", "Morning/three.wav"});

    s.m_classes.toggle("Human");
    s.m_classes.toggle("Dog");
    CHECK(p.names(s) == names_t{"evening/two.wav", "morning/one.wav", "Morning/three.wav"});

    // By count, ties in the project's order
    s.m_sort = file_sort::class_count;
    s.m_descending = true;
    CHECK(p.names(s) == names_t{"morning/one.wav", "Morning/three.wav", "evening/two.wav"});
    s.m_descending = false;
    CHECK(p.names(s) == names_t{"evening/two.wav", "Morning/three.wav", "morning/one.wav"});

    // Without the class filter, the count order falls back to the path's
    s.m_finder = file_finder::none;
    CHECK(p.names(s) == names_t{"evening/two.wav", "morning/one.wav", "Morning/three.wav"});
    s.m_descending = true;
    s.m_sort = file_sort::path;
    CHECK(p.names(s) == names_t{"Morning/three.wav", "morning/one.wav", "evening/two.wav"});

    s.m_classes.toggle("Dog");
    CHECK_FALSE(s.m_classes.has("Dog"));
}
