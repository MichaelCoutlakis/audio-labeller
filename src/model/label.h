/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once
#include <atomic>
#include <string>

#include "ordered_registry.h"
using label_id = uint64_t;
using label_defn_id = uint32_t;

inline label_defn_id get_next_label_defn_id()
{
    static std::atomic<label_defn_id> id{};
    return ++id;
}

inline label_id get_next_label_id()
{
    static std::atomic<label_id> id{};
    return ++id;
}

struct label_defn
{
    label_defn_id m_id;
    std::string m_class;
    uint32_t m_color_rgba{};
};

struct label
{
    label_id m_id;           //!< This specific label instance
    label_defn_id m_defn_id; //!< ID of the label definition in the dictionary
    double m_start_s{};
    double m_stop_s{};
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
