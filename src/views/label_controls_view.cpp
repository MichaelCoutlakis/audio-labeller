/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#include <algorithm>
#include <string>

#include <imgui.h>

#include "../model/palette.h"
#include "label_controls_view.h"

namespace
{
constexpr float button_w = 310.0f;
constexpr float button_h = 32.0f;

/// Columns of buttons that fit in a width
int columns_for(float width)
{
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    return std::max(1, static_cast<int>((width + spacing) / (button_w + spacing)));
}
}

void label_control_view::render(const project_model &project, app_state &state)
{
    ImGui::Begin(m_window_name.c_str());

    // Favourites on the left, the filter on the right, as wide as a button
    const float filter_w = button_w + ImGui::GetStyle().CellPadding.x * 2.f;
    if(ImGui::BeginTable("Palette top", 2, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("Favourites", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Filter", ImGuiTableColumnFlags_WidthFixed, filter_w);
        ImGui::TableNextColumn();
        render_favourites(project, state, ImGui::GetContentRegionAvail().x);
        ImGui::TableNextColumn();
        render_filter(project, state, ImGui::GetContentRegionAvail().x);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Palette");
    render_palette(project, state);
    ImGui::End();
}

void label_control_view::class_button(
    const label_defn &def,
    app_state &state,
    float width,
    std::optional<size_t> hotkey)
{
    const bool active = state.m_active_label_defn == def.m_id;
    ImGui::PushID(static_cast<int>(def.m_id));

    // Colour with a slightly more opaque "active"
    ImVec4 col_v = ImGui::ColorConvertU32ToFloat4(def.m_color_rgba);
    if(active)
    {
        col_v.x = std::min(col_v.x + 0.25f, 1.0f);
        col_v.y = std::min(col_v.y + 0.25f, 1.0f);
        col_v.z = std::min(col_v.z + 0.25f, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
    }
    ImGui::PushStyleColor(ImGuiCol_Button, col_v);

    // Toggles. Selecting a class also applies it to the selected labels. Ctrl+click adds a label of
    // it over each selected one instead.
    std::string text = def.m_class;
    if(hotkey)
        text = std::to_string(*hotkey + 1) + "   " + text;
    if(state.is_favourite(def))
        text += " *";
    const bool clicked = ImGui::Button(text.c_str(), {width, button_h});
    if(clicked && ImGui::GetIO().KeyMods == ImGuiMod_Ctrl)
        state.add_action(actions::add_class{def.m_id});
    else if(clicked)
        state.add_action(
            actions::set_active_class{
                active ? std::nullopt : std::optional<label_defn_id>{def.m_id}
            });

    if(active)
        ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if(ImGui::BeginPopupContextItem("pin"))
    {
        if(ImGui::MenuItem(state.is_favourite(def) ? "Unpin from favourites" : "Pin to favourites"))
            state.toggle_favourite(def);
        ImGui::EndPopup();
    }
    ImGui::SetItemTooltip(
        "Ctrl+click to add it over the selected labels\nRight-click to %s",
        state.is_favourite(def) ? "unpin" : "pin");
    ImGui::PopID();
}

namespace
{
std::optional<size_t> hotkey_of(const std::vector<label_defn_id> &numbered, label_defn_id id)
{
    const auto it = std::ranges::find(numbered, id);
    if(it == numbered.end())
        return std::nullopt;
    return static_cast<size_t>(it - numbered.begin());
}
}

void label_control_view::render_favourites(
    const project_model &project,
    app_state &state,
    float width)
{
    std::vector<const label_defn *> favourites;
    for(const auto &name : state.m_settings.m_favourite_classes)
        for(const auto &defn : project.m_label_dict)
            if(defn.m_class == name)
                favourites.push_back(&defn);

    ImGui::TextUnformatted("Favourites");
    if(favourites.empty())
    {
        ImGui::TextDisabled("Right-click a class to pin it here. Pinned, 1-9 choose them.");
        return;
    }

    const auto numbered = state.numbered_classes(project);
    ImGui::PushID("favourites");
    const int cols = columns_for(width);
    for(size_t i = 0; i != favourites.size(); ++i)
    {
        if(i % static_cast<size_t>(cols) != 0U)
            ImGui::SameLine();
        class_button(*favourites[i], state, button_w, hotkey_of(numbered, favourites[i]->m_id));
    }
    ImGui::PopID();
}

void label_control_view::render_filter(
    const project_model &project,
    app_state &state,
    float width)
{
    const auto matches = filter_classes(project.m_label_dict, m_filter.data());

    if(state.m_focus_class_filter)
    {
        ImGui::SetKeyboardFocusHere();
        state.m_focus_class_filter = false;
    }
    ImGui::SetNextItemWidth(width);
    // Enter chooses the first match. Escape clears the box.
    if(ImGui::InputTextWithHint(
           "##class filter",
           "Find a class (Ctrl+F)",
           m_filter.data(),
           m_filter.size(),
           ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll) &&
       !matches.empty())
        state.add_action(actions::set_active_class{matches.front()->m_id});

    if(m_filter[0] == '\0')
        return;
    if(matches.empty())
    {
        ImGui::TextDisabled("No class matches");
        return;
    }

    // Room for two buttons and a bit, so that it's clear when there are more to scroll to
    const auto numbered = state.numbered_classes(project);
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    const float shown = std::min(static_cast<float>(matches.size()), 2.5f);
    const float height = shown * (button_h + spacing);
    ImGui::PushID("matches");
    if(ImGui::BeginChild("matches", {width, height}))
        for(const auto *def : matches)
            class_button(*def, state, ImGui::GetContentRegionAvail().x, hotkey_of(numbered, def->m_id));
    ImGui::EndChild();
    ImGui::PopID();
}

void label_control_view::render_palette(const project_model &project, app_state &state)
{
    const auto numbered = state.numbered_classes(project);
    const int num_cols = columns_for(ImGui::GetContentRegionAvail().x);

    if(ImGui::BeginTable("Label Palette", num_cols))
    {
        for(const auto &def : project.m_label_dict)
        {
            ImGui::TableNextColumn();
            class_button(def, state, button_w, hotkey_of(numbered, def.m_id));
        }
        ImGui::EndTable();
    }
}
