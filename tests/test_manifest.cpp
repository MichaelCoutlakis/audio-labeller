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

    CHECK(apply_clip_verdict(m, rec, 1., 0.05, clip_verdict::correct, dog_id) == dog.m_id);
    CHECK(m.get_labels(rec).find_ptr(dog.m_id)->m_state == label_state::confirmed);

    CHECK(apply_clip_verdict(m, rec, 1., 0.05, clip_verdict::relabel, label_defn_id{1}) == dog.m_id);
    CHECK(m.get_labels(rec).find_ptr(dog.m_id)->m_defn_id == label_defn_id{1});

    CHECK(apply_clip_verdict(m, rec, 1., 0.05, clip_verdict::not_event, dog_id) == dog.m_id);
    CHECK(m.get_labels(rec).find_ptr(dog.m_id)->m_state == label_state::rejected);

    // No label at the second clip's onset, so the verdict makes one
    const auto made = apply_clip_verdict(m, rec, 2., 0.05, clip_verdict::not_event, dog_id);
    const auto *l = m.get_labels(rec).find_ptr(made);
    REQUIRE(l);
    CHECK(l->is_onset());
    CHECK(l->m_state == label_state::rejected);
    CHECK(m.get_labels(rec).size() == 2);

    CHECK(find_label_at(m.get_labels(rec), 1.5, 0.05) == nullptr);

    auto bad = *settings;
    bad.m_onset_column = "nope";
    CHECK_THROWS(load_clip_manifest(m, bad));
}
