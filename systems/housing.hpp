// Gaius — systems/housing.hpp
//
// Layer 3, GAIUS_ROADMAP.md Phase 4: the residential development state
// machine (43A5 tile IDs 0x00-0x15) and population derivation.
//
// Scope discipline, same as systems/service.hpp: only what's actually
// documented gets implemented.
//
//   - `land_value_allows` is the one mechanism CAESAR_CITY_STATE_v5.md
//     traces in full (routine 0x2DB49): DEFINITIVE.
//   - Tile 00's handler (0x297AC) is the only one of the 22 tile IDs
//     (0x00-0x15) traced in enough detail to implement -- it calls
//     land_value_allows(threshold=0x28), then (per the RE doc) "checks the
//     local A2C4 value and may replace the tile with C9, CA, etc." without
//     giving the actual A2C4 thresholds. That part is NOT implemented --
//     flagged as open RE work, not guessed. Tiles 0x01-0x15 have known
//     dispatch-table addresses (v5) but no traced behavior at all.
//   - Population density-per-grade: the manual (Caesar_1_Manual.pdf)
//     confirms the SHAPE (monotonic increase across the sixteen grades,
//     "the fanciest houses actually have a slight drop in density") but no
//     RE work has traced the actual population-derivation routine, and
//     this project's copy of the manual gives no numeric table either --
//     it's a condensed player's guide, not the full technical manual.
//     `provisional_density_per_grade` is therefore exactly what its name
//     says: the simplest function matching the confirmed SHAPE (grade
//     number as its own density unit), not a real game-balance number.
//   - Grade-to-tile-ID mapping: explicitly deferred to Phase 5 by
//     GAIUS_ROADMAP.md itself ("map grade -> tile ID range once the
//     construction dispatcher work... clarifies IDs") -- not attempted
//     here. HousingGrade is a standalone Layer 3 concept for now, not
//     derived from or mapped to any `model::CityMap::tile` value.

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

// The tile-00 handler (0x297AC): calls land_value_allows(x, y, 0x28) (the
// confirmed threshold, 40). Only meaningful when city.tile[y][x] == 0x00;
// the caller is responsible for that (this module has no per-tile
// dispatcher yet -- see the file header for why tiles 0x01-0x15 aren't
// handled at all).
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
