// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — systems/housing.hpp
//
// Layer 3, GAIUS_ROADMAP.md Phase 4: housing development, population, and the
// gate that removes a house when land value runs too high. Transcribed from the
// decompressed US-build CSR.EXE (2026-09-13) -- see
// docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 16.
//
// How the engine runs housing. A game month is 106 tick steps. Steps 0-99 each
// run the development pass (routine 0x294CF) over one row of the city; steps
// 100-105 rebuild the service layers and count population
// (systems::service::rebuild_services, population_units below). So houses react
// to the services computed at the end of the previous month.
//
// For each cell of its row, the pass:
//   - below tile 0xC8: zeroes land value. Fountains (0xB9-0xBD) may change size.
//     Tiles 0xA8/AB/AE/B1 run a random decay-and-spread routine (0x29624) that
//     is NOT modeled: it needs the engine's RNG and an untraced far call.
//   - tiles 0xC8-0xD6: land value takes one evolve_land_value step. 0xD7 and
//     above: land value is zeroed.
//   - then, for the anchor cell of a building (7BB4 part index 0 -- see
//     systems::construction::place), runs that tile's development handler from
//     the far-pointer table at DS:1212.
//
// DS:1212 and DS:153A are adjacent tables: DS:1212 is indexed by tile id for
// ids >= 0xC8, and its entries for 0xCA-0xFF are the same memory as DS:153A's
// slots for 0x00-0x35. Earlier RE, and this project, read those slots as
// handlers for tiles 0x00-0x35. They are housing development handlers.
//
// The development handlers. Each compares the anchor's coverage (A2C4, signed)
// with a demotion and a promotion threshold, requires more of the anchor's C9D4
// service bits as the grade rises (water 0x01, then 0x02, market 0x08, bath
// houses 0x04, school/hospital 0x40, entertainment 0x80), and gates promotion on
// the city's population units. Houses grow by absorbing neighbouring cells:
// 1x1 grades 0xC8-0xCB; pairs 0xCC-0xCE and 0xD1-0xD4; one-cell 0xCF/0xD0 where
// a pair can't form; 2x2 0xD5-0xD6; 3x3 0xD7. Tiles 0xD8-0xDF follow the same
// pattern without service bits (Temple's construction seed is 0xD8). Bath houses
// (0xE8/0xEA) grow from 1x1 to 2x2. Every threshold, gate and absorbable-cell
// rule is transcribed exactly.
//
// Identities, as far as the evidence goes: that 0xC8-0xD7 are the manual's
// sixteen housing grades is STRONG INFERENCE (Housing's construction seed is
// 0xC8, and the population table covers exactly these ids); that C9D4.01 is
// water is STRONG INFERENCE (only wells, reservoirs and fountains set it); that
// the unit sum is population is STRONG INFERENCE.

#pragma once

#include <array>
#include <cstdint>

#include "model/city_state.hpp"

namespace gaius::systems::housing {

// Routine 0x2DB49. If the cell's land value (54A4, signed) exceeds `threshold`:
// the tile becomes 0xA7 and A2C4, 7BB4 and 54A4 at the cell are zeroed, and it
// returns true. Called by grades 0xC8-0xCB with thresholds 20/30/40/48. When it
// fires, the engine also conditionally spawns an actor, lowers DS:0x6C3C by 2
// and sets DS:0x6C84 to 2 -- not modeled, since there is no actor system yet.
bool land_value_allows(model::CityMap& city, int x, int y, int threshold);

// Population units per cell for tiles 0xC8-0xD7: the table at 3496:007E. Per
// cell, not per building -- larger houses span more cells.
inline constexpr std::array<uint8_t, 16> kPopulationUnitsPerCell = {1, 1, 2, 3, 3, 5, 6, 5, 6, 4, 4, 3, 3, 2, 2, 1};

// Routine 0x2C93F, population half: kPopulationUnitsPerCell summed over the
// grid. The engine stores this at DS:0x6C10, gates promotion on it, and stores
// four times it at DS:0x6C0E. Matches the saved value exactly in three of four
// real saves; the fourth is 2 higher, exactly one 0xCB -> 0xCF upgrade made
// after that month's count.
int population_units(const model::CityMap& city);
inline int population(const model::CityMap& city) { return 4 * population_units(city); }

// What the development pass needs from outside the grid.
struct DevelopmentContext {
    // DS:0x6C10: population_units() as of the month's step 101.
    int population_units = 0;
    // The growth evolve_land_value applies this row to housing cells with
    // C9D4.20. In the engine it is DS:0x6BF6 + (random & 3) - 1, drawn once per
    // row; the RNG isn't transcribed, so the caller supplies the value.
    int land_value_growth = 0;
};

// Routine 0x294CF for one row -- see the file header.
void develop_row(model::CityMap& city, int row, const DevelopmentContext& ctx);

// The DS:1212 development handler for the building anchored at (x, y), chosen
// by its tile id. `flags` is the anchor's C9D4 byte, which the pass passes in.
// Returns how many further columns the pass skips (0, 1 or 2) because the
// handler covered them; returns 0 and does nothing for ids without a handler.
int develop_building(model::CityMap& city, int x, int y, uint8_t flags, const DevelopmentContext& ctx);

}  // namespace gaius::systems::housing
