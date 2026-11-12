/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <set>
#include <utility>

#include "label.h"

/// The undoable state of one file: its labels, plus the unclassified selections and the selection
/// when it's the active file
struct label_snapshot
{
    labels m_labels;
    labels m_unlabelled;
    std::set<label_id> m_selection;

    bool operator==(const label_snapshot &) const = default;
};

/// Undo/redo by snapshots of a file's labels before and after each change. Snapshots can't get out
/// of step with the code making the changes, and a file's labels are small.
class history
{
public:
    /// Consecutive entries with the same key (eg. one drag of one label) merge into one step
    using merge_key = std::pair<uint64_t, label_id>;

    struct entry
    {
        std::filesystem::path m_file;
        label_snapshot m_before;
        label_snapshot m_after;
        std::optional<merge_key> m_merge_key;
    };

    void push(entry e)
    {
        if(e.m_before == e.m_after)
            return;
        m_redo.clear();

        if(e.m_merge_key &&
           !m_undo.empty() &&
           m_undo.back().m_merge_key == e.m_merge_key &&
           m_undo.back().m_file == e.m_file)
        {
            m_undo.back().m_after = std::move(e.m_after);
            return;
        }
        m_undo.push_back(std::move(e));
        if(m_undo.size() > max_entries)
            m_undo.pop_front();
    }

    /// @return the entry to undo, whose m_before should be restored
    std::optional<entry> undo() { return move_back(m_undo, m_redo); }

    /// @return the entry to redo, whose m_after should be restored
    std::optional<entry> redo() { return move_back(m_redo, m_undo); }

    bool can_undo() const { return !m_undo.empty(); }
    bool can_redo() const { return !m_redo.empty(); }

private:
    static std::optional<entry> move_back(std::deque<entry> &from, std::deque<entry> &to)
    {
        if(from.empty())
            return std::nullopt;
        to.push_back(std::move(from.back()));
        from.pop_back();
        return to.back();
    }

    static constexpr size_t max_entries = 1000U;
    std::deque<entry> m_undo;
    std::deque<entry> m_redo;
};
