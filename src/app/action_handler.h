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
    void operator()(const actions::assign_class &a);
    void operator()(const actions::accept_labels &a);
    void operator()(const actions::reject_labels &r);
    void operator()(const actions::delete_labels &d);
    void operator()(const actions::move_label &m);
    void operator()(const actions::snap_labels &s);
    void operator()(const actions::review_step &r);
    void operator()(const actions::undo &u);
    void operator()(const actions::redo &r);
    void operator()(const actions::set_file_status &s);
    void operator()(const actions::set_channel &c);
    void operator()(const actions::select_playback_device &dev);
    void operator()(const actions::select_playback_region &r);
    void operator()(const actions::toggle_playback &a);
    void operator()(const actions::playback_loop &p);
    void operator()(const actions::set_playback_speed &s);
    void operator()(const actions::set_playback_gain &g);
    void operator()(const actions::rescan_files &r);
    void operator()(const actions::export_csv &e);
    void operator()(const actions::export_dataset &e);
    void operator()(const actions::cancel_export &c);
    void operator()(const actions::set_export_settings &s);
    void operator()(const actions::import_annotations &i);
    void operator()(const actions::export_annotations &e);
    void operator()(const actions::play_clip &p);
    void operator()(const actions::judge_clip &j);
    void operator()(const actions::jump_to &j);
    void operator()(const actions::run_assist &r);
    void operator()(const actions::cancel_assist &c);

    /// Pick up the results of background exports and assisted labelling once they've finished.
    /// Call every frame.
    void poll_background();

    /// Stop a background export, waiting for it
    void cancel_background();

private:
    audio_engine &m_audio_engine;
    project_model &m_proj;
    app_state &m_state;

    label_snapshot snapshot(const std::filesystem::path &file) const;
    /// If a label whose span is being played is removed, play on to the end of the file instead
    void release_playback_of_removed(const label_snapshot &before, const label_snapshot &after);
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

    /// Change the labels (in the active file, including unclassified selections) with these IDs
    template <typename F>
    void edit_labels(const std::set<label_id> &ids, F &&edit);

    void assign_class(const std::set<label_id> &ids, label_defn_id defn_id);

    /// Play a label's playback window, if auto play is on
    void play_label(const label &l);

    /// Give the audio engine the active file again, after playing a manifest clip
    void restore_active_audio();

    void report_export(const export_summary &summary);
    void poll_assist();
};
