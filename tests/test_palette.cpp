/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <doctest/doctest.h>

#include "model/palette.h"

namespace
{
label_dict make_dict(std::initializer_list<const char *> names)
{
    label_dict dict;
    uint32_t id = 0;
    for(const auto *name : names)
        dict.add(label_defn{label_defn_id{id++}, name});
    return dict;
}

std::vector<std::string> names(const std::vector<const label_defn *> &defns)
{
    std::vector<std::string> out;
    for(const auto *d : defns)
        out.push_back(d->m_class);
    return out;
}
}

TEST_CASE("the palette filter matches every word, names starting with it first")
{
    const auto dict = make_dict({"Kangaroo", "Rooster", "Grey kangaroo", "Dog"});
    CHECK(names(filter_classes(dict, "roo")) ==
          std::vector<std::string>{"Rooster", "Kangaroo", "Grey kangaroo"});
    CHECK(names(filter_classes(dict, "  KANG grey ")) == std::vector<std::string>{"Grey kangaroo"});
    CHECK(filter_classes(dict, "cat").empty());
    CHECK(filter_classes(dict, " ").empty());
}

TEST_CASE("number keys go to the favourites, else the start of the palette")
{
    const auto dict = make_dict({"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k"});
    const auto ids = [](std::initializer_list<uint32_t> v)
    {
        std::vector<label_defn_id> out;
        for(auto i : v)
            out.push_back(label_defn_id{i});
        return out;
    };
    CHECK(numbered_classes(dict, {}) == ids({0, 1, 2, 3, 4, 5, 6, 7, 8}));
    CHECK(numbered_classes(dict, {"k", "missing", "b"}) == ids({10, 1}));
    CHECK(numbered_classes(dict, {"missing"}) == ids({0, 1, 2, 3, 4, 5, 6, 7, 8}));
}
