/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <doctest/doctest.h>

#include "model/history.h"

namespace
{
label_snapshot with_label(label_id id, double start)
{
    label_snapshot s;
    s.m_labels.add(label{id, label_defn_id{0}, start, start + 1.});
    return s;
}
}

TEST_CASE("history undoes and redoes")
{
    history h;
    CHECK(!h.can_undo());

    h.push({"a.wav", label_snapshot{}, with_label(1, 0.), std::nullopt});
    h.push({"a.wav", with_label(1, 0.), with_label(1, 2.), std::nullopt});

    auto e = h.undo();
    REQUIRE(e);
    CHECK(e->m_before == with_label(1, 0.));
    CHECK(h.can_redo());

    e = h.redo();
    REQUIRE(e);
    CHECK(e->m_after == with_label(1, 2.));
    CHECK(!h.can_redo());
}

TEST_CASE("history ignores no-op changes and clears redo on a new change")
{
    history h;
    h.push({"a.wav", with_label(1, 0.), with_label(1, 0.), std::nullopt});
    CHECK(!h.can_undo());

    h.push({"a.wav", label_snapshot{}, with_label(1, 0.), std::nullopt});
    h.undo();
    h.push({"a.wav", label_snapshot{}, with_label(2, 0.), std::nullopt});
    CHECK(!h.can_redo());
}

TEST_CASE("history merges consecutive changes with the same key")
{
    history h;
    const history::merge_key drag{1, 7};
    h.push({"a.wav", with_label(7, 0.), with_label(7, 1.), drag});
    h.push({"a.wav", with_label(7, 1.), with_label(7, 2.), drag});
    h.push({"a.wav", with_label(7, 2.), with_label(7, 3.), history::merge_key{2, 7}});

    auto e = h.undo();
    CHECK(e->m_before == with_label(7, 2.));
    e = h.undo();
    CHECK(e->m_before == with_label(7, 0.)); // The first drag is one step
    CHECK(e->m_after == with_label(7, 2.));
    CHECK(!h.can_undo());
}
