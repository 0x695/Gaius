// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/housing.hpp"

namespace gaius::systems::housing {

bool land_value_allows(model::CityMap& city, int x, int y, int threshold) {
    int8_t lv = city.land_value[y][x];
    if (lv > threshold) {
        city.tile[y][x] = 0xA7;
        city.coverage[y][x] = 0;
        city.operational_state[y][x] = 0;
        city.land_value[y][x] = 0;
        return true;
    }
    return false;
}

void tick_tile_00(model::CityMap& city, int x, int y) {
    land_value_allows(city, x, y, 0x28);  // 40, per CAESAR_CITY_STATE_v5.md
    // Steps 2-4 of the decoded handler (A2C4-driven writes of 0xC9/0xCB) are
    // deliberately not modeled -- the handler is unreachable in the engine.
    // See housing.hpp.
}

int provisional_density_per_grade(HousingGrade grade) {
    int g = static_cast<int>(grade);
    return g == static_cast<int>(HousingGrade::Grade16) ? g - 2 : g;  // 14 < grade15's 15: a drop, not a plateau
}

int population(int squares, HousingGrade grade) { return squares * provisional_density_per_grade(grade); }

}  // namespace gaius::systems::housing
