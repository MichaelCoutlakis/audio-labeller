/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#include <algorithm>
#include <chrono>
#include <cmath>

#include <spdlog/spdlog.h>

#include "../dsp/onset_snap.h"
#include "../export/clip_export.h"
#include "../interchange/annotation_tree.h"
#include "../io/audio_source.h"
#include "../manifest/clip_manifest.h"
#include "../model/paths.h"
#include "action_handler.h"
#include "actions.h"
#include "lanes.h"

action_handler::action_handler(audio_engine &aud_eng, project_model &project, app_state &state) :
    m_audio_engine(aud_eng),
    m_proj(project),
    m_state(state)
{
}

label_snapshot action_handler::snapshot(const std::filesystem::path &file) const
{
    label_snapshot s{m_proj.get_labels(file)};
    if(m_state.is_selected_file(file))
    {
        s.m_unlabelled = m_state.m_unlabelled;
        s.m_selection = m_state.selected_labels();
    }
    return s;
}

void action_handler::restore(const std::filesystem::path &file, const label_snapshot &s)
{
    m_proj.set_labels(file, s.m_labels);

    // Selections only exist for the active file:
    if(m_state.is_selected_file(file))
    {
        m_state.m_unlabelled = s.m_unlabelled;
        m_state.set_selection(s.m_selection);
    }
}

template <typename F>
void action_handler::edit_labels(const std::set<label_id> &ids, F &&edit)
{
    if(!m_state.has_active_file())
        return;
    const auto &file = m_state.m_active_file.value();
    for(auto id : ids)
    {
        if(auto *unlabelled = m_state.m_unlabelled.find_ptr(id))
            edit(*unlabelled);
        else if(const auto *existing = m_proj.get_labels(file).find_ptr(id))
        {
            auto l = *existing;
            edit(l);
            m_proj.update_label(file, l);
        }
    }
}

void action_handler::restore_active_audio()
{
    if(!m_state.m_playing_clip)
        return;
    m_state.m_playing_clip = false;
    m_audio_engine.set_audio_clip(m_state.get_audio_buffer_ptr());
}

void action_handler::play_label(const label &l)
{
    restore_active_audio();
    m_audio_engine.select_region(m_state.playback_window(l), m_state.m_loop);
    if(m_state.m_auto_start)
        m_audio_engine.play();
}

void action_handler::operator()(const actions::load_file &f)
{
    if(m_state.is_selected_file(f.file_path))
    {
        restore_active_audio();
        return;
    }
    spdlog::info("loading file {}", generic_utf8(f.file_path));
    m_state.set_selected_file(f.file_path, m_proj.get_channel(f.file_path));
    m_state.m_playing_clip = false;

    m_audio_engine.set_audio_clip(m_state.get_audio_buffer_ptr());
    m_audio_engine.select_region(
        time_span{0, m_state.get_audio_buffer().duration_s()},
        m_state.m_loop);
}

void action_handler::operator()(const actions::add_label &r)
{
    undoable(
        r.m_file,
        [&]
        {
            label l{
                get_next_label_id(),
                r.m_defn_id.value_or(label_defn_id{}),
                r.m_t.first,
                r.m_t.second
            };
            // If a class was selected in the label palette, apply that, otherwise leave it as an
            // unlabelled selection:
            if(r.m_defn_id)
                m_proj.add_label(r.m_file, l);
            else
                m_state.m_unlabelled.add(l);

            // Addition of a label / selection replaces the selection:
            m_state.set_selection({l.m_id});
        });
}

