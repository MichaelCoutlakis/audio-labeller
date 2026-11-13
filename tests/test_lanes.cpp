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

TEST_CASE("stacked labels share the height only where they overlap")
{
    const label a{1, label_defn_id{0}, 0., 2.};
    const label b{2, label_defn_id{0}, 1., 3.};
    const label c{3, label_defn_id{0}, 2.5, 4.}; // Overlaps b, but a's column is free again
    const label d{4, label_defn_id{0}, 5., 6.};  // Alone
    const label onset{5, label_defn_id{0}, 1.5, 1.5};

    const auto bands = stack_labels({&a, &b, &c, &d, &onset});
    CHECK(bands.at(1) == lane{0., 1.});
    CHECK(bands.at(2) == lane{-1., 0.});
    CHECK(bands.at(3) == lane{0., 1.});
    CHECK(bands.at(4) == lane{});
    CHECK(bands.at(5) == lane{}); // Onsets are lines over the full height

    label_dict dict;
    dict.add(label_defn{label_defn_id{0}, "Dog", 0});
    dict.add(label_defn{label_defn_id{1}, "Fox", 0});
    const auto lanes = layout_labels({&a, &onset}, dict, label_layout::class_lanes);
    CHECK(lanes.at(1) == lane{0., 1.});
    CHECK(lanes.at(5) == lane{});
    CHECK(layout_labels({&a}, dict, label_layout::full_height).at(1) == lane{});
}
