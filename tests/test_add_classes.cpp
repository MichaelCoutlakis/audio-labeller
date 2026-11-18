/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <doctest/doctest.h>

#include "model/add_classes.h"

namespace
{
const label_defn_id ambience{0};
const label_defn_id wind{1};

label region(label_defn_id defn, double start_s, double stop_s)
{
    return label{get_next_label_id(), defn, start_s, stop_s};
}
}

TEST_CASE("adding a class gives each selected span a new label of it")
{
    labels existing;
    const auto whole = region(ambience, 0., 60.);
    auto boxed = region(ambience, 2., 3.);
    boxed.m_band = frequency_band{500., 2000.};
    existing.add(whole);
    existing.add(boxed);

    const auto added = labels_to_add(existing, {whole, boxed}, wind);
    REQUIRE(added.size() == 2U);
    CHECK(added[0].m_defn_id == wind);
    CHECK(added[0].m_start_s == 0.);
    CHECK(added[0].m_stop_s == 60.);
    CHECK(added[0].is_confirmed());
    CHECK(added[0].m_id != whole.m_id);
    CHECK(added[1].m_band == boxed.m_band); // Drawn on the spectrogram, kept there
}

TEST_CASE("a class that already has a label over the span is skipped, within a tolerance")
{
    labels existing;
    const auto whole = region(ambience, 0., 60.);
    existing.add(whole);
    existing.add(region(wind, 0.0005, 59.9995));

    CHECK(labels_to_add(existing, {whole}, wind).empty());
    CHECK(labels_to_add(existing, {whole}, ambience).empty()); // It's already that class
    CHECK(labels_to_add(existing, {region(ambience, 0., 30.)}, wind).size() == 1U);
}

TEST_CASE("rejected labels don't count as covering the span")
{
    labels existing;
    auto rejected = region(wind, 0., 10.);
    rejected.m_state = label_state::rejected;
    existing.add(rejected);
    CHECK(labels_to_add(existing, {region(ambience, 0., 10.)}, wind).size() == 1U);
    CHECK(!find_over(existing, wind, {0., 10.}));
}

TEST_CASE("selected labels over the same span get one new label between them")
{
    labels existing;
    const auto a = region(ambience, 1., 2.);
    const auto b = region(label_defn_id{2}, 1., 2.);
    existing.add(a);
    existing.add(b);
    CHECK(labels_to_add(existing, {a, b}, wind).size() == 1U);
}

TEST_CASE("an unclassified selection is given the class rather than left beside a new label")
{
    labels existing;
    auto drawn = label{get_next_label_id(), label_defn_id{}, 0., 60.};
    drawn.m_band = frequency_band{100., 4000.};

    const auto plan = plan_add_class(existing, {drawn}, {}, ambience);
    REQUIRE(plan.m_classified.size() == 1U);
    CHECK(plan.m_classified[0].m_id == drawn.m_id); // Stays selected
    CHECK(plan.m_classified[0].m_defn_id == ambience);
    CHECK(plan.m_classified[0].m_band == drawn.m_band);
    CHECK(plan.m_added.empty());
    CHECK(plan.m_merged.empty());

    // Selected with a label over the same span, which then gets nothing more
    const auto whole = region(wind, 0., 60.);
    existing.add(whole);
    const auto both = plan_add_class(existing, {drawn}, {whole}, ambience);
    CHECK(both.m_classified.size() == 1U);
    CHECK(both.m_added.empty());
    CHECK(both.m_skipped == 1U);
}

TEST_CASE("an unclassified selection where the class already has a label gives way to it")
{
    labels existing;
    const auto there = region(ambience, 0., 60.);
    existing.add(there);
    const auto drawn = label{get_next_label_id(), label_defn_id{}, 0., 60.};

    const auto plan = plan_add_class(existing, {drawn}, {}, ambience);
    CHECK(plan.m_classified.empty());
    REQUIRE(plan.m_merged.size() == 1U);
    CHECK(plan.m_merged[0] == std::pair{drawn.m_id, there.m_id});
    CHECK(!plan.empty());

    CHECK(plan_add_class(existing, {}, {there}, ambience).empty()); // Nothing to do
}

TEST_CASE("find_over finds a label of the class over the span")
{
    labels existing;
    const auto whole = region(ambience, 0., 60.);
    existing.add(whole);
    CHECK(find_over(existing, ambience, {0., 60.}) == whole.m_id);
    CHECK(!find_over(existing, wind, {0., 60.}));
    CHECK(!find_over(existing, ambience, {0., 59.}));
}
