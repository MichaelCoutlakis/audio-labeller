/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../app/app_state.h"
#include "../manifest/clip_manifest.h"
#include "../model/project_model.h"

/// The clips an external pipeline cut, as a filterable, sortable list. Each can be played and
/// judged with one key; verdicts edit the label at the clip's onset in its source recording.
class clip_manifest_view
{
public:
    void render(const project_model &project, app_state &state);

    std::string m_window_name{"Clip Manifest"};

private:
    void load(const project_model &project);
    void apply_filter_and_sort(const project_model &project);
    void handle_keys(const project_model &project, app_state &state);
    void select(app_state &state, size_t position);
    void verdict(const project_model &project, app_state &state, clip_verdict v, label_defn_id cls);

    /// What the source recording now says at the clip's onset
    std::string current_label(const project_model &project, const clip_manifest::row &row) const;

    bool m_loaded{false};
    std::optional<clip_manifest_settings> m_settings;
    clip_manifest m_manifest;
    std::string m_error;

    std::string m_filter;
    std::vector<size_t> m_order; ///< Rows passing the filter, in sorted order
    int m_sort_column{-1};
    bool m_sort_descending{false};
    bool m_dirty{true};
    std::optional<size_t> m_position; ///< Selected position in m_order
    bool m_scroll_to_selection{false};
};
