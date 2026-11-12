/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include "../app/app_state.h"
#include "../model/project_model.h"

/// Global keyboard shortcuts, turned into actions
void handle_shortcuts(const project_model &project, app_state &state);
