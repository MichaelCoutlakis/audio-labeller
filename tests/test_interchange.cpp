/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <sstream>

#include <doctest/doctest.h>

#include "interchange/annotation_tree.h"
#include "interchange/label_format.h"
#include "model/paths.h"
#include "model/project_model.h"
#include "test_utils.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
std::vector<text_label> read_as(const char *format, const std::string &text)
{
    std::istringstream in(text);
    return make_label_format(format)->read(in);
}

std::string write_as(const char *format, const std::vector<text_label> &labels, uint32_t rate = 0)
{
    std::ostringstream out;
    make_label_format(format)->write(out, labels, recording_info{rate});
    return out.str();
}

constexpr auto palette =
    R"({"label-palette":[{"class-id":0,"class":"Dog","colour":"#ccb42aab"},{"class-id":1,"class":"Click","colour":"#ccb42aab","kind":"onset"}]})";

struct tree_project
{
    temp_dir dir;

    explicit tree_project(const std::string &annotations)
    {
        write_text(dir / "pal.json", palette);
        write_text(
            dir / "proj.json",
            R"({"dataset-roots":["data"],"export-prefix":"out","label-palette":"pal.json",)"
            R"("label-file":"labels.json","annotations":)" +
                annotations +
                "}");
        write_text(dir / "data/site1/rec1.wav", "");
        write_text(dir / "data/site1/rec2.WAV", "");
        write_text(dir / "data/rec3.wav", "");
    }

    project_config config() const
    {
        return project_config::parse_from_file((dir / "proj.json").string());
    }

    annotation_tree tree(const project_model &m, size_t i = 0U) const
    {
        return annotation_tree(
            m.config(),
            annotation_tree_settings::list_from_json(m.config().m_annotations).at(i));
    }
};
}

TEST_CASE("Audacity labels skip frequency lines and read an optional score")
{
    const auto labels = read_as(
        "audacity",
        "1.5\t2.25\tDog\r\n"
        "\\\t100.0\t2000.0\n"
        "\n"
        "3\t3\tx:Click\t0.25\n"
        "4\t5\n");
    REQUIRE(labels.size() == 3);
    CHECK(labels[0] == text_label{1.5, 2.25, "Dog"});
    CHECK(labels[1].m_text == "x:Click");
    REQUIRE(labels[1].m_score);
    CHECK(*labels[1].m_score == doctest::Approx(0.25));
    CHECK(labels[2].m_text.empty());

    CHECK_THROWS(read_as("audacity", "one\ttwo\tDog\n"));
    CHECK(write_as("audacity", {{1.5, 2.25, "Dog"}}) == "1.500000\t2.250000\tDog\n");
}

TEST_CASE("DCASE labels read with or without a header")
{
    const auto plain = read_as("dcase", "0.5\t1.0\tDog\n2\t2.5\tCat\t0.9\n");
    REQUIRE(plain.size() == 2);
    CHECK(plain[1].m_score.value() == doctest::Approx(0.9));

    const auto headed = read_as("dcase", "event_label\tonset\toffset\nDog\t0.5\t1\n");
    REQUIRE(headed.size() == 1);
    CHECK(headed[0] == text_label{0.5, 1., "Dog"});

    const auto text = write_as("dcase", {{0.5, 1., "Dog"}, {2., 2., "Click", 0.5f}});
    CHECK(text.starts_with("onset\toffset\tevent_label\tscore\n"));
    CHECK(read_as("dcase", text).size() == 2);
}

TEST_CASE("Raven selection tables keep one row per selection")
{
    const auto labels = read_as(
        "raven",
        "Selection\tView\tChannel\tBegin Time (s)\tEnd Time (s)\tLow Freq (Hz)\tHigh Freq "
        "(Hz)\tSpecies\n"
        "1\tWaveform 1\t1\t0.5\t1.5\t0\t8000\tDog\n"
        "1\tSpectrogram 1\t1\t0.5\t1.5\t0\t8000\tDog\n"
        "2\tWaveform 1\t1\t3\t4\t0\t8000\tFox\n");
    REQUIRE(labels.size() == 2);
    CHECK(labels[1] == text_label{3., 4., "Fox"});

    const auto text = write_as("raven", {{0.5, 1.5, "Dog"}}, 16000);
    CHECK(text.find("8000.0\tDog") != std::string::npos);
    CHECK(read_as("raven", text).size() == 1);
    CHECK_THROWS(read_as("raven", "a\tb\n1\t2\n"));
    CHECK_THROWS(make_label_format("praat"));
}

TEST_CASE("annotation trees mirror the dataset root")
{
    tree_project p(R"([{"root":"ann"},{"root":"ann2","naming":"append","format":"dcase"}])");
    project_model m(p.config());
    CHECK(m.get_files().size() == 3); // .WAV is found too

    const auto rec1 = m.to_portable(p.dir / "data/site1/rec1.wav");
    CHECK(p.tree(m).path_for(rec1) == p.dir / "ann/site1/rec1.txt");
    CHECK(p.tree(m, 1).path_for(rec1) == p.dir / "ann2/site1/rec1.wav.tsv");
    CHECK(!p.tree(m).path_for({{}, "elsewhere.wav"}));
}

