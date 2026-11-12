/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

#include <spdlog/spdlog.h>

#include "../io/audio_source.h"
#include "../io/files.h"
#include "../io/wav_writer.h"
#include "../model/paths.h"
#include "../model/project_model.h"
#include "clip_export.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
constexpr int export_format_version = 1;

const char *to_string(export_settings::clip_mode mode)
{
    return mode == export_settings::clip_mode::fixed ? "fixed" : "padded";
}

std::vector<json> read_jsonl(const fs::path &path)
{
    std::vector<json> rows;
    std::ifstream f(path);
    std::string line;
    while(std::getline(f, line))
        if(line.find_first_not_of(" \t\r") != std::string::npos)
            rows.push_back(json::parse(line));
    return rows;
}

json read_json_or(const fs::path &path, json fallback)
{
    std::ifstream f(path);
    return f ? json::parse(f) : fallback;
}

std::string class_name(const label_dict &dict, label_defn_id id)
{
    const auto *defn = dict.find_ptr(id);
    return defn ? defn->m_class : std::string{};
}
}

export_settings export_settings::from_json(const json &j)
{
    export_settings s;
    if(!j.is_object())
        return s;

    if(auto it = j.find("directory"); it != j.end())
        s.m_directory = path_from_utf8(it->get<std::string>());
    if(auto it = j.find("mode"); it != j.end())
    {
        const auto mode = it->get<std::string>();
        if(mode == "fixed")
            s.m_mode = clip_mode::fixed;
        else if(mode != "padded")
            throw std::runtime_error("Unknown export mode " + mode + ", expected padded or fixed");
    }
    s.m_padding_s = j.value("padding_s", s.m_padding_s);
    s.m_fixed_length_s = j.value("fixed_length_s", s.m_fixed_length_s);
    s.m_max_clip_s = j.value("max_clip_s", s.m_max_clip_s);
    s.m_background_per_file = j.value("background_per_file", s.m_background_per_file);
    s.m_background_length_s = j.value("background_length_s", s.m_background_length_s);
    return s;
}

json export_settings::to_json() const
{
    return json{
        {"mode", to_string(m_mode)},
        {"padding_s", m_padding_s},
        {"fixed_length_s", m_fixed_length_s},
        {"max_clip_s", m_max_clip_s},
        {"background_per_file", m_background_per_file},
        {"background_length_s", m_background_length_s}
    };
}

std::vector<clip_plan> plan_clips(
    std::vector<label> labels,
    double duration_s,
    const export_settings &settings)
{
    std::ranges::sort(labels, {}, [](const label &l) { return std::pair{l.m_start_s, l.m_id}; });

    struct window
    {
        double m_start;
        double m_stop;
    };
    std::vector<window> windows;
    for(const auto &l : labels)
    {
        double start{}, stop{};
        const double length = l.m_stop_s - l.m_start_s;
        if(settings.m_mode == export_settings::clip_mode::fixed)
        {
            if(length < settings.m_fixed_length_s)
            {
                const double centre = (l.m_start_s + l.m_stop_s) / 2.;
                start = centre - settings.m_fixed_length_s / 2.;
                stop = centre + settings.m_fixed_length_s / 2.;

                // Keep the full length near the ends of the file where possible
                if(start < 0.)
                {
                    stop -= start;
                    start = 0.;
                }
                if(stop > duration_s)
                {
                    start -= stop - duration_s;
                    stop = duration_s;
                }
            }
            else
            {
                start = l.m_start_s; // Longer than the window: the clip covers the label
                stop = l.m_stop_s;
            }
        }
        else
        {
            start = l.m_start_s - settings.m_padding_s;
            stop = l.m_stop_s + settings.m_padding_s;
        }

        start = std::clamp(start, 0., duration_s);
        stop = std::clamp(stop, start, duration_s);
        if(stop > start)
            windows.push_back({start, stop});
    }
    std::ranges::sort(windows, {}, &window::m_start);

    if(settings.m_mode == export_settings::clip_mode::padded)
    {
        std::vector<window> merged;
        for(const auto &w : windows)
        {
            if(!merged.empty() &&
               w.m_start <= merged.back().m_stop &&
               std::max(merged.back().m_stop, w.m_stop) - merged.back().m_start <=
                   settings.m_max_clip_s)
                merged.back().m_stop = std::max(merged.back().m_stop, w.m_stop);
            else
                merged.push_back(w);
        }
        windows = std::move(merged);
    }

    // Every label overlapping a clip is in it, cut to the clip
    std::vector<clip_plan> plans;
    for(const auto &w : windows)
    {
        clip_plan plan{w.m_start, w.m_stop, {}};
        for(const auto &l : labels)
        {
            if(l.m_stop_s <= w.m_start || l.m_start_s >= w.m_stop)
                continue;
            plan.m_events.push_back(
                clip_event{
                    std::max(l.m_start_s, w.m_start) - w.m_start,
                    std::min(l.m_stop_s, w.m_stop) - w.m_start,
                    l.m_defn_id,
                    l.m_id,
                    l.m_start_s < w.m_start || l.m_stop_s > w.m_stop
                });
        }
        plans.push_back(std::move(plan));
    }
    return plans;
}

