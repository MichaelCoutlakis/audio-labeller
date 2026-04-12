/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once
#include "../model/project_model.h"

class app_state
{
public:
    bool is_label_selected(label_id id) { return m_selected_labels.count(id); }

    void toggle_label_selection(label_id id)
    {
        if(!m_selected_labels.erase(id))
            m_selected_labels.insert(id);
    }

    std::optional<std::filesystem::path> m_active_file;

private:
    std::set<label_id> m_selected_labels;
};
