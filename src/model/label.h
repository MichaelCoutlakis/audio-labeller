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

/// What a palette class marks. Onset classes are transients, defined by their start time alone.
enum class class_kind : uint8_t
{
    region, ///< A start and a stop, eg. context like rain or a passing vehicle
    onset   ///< A point in time, eg. a call or a click
};

struct label_defn
{
    label_defn_id m_id;
    std::string m_class;
    uint32_t m_color_rgba{}; ///< See colour.h
    class_kind m_kind{class_kind::region};
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
    double m_stop_s{}; ///< Equal to the start for an onset
    label_state m_state{label_state::confirmed};
    std::optional<float> m_score; ///< The proposer's confidence, if it gave one

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
