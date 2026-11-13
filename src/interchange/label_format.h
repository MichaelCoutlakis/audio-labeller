/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

/// A label as it appears in another program's label file: times and free text, before the text is
/// mapped to a palette class (see label_text.h)
struct text_label
{
    double m_start_s{};
    double m_stop_s{}; ///< Equal to the start for an onset
    std::string m_text;
    std::optional<float> m_score; ///< A proposer's confidence, where the format carries one

    bool operator==(const text_label &) const = default;
};

/// What a format may need to know about the recording when writing
struct recording_info
{
    uint32_t m_sample_rate_hz{}; ///< 0 if unknown
};

/// One per-recording label file format. Implementations are in label_format.cpp; add new ones
/// there and to make_label_format().
class label_format
{
public:
    virtual ~label_format() = default;

    virtual std::string_view name() const = 0;

    /// Extension of the label file for a recording, including the leading dot
    virtual std::string_view default_extension() const = 0;

    /// Whether write() uses recording_info, which costs opening the audio file
    virtual bool needs_recording_info() const { return false; }

    /// Throws on malformed input
    virtual std::vector<text_label> read(std::istream &in) const = 0;

    virtual void write(
        std::ostream &out,
        const std::vector<text_label> &labels,
        const recording_info &info) const = 0;
};

/// "audacity", "dcase" or "raven". Options are format specific, eg. Raven's label column.
/// Throws for an unknown name.
std::unique_ptr<label_format> make_label_format(
    std::string_view name,
    const nlohmann::json &options = nlohmann::json::object());

std::vector<std::string_view> label_format_names();
