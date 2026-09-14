// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — systems/campaign.hpp
//
// Starting a game and a province: the city's random terrain, the new game's
// governor, and everything a province resets. Transcribed from the US-build
// CSR.EXE (2026-09-15); docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 31.
//
// City terrain (0x6F05, repeated until a river fits):
//   0x6F42  grass 0x1D everywhere; a 2x2 lake where a draw's low 11 bits are
//           0 or 1; then lakes grow by 2x2 steps west and east
//   0x72DD  three times: land with no land above-or-below, or none
//           left-or-right, or few land neighbours, becomes water (0)
//   0x7228  shores: grass next to water takes the first of twelve neighbour
//           patterns' tiles (3496:15D8), some alternating with the next tile
//   0x7180  grass becomes one of the variants 0x1E-0x35
//   0x7C12  a river from the top edge (0x4A down, 0x56 across, 0x62/0x66/
//           0x6A/0x6E its bends) wandering by the draws until it leaves the
//           map; three tries on a backup of the tiles, then the whole map again
// Water is tile 0, shores 0x01-0x1C. Checked on real saves: every shore cell
// of all 17 saves' maps is the tile the pattern rule gives
// (test_campaign_terrain_matches_saves).

#pragma once

#include "formats/empire2/empire2.hpp"
#include "model/city_state.hpp"
#include "systems/month.hpp"

namespace gaius::systems::campaign {

// 0x6F05. `shore_variant` is DS:0x079C, a static the shore pass toggles
// between 0 and 1 (0 when the program starts), which survives from one map to
// the next.
void generate_city(model::CityMap& city, month::Random& random, int& shore_variant);

// 0x0056B8, a new game: year -13, rank 1, savings 100, salary 10, no provinces
// given yet, the first yearly notice at year -12 with random topics, and the
// rate sums 5.
void new_game(model::CityState& state, month::Random& random);

// A new province (the main loop's 0x0FB34 once DS:0x6C26 is set, and the start
// of a game): the province's EMPIRE2 map `province_map` replaces the embedded
// one (0x0FF1C loads EMPIRE2.0NN), a new city is generated, and 0x05730 resets
// the city: the month, counters, funds from the starting funding DS:0x6C0C
// less 1000 for each of ranks 2-4 while at least 5000 and 200 x (rank - 4)
// above rank 4 (not below 4000), 120 plebs with 10 on each duty, the rates,
// ratings, the Legion's 2 regular Centuries, the accounts and a tribute due of
// 50; every city layer, walker, record and history cleared; the Prima Cohors
// on the city's province cell (0x0621F); the race, the plebs' needs,
// assignment and thresholds; and the Imperial Highway's entry (0x06307).
// The order of the terrain generation within this is STRONG INFERENCE (its
// caller isn't traced); everything else follows 0x05730's order.
void start_province(model::CityState& state, const formats::empire2::EmpireMap& province_map,
                    month::Random& random, int difficulty, int& shore_variant);

}  // namespace gaius::systems::campaign
