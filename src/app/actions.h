/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <filesystem>
#include <optional>
#include <set>
#include <variant>

#include "../model/label.h"
#include "../model/types.h"

namespace actions
{
struct add_label
{
    /// The time span
    time_span m_t;
    std::filesystem::path m_file;           //!< Active file to which label applied
    std::optional<label_defn_id> m_defn_id; //!< The selected label definition id at the time
};

struct delete_label
{
    label_id m_id;
};

struct select_labels
{
    double m_x;
};

struct assign_label_class
{
    std::set<label_id> m_ids;
    label_defn_id m_defn_id;
};

using app_action = std::variant<add_label, select_labels, delete_label, assign_label_class>;
}
