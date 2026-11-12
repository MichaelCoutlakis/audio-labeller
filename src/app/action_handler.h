/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <optional>

#include "../audio/audio_engine.h"
#include "../model/history.h"
#include "../model/project_model.h"
#include "actions.h"
#include "app_state.h"

struct action_handler
{
    action_handler(audio_engine &aud_eng, project_model &project, app_state &state);

    void operator()(const actions::load_file &f);
    void operator()(const actions::add_label &r);
    void operator()(const actions::select_labels &s);
    void operator()(const actions::toggle_label_selection &t);
    void operator()(const actions::clear_selection &c);
    void operator()(const actions::set_active_class &a);
    void operator()(const actions::delete_labels &d);
    void operator()(const actions::move_label &m);
    void operator()(const actions::undo &u);
    void operator()(const actions::redo &r);
    void operator()(const actions::set_file_status &s);
    void operator()(const actions::select_playback_device &dev);
    void operator()(const actions::select_playback_region &r);
    void operator()(const actions::toggle_playback &a);
    void operator()(const actions::playback_loop &p);
    void operator()(const actions::rescan_files &r);
    void operator()(const actions::export_csv &e);

private:
    audio_engine &m_audio_engine;
    project_model &m_proj;
    app_state &m_state;

    label_snapshot snapshot(const std::filesystem::path &file) const;
    void restore(const std::filesystem::path &file, const label_snapshot &s);

    /// Apply a change to a file's labels as one undo step
    template <typename F>
    void undoable(
        const std::filesystem::path &file,
        F &&change,
        std::optional<history::merge_key> key = std::nullopt)
    {
        auto before = snapshot(file);
        change();
        m_state.m_history.push(history::entry{file, std::move(before), snapshot(file), key});
    }

    void assign_class(const std::set<label_id> &ids, label_defn_id defn_id);
};
