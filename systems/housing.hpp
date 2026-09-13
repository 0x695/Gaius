// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — systems/housing.hpp
//
// Layer 3, GAIUS_ROADMAP.md Phase 4: land-value gating and population
// derivation for housing.
//
// CORRECTED 2026-09-13, against real saves and the disassembly. This file was
// written on the premise (from CAESAR_CITY_STATE_v5.md) that tile ids
// 0x00-0x15 are the residential state machine. They are not simulated at all:
// the engine's only reader of the DS:153A tile-handler table (the scan at
// routine 0x2BBBB) skips every tile <= 0x35, and in four real saves the
// 0x00-0x1C cells form bordered terrain blobs that don't change across a whole
// play session. The housing tiers the engine actually simulates are 0xC8-0xD7
// -- see systems::service::apply_housing_tier.
//
//   - `land_value_allows` (routine 0x2DB49): DEFINITIVE, and still a real
//     routine -- only its caller in this file was misattributed.
//   - `tick_tile_00` transcribes DS:153A's entry for tile 0x00 (0x297AC). That
//     handler is unreachable from the main tick, so this models code the engine
//     never runs; it is kept because the handler is real and fully decoded.
//     The full behaviour is documented at its declaration.
//   - Population density per grade: the manual confirms the SHAPE (monotonic
//     increase across sixteen grades, "the fanciest houses actually have a
//     slight drop in density"), and no numeric table exists in this project's
//     sources, so `provisional_density_per_grade` uses grade number as its own
//     density -- the shape, not a real game-balance number.
//   - HousingGrade is still not mapped to tile ids. The strongest candidate is
//     now 0xC8-0xD7 in order, sixteen ids for sixteen grades, but that remains
//     an inference, not a finding.

#pragma once

#include "model/city_state.hpp"

namespace gaius::systems::housing {

// CAESAR_CITY_STATE_v5.md routine 0x2DB49. Tests land value at (x,y)
// against `threshold`; if it exceeds it, transitions the tile to the
// confirmed "development succeeded" id (0xA7) and resets coverage/
// operational_state/land_value at that cell to 0 -- exactly the traced
// behavior. The RE doc notes the real routine "then performs further
// population/object work" after this; that part isn't traced in enough
// detail to implement and is not modeled here. Returns whether the
// transition happened.
bool land_value_allows(model::CityMap& city, int x, int y, int threshold);

// DS:153A's handler for tile 0x00 (0x297AC). UNREACHABLE in the engine: its
// only dispatch site skips tiles <= 0x35, and nothing else calls it. Its full
// decoded behaviour, for the record:
//   1. land_value_allows(x, y, 0x28); if that fired (tile -> 0xA7), stop.
//   2. Otherwise read A2C4 at the cell (signed). If it is < 2, or the runtime
//      flag DS:0x6DC7 bit 0 is clear, write tile 0xC9.
//   3. Otherwise (A2C4 >= 2 and the flag set): if A2C4 == 2 do nothing; if
//      A2C4 > 2, write tile 0xCB.
//   4. After writing 0xC9 or 0xCB, clear 7BB4 at the cell.
// Only step 1 is implemented. Steps 2-4 depend on DS:0x6DC7, a runtime global
// whose meaning isn't traced, and modeling them would add API for a handler
// that never runs.
void tick_tile_00(model::CityMap& city, int x, int y);

// Sixteen grades per the manual ("There are sixteen grades of housing
// that a tent can evolve into"). NOT mapped to model::CityMap::tile values
// -- see file header.
enum class HousingGrade {
    Grade1 = 1,
    Grade2,
    Grade3,
    Grade4,
    Grade5,
    Grade6,
    Grade7,
    Grade8,
    Grade9,
    Grade10,
    Grade11,
    Grade12,
    Grade13,
    Grade14,
    Grade15,
    Grade16,
};

// PROVISIONAL -- see file header. Matches the manual's confirmed shape
// (monotonic 1-15, grade 16 dips below grade 15) using grade number itself
// as the density unit. Not a real game-balance number.
int provisional_density_per_grade(HousingGrade grade);

// Population is (squares * density), per the manual: "Population is based
// on the number of housing squares in the City, and modified by the type
// of housing in each square."
int population(int squares, HousingGrade grade);

}  // namespace gaius::systems::housing