void action_handler::operator()(const actions::select_labels &s)
{
    const auto bands = m_state.label_bands(m_proj);
    auto hit = [&](const label &l)
    {
        const auto band = bands.find(l.m_id);
        if(band == bands.end() || !band->second.contains(s.m_y))
            return false;
        if(l.is_onset())
            return std::abs(s.m_x - l.m_start_s) <= s.m_tolerance_s;
        return l.m_start_s <= s.m_x && s.m_x <= l.m_stop_s;
    };

    std::vector<const label *> hits;
    for(const auto *l : m_state.visible_labels(m_proj))
    {
        if(!hit(*l))
            continue;
        // Prevent the selected label palette from going out of sync with the selections:
        if(!m_state.is_label_selected(l->m_id) && l->m_defn_id != m_state.m_active_label_defn)
            m_state.m_active_label_defn.reset();

        if(m_state.toggle_label_selection(l->m_id))
            hits.push_back(l);
    }

    // Set the playback selection to the most recently selected region:
    if(!hits.empty())
    {
        const auto *last = hits.back();
        if(last->m_state == label_state::suggested)
            m_state.m_review_pos = review_item{
                m_state.m_active_file.value(),
                last->m_id,
                last->m_start_s,
                last->m_score
            };
        play_label(*last);
    }
}

void action_handler::operator()(const actions::toggle_label_selection &t)
{
    m_state.toggle_label_selection(t.m_id, t.m_clear_others);
}

void action_handler::operator()(const actions::clear_selection &c) { m_state.clear_selections(); }

void action_handler::assign_class(const std::set<label_id> &ids, label_defn_id defn_id)
{
    const auto &file = m_state.m_active_file.value();
    for(auto id : ids)
    {
        if(auto unlabelled = m_state.m_unlabelled.find_ptr(id))
        {
            auto l = *unlabelled;
            m_state.m_unlabelled.erase(unlabelled);
            l.m_defn_id = defn_id;
            m_proj.add_label(file, l);
        }
        else if(auto existing = m_proj.get_labels(file).find_ptr(id))
        {
            // Relabelling a suggestion (or a rejected one) confirms it
            auto l = *existing;
            l.m_defn_id = defn_id;
            l.m_state = label_state::confirmed;
            m_proj.update_label(file, l);
        }
    }
}

void action_handler::operator()(const actions::set_active_class &a)
{
    m_state.m_active_label_defn = a.m_defn_id;
    if(!a.m_defn_id || !m_state.has_active_file())
        return;

    undoable(
        m_state.m_active_file.value(),
        [&] { assign_class(m_state.selected_labels(), a.m_defn_id.value()); });
}

void action_handler::operator()(const actions::assign_class &a)
{
    if(!m_state.has_active_file())
        return;
    undoable(m_state.m_active_file.value(), [&] { assign_class(a.m_ids, a.m_defn_id); });
}

void action_handler::operator()(const actions::accept_labels &a)
{
    if(!m_state.has_active_file())
        return;
    undoable(
        m_state.m_active_file.value(),
        [&] { edit_labels(a.m_ids, [](label &l) { l.m_state = label_state::confirmed; }); });
}

void action_handler::operator()(const actions::reject_labels &r)
{
    if(!m_state.has_active_file())
        return;
    undoable(
        m_state.m_active_file.value(),
        [&]
        {
            // Unclassified selections aren't anything yet, so there's nothing to keep
            for(auto id : r.m_ids)
                if(m_state.m_unlabelled.erase(id))
                    m_state.deselect(id);
            edit_labels(r.m_ids, [](label &l) { l.m_state = label_state::rejected; });
            if(!m_state.settings.m_show_rejected)
                for(auto id : r.m_ids)
                    m_state.deselect(id);
        });
}

void action_handler::operator()(const actions::delete_labels &d)
{
    if(!m_state.has_active_file())
        return;

    const auto &file = m_state.m_active_file.value();
    undoable(
        file,
        [&]
        {
            for(auto id : d.m_ids)
            {
                m_state.m_unlabelled.erase(id);
                m_proj.erase_label(file, id);
                m_state.deselect(id);
            }
        });
}

void action_handler::operator()(const actions::move_label &m)
{
    if(!m_state.has_active_file())
        return;

    const auto &file = m_state.m_active_file.value();
    const double duration_s = m_state.get_audio_buffer().duration_s();
    const double start = std::clamp(std::min(m.m_start_s, m.m_stop_s), 0., duration_s);
    const double stop = std::clamp(std::max(m.m_start_s, m.m_stop_s), 0., duration_s);

    undoable(
        file,
        [&]
        {
            edit_labels(
                {m.m_id},
                [&](label &l)
                {
                    l.m_start_s = start;
                    l.m_stop_s = stop;
                });
        },
        history::merge_key{m.m_drag_seq, 0U});
}

