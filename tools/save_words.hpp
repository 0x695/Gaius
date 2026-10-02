// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius -- tools/save_words.hpp
//
// The saved global words that have a name, for the save tools: a word's DS
// address, what the game calls it (as far as the findings established), and a
// way to look one up in a model::CityState. Anything not listed is shown by
// its address alone. Every entry comes from the findings (dispatch findings
// sections 15-45), where its confidence is stated; this table only names.

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "model/city_state.hpp"
#include "systems/administration.hpp"

namespace gaius::tools {

struct WordName {
    uint16_t ds;
    const char* name;
};

inline constexpr WordName kWordNames[] = {
    {0x6C1C, "month (0-11)"},
    {0x6C32, "year"},
    {0x6C30, "rank"},
    {0x6CB8, "difficulty"},
    {0x6CA2, "funds"},
    {0x6C2C, "governor's salary"},
    {0x6C2E, "personal savings"},
    {0x6C10, "population units"},
    {0x6C0E, "population"},
    {0x6C3C, "peace rating"},
    {0x6C3A, "culture rating"},
    {0x6C38, "prosperity rating"},
    {0x6C36, "empire rating"},
    {0x6C34, "average rating"},
    {0x6C4E, "regular centuries"},
    {0x6C4C, "regular centuries last year"},
    {0x6C4A, "irregular centuries"},
    {0x6C48, "irregular centuries last year"},
    {0x6C52, "auxiliary centuries"},
    {0x6C50, "auxiliary centuries last year"},
    {0x6C06, "conscription"},
    {0x6C08, "army wages"},
    {0x6C56, "plebs"},
    {0x6C58, "unassigned plebs"},
    {0x6C46, "welfare"},
    {0x6C62, "pleb duty: fire prevention"},
    {0x6C60, "pleb duty: building maintenance"},
    {0x6C5E, "pleb duty: road maintenance"},
    {0x6C5C, "pleb duty: province construction"},
    {0x6C5A, "pleb duty: army"},
    {0x6C44, "fire prevention needs"},
    {0x6C42, "building maintenance needs"},
    {0x6C40, "road maintenance needs"},
    {0x6C3E, "province construction needs"},
    {0x6CA0, "forums"},
    {0x6C9E, "workshops"},
    {0x6C9C, "barracks"},
    {0x6BF6, "unrest growth base"},
    {0x6BF8, "housing land value base"},
    {0x6BF2, "buildings last month"},
    {0x6BF0, "roads last month"},
    {0x6C6A, "fire counter"},
    {0x6C68, "collapse counter"},
    {0x6C66, "road wear counter"},
    {0x6BE6, "funds warning given"},
    {0x6BE8, "industry report average"},
    {0x6CB6, "city view column"},
    {0x6CB4, "city view row"},
    {0x6CB2, "province view column"},
    {0x6CB0, "province view row"},
    {0x6CAE, "view (0 city, 1 province)"},
    {0x6C78, "messages option"},
    {0x6C7C, "new year banner timer"},
    {0x6CE2, "Cohort 2 hand-over pending"},
};

inline const char* word_name(uint16_t ds) {
    for (const WordName& w : kWordNames)
        if (w.ds == ds) return w.name;
    return nullptr;
}

// The rank's title as the viewer shows it, or "" outside the table.
inline std::string rank_title(int rank) {
    const auto& names = systems::administration::kRankNames;
    return rank >= 0 && rank < static_cast<int>(names.size()) ? names[static_cast<size_t>(rank)] : "";
}

}  // namespace gaius::tools
