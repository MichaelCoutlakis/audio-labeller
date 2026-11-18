/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <imgui.h>

#include "../model/paths.h"
#include "paste_dialog_view.h"

void paste_dialog_view::render(app_state &state)
{
    constexpr const char *title = "Paste labels";
    if(state.m_show_paste_dialog && !ImGui::IsPopupOpen(title))
    {
        ImGui::OpenPopup(title);
        m_focus_shift = true;
    }
    if(!ImGui::BeginPopupModal(
           title,
           &state.m_show_paste_dialog,
           ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const auto close = [&]
    {
        state.m_show_paste_dialog = false;
        ImGui::CloseCurrentPopup();
    };
    const auto &clipboard = state.m_label_clipboard;
    if(!state.m_show_paste_dialog || clipboard.m_labels.empty())
    {
        close();
        ImGui::EndPopup();
        return;
    }

    ImGui::Text(
        "%zu labels copied from %s",
        clipboard.m_labels.size(),
        generic_utf8(clipboard.m_source.filename()).c_str());

    if(m_focus_shift)
    {
        ImGui::SetKeyboardFocusHere();
        m_focus_shift = false;
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.f);
    bool paste = ImGui::InputDouble(
        "Shift (s)",
        &state.m_paste_offset_s,
        0.1,
        1.,
        "%.3f",
        ImGuiInputTextFlags_EnterReturnsTrue);
    if(ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Added to the labels' times: for another recording of the same scene, its start "
            "offset (negative if this recording started later). Labels that fall outside this "
            "recording are left out.");

    int as_labels = state.m_paste_as_labels ? 1 : 0;
    ImGui::RadioButton("As suggestions", &as_labels, 0);
    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("To check that this recording heard each sound: Enter accepts, X drops");
    ImGui::SameLine();
    ImGui::RadioButton("As labels", &as_labels, 1);
    state.m_paste_as_labels = as_labels == 1;

    ImGui::TextDisabled("They stay selected, so the arrow keys move them together.");

    paste |= ImGui::Button("Paste");
    paste |= !ImGui::GetIO().WantTextInput &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape);

    if(paste)
        state.add_action(
            actions::paste_labels{
                state.m_paste_offset_s,
                state.m_paste_as_labels ? label_state::confirmed : label_state::suggested
            });
    if(paste || cancel)
        close();
    ImGui::EndPopup();
}