void action_handler::operator()(const actions::snap_labels &s)
{
    if(!m_state.has_active_file())
        return;
    const auto &audio = m_state.get_audio_buffer();
    const double window = m_state.m_review.m_snap_window_s;
    undoable(
        m_state.m_active_file.value(),
        [&]
        {
            edit_labels(
                s.m_ids,
                [&](label &l)
                {
                    const double start = snap_onset(audio, l.m_start_s, window);
                    if(l.is_onset())
                        l.m_stop_s = start;
                    l.m_start_s = std::min(start, l.m_stop_s);
                });
        });
}

void action_handler::operator()(const actions::review_step &r)
{
    const auto queue = review_queue(m_proj, m_state.m_review);
    auto from = m_state.m_review_pos;
    const auto next = step_review(queue, from, r.m_forward, m_state.m_review);
    if(!next)
    {
        m_state.report_info("No suggestions left to review.");
        m_state.m_review_pos.reset();
        return;
    }
    m_state.m_review_pos = next;

    (*this)(actions::load_file{next->m_file});
    const auto *l = m_proj.get_labels(next->m_file).find_ptr(next->m_id);
    if(!l)
        return;
    m_state.set_selection({l->m_id});
    m_state.m_view_request = review_view(*l, m_state.m_review);
    restore_active_audio();
    m_audio_engine.select_region(m_state.playback_window(*l), m_state.m_loop);
    m_audio_engine.play(); // Reviewing means listening, whatever auto play says
}

void action_handler::operator()(const actions::undo &u)
{
    if(auto entry = m_state.m_history.undo())
        restore(entry->m_file, entry->m_before);
}

void action_handler::operator()(const actions::redo &r)
{
    if(auto entry = m_state.m_history.redo())
        restore(entry->m_file, entry->m_after);
}

void action_handler::operator()(const actions::set_file_status &s)
{
    m_proj.set_status(s.m_file, s.m_status);
}

void action_handler::operator()(const actions::set_channel &c)
{
    m_proj.set_channel(c.m_file, c.m_channel);
    if(!m_state.is_selected_file(c.m_file))
        return;
    m_state.load_audio(c.m_file, m_proj.get_channel(c.m_file));
    m_state.m_playing_clip = false;
    const auto state = m_audio_engine.get_state();
    m_audio_engine.set_audio_clip(m_state.get_audio_buffer_ptr());
    m_audio_engine.select_region(
        time_span{state.playhead_s, m_state.get_audio_buffer().duration_s()},
        m_state.m_loop);
}

void action_handler::operator()(const actions::select_playback_device &dev)
{
    m_audio_engine.set_device(dev.dev);
    m_audio_engine.set_speed(m_state.settings.m_playback_speed);
    (*this)(actions::set_playback_gain{});
    m_state.settings.m_device_name = dev.dev.name;
}

void action_handler::operator()(const actions::select_playback_region &r)
{
    restore_active_audio();
    m_audio_engine.select_region(r.t, r.loop);
    if(m_state.m_auto_start)
        m_audio_engine.play();
}

void action_handler::operator()(const actions::toggle_playback &a)
{
    if(m_audio_engine.get_state().playing)
        m_audio_engine.stop();
    else
        m_audio_engine.play();
}

void action_handler::operator()(const actions::playback_loop &p)
{
    m_audio_engine.set_playback_loop(p.loop);
}

void action_handler::operator()(const actions::set_playback_speed &s)
{
    m_state.settings.m_playback_speed = std::clamp(s.m_speed, 0.1f, 2.f);
    m_audio_engine.set_speed(m_state.settings.m_playback_speed);
}

void action_handler::operator()(const actions::set_playback_gain &)
{
    const auto &s = m_state.settings;
    auto_gain::settings g;
    g.m_automatic = s.m_auto_gain;
    g.m_gain_db = s.m_playback_gain_db;
    g.m_max_gain_db = s.m_auto_gain_max_db;
    m_audio_engine.set_gain(g);
}

