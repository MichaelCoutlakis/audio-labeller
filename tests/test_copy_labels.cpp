/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <doctest/doctest.h>

#include "model/copy_labels.h"

namespace
{
const label_defn_id dog{0};
const label_defn_id bird{1};

label make(
    label_defn_id defn,
    double start_s,
    double stop_s,
    label_state state = label_state::confirmed)
{
    return label{get_next_label_id(), defn, start_s, stop_s, state};
}
}

TEST_CASE("pasted labels are shifted, in the state asked for")
{
    auto boxed = make(dog, 10., 12., label_state::suggested);
    boxed.m_band = frequency_band{500., 2000.};
    boxed.m_score = 0.3f;
    const std::vector<label> copied{
        boxed,
        make(bird, 3., 3.),   // Shifted before the start
        make(bird, 62., 67.), // Cut short at the end
        make(dog, 70., 71.),  // Past the end
        make(dog, 30., 31.),  // Already judged here
        make(dog, 30., 31.),  // Twice in the clipboard
    };

    labels existing;
    existing.add(make(dog, 25., 26., label_state::rejected));

    const auto pasted = labels_to_paste(copied, existing, -5., 60., label_state::suggested);
    REQUIRE(pasted.m_added.size() == 2U);
    CHECK(pasted.m_outside == 2U);
    CHECK(pasted.m_present == 2U);

    const auto &first = pasted.m_added[0];
    CHECK(first.m_defn_id == dog);
    CHECK(first.m_start_s == doctest::Approx(5.));
    CHECK(first.m_stop_s == doctest::Approx(7.));
    CHECK(first.m_state == label_state::suggested);
    CHECK(first.m_band == boxed.m_band);
    CHECK(!first.m_score); // The score was for the other recording
    CHECK(first.m_id != boxed.m_id);

    const auto &second = pasted.m_added[1];
    CHECK(second.m_start_s == doctest::Approx(57.));
    CHECK(second.m_stop_s == doctest::Approx(60.));

    // As labels, with no shift
    const auto as_labels = labels_to_paste({boxed}, labels{}, 0., 60., label_state::confirmed);
    REQUIRE(as_labels.m_added.size() == 1U);
    CHECK(as_labels.m_added[0].is_confirmed());
    CHECK(as_labels.m_added[0].m_start_s == 10.);
}
