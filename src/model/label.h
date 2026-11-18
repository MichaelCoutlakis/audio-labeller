/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once
#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include "ordered_registry.h"
#include "types.h"
using label_id = uint64_t;

struct label_defn_id
{
    operator uint32_t() const { return m_id; }
    bool operator==(const label_defn_id &other) const = default;

    uint32_t m_id{std::numeric_limits<uint32_t>::max()};
};

inline std::atomic<label_id> &last_label_id()
{
    static std::atomic<label_id> id{};
    return id;
}

inline label_id get_next_label_id() { return ++last_label_id(); }

/// Ensure that IDs handed out from now on are greater than the specified one
inline void reserve_label_ids(label_id id)
{
    auto &last = last_label_id();
    if(last < id)
        last = id;
}

/// A palette class. Its labels mark a sound from where it starts to where it stops.
struct label_defn
{
    label_defn_id m_id;
    std::string m_class;
    uint32_t m_color_rgba{}; ///< See colour.h
};

/// Whether a label has been checked by a person
enum class label_state : uint8_t
{
    confirmed, ///< A label
    suggested, ///< Proposed (eg. by a detector), waiting to be accepted, rejected or relabelled
    rejected   ///< Proposed, then judged not to be an event. Kept as a record of what was checked.
};

struct label
{
    label_id m_id;           //!< This specific label instance
    label_defn_id m_defn_id; //!< ID of the label definition in the dictionary
    double m_start_s{};
    /// Equal to the start for a zero-length label (a point, from an old label file or an import),
    /// which can be widened into a region
    double m_stop_s{};
    label_state m_state{label_state::confirmed};
    std::optional<float> m_score; ///< The proposer's confidence, if it gave one
    /// Set by drawing on the spectrogram, and played filtered to it. Not written to annotation
    /// trees or exports yet.
    frequency_band m_band;

    /// Zero length: a point in time rather than a region
    bool is_onset() const { return m_stop_s <= m_start_s; }
    bool is_confirmed() const { return m_state == label_state::confirmed; }

    bool operator==(const label &other) const = default;
};

/// Used to extract the label definition id from a label definition to be used as a registry key in
/// an ordered registry wrapper
inline constexpr label_defn_id get_label_defn_key(const label_defn &defn) { return defn.m_id; }
inline constexpr label_id get_label_id_key(const label &l) { return l.m_id; }

class label_dict : public ordered_registry<label_defn, label_defn_id, get_label_defn_key>
{
public:
};

using labels = ordered_registry<label, label_id, get_label_id_key>;
