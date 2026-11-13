/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <optional>
#include <string>

#include "../app/app_state.h"
#include "../dsp/loudness.h"
#include "../model/project_model.h"

/// Runs an assisted labeller (an external detector from the project, or a built-in) over the
/// active file or many, adding its proposals as suggestions to review
class assist_view
{
public:
    void render(const project_model &project, app_state &state);

    std::string m_window_name{"Assisted Labelling"};

private:
    int m_labeller{0};
    int m_scope{0};
    std::optional<label_defn_id> m_class; ///< For built-ins; follows the palette brush until set
    std::optional<loudness_settings> m_loudness; ///< Edited copy of the built-in's settings
    int m_loudness_for{-1};                      ///< Which labeller m_loudness was copied from
};