std::vector<clip_plan> plan_background(
    const std::vector<label> &labels,
    double duration_s,
    const export_settings &settings)
{
    const double length = settings.m_background_length_s;
    if(settings.m_background_per_file == 0U || length <= 0.)
        return {};

    // Candidate windows on a grid, kept clear of labels by the padding:
    std::vector<double> starts;
    for(double t = 0.; t + length <= duration_s; t += length)
    {
        const bool clear = std::ranges::none_of(
            labels,
            [&](const label &l)
            {
                return l.m_stop_s + settings.m_padding_s > t &&
                    l.m_start_s - settings.m_padding_s < t + length;
            });
        if(clear)
            starts.push_back(t);
    }

    std::vector<clip_plan> plans;
    const size_t count = std::min<size_t>(settings.m_background_per_file, starts.size());
    for(size_t i = 0U; i != count; ++i)
    {
        const double start = starts[i * starts.size() / count];
        plans.push_back(clip_plan{start, start + length, {}});
    }
    return plans;
}

export_summary export_dataset(project_model &project, const export_settings &settings)
{
    if(!project.has_project())
        throw std::runtime_error("No project loaded, nothing to export");

    export_summary summary;
    const auto dir = project.config().resolve_proj(settings.m_directory);
    summary.m_directory = dir;
    fs::create_directories(dir / "clips");

    // What's already exported. Sources exported now replace their earlier entries.
    const auto old_rows = read_jsonl(dir / "metadata.jsonl");
    auto dataset = read_json_or(dir / "dataset.json", json::object());
    auto sources = dataset.value("sources", json::object());

    std::map<std::string, std::vector<json>> new_rows; // By source ID
    const auto now = utc_now_iso8601();

    for(const auto &file : project.get_files())
    {
        const auto status = project.get_status(file.m_path);
        const auto &file_labels = project.get_labels(file.m_path);
        if(status != file_status::done)
        {
            summary.m_todo_with_labels += status == file_status::todo && !file_labels.empty();
            continue;
        }

        try
        {
            const auto source_id = project.get_source_id(file.m_path);
            audio_source source(file.m_path);
            const auto &format = source.format();
            const std::vector<label> labels(file_labels.begin(), file_labels.end());

            auto plans = plan_clips(labels, format.duration_s(), settings);
            for(auto &background : plan_background(labels, format.duration_s(), settings))
                plans.push_back(std::move(background));

            auto &rows = new_rows[source_id];
            rows.clear();
            std::set<std::string> names;
            size_t verified = 0U;
            for(const auto &plan : plans)
            {
                const auto rate = format.m_sample_rate_hz;
                const auto start = static_cast<uint64_t>(std::llround(plan.m_start_s * rate));
                const auto stop =
                    std::min<uint64_t>(std::llround(plan.m_stop_s * rate), format.m_frames);
                if(stop <= start)
                    continue;

                auto name =
                    fmt::format("{}_{:09d}", source_id, std::llround(plan.m_start_s * 1000.));
                for(int n = 1; names.contains(name); ++n)
                    name = fmt::format(
                        "{}_{:09d}_{}",
                        source_id,
                        std::llround(plan.m_start_s * 1000.),
                        n);
                names.insert(name);
                const auto file_name = "clips/" + name + ".wav";

                const auto copied =
                    copy_wav_range(source, start, stop - start, dir / path_from_utf8(file_name));
                verified += copied.m_verified;

                // Times are from the samples actually cut, rather than the plan
                const double offset_s = double(start) / rate;
                json events = json::array();
                for(const auto &e : plan.m_events)
                    events.push_back(
                        json{
                            {"onset_s", std::max(0., e.m_onset_s + plan.m_start_s - offset_s)},
                            {"offset_s", e.m_offset_s + plan.m_start_s - offset_s},
                            {"event_label", class_name(project.m_label_dict, e.m_class)},
                            {"class_id", e.m_class.m_id},
                            {"label_id", e.m_label_id},
                            {"truncated", e.m_truncated}
                        });
                rows.push_back(
                    json{
                        {"file_name", file_name},
                        {"source_id", source_id},
                        {"source_offset_s", offset_s},
                        {"duration_s", double(copied.m_frames) / rate},
                        {"sample_rate", rate},
                        {"channels", format.m_channels},
                        {"events", std::move(events)}
                    });
            }

            const auto portable = project.to_portable(file.m_path);
            sources[source_id] = json{
                {"dataset_root", portable.m_root},
                {"path", portable.m_relative},
                {"duration_s", format.duration_s()},
                {"sample_rate", format.m_sample_rate_hz},
                {"channels", format.m_channels},
                {"bits_per_sample", format.m_bits_per_sample},
                {"exported_at", now}
            };

            const bool all_verified = verified == rows.size();
            project.record_export(
                file.m_path,
                export_record{now, generic_utf8(dir), rows.size(), all_verified});
            ++summary.m_files;
            summary.m_clips += rows.size();
            summary.m_unverified_clips += rows.size() - verified;
        }
        catch(const std::exception &e)
        {
            summary.m_errors.push_back(file.m_display_name + ": " + e.what());
        }
    }

    // Merge: drop the earlier entries of re-exported sources, deleting clips no longer produced
    std::set<std::string> new_names;
    for(const auto &[id, rows] : new_rows)
        for(const auto &row : rows)
            new_names.insert(row.at("file_name").get<std::string>());

    std::vector<json> rows;
    for(const auto &row : old_rows)
    {
        if(!new_rows.contains(row.at("source_id").get<std::string>()))
            rows.push_back(row);
        else if(const auto name = row.at("file_name").get<std::string>(); !new_names.contains(name))
        {
            std::error_code ec;
            fs::remove(dir / path_from_utf8(name), ec);
        }
    }
    for(auto &[id, source_rows] : new_rows)
        for(auto &row : source_rows)
            rows.push_back(std::move(row));
    std::ranges::sort(
        rows,
        {},
        [](const json &row) { return row.at("file_name").get<std::string>(); });

    // Classes: the current palette, plus any earlier ones still referred to
    json classes = json::object();
    for(const auto &c : dataset.value("classes", json::array()))
        classes[std::to_string(c.at("class_id").get<uint32_t>())] = c;
    for(const auto &defn : project.m_label_dict)
        classes[std::to_string(defn.m_id.m_id)] =
            json{{"class_id", defn.m_id.m_id}, {"name", defn.m_class}};
    std::map<uint32_t, json> by_id;
    for(auto &[key, c] : classes.items())
        by_id[c.at("class_id").get<uint32_t>()] = c;
    json class_list = json::array();
    for(auto &[id, c] : by_id)
        class_list.push_back(c);

    std::ostringstream metadata, labels_tsv, durations_tsv;
    labels_tsv << "filename\tonset\toffset\tevent_label\n" << std::fixed << std::setprecision(6);
    durations_tsv << "filename\tduration\n" << std::fixed << std::setprecision(6);
    for(const auto &row : rows)
    {
        metadata << row.dump() << "\n";
        const auto name = row.at("file_name").get<std::string>();
        durations_tsv << name << "\t" << row.at("duration_s").get<double>() << "\n";
        for(const auto &e : row.at("events"))
            labels_tsv << name << "\t" << e.at("onset_s").get<double>() << "\t"
                       << e.at("offset_s").get<double>() << "\t"
                       << e.at("event_label").get<std::string>() << "\n";
    }

    std::ostringstream sources_csv;
    sources_csv << "source_id,dataset_root,path,duration_s,sample_rate,channels,bits_per_sample,"
                   "exported_at\n";
    for(const auto &[id, s] : sources.items())
        sources_csv << id << "," << csv_escape(s.value("dataset_root", "")) << ","
                    << csv_escape(s.value("path", "")) << "," << s.value("duration_s", 0.) << ","
                    << s.value("sample_rate", 0U) << "," << s.value("channels", 0U) << ","
                    << s.value("bits_per_sample", 0U) << "," << s.value("exported_at", "") << "\n";

    dataset = json{
        {"format_version", export_format_version},
        {"app", "audio-labeller " AUDIOLAB_VERSION},
        {"exported_at", now},
        {"settings", settings.to_json()},
        {"classes", std::move(class_list)},
        {"sources", std::move(sources)}
    };

    write_file_atomically(dir / "metadata.jsonl", metadata.str());
    write_file_atomically(dir / "labels.tsv", labels_tsv.str());
    write_file_atomically(dir / "durations.tsv", durations_tsv.str());
    write_file_atomically(dir / "sources.csv", sources_csv.str());
    write_file_atomically(dir / "dataset.json", dataset.dump(2));

    spdlog::info(
        "Exported {} clips from {} files to {}",
        summary.m_clips,
        summary.m_files,
        generic_utf8(dir));
    return summary;
}