void action_handler::operator()(const actions::rescan_files &r) { m_proj.rescan_files(); }

void action_handler::operator()(const actions::export_csv &e) { m_proj.export_csv(); }

void action_handler::operator()(const actions::export_dataset &e)
{
    if(m_state.is_exporting())
        return;
    const auto settings = export_settings::from_json(m_proj.config().m_export);

    // The job is a copy of what the export needs, so labelling can carry on meanwhile
    auto job = std::make_shared<export_job>(prepare_export(m_proj, settings));
    auto progress = std::make_shared<export_progress>();
    m_state.m_export_progress = progress;
    m_state.m_export_result = std::async(
        std::launch::async,
        [job, progress] { return run_export(*job, progress.get()); });
}

void action_handler::operator()(const actions::cancel_export &c)
{
    if(m_state.m_export_progress)
        m_state.m_export_progress->m_cancel = true;
}

void action_handler::operator()(const actions::run_assist &r)
{
    if(m_state.is_assisting())
        return;
    auto settings = m_state.m_assisted_labellers.at(r.m_labeller);
    if(r.m_loudness)
        settings.m_loudness = *r.m_loudness;
    auto job = std::make_shared<assist_job>(
        prepare_assist(m_proj, settings, r.m_scope, m_state.m_active_file, r.m_class));
    if(job->m_inputs.empty())
    {
        m_state.m_assist_report = "No files to run on.";
        return;
    }
    auto progress = std::make_shared<assist_progress>();
    m_state.m_assist_progress = progress;
    m_state.m_assist_report.clear();
    m_state.m_assist_result = std::async(
        std::launch::async,
        [job, progress] { return run_assist(*job, progress.get()); });
}

void action_handler::operator()(const actions::cancel_assist &c)
{
    if(m_state.m_assist_progress)
        m_state.m_assist_progress->m_cancel = true;
}

