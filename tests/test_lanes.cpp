/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <doctest/doctest.h>

#include "app/lanes.h"

TEST_CASE("each class gets its own lane, in palette order")
{
    label_dict dict;
    dict.add(label_defn{label_defn_id{5}, "Dog", 0});
    dict.add(label_defn{label_defn_id{2}, "Fox", 0});

    const auto dog = lane_of(dict, label_defn_id{5}, true);
    const auto fox = lane_of(dict, label_defn_id{2}, true);
    CHECK(dog.m_y_max == 1.);
    CHECK(dog.m_y_min == doctest::Approx(0.));
    CHECK(fox.m_y_max == doctest::Approx(0.));
    CHECK(fox.m_y_min == -1.);
    CHECK(dog.contains(0.5));
    CHECK(!fox.contains(0.5));

    // Unclassified, or lanes turned off: full height
    CHECK(lane_of(dict, label_defn_id{}, true).m_y_min == -1.);
    CHECK(lane_of(dict, label_defn_id{2}, false).m_y_max == 1.);
}
