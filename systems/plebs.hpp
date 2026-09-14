// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — systems/plebs.hpp
//
// The plebs: the labour the Tribune of the Plebs assigns, what each duty
// needs, and how welfare grows or shrinks the pleb count. Transcribed from the
// US-build CSR.EXE (2026-09-14): four of step 105's monthly routines, run
// after the province pass and before the event rolls (0x29429-0x29442).
// docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 29.
//
// The words, named from the manual's Tribune screen ("five sets of numbers
// ... the numbers of plebs assigned to each of the five duties", with "the
// amounts of pleb groups needed" in parentheses) and from what each duty's
// shortfall does below. STRONG INFERENCE for the names; the arithmetic is
// transcribed.
//   DS:0x6C56  pleb groups             DS:0x6C46  welfare expenditure
//   DS:0x6C58  unassigned              DS:0x6C64  all of them, while 50 or fewer
//   assigned / needed:
//   DS:0x6C62 / 0x6C44  fire prevention       -> fire threshold DS:0x6BE4
//   DS:0x6C60 / 0x6C42  building maintenance  -> collapse threshold DS:0x6BE2
//   DS:0x6C5E / 0x6C40  road maintenance      -> road wear threshold DS:0x6BE0
//   DS:0x6C5C / 0x6C3E  construction, which the manual says the Tribune
//                       handles; its shortfall wears province roads (DS:0x6BDE)
//   DS:0x6C5A           army duty -> auxiliary Centuries DS:0x6C52 (/ 16)
// A threshold is the share of the need covered, in percent (100 when covered):
// month 0x2DF7D's rolls fire when the generator's walk (1-99) exceeds it.
//
// Checked against real saves: in all 17, the needs recomputed from the saved
// counts, the assignment and the three saved thresholds match, and welfare
// leaves the pleb count as saved (test_plebs_match_saves).

#pragma once

#include <cstdint>

#include "model/city_state.hpp"

namespace gaius::systems::plebs {

inline constexpr uint16_t kPlebs = 0x6C56;
inline constexpr uint16_t kUnassigned = 0x6C58;
inline constexpr uint16_t kWelfare = 0x6C46;
inline constexpr uint16_t kFirePrevention = 0x6C62, kBuildingMaintenance = 0x6C60, kRoadMaintenance = 0x6C5E,
                          kConstruction = 0x6C5C, kArmyDuty = 0x6C5A;
inline constexpr uint16_t kFireNeed = 0x6C44, kBuildingNeed = 0x6C42, kRoadNeed = 0x6C40, kConstructionNeed = 0x6C3E;

// 0x2DC72: the needs. With shift 4 at rank 1 or below, 3 at ranks 2-3, 2
// above (1 on the hard difficulty) and 16 more buildings counted at rank 3:
//   fire prevention       (buildings DS:0x6BF2 + extra) >> shift
//   building maintenance  (buildings + extra + 16) >> (shift + 1)
//   road maintenance      (roads DS:0x6BF0 + extra) >> shift
//   construction          (province roads DS:0x6C8A) >> (shift - 1)
// each at least 1. Returns the construction need DS:0x6C3E, which the save
// doesn't keep; the other three are saved words.
int set_needs(model::CityState& state, int difficulty);

// 0x2DEC8: the welfare the plebs expect is (plebs - unassigned / 4 +
// (plebs / 20) x rank + 150) / 3. Paying less loses the difference in pleb
// groups, at most 10 a month; paying more gains half the difference, at most 5,
// up to 2000.
void pay_welfare(model::CityState& state);

// 0x2DD21: 50 pleb groups are kept back; the rest go to the duties in order --
// fire prevention, building maintenance, road maintenance, construction, army
// duty -- each cut to what's left (and the later ones zeroed, auxiliaries
// recomputed if army duty is the one cut); what remains is unassigned.
void assign(model::CityState& state);

// 0x2DE0F: the three city thresholds (saved words) and the province road
// wear threshold DS:0x6BDE, which the save doesn't keep and this returns, from
// set_needs' construction need.
int set_thresholds(model::CityState& state, int construction_need);

}  // namespace gaius::systems::plebs
