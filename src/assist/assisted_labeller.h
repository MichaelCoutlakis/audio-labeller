/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../dsp/loudness.h"
#include "../interchange/annotation_tree.h"
#include "../interchange/label_format.h"
#include "../model/project_model.h"

/// One entry of the project's "assisted-labellers" list, or a built-in. Each proposes
/// suggestions for a recording, which are then reviewed like imported ones.
struct assisted_labeller_settings
{
    enum class kind
    {
        command, ///< An external program, eg. a detector script, that writes a label file
        loudness ///< Built in: sounds that stand out from the background
    };

    std::string m_name;
    kind m_kind{kind::command};

    /* Commands: */
    /// Program and arguments, run without a shell. Placeholders are replaced in each: {audio},
    /// {output}, {relative} (the audio's path in its dataset root), {root}, {project_dir},
    /// {channel} (from 1, or empty for a mix), {class}.
    std::vector<std::string> m_command;
    bool m_shell{false};      ///< The command was given as one string, so it goes through a shell
    bool m_from_stdout{false}; ///< No {output} in the command: labels are read from its output
    std::string m_format{"audacity"}; ///< Of what the command writes
    nlohmann::json m_format_options;
    std::filesystem::path m_working_dir; ///< Default: the project's folder
    double m_timeout_s{0.};              ///< Per recording, 0 for none

    /* Built-ins: */
    std::string m_class; ///< Class of the suggestions, empty to choose in the app
    loudness_settings m_loudness;

    /// The project's list (which may be null), followed by the built-ins it doesn't configure
    static std::vector<assisted_labeller_settings> list_from_json(const nlohmann::json &j);

    bool is_builtin() const { return m_kind != kind::command; }
};

/// One recording to suggest labels for
struct suggest_input
{
    std::filesystem::path m_audio;
    project_model::portable_path m_portable;
    std::optional<uint32_t> m_channel; ///< nullopt for a mix
    std::string m_class;               ///< For suggesters that don't name classes themselves
    std::filesystem::path m_project_dir;
    std::filesystem::path m_scratch_dir; ///< For temporary files, unique to the run
};

/// Proposes labels for one recording at a time. Called on a worker thread; implementations must
/// not touch the project. Add built-ins in assisted_labeller.cpp, make_suggester().
class suggester
{
public:
    virtual ~suggester() = default;
    /// Throws on failure
    virtual std::vector<text_label> suggest(const suggest_input &in, const std::atomic<bool> &cancel) = 0;
};

std::unique_ptr<suggester> make_suggester(const assisted_labeller_settings &settings);

/// Replace the placeholders in a command's arguments
std::vector<std::string> expand_command(
    const std::vector<std::string> &command,
    const suggest_input &in,
    const std::filesystem::path &output);

/// Everything a run needs, copied from the project so it runs on a worker thread
struct assist_job
{
    assisted_labeller_settings m_settings;
    std::vector<suggest_input> m_inputs;
};

struct assist_progress
{
    std::atomic<size_t> m_done{};
    std::atomic<size_t> m_total{};
    std::atomic<bool> m_cancel{};
};

struct assist_outcome
{
    std::string m_name;
    std::map<std::filesystem::path, std::vector<text_label>> m_suggestions; ///< By recording
    std::vector<std::string> m_errors;
    std::filesystem::path m_log_dir; ///< Where the commands' output is kept
    bool m_cancelled{};
};

/// Which recordings a run covers
enum class assist_scope
{
    active_file,
    todo_files, ///< Files to do, excluding those already with suggestions waiting for review
    all_files   ///< Every file not skipped
};

/// The job for a scope. The class is used by built-ins whose settings don't name one.
assist_job prepare_assist(
    const project_model &project,
    const assisted_labeller_settings &settings,
    assist_scope scope,
    const std::optional<std::filesystem::path> &active_file,
    const std::string &class_name);

/// Runs the suggester on each recording in turn, on any thread. A failure on one recording is
/// recorded and the rest carry on.
assist_outcome run_assist(const assist_job &job, assist_progress *progress = nullptr);

/// Add the suggestions to the project, skipping any already there
annotation_import_summary apply_assist(project_model &project, const assist_outcome &outcome);
