/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <string>
#include <vector>

#include "label.h"

/// Palette classes whose names contain every word of the query (case-insensitive): those
/// starting with its first word, then the rest, each in palette order. None for an empty query.
std::vector<const label_defn *> filter_classes(const label_dict &dict, const std::string &query);

/// The classes the number keys 1-9 choose, in order: the favourites (by name) that the palette
/// has, if any, otherwise the first nine of the palette
std::vector<label_defn_id> numbered_classes(
    const label_dict &dict,
    const std::vector<std::string> &favourites);
