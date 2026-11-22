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
#include <string>
#include <utility>
#include <vector>

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

/// What a review list (see manifest/clip_manifest.h) says about one of its rows once heard
struct row_verdict
{
    /// correct, relabel, not_event, or added for a row the reviewer added; empty while unheard
    std::string m_verdict;
    std::string m_new_label; ///< The class it was relabelled to, or an added row's class
    /// Where the reviewer moved it to, in the recording's time; nullopt if not moved
    std::optional<double> m_new_start_s;
    std::optional<double> m_new_stop_s;
    std::string m_note;

    bool operator==(const row_verdict &) const = default;
};

/// A change to one row's verdict in a review list, undone with the labels it changed
struct row_verdict_change
{
    std::filesystem::path m_list;
    size_t m_row{};
    row_verdict m_before;
    row_verdict m_after;
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
        /// A review list row the step also changed. m_file is empty if it changed no labels.
        std::optional<row_verdict_change> m_list_change;
        /// More recordings the step changed, eg. importing a review's edits
        std::vector<entry> m_more;
    };

    void push(entry e)
    {
        const bool list_changed =
            e.m_list_change && e.m_list_change->m_before != e.m_list_change->m_after;
        if(e.m_before == e.m_after && !list_changed)
            return;
        m_redo.clear();

        if(e.m_merge_key &&
           !m_undo.empty() &&
           m_undo.back().m_merge_key == e.m_merge_key &&
           m_undo.back().m_file == e.m_file)
        {
            m_undo.back().m_after = std::move(e.m_after);
            if(auto &change = m_undo.back().m_list_change; change && e.m_list_change)
                change->m_after = std::move(e.m_list_change->m_after);
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
