/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "io/reveal.h"
#include "model/new_project.h"
#include "model/paths.h"
#include "model/project_model.h"
#include "test_utils.h"

namespace fs = std::filesystem;
using nlohmann::json;

TEST_CASE("class names are split on commas and trimmed")
{
    CHECK(
        split_class_names(" Dog, Cat ,,Bird call ,") ==
        std::vector<std::string>{"Dog", "Cat", "Bird call"});
    CHECK(split_class_names("").empty());
    CHECK_THROWS(split_class_names("Dog, Cat, Dog"));
}

TEST_CASE("project paths are relative only inside the project's folder")
{
    temp_dir d;
    CHECK(path_for_project(d / "proj/data/a", d / "proj") == "data/a");
    CHECK(path_for_project(d / "proj", d / "proj") == ".");
    CHECK(path_for_project(d / "data", d / "proj") == generic_utf8(d / "data"));
    CHECK(path_for_project(d / "projects", d / "proj") == generic_utf8(d / "projects"));
}

TEST_CASE("a new project labels a data folder kept apart from it")
{
    temp_dir d;
    write_text(d / "data/birds/a.wav", "");
    write_text(d / "data/birds/sub/b.wav", "");

    new_project_spec spec;
    spec.m_project_file = d / "projects/birds"; // .json is added
    spec.m_data_root = d / "data/birds";
    spec.m_classes = {"Robin", "Wren"};
    const auto file = create_project(spec);
    CHECK(file == d / "projects/birds.json");

    const auto j = json::parse(read_text(file));
    CHECK(j["dataset-roots"] == json::array({generic_utf8(d / "data/birds")}));
    CHECK(j["label-palette"] == "birds-palette.json");
    CHECK(j["label-file"] == "birds-labels.json");
    CHECK(!fs::exists(d / "projects/birds-labels.json")); // Written at the first save

    const auto palette = json::parse(read_text(d / "projects/birds-palette.json"));
    REQUIRE(palette["label-palette"].size() == 2);
    CHECK(palette["label-palette"][1]["class"] == "Wren");
    CHECK(palette["label-palette"][1]["class-id"] == 1);
    CHECK(palette["label-palette"][0]["colour"] != palette["label-palette"][1]["colour"]);

    project_model m(project_config::parse_from_file(file));
    CHECK(m.get_files().size() == 2);
    CHECK(m.m_label_dict.size() == 2);

    SUBCASE("nothing is overwritten")
    {
        CHECK_THROWS(create_project(spec));
        spec.m_project_file = d / "projects/other.json";
        spec.m_palette = d / "projects/birds-palette.json";
        CHECK_NOTHROW(create_project(spec));
        CHECK(
            json::parse(read_text(d / "projects/other.json"))["label-palette"] ==
            "birds-palette.json");
        CHECK(!fs::exists(d / "projects/other-palette.json"));
    }
}

TEST_CASE("a new project needs its data, classes and unused names")
{
    temp_dir d;
    fs::create_directories(d / "data");
    new_project_spec spec{d / "p.json", d / "missing", {}, {"Dog"}};
    CHECK_THROWS(create_project(spec));

    spec.m_data_root = d / "data";
    spec.m_classes.clear();
    CHECK_THROWS(create_project(spec));

    spec.m_classes = {"Dog"};
    write_text(d / "p-labels.json", "{}"); // Another project's labels
    CHECK_THROWS(create_project(spec));
    CHECK(!fs::exists(d / "p.json"));
    CHECK(!fs::exists(d / "p-palette.json"));
}

TEST_CASE("switching projects saves the first and takes over the second")
{
    temp_dir d;
    write_text(d / "a/x.wav", "");
    write_text(d / "b/y.wav", "");
    create_project({d / "a.json", d / "a", {}, {"Dog"}});
    create_project({d / "b.json", d / "b", {}, {"Cat", "Cow"}});

    project_model m(project_config::parse_from_file(d / "a.json"));
    m.add_label(d / "a/x.wav", label{get_next_label_id(), label_defn_id{0}, 1., 2.});
    {
        project_model next(project_config::parse_from_file(d / "b.json"));
        m = std::move(next);
        CHECK(!next.has_project());
    }
    CHECK(fs::exists(d / "a-labels.json"));
    CHECK(json::parse(read_text(d / "a-labels.json"))["labels"].size() == 1);
    CHECK(m.config().m_filename == d / "b.json");
    CHECK(m.m_label_dict.size() == 2);
    REQUIRE(m.get_files().size() == 1);
    CHECK(m.get_files()[0].m_path == d / "b/y.wav");
    CHECK(m.get_labels(d / "a/x.wav").empty());
}

TEST_CASE("file URIs percent-encode")
{
    CHECK(file_uri("/home/a b/x,1.wav") == "file:///home/a%20b/x%2C1.wav");
    CHECK(file_uri("/d/caf\xc3\xa9.wav") == "file:///d/caf%C3%A9.wav");
    CHECK(file_uri("C:/x/y.wav") == "file:///C:/x/y.wav");
}