void action_handler::poll_assist()
{
    auto &result = m_state.m_assist_result;
    if(!result.valid() || result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    m_state.m_assist_progress.reset();

    const auto outcome = result.get();
    const auto summary = apply_assist(m_proj, outcome);
    if(summary.m_added)
        m_state.m_history = history{}; // As for imports: older snapshots would drop them

    auto &report = m_state.m_assist_report;
    report = fmt::format(
        "{}{}: {} suggestions added in {} files{}.",
        outcome.m_name,
        outcome.m_cancelled ? " (cancelled)" : "",
        summary.m_added,
        summary.m_files,
        summary.m_duplicates ? fmt::format(", {} already there", summary.m_duplicates) : "");
    if(!summary.m_new_classes.empty())
    {
        report += "\nNot in the palette, added for this session:";
        for(const auto &name : summary.m_new_classes)
            report += " " + name;
    }
    if(!outcome.m_errors.empty())
    {
        report += fmt::format("\n{} files failed.", outcome.m_errors.size());
        if(!outcome.m_log_dir.empty())
            report += " Logs are in " + generic_utf8(outcome.m_log_dir);
        std::string errors;
        for(size_t i = 0U; i != std::min<size_t>(outcome.m_errors.size(), 5U); ++i)
            errors += (i ? "\n\n" : "") + outcome.m_errors[i];
        if(outcome.m_errors.size() > 5U)
            errors += fmt::format("\n\n...and {} more", outcome.m_errors.size() - 5U);
        m_state.report_error(outcome.m_name + " failed:\n" + errors);
    }
    spdlog::info("{}", report);
}

void action_handler::poll_background()
{
    poll_assist();

    auto &result = m_state.m_export_result;
    if(!result.valid() || result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;

    try
    {
        const auto outcome = result.get();
        apply_export(m_proj, outcome);
        if(outcome.m_cancelled)
            m_state.report_info("Export cancelled. Nothing was recorded.");
        else
            report_export(outcome.m_summary);
    }
    catch(const std::exception &e)
    {
        m_state.report_error(std::string{"Export failed: "} + e.what());
    }
    m_state.m_export_progress.reset();
}

void action_handler::cancel_background()
{
    if(m_state.is_assisting())
    {
        (*this)(actions::cancel_assist{});
        m_state.m_assist_result.wait();
    }
    if(m_state.is_exporting())
    {
        (*this)(actions::cancel_export{});
        m_state.m_export_result.wait();
    }
    poll_background();
}

void action_handler::report_export(const export_summary &summary)
{
    std::string message = fmt::format(
        "Exported {} clips from {} files to\n{}",
        summary.m_clips,
        summary.m_files,
        generic_utf8(summary.m_directory));
    if(summary.m_unverified_clips)
        message += fmt::format(
            "\n\n{} clips didn't read back correctly. Keep their originals.",
            summary.m_unverified_clips);
    if(summary.m_todo_with_labels)
        message += fmt::format(
            "\n\n{} files with labels weren't exported because they aren't marked done.",
            summary.m_todo_with_labels);
    m_state.report_info(message);

    for(const auto &error : summary.m_errors)
        m_state.report_error("Export failed for " + error);
}

void action_handler::operator()(const actions::set_export_settings &s)
{
    m_proj.set_export_section(s.m_section);
    m_state.report_info("Saved the export settings to " + generic_utf8(m_proj.config().m_filename));
}

void action_handler::operator()(const actions::import_annotations &i)
{
    const annotation_tree tree(m_proj.config(), m_state.m_annotation_trees.at(i.m_tree));
    const auto summary = import_annotations(m_proj, tree);

    // An undo step from before the import would put back labels without what it added
    m_state.m_history = history{};

    std::string message = fmt::format(
        "Read {} label files from {}:\n{} added, {} already present.",
        summary.m_files,
        generic_utf8(tree.root()),
        summary.m_added,
        summary.m_duplicates);
    if(summary.m_marked_done)
        message += fmt::format(
            "\n{} empty files: their recordings were marked done (no events).",
            summary.m_marked_done);
    if(summary.m_unmatched)
        message += fmt::format(
            "\n{} label files have no recording in the project.",
            summary.m_unmatched);
    if(!summary.m_new_classes.empty())
    {
        message += "\n\nNot in the palette, so added for this session (grey):";
        for(const auto &name : summary.m_new_classes)
            message += "\n  " + name;
    }
    m_state.report_info(message);
    for(const auto &error : summary.m_errors)
        m_state.report_error("Could not import " + error);
}

void action_handler::operator()(const actions::export_annotations &e)
{
    const annotation_tree tree(m_proj.config(), m_state.m_annotation_trees.at(e.m_tree));
    const auto summary = export_annotations(m_proj, tree);
    m_state.report_info(
        fmt::format(
            "Wrote {} label files ({} labels) to\n{}\n{} were already up to date.",
            summary.m_files - summary.m_unchanged,
            summary.m_labels,
            generic_utf8(tree.root()),
            summary.m_unchanged));
    for(const auto &error : summary.m_errors)
        m_state.report_error("Could not export " + error);
}

void action_handler::operator()(const actions::play_clip &p)
{
    auto clip = std::make_shared<const audio_buffer>(load_mono(p.m_clip));
    m_audio_engine.set_audio_clip(clip);
    m_audio_engine.select_region(time_span{0., clip->duration_s()}, false);
    m_audio_engine.play();
    m_state.m_playing_clip = true;
}

void action_handler::operator()(const actions::judge_clip &j)
{
    undoable(
        j.m_source,
        [&]
        {
            apply_clip_verdict(m_proj, j.m_source, j.m_onset_s, j.m_window_s, j.m_verdict, j.m_defn_id);
        });
}

void action_handler::operator()(const actions::jump_to &j)
{
    (*this)(actions::load_file{j.m_file});
    restore_active_audio();
    m_state.m_view_request = time_span{j.m_time_s - 2., j.m_time_s + 2.};
    if(const auto *l = find_label_at(m_proj.get_labels(j.m_file), j.m_time_s, 0.05))
    {
        m_state.set_selection({l->m_id});
        play_label(*l);
    }
    else
    {
        const label at{0U, label_defn_id{}, j.m_time_s, j.m_time_s};
        m_audio_engine.select_region(m_state.playback_window(at), m_state.m_loop);
        m_audio_engine.play();
    }
}
