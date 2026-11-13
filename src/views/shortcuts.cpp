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

/// Visible labels in the active file, including unclassified selections
std::vector<label> active_labels(const project_model &project, const app_state &state)
{
    std::vector<label> all;
    for(const auto *l : state.visible_labels(project))
        all.push_back(*l);
    return all;
}

bool any_selected_suggestion(const project_model &project, const app_state &state)
{
    return std::ranges::any_of(
        active_labels(project, state),
        [&](const label &l)
        { return state.is_label_selected(l.m_id) && l.m_state == label_state::suggested; });
}
}

const std::vector<std::pair<const char *, const char *>> &keyboard_controller::descriptions()
{
    static const std::vector<std::pair<const char *, const char *>> keys = {
        {"1-9", "Choose a palette class (applies to the selection, confirming suggestions)"},
        {"Space", "Play / stop"},
        {"Tab, Shift+Tab", "Select and play the next / previous label (suggestion in review mode)"},
        {"R", "Review mode: step through suggestions in all files"},
        {"Enter", "Accept the selected suggestions"},
        {"X", "Reject the selected labels: not an event"},
        {"H", "Show / hide rejected candidates"},
        {"O", "Add an onset under the mouse, or at the playhead"},
        {"A", "Snap the selected onsets to the sharpest rise nearby (Ctrl+Z undoes)"},
        {"Left, Right", "Move the selected labels (Alt: finer)"},
        {"Ctrl+Left, Ctrl+Right", "Move their start edges"},
        {"Shift+Left, Shift+Right", "Move their stop edges"},
        {"Delete", "Delete the selected labels"},
        {"Escape", "Clear the selection and palette class"},
        {"Ctrl+Z, Ctrl+Y", "Undo, redo"},
        {"N, P", "Next / previous file in the list"},
        {"D", "Mark the file done and open the next one to do"},
        {"S", "Mark the file skipped and open the next one to do"},
    };
    return keys;
}

void keyboard_controller::handle(const project_model &project, app_state &state)
{
    const auto &io = ImGui::GetIO();
    if(io.WantTextInput || state.m_keys_captured)
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

    if(pressed(ImGuiKey_H))
        state.settings.m_show_rejected = !state.settings.m_show_rejected;
    if(pressed(ImGuiKey_R))
        state.m_review_mode = !state.m_review_mode;

    // Number keys choose a palette class, like clicking its button. In review mode they relabel
    // the selection and move on.
    size_t index = 0U;
    for(const auto &defn : project.m_label_dict)
    {
        if(index == 9U)
            break;
        const auto key = static_cast<ImGuiKey>(ImGuiKey_1 + index);
        const auto keypad = static_cast<ImGuiKey>(ImGuiKey_Keypad1 + index);
        if(pressed(key) || pressed(keypad))
        {
            const bool advance = state.m_review_mode &&
                state.settings.m_auto_advance &&
                any_selected_suggestion(project, state);
            if(advance)
            {
                state.add_action(actions::assign_class{state.selected_labels(), defn.m_id});
                state.add_action(actions::review_step{true});
            }
            else
            {
                const bool active = state.m_active_label_defn == defn.m_id;
                state.add_action(
                    actions::set_active_class{
                        active ? std::nullopt : std::optional<label_defn_id>{defn.m_id}
                    });
            }
        }
        ++index;
    }

    if(pressed(ImGuiKey_O) && state.has_active_file())
    {
        const double t = state.m_hover_time_s.value_or(state.m_playback_state.playhead_s);
        state.add_action(
            actions::add_label{{t, t}, state.m_active_file.value(), state.m_active_label_defn});
    }
    if(pressed(ImGuiKey_A) && !state.selected_labels().empty())
        state.add_action(actions::snap_labels{state.selected_labels()});

    handle_review(project, state);
    handle_nudges(project, state);
    handle_files(project, state);
}

void keyboard_controller::handle_review(const project_model &project, app_state &state)
{
    const bool advance = state.m_review_mode && state.settings.m_auto_advance;
    const auto &selection = state.selected_labels();

    if((pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) && !selection.empty())
    {
        const bool was_suggestion = any_selected_suggestion(project, state);
        state.add_action(actions::accept_labels{selection});
        if(advance && was_suggestion)
            state.add_action(actions::review_step{true});
    }
    if(pressed(ImGuiKey_X) && !selection.empty())
    {
        const bool was_suggestion = any_selected_suggestion(project, state);
        state.add_action(actions::reject_labels{selection});
        if(advance && was_suggestion)
            state.add_action(actions::review_step{true});
    }

    for(const bool forward : {true, false})
    {
        if(!pressed(ImGuiKey_Tab, forward ? ImGuiMod_None : ImGuiMod_Shift))
            continue;
        if(state.m_review_mode)
            state.add_action(actions::review_step{forward});
        else
            select_adjacent_label(project, state, forward);
    }
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
    const auto window = state.playback_window(*next);
    state.select_playback_region(window.first, window.second);
}

void keyboard_controller::handle_nudges(const project_model &project, app_state &state)
{
    // Arrows move whole labels, Ctrl+arrows the start edge and Shift+arrows the stop edge. Alt
    // makes the step finer.
    const bool held = ImGui::IsKeyDown(ImGuiKey_LeftArrow) || ImGui::IsKeyDown(ImGuiKey_RightArrow);
    if(held && !m_nudging)
        m_nudge_seq = state.next_edit_seq(); // A burst of nudges is one undo step
    m_nudging = held;

    for(const auto edge : {ImGuiMod_None, ImGuiMod_Ctrl, ImGuiMod_Shift})
    {
        for(const bool fine : {false, true})
        {
            const ImGuiKeyChord mods = edge | (fine ? ImGuiMod_Alt : ImGuiMod_None);
            const double step = fine ? state.settings.m_fine_nudge_s : state.settings.m_nudge_s;
            double delta = 0.;
            if(pressed(ImGuiKey_LeftArrow, mods, true))
                delta = -step;
            if(pressed(ImGuiKey_RightArrow, mods, true))
                delta = step;
            if(delta == 0.)
                continue;

            for(const auto &l : active_labels(project, state))
            {
                if(!state.is_label_selected(l.m_id))
                    continue;
                // An onset has one edge, which always moves
                const bool move_start = edge != ImGuiMod_Shift || l.is_onset();
                const bool move_stop = edge != ImGuiMod_Ctrl || l.is_onset();
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
