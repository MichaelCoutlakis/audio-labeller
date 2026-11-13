/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "../dsp/loudness.h"
#include "../io/audio_source.h"
#include "../io/process.h"
#include "../model/paths.h"
#include "assisted_labeller.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
std::string read_tail(const fs::path &p, size_t max_chars = 600U)
{
    std::ifstream in(p, std::ios::binary);
    std::string s{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if(s.size() > max_chars)
        s = "..." + s.substr(s.size() - max_chars);
    while(!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}

std::string replace_all(std::string s, const std::string &from, const std::string &to)
{
    for(size_t pos = 0U; (pos = s.find(from, pos)) != std::string::npos; pos += to.size())
        s.replace(pos, from.size(), to);
    return s;
}

/// A unique folder for one run's temporary files and logs
fs::path make_run_dir(const std::string &name)
{
    static std::atomic<unsigned> counter{};
    const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
    std::string safe = name;
    std::ranges::replace_if(safe, [](unsigned char c) { return !std::isalnum(c); }, '_');
    auto dir = fs::temp_directory_path() /
        "audio_labeller" /
        fmt::format("{}_{}_{}", safe, stamp, ++counter);
    fs::create_directories(dir);
    return dir;
}

/// Runs an external program per recording, which writes its suggestions in a label format
class command_suggester : public suggester
{
public:
    explicit command_suggester(const assisted_labeller_settings &s) :
        m_settings(s),
        m_format(make_label_format(s.m_format, s.m_format_options))
    {
        if(s.m_command.empty())
            throw std::runtime_error("Assisted labeller " + s.m_name + " has no command");
    }

    std::vector<text_label> suggest(const suggest_input &in, const std::atomic<bool> &cancel)
        override
    {
        const auto stem = generic_utf8(in.m_audio.stem());
        const auto output = in.m_scratch_dir / path_from_utf8(stem + std::string{m_format->default_extension()});
        const auto log = in.m_scratch_dir / path_from_utf8(stem + ".log");
        std::error_code ec;
        fs::remove(output, ec);

        auto args = expand_command(m_settings.m_command, in, output);
        if(m_settings.m_shell)
            args = shell_command(args.front());
        const auto working_dir =
            m_settings.m_working_dir.empty() ? in.m_project_dir : m_settings.m_working_dir;
        const auto result = run_process(
            args,
            working_dir,
            m_settings.m_from_stdout ? output : log,
            log,
            &cancel,
            m_settings.m_timeout_s);

        if(result.m_cancelled)
            return {};
        if(result.m_timed_out)
            throw std::runtime_error(
                fmt::format("Stopped after {} s. Its output:\n{}", m_settings.m_timeout_s, read_tail(log)));
        if(result.m_exit_code != 0)
            throw std::runtime_error(
                fmt::format("Exited with code {}. Its output:\n{}", result.m_exit_code, read_tail(log)));
        if(!fs::exists(output, ec))
            throw std::runtime_error(
                "Wrote no label file to {output}. Its output:\n" + read_tail(log));

        std::ifstream labels(output, std::ios::binary);
        return m_format->read(labels);
    }

private:
    assisted_labeller_settings m_settings;
    std::unique_ptr<label_format> m_format;
};

/// Sounds that stand out from the background, as suggestions of one class
class loudness_suggester : public suggester
{
public:
    explicit loudness_suggester(const assisted_labeller_settings &s) :
        m_settings(s)
    {
    }

    std::vector<text_label> suggest(const suggest_input &in, const std::atomic<bool> &)
        override
    {
        const auto cls = m_settings.m_class.empty() ? in.m_class : m_settings.m_class;
        if(cls.empty())
            throw std::runtime_error("Choose a class for the suggestions");

        const auto audio = load_mono(in.m_audio, in.m_channel);
        std::vector<text_label> out;
        for(const auto &r : find_loud_regions(audio, m_settings.m_loudness))
        {
            // The score is how clearly it stands out: under 0.5 is within 10 dB of the threshold,
            // so the faintest are reviewed first
            const float score = static_cast<float>(std::clamp(r.m_above_db / 20., 0., 1.));
            out.push_back(text_label{r.m_start_s, r.m_stop_s, cls, score});
        }
        return out;
    }

private:
    assisted_labeller_settings m_settings;
};
}

std::vector<assisted_labeller_settings> assisted_labeller_settings::list_from_json(const json &j)
{
    std::vector<assisted_labeller_settings> list;
    if(!j.is_null() && !j.is_array())
        throw std::runtime_error("\"assisted-labellers\" in the project should be a list");

    bool has_loudness = false;
    for(const auto &entry : j.is_array() ? j : json::array())
    {
        assisted_labeller_settings s;
        s.m_name = entry.at("name").get<std::string>();
        const auto builtin = entry.value("builtin", std::string{});
        if(builtin == "loudness")
        {
            s.m_kind = kind::loudness;
            s.m_loudness = loudness_settings::from_json(entry.value("options", json::object()));
            has_loudness = true;
        }
        else if(!builtin.empty())
            throw std::runtime_error("Unknown built-in assisted labeller " + builtin);
        else
        {
            const auto &command = entry.at("command");
            if(command.is_string())
            {
                s.m_command = {command.get<std::string>()};
                s.m_shell = true;
            }
            else
                s.m_command = command.get<std::vector<std::string>>();
            s.m_format = entry.value("format", s.m_format);
            s.m_format_options = entry.value("format-options", json::object());
            if(auto it = entry.find("working-dir"); it != entry.end())
                s.m_working_dir = path_from_utf8(it->get<std::string>());
            s.m_timeout_s = entry.value("timeout_s", s.m_timeout_s);
            s.m_from_stdout = std::ranges::none_of(
                s.m_command,
                [](const std::string &a) { return a.find("{output}") != std::string::npos; });
            make_label_format(s.m_format, s.m_format_options); // Check it early
        }
        s.m_class = entry.value("class", std::string{});
        list.push_back(std::move(s));
    }

    if(!has_loudness)
    {
        assisted_labeller_settings s;
        s.m_name = "Loudness (built in)";
        s.m_kind = kind::loudness;
        list.push_back(std::move(s));
    }
    return list;
}

std::unique_ptr<suggester> make_suggester(const assisted_labeller_settings &settings)
{
    switch(settings.m_kind)
    {
    case assisted_labeller_settings::kind::loudness:
        return std::make_unique<loudness_suggester>(settings);
    case assisted_labeller_settings::kind::command:
    default:
        return std::make_unique<command_suggester>(settings);
    }
}

std::vector<std::string> expand_command(
    const std::vector<std::string> &command,
    const suggest_input &in,
    const fs::path &output)
{
    std::vector<std::string> out;
    for(auto arg : command)
    {
        arg = replace_all(arg, "{audio}", native_utf8(in.m_audio));
        arg = replace_all(arg, "{output}", native_utf8(output));
        arg = replace_all(arg, "{relative}", in.m_portable.m_relative);
        arg = replace_all(arg, "{root}", in.m_portable.m_root);
        arg = replace_all(arg, "{project_dir}", native_utf8(in.m_project_dir));
        arg = replace_all(arg, "{channel}", in.m_channel ? std::to_string(*in.m_channel + 1U) : "");
        arg = replace_all(arg, "{class}", in.m_class);
        out.push_back(std::move(arg));
    }
    return out;
}

assist_job prepare_assist(
    const project_model &project,
    const assisted_labeller_settings &settings,
    assist_scope scope,
    const std::optional<fs::path> &active_file,
    const std::string &class_name)
{
    assist_job job{settings, {}};
    auto &s = job.m_settings;
    s.m_working_dir =
        s.m_working_dir.empty() ? fs::path{} : project.config().resolve_root(s.m_working_dir);

    const auto project_dir = project.config().m_filename.parent_path();
    for(const auto &file : project.get_files())
    {
        const auto status = project.get_status(file.m_path);
        bool wanted = false;
        switch(scope)
        {
        case assist_scope::active_file: wanted = active_file == file.m_path; break;
        case assist_scope::all_files: wanted = status != file_status::skip; break;
        case assist_scope::todo_files:
            wanted = status == file_status::todo &&
                std::ranges::none_of(
                         project.get_labels(file.m_path),
                         [](const label &l) { return l.m_state == label_state::suggested; });
            break;
        }
        if(!wanted)
            continue;
        job.m_inputs.push_back(
            suggest_input{
                file.m_path,
                project.to_portable(file.m_path),
                project.get_channel(file.m_path),
                class_name,
                project_dir,
                {}
            });
    }
    return job;
}

assist_outcome run_assist(const assist_job &job, assist_progress *progress)
{
    assist_outcome outcome;
    outcome.m_name = job.m_settings.m_name;
    if(progress)
        progress->m_total = job.m_inputs.size();
    static const std::atomic<bool> never{false};
    const auto &cancel = progress ? progress->m_cancel : never;

    std::unique_ptr<suggester> s;
    try
    {
        s = make_suggester(job.m_settings);
        if(!job.m_settings.is_builtin())
            outcome.m_log_dir = make_run_dir(job.m_settings.m_name);
    }
    catch(const std::exception &e)
    {
        outcome.m_errors.push_back(e.what());
        return outcome;
    }

    for(const auto &input : job.m_inputs)
    {
        if(cancel)
        {
            outcome.m_cancelled = true;
            break;
        }
        try
        {
            auto in = input;
            in.m_scratch_dir = outcome.m_log_dir;
            auto suggestions = s->suggest(in, cancel);
            if(cancel)
            {
                outcome.m_cancelled = true;
                break;
            }
            outcome.m_suggestions[input.m_audio] = std::move(suggestions);
        }
        catch(const std::exception &e)
        {
            outcome.m_errors.push_back(input.m_portable.m_relative + ": " + e.what());
        }
        if(progress)
            ++progress->m_done;
    }
    spdlog::info(
        "{} suggested labels for {} recordings",
        outcome.m_name,
        outcome.m_suggestions.size());
    return outcome;
}

annotation_import_summary apply_assist(project_model &project, const assist_outcome &outcome)
{
    // What finished before a cancel is still worth keeping
    annotation_import_summary summary;
    for(const auto &[file, suggestions] : outcome.m_suggestions)
    {
        add_text_labels(project, file, suggestions, label_state::suggested, summary);
        ++summary.m_files;
    }
    finish_summary(summary);
    return summary;
}
