/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <istream>
#include <map>
#include <ostream>
#include <set>
#include <stdexcept>

#include <fmt/format.h>

#include "label_format.h"

namespace
{
std::vector<std::string> split_tabs(const std::string &line)
{
    std::vector<std::string> fields;
    size_t pos = 0U;
    while(true)
    {
        const auto tab = line.find('\t', pos);
        fields.push_back(line.substr(pos, tab == std::string::npos ? tab : tab - pos));
        if(tab == std::string::npos)
            return fields;
        pos = tab + 1U;
    }
}

std::string trim(std::string s)
{
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::ranges::find_if(s, not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

/// A number filling the whole field, if it is one
std::optional<double> parse_number(const std::string &field)
{
    const auto s = trim(field);
    if(s.empty())
        return std::nullopt;
    char *end{};
    errno = 0;
    const double v = std::strtod(s.c_str(), &end);
    if(errno != 0 || end != s.c_str() + s.size())
        return std::nullopt;
    return v;
}

double require_number(const std::string &field, size_t line_no)
{
    if(auto v = parse_number(field))
        return *v;
    throw std::runtime_error(fmt::format("Line {}: expected a time, got \"{}\"", line_no, field));
}

std::string lower(std::string s)
{
    std::ranges::transform(
        s,
        s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// Lines without their line endings, numbered from 1, skipping blank ones
template <typename F>
void for_each_line(std::istream &in, F &&f)
{
    std::string line;
    size_t line_no = 0U;
    while(std::getline(in, line))
    {
        ++line_no;
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        if(line_no == 1U && line.starts_with("\xEF\xBB\xBF"))
            line.erase(0, 3); // UTF-8 byte order mark
        if(trim(line).empty())
            continue;
        f(line, line_no);
    }
}

bool any_scores(const std::vector<text_label> &labels)
{
    return std::ranges::any_of(labels, [](const text_label &l) { return l.m_score.has_value(); });
}

std::string format_time(double t) { return fmt::format("{:.6f}", t); }

/// Audacity label track export: start<TAB>stop<TAB>label. Lines starting with a backslash hold the
/// frequency range of the label before them, and are skipped. An optional fourth column holding a
/// number is read as a score; Audacity itself never writes one.
class audacity_format : public label_format
{
public:
    std::string_view name() const override { return "audacity"; }
    std::string_view default_extension() const override { return ".txt"; }

    std::vector<text_label> read(std::istream &in) const override
    {
        std::vector<text_label> labels;
        for_each_line(
            in,
            [&](const std::string &line, size_t line_no)
            {
                if(line.starts_with('\\'))
                    return;
                const auto fields = split_tabs(line);
                if(fields.size() < 2U)
                    throw std::runtime_error(
                        fmt::format("Line {}: expected start, stop and label", line_no));
                text_label l{
                    require_number(fields[0], line_no),
                    require_number(fields[1], line_no),
                    fields.size() > 2U ? fields[2] : std::string{}
                };
                if(fields.size() > 3U)
                    if(auto score = parse_number(fields[3]))
                        l.m_score = static_cast<float>(*score);
                labels.push_back(std::move(l));
            });
        return labels;
    }

    void write(std::ostream &out, const std::vector<text_label> &labels, const recording_info &)
        const override
    {
        for(const auto &l : labels)
            out << format_time(l.m_start_s) << '\t' << format_time(l.m_stop_s) << '\t' << l.m_text
                << '\n';
    }
};

/// A table with a header row, whose columns are found by name
class header_table
{
public:
    explicit header_table(const std::vector<std::string> &header)
    {
        for(size_t i = 0U; i != header.size(); ++i)
            m_columns.emplace(lower(trim(header[i])), i);
    }

    /// The first of the names present, case-insensitively
    std::optional<size_t> find(std::initializer_list<std::string_view> names) const
    {
        for(auto name : names)
            if(auto it = m_columns.find(lower(std::string{name})); it != m_columns.end())
                return it->second;
        return std::nullopt;
    }

private:
    std::map<std::string, size_t> m_columns;
};

const std::string &field_at(const std::vector<std::string> &fields, size_t i, size_t line_no)
{
    if(i >= fields.size())
        throw std::runtime_error(fmt::format("Line {}: too few columns", line_no));
    return fields[i];
}

/// DCASE strong labels for one recording: onset<TAB>offset<TAB>event_label, with or without a
/// header row. A header may name a score column (score, confidence or probability).
class dcase_format : public label_format
{
public:
    std::string_view name() const override { return "dcase"; }
    std::string_view default_extension() const override { return ".tsv"; }

    std::vector<text_label> read(std::istream &in) const override
    {
        std::vector<text_label> labels;
        size_t onset = 0U, offset = 1U, label = 2U;
        std::optional<size_t> score;
        bool first = true;
        for_each_line(
            in,
            [&](const std::string &line, size_t line_no)
            {
                const auto fields = split_tabs(line);
                if(first && !parse_number(fields[0]))
                {
                    const header_table header(fields);
                    const auto find = [&](std::initializer_list<std::string_view> names)
                    {
                        if(auto i = header.find(names))
                            return *i;
                        throw std::runtime_error(
                            fmt::format("No {} column in the header", *names.begin()));
                    };
                    onset = find({"onset", "start"});
                    offset = find({"offset", "stop", "end"});
                    label = find({"event_label", "label", "class"});
                    score = header.find({"score", "confidence", "probability"});
                    first = false;
                    return;
                }
                first = false;
                text_label l{
                    require_number(field_at(fields, onset, line_no), line_no),
                    require_number(field_at(fields, offset, line_no), line_no),
                    label < fields.size() ? fields[label] : std::string{}
                };
                if(!score && fields.size() > 3U)
                    score = 3U; // Headerless, with a fourth column
                if(score && *score < fields.size())
                    if(auto v = parse_number(fields[*score]))
                        l.m_score = static_cast<float>(*v);
                labels.push_back(std::move(l));
            });
        return labels;
    }

    void write(std::ostream &out, const std::vector<text_label> &labels, const recording_info &)
        const override
    {
        const bool scores = any_scores(labels);
        out << "onset\toffset\tevent_label" << (scores ? "\tscore" : "") << '\n';
        for(const auto &l : labels)
        {
            out << format_time(l.m_start_s) << '\t' << format_time(l.m_stop_s) << '\t' << l.m_text;
            if(scores)
                out << '\t' << (l.m_score ? fmt::format("{}", *l.m_score) : std::string{});
            out << '\n';
        }
    }
};

/// Raven selection table. Raven writes a row per view of each selection, so rows are
/// de-duplicated by their Selection number. The label column is "Annotation" unless the
/// "label-column" option says otherwise.
class raven_format : public label_format
{
public:
    explicit raven_format(const nlohmann::json &options) :
        m_label_column(options.value("label-column", std::string{"Annotation"}))
    {
    }

    std::string_view name() const override { return "raven"; }
    std::string_view default_extension() const override { return ".Table.1.selections.txt"; }
    bool needs_recording_info() const override { return true; }

    std::vector<text_label> read(std::istream &in) const override
    {
        std::vector<text_label> labels;
        std::optional<header_table> header;
        size_t begin{}, end{};
        std::optional<size_t> label, score, selection;
        std::set<std::string> seen;
        for_each_line(
            in,
            [&](const std::string &line, size_t line_no)
            {
                const auto fields = split_tabs(line);
                if(!header)
                {
                    header.emplace(fields);
                    const auto b = header->find({"Begin Time (s)"});
                    const auto e = header->find({"End Time (s)"});
                    if(!b || !e)
                        throw std::runtime_error("Not a Raven selection table: no time columns");
                    begin = *b;
                    end = *e;
                    label = header->find({m_label_column, "Annotation", "Species", "Label"});
                    score = header->find({"Score", "Confidence"});
                    selection = header->find({"Selection"});
                    return;
                }
                if(selection && *selection < fields.size() && !seen.insert(fields[*selection]).second)
                    return;
                text_label l{
                    require_number(field_at(fields, begin, line_no), line_no),
                    require_number(field_at(fields, end, line_no), line_no),
                    label && *label < fields.size() ? fields[*label] : std::string{}
                };
                if(score && *score < fields.size())
                    if(auto v = parse_number(fields[*score]))
                        l.m_score = static_cast<float>(*v);
                labels.push_back(std::move(l));
            });
        return labels;
    }

    void write(std::ostream &out, const std::vector<text_label> &labels, const recording_info &info)
        const override
    {
        const bool scores = any_scores(labels);
        const double high_hz = info.m_sample_rate_hz / 2.;
        out << "Selection\tView\tChannel\tBegin Time (s)\tEnd Time (s)\tLow Freq (Hz)\t"
               "High Freq (Hz)\t"
            << m_label_column << (scores ? "\tScore" : "") << '\n';
        size_t n = 0U;
        for(const auto &l : labels)
        {
            out << ++n << "\tWaveform 1\t1\t" << format_time(l.m_start_s) << '\t'
                << format_time(l.m_stop_s) << "\t0\t" << fmt::format("{:.1f}", high_hz) << '\t'
                << l.m_text;
            if(scores)
                out << '\t' << (l.m_score ? fmt::format("{}", *l.m_score) : std::string{});
            out << '\n';
        }
    }

private:
    std::string m_label_column;
};
}

std::unique_ptr<label_format> make_label_format(std::string_view name, const nlohmann::json &options)
{
    const auto &opts = options.is_object() ? options : nlohmann::json::object();
    if(name == "audacity")
        return std::make_unique<audacity_format>();
    if(name == "dcase")
        return std::make_unique<dcase_format>();
    if(name == "raven")
        return std::make_unique<raven_format>(opts);
    throw std::runtime_error(
        fmt::format("Unknown label format \"{}\", expected audacity, dcase or raven", name));
}

std::vector<std::string_view> label_format_names() { return {"audacity", "dcase", "raven"}; }
