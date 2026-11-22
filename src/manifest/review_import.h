/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <set>
#include <string>

#include "../model/label.h"
#include "../model/project_model.h"
#include "clip_manifest.h"

/// What applying a review's results to the project's labels would do. Reviewing a list never
/// changes the labels; this is the one way its verdicts get into them, chosen by the user.
///
/// A row acts only on a label of its own class whose span holds the row's start (give or take the
/// window): a label of another class there may be unrelated, eg. from another dataset. correct
/// confirms the label, relabel changes its class, not_event rejects it, new times move it, and
/// added makes a label. Where the row's class has no label there, correct and relabel make one
/// over the row's span if it has a stop. Applying the same results again changes nothing.
struct review_import_plan
{
    /// The labels of each recording that changes, as they'd be after
    std::map<std::filesystem::path, labels> m_files;
    size_t m_changed{};   ///< Labels relabelled, moved or confirmed
    size_t m_added{};     ///< Labels made
    size_t m_rejected{};  ///< Labels marked not an event
    size_t m_unchanged{}; ///< Rows whose label is already as the verdict says
    /// Rows that can't be applied, counted by why
    std::map<std::string, size_t> m_unapplied;
    /// Classes the rows name that the palette doesn't have
    std::set<std::string> m_unknown_classes;

    size_t unapplied() const;
};

/// Work out what applying a results file's verdicts would do, without changing anything
/// @param window_s How far a row's start may be outside a label's span and still be its label
review_import_plan plan_review_import(
    const project_model &project,
    const clip_manifest &results,
    double window_s);
