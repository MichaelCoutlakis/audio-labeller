/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <vector>

#include <imgui.h>

#include "shortcuts.h"

namespace
{
/// A key pressed with exactly the given modifiers (and none others)
bool pressed(ImGuiKey key, ImGuiKeyChord mods = ImGuiMod_None, bool repeat = false)
{
    return ImGui::GetIO().KeyMods == mods && ImGui::IsKeyPressed(key, repeat);
}

/// All labels in the active file, including unclassified selections
std::vector<label> active_labels(const project_model &project, const app_state &state)
{
    std::vector<label> all(state.m_unlabelled.begin(), state.m_unlabelled.end());
    if(state.has_active_file())
        for(const auto &l : project.get_labels(state.m_active_file.value()))
            all.push_back(l);
    return all;
}
}

void keyboard_controller::handle(const project_model &project, app_state &state)
{
    const auto &io = ImGui::GetIO();
    if(io.WantTextInput)
        return;

    if(pressed(ImGuiKey_Space))
        state.add_action(actions::toggle_playback{});

    if(pressed(ImGuiKey_Delete) && !state.selected_labels().empty())
        state.add_action(actions::delete_labels{state.selected_labels()});

    if(pressed(ImGuiKey_Escape))
    {
        state.add_action(actions::clear_selection{});
        state.add_action(actions::set_active_class{std::nullopt});
    }

    if(pressed(ImGuiKey_Z, ImGuiMod_Ctrl))
        state.add_action(actions::undo{});
    if(pressed(ImGuiKey_Y, ImGuiMod_Ctrl) || pressed(ImGuiKey_Z, ImGuiMod_Ctrl | ImGuiMod_Shift))
        state.add_action(actions::redo{});

    // Number keys choose a palette class, like clicking its button:
    size_t index = 0U;
    for(const auto &defn : project.m_label_dict)
    {
        if(index == 9U)
            break;
        const auto key = static_cast<ImGuiKey>(ImGuiKey_1 + index);
        const auto keypad = static_cast<ImGuiKey>(ImGuiKey_Keypad1 + index);
        if(pressed(key) || pressed(keypad))
        {
            const bool active = state.m_active_label_defn == defn.m_id;
            state.add_action(
                actions::set_active_class{
                    active ? std::nullopt : std::optional<label_defn_id>{defn.m_id}
                });
        }
        ++index;
    }

    if(pressed(ImGuiKey_Tab))
        select_adjacent_label(project, state, true);
    if(pressed(ImGuiKey_Tab, ImGuiMod_Shift))
        select_adjacent_label(project, state, false);

    handle_nudges(project, state);
    handle_files(project, state);
}

void keyboard_controller::select_adjacent_label(
    const project_model &project,
    app_state &state,
    bool forward)
{
    auto all = active_labels(project, state);
    if(all.empty())
        return;
    std::ranges::sort(all, {}, [](const label &l) { return std::pair{l.m_start_s, l.m_id}; });

    // From the earliest (or latest) selected label, else from the playhead:
    auto current =
        std::ranges::find_if(all, [&](const label &l) { return state.is_label_selected(l.m_id); });
    std::vector<label>::iterator next;
    if(current == all.end())
    {
        const double t = state.m_playback_state.playhead_s;
        next = std::ranges::find_if(all, [&](const label &l) { return l.m_start_s >= t; });
        if(!forward)
            next = next == all.begin() ? all.end() - 1 : next - 1;
    }
    else if(forward)
        next = current + 1 == all.end() ? all.begin() : current + 1;
    else
        next = current == all.begin() ? all.end() - 1 : current - 1;
    if(next == all.end())
        next = all.begin();

    state.add_action(actions::clear_selection{});
    state.add_action(actions::toggle_label_selection{next->m_id});
    state.select_playback_region(next->m_start_s, next->m_stop_s);
}

void keyboard_controller::handle_nudges(const project_model &project, app_state &state)
{
    // Arrows move whole labels, Ctrl+arrows the start edge and Shift+arrows the stop edge
    const bool held = ImGui::IsKeyDown(ImGuiKey_LeftArrow) || ImGui::IsKeyDown(ImGuiKey_RightArrow);
    if(held && !m_nudging)
        m_nudge_seq = state.next_edit_seq(); // A burst of nudges is one undo step
    m_nudging = held;

    for(const auto mods : {ImGuiMod_None, ImGuiMod_Ctrl, ImGuiMod_Shift})
    {
        double delta = 0.;
        if(pressed(ImGuiKey_LeftArrow, mods, true))
            delta = -nudge_s;
        if(pressed(ImGuiKey_RightArrow, mods, true))
            delta = nudge_s;
        if(delta == 0.)
            continue;

        for(const auto &l : active_labels(project, state))
        {
            if(!state.is_label_selected(l.m_id))
                continue;
            const bool move_start = mods != ImGuiMod_Shift;
            const bool move_stop = mods != ImGuiMod_Ctrl;
            state.add_action(
                actions::move_label{
                    l.m_id,
                    l.m_start_s + (move_start ? delta : 0.),
                    l.m_stop_s + (move_stop ? delta : 0.),
                    m_nudge_seq
                });
        }
    }
}

void keyboard_controller::handle_files(const project_model &project, app_state &state)
{
    const auto files = state.filtered_files(project);
    const auto &all = project.get_files();
    if(all.empty())
        return;

    auto load = [&](const audio_file_entry *file)
    {
        if(file)
            state.add_action(actions::load_file{file->m_path});
    };

    // Position of the active file in a list, or -1
    auto position = [&](const auto &list, auto to_path) -> ptrdiff_t
    {
        for(size_t i = 0; i != list.size(); ++i)
            if(state.is_selected_file(to_path(list[i])))
                return static_cast<ptrdiff_t>(i);
        return -1;
    };

    // N and P step through the files in the list as filtered
    if(!files.empty() && (pressed(ImGuiKey_N) || pressed(ImGuiKey_P)))
    {
        const auto n = static_cast<ptrdiff_t>(files.size());
        const auto i = position(files, [](const audio_file_entry *f) { return f->m_path; });
        const auto step = pressed(ImGuiKey_N) ? 1 : -1;
        const auto next = i < 0 ? (step > 0 ? 0 : n - 1) : (i + step + n) % n;
        load(files[static_cast<size_t>(next)]);
    }

    if(!state.has_active_file())
        return;
    const auto &active = state.m_active_file.value();
    const auto status = project.get_status(active);

    // D marks the file done and moves on to the next one to do. S does the same for skip.
    for(const auto [key, target] :
        {std::pair{ImGuiKey_D, file_status::done}, std::pair{ImGuiKey_S, file_status::skip}})
    {
        if(!pressed(key))
            continue;
        if(status == target)
        {
            state.add_action(actions::set_file_status{active, file_status::todo});
            continue;
        }
        state.add_action(actions::set_file_status{active, target});

        const auto i = position(all, [](const audio_file_entry &f) { return f.m_path; });
        for(size_t step = 1; step < all.size(); ++step)
        {
            const auto &candidate = all[(static_cast<size_t>(i) + step) % all.size()];
            if(project.get_status(candidate.m_path) == file_status::todo)
            {
                load(&candidate);
                break;
            }
        }
    }
}
