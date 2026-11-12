/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <imgui.h>

#include "shortcuts.h"

void handle_shortcuts(const project_model &project, app_state &state)
{
    const auto &io = ImGui::GetIO();
    if(io.WantTextInput)
        return;

    auto pressed = [](ImGuiKeyChord chord) { return ImGui::IsKeyChordPressed(chord); };

    if(pressed(ImGuiKey_Space))
        state.add_action(actions::toggle_playback{});

    if(pressed(ImGuiKey_Delete) && !state.selected_labels().empty())
        state.add_action(actions::delete_labels{state.selected_labels()});

    if(pressed(ImGuiKey_Escape))
    {
        state.add_action(actions::clear_selection{});
        state.add_action(actions::set_active_class{std::nullopt});
    }

    if(pressed(ImGuiMod_Ctrl | ImGuiKey_Z))
        state.add_action(actions::undo{});
    if(pressed(ImGuiMod_Ctrl | ImGuiKey_Y) || pressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
        state.add_action(actions::redo{});
}
