/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include "../app/app_state.h"

/// Ctrl+Shift+V: paste the copied labels with a shift (eg. the start offset between two
/// recordings of the same scene), as suggestions or labels
class paste_dialog_view
{
public:
    void render(app_state &state);

private:
    bool m_focus_shift{false};
};
