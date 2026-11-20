/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../app/app_state.h"
#include "../manifest/clip_manifest.h"
#include "../model/project_model.h"

/// The open review list (clip manifest) as a filterable, sortable table. Stepping through it shows
/// each event in its recording and plays it there; one key gives a verdict, which edits the label
/// at the event and is written to the list's results file.
class clip_manifest_view
{
public:
    void render(const project_model &project, app_state &state);

    std::string m_window_name{"Review List"};

private:
    /// Start again on a newly opened list, with its remembered filter and sort
    void take_list(const project_model &project, app_state &state);
    void apply_filter_and_sort(const project_model &project, const app_state &state);
    void remember_view(app_state &state) const;
    void handle_keys(const project_model &project, app_state &state);
    void select(app_state &state, size_t position, bool play = true);
    void verdict(const project_model &project, app_state &state, clip_verdict v, label_defn_id cls);
    void render_note(app_state &state);

    /// The text of a cell: the list's columns, then the verdict, note and what the recording says
    std::string cell(const project_model &project, const app_state &state, size_t row, size_t column)
        const;
    /// What the source recording now says at the clip's onset
    std::string current_label(
        const project_model &project,
        const app_state &state,
        const clip_manifest::row &row) const;
    size_t column_count(const app_state &state) const;
    std::string column_name(const app_state &state, size_t column) const;

    uint64_t m_generation{}; ///< Of the list shown
    std::string m_filter;
    std::vector<size_t> m_order; ///< Rows passing the filter, in sorted order
    int m_sort_column{-1};
    bool m_sort_descending{false};
    bool m_dirty{true};
    std::optional<size_t> m_position; ///< Selected position in m_order
    bool m_scroll_to_selection{false};

    std::string m_note;                  ///< Being typed for the selected row
    std::optional<size_t> m_note_row;    ///< The row m_note is for
    bool m_focus_note{false};
};