TEST_CASE("importing proposals adds suggestions once, and empty files as done")
{
    tree_project p(R"([{"name":"pipeline","root":"ann","import":"suggestions"},
                        {"name":"reviewed","root":"ann","import":"labels"}])");
    write_text(p.dir / "ann/site1/rec1.txt", "1\t1\tclick\t0.4\n2\t3\tx:Dog\n5\t6\tBird\n");
    write_text(p.dir / "ann/site1/rec2.txt", "");
    write_text(p.dir / "ann/rec3.txt", "");
    write_text(p.dir / "ann/gone.txt", "1\t2\tDog\n");

    const auto rec1 = p.dir / "data/site1/rec1.wav";
    const auto rec2 = p.dir / "data/site1/rec2.WAV";
    project_model m(p.config());
    m.add_label(p.dir / "data/rec3.wav", label{get_next_label_id(), label_defn_id{0}, 1., 2.});

    auto summary = import_annotations(m, p.tree(m));
    CHECK(summary.m_files == 3);
    CHECK(summary.m_added == 3);
    CHECK(summary.m_unmatched == 1);
    CHECK(summary.m_marked_done == 0); // Not for proposals: the detector found nothing
    CHECK(summary.m_new_classes == std::vector<std::string>{"Bird"});

    const auto &ls = m.get_labels(rec1);
    REQUIRE(ls.size() == 3);
    auto it = ls.begin();
    CHECK(it->m_defn_id == label_defn_id{1}); // Matched ignoring case
    CHECK(it->m_state == label_state::suggested);
    CHECK(it->is_onset());
    CHECK(it->m_score.value() == doctest::Approx(0.4));
    ++it;
    CHECK(it->m_state == label_state::rejected);

    summary = import_annotations(m, p.tree(m));
    CHECK(summary.m_added == 0);
    CHECK(summary.m_duplicates == 3);

    // As reviewed labels, an empty file means checked with no events
    summary = import_annotations(m, p.tree(m, 1));
    CHECK(summary.m_marked_done == 1);
    CHECK(m.get_status(rec2) == file_status::done);
    CHECK(m.get_status(p.dir / "data/rec3.wav") == file_status::todo); // It has a label
}

TEST_CASE("exporting writes done files, empty when they have no events")
{
    tree_project p(R"([{"root":"out/ann"}, {"root":"out/all","export-files":"all","include-suggestions":true}])");
    const auto rec1 = p.dir / "data/site1/rec1.wav";
    const auto rec2 = p.dir / "data/site1/rec2.WAV";
    const auto rec3 = p.dir / "data/rec3.wav";

    project_model m(p.config());
    m.add_label(rec1, label{get_next_label_id(), label_defn_id{0}, 2., 3.});
    m.add_label(rec1, label{get_next_label_id(), label_defn_id{1}, 1., 1., label_state::rejected});
    m.add_label(rec1, label{get_next_label_id(), label_defn_id{1}, 4., 4., label_state::suggested});
    m.set_status(rec1, file_status::done);
    m.set_status(rec2, file_status::done);
    m.add_label(rec3, label{get_next_label_id(), label_defn_id{0}, 1., 2.}); // To do

    auto summary = export_annotations(m, p.tree(m));
    CHECK(summary.m_errors.empty());
    CHECK(summary.m_files == 2);
    CHECK(read_text(p.dir / "out/ann/site1/rec1.txt") ==
          "1.000000\t1.000000\tx:Click\n2.000000\t3.000000\tDog\n");
    CHECK(fs::exists(p.dir / "out/ann/site1/rec2.txt"));
    CHECK(read_text(p.dir / "out/ann/site1/rec2.txt").empty());
    CHECK(!fs::exists(p.dir / "out/ann/rec3.txt"));

    summary = export_annotations(m, p.tree(m));
    CHECK(summary.m_unchanged == 2);

    summary = export_annotations(m, p.tree(m, 1));
    CHECK(summary.m_files == 3);
    CHECK(read_text(p.dir / "out/all/site1/rec1.txt").find("Click\n") != std::string::npos);
    CHECK(fs::exists(p.dir / "out/all/rec3.txt"));
}

TEST_CASE("environment variables in project paths")
{
    temp_dir dir;
    write_text(dir / "pal.json", palette);
    write_text(dir / "elsewhere/data/a.wav", "");
#ifdef _WIN32
    _putenv_s("AUDIOLAB_TEST_DIR", (dir / "elsewhere").string().c_str());
#else
    setenv("AUDIOLAB_TEST_DIR", (dir / "elsewhere").string().c_str(), 1);
#endif
    write_text(
        dir / "proj.json",
        R"({"dataset-roots":["${AUDIOLAB_TEST_DIR}/data"],"export-prefix":"out",)"
        R"("label-palette":"pal.json","label-file":"labels.json"})");
    {
        project_model m(project_config::parse_from_file((dir / "proj.json").string()));
        REQUIRE(m.get_files().size() == 1);
        m.add_label(m.get_files()[0].m_path, label{get_next_label_id(), label_defn_id{0}, 1., 2.});
    }
    // The root is stored as written, so it's the same on a machine where the variable differs
    const auto saved = json::parse(read_text(dir / "labels.json"));
    CHECK(saved["labels"][0]["dataset-root"] == "${AUDIOLAB_TEST_DIR}/data");

    CHECK(expand_env_vars(std::string{"a/${AUDIOLAB_TEST_DIR}"}).starts_with("a/"));
    CHECK_THROWS(expand_env_vars(std::string{"${AUDIOLAB_NOT_SET_ANYWHERE}/x"}));
    CHECK_THROWS(expand_env_vars(std::string{"${UNTERMINATED"}));
}
