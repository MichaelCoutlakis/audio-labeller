/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include "../model/project_model.h"
#include "actions.h"
#include "app_state.h"


struct action_handler
{
    action_handler(project_model &project, app_state &state);

    void operator()(const actions::add_label &r);

    void operator()(const actions::select_labels &s);

    project_model &m_proj;
    app_state &m_state;
};