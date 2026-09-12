// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/service.hpp"

#include <algorithm>

namespace gaius::systems::service {

namespace {

// Shared square-radius iteration for apply_coverage/apply_land_value/
// apply_flags -- Chebyshev distance <= radius, clipped to the grid.
template <typename Fn>
void for_each_in_square(int x, int y, int radius, Fn&& fn) {
    int y0 = std::max(0, y - radius);
    int y1 = std::min(model::kCityH - 1, y + radius);
    int x0 = std::max(0, x - radius);
    int x1 = std::min(model::kCityW - 1, x + radius);
    for (int cy = y0; cy <= y1; ++cy) {
        for (int cx = x0; cx <= x1; ++cx) {
            fn(cy, cx);
        }
    }
}

bool connected(const model::CityMap& city, int x, int y) { return (city.operational_state[y][x] & 0x10) != 0; }

}  // namespace

void reset_tick(model::CityMap& city, ServiceState& service) {
    for (int y = 0; y < model::kCityH; ++y) {
        for (int x = 0; x < model::kCityW; ++x) {
            city.service_flags[y][x] &= 0x12;  // only bits 0x02/0x10 survive -- CAESAR_CITY_STATE_v6.md routine 0x2C8D3
            city.coverage[y][x] = 0;
            service.coverage_ceiling[y][x] = 0x3F;  // 63
        }
    }
}

void apply_coverage(model::CityMap& city, int x, int y, int delta, int radius, int ceiling) {
    for_each_in_square(x, y, radius, [&](int cy, int cx) {
        int v = static_cast<int>(city.coverage[cy][cx]) + delta;
        city.coverage[cy][cx] = static_cast<uint8_t>(std::clamp(v, 0, ceiling));
    });
}

void apply_land_value(model::CityMap& city, int x, int y, int delta, int radius, int ceiling) {
    int effective_ceiling = std::min(ceiling, 50);  // -8..+50 is DEFINITIVE and never exceeded regardless of `ceiling`
    for_each_in_square(x, y, radius, [&](int cy, int cx) {
        int v = static_cast<int>(city.land_value[cy][cx]) + delta;
        city.land_value[cy][cx] = static_cast<int8_t>(std::clamp(v, -8, effective_ceiling));
    });
}

void apply_flags(model::CityMap& city, int x, int y, int radius, uint8_t mask) {
    for_each_in_square(x, y, radius, [&](int cy, int cx) { city.service_flags[cy][cx] |= mask; });
}

void apply_tile_36_3b(model::CityMap& city, int x, int y) {
    if (connected(city, x, y)) apply_coverage(city, x, y, 1, 1, kObservedCoverageCeiling);
}

void apply_tile_3c_3f(model::CityMap& city, int x, int y) {
    apply_coverage(city, x, y, 1, 2, kObservedCoverageCeiling);
    apply_land_value(city, x, y, -2, 2, 0x40);
}

void apply_tile_40(model::CityMap& city, int x, int y) {
    int delta = connected(city, x, y) ? 3 : 2;
    apply_coverage(city, x, y, delta, 1, kObservedCoverageCeiling);
}

void apply_temple(model::CityMap& city, int x, int y, TempleVariant variant) {
    static constexpr int kRadiusByVariant[] = {0, 6, 8, 10, 12};  // index 0 unused; variants are 1-based
    int radius = kRadiusByVariant[static_cast<int>(variant)];
    apply_flags(city, x, y, radius, 0x20);
    if (variant == TempleVariant::Variant4) {
        apply_coverage(city, x, y, 1, 5, kObservedCoverageCeiling);  // only variant 4's coverage is confirmed
    }
}

void apply_bath_houses(model::CityMap& city, int x, int y) {
    if (!connected(city, x, y)) return;
    apply_flags(city, x, y, 4, 0x04);
    apply_coverage(city, x, y, 1, 3, kObservedCoverageCeiling);
}

void apply_hospital(model::CityMap& city, int x, int y) {
    apply_flags(city, x, y, 4, 0x40);
    apply_coverage(city, x, y, 1, 3, kObservedCoverageCeiling);
}

void apply_theater(model::CityMap& city, int x, int y) {
    apply_flags(city, x, y, 4, 0x80);
    apply_coverage(city, x, y, 1, 3, kObservedCoverageCeiling);
}

void apply_coliseum(model::CityMap& city, int x, int y) {
    apply_flags(city, x, y, 6, 0x80);
    apply_coverage(city, x, y, 1, 4, kObservedCoverageCeiling);
}

void apply_hippodrome(model::CityMap& city, int x, int y) {
    apply_flags(city, x, y, 7, 0x80);
    apply_coverage(city, x, y, 1, 5, kObservedCoverageCeiling);
}

void apply_plaza(model::CityMap& city, int x, int y) { apply_coverage(city, x, y, 1, 4, kObservedCoverageCeiling); }

void apply_barracks(model::CityMap& city, int x, int y) {
    apply_flags(city, x, y, 6, 0x08);
    apply_coverage(city, x, y, 1, 1, kObservedCoverageCeiling);
}

void apply_prefecture(model::CityMap& city, int x, int y) {
    apply_coverage(city, x, y, 1, 3, kObservedCoverageCeiling);
}

namespace {
// Shared mechanism for apply_school/apply_oracle -- see their header
// comment for why this is one function behind two names, not two
// independently-confirmed mechanisms.
void apply_school_or_oracle_mechanism(model::CityMap& city, int x, int y) {
    apply_coverage(city, x, y, 1, 2, kObservedCoverageCeiling);
    apply_flags(city, x, y, 4, 0x20);
    apply_land_value(city, x, y, -2, 3);
}
}  // namespace

void apply_school(model::CityMap& city, int x, int y) { apply_school_or_oracle_mechanism(city, x, y); }
void apply_oracle(model::CityMap& city, int x, int y) { apply_school_or_oracle_mechanism(city, x, y); }

void apply_tile_ea(model::CityMap& city, int x, int y) {
    apply_coverage(city, x, y, 2, 8, kObservedCoverageCeiling);
    apply_land_value(city, x, y, -2, 5);
}

void dispatch_tile(model::CityMap& city, int x, int y) {
    uint8_t tile = city.tile[y][x];
    switch (tile) {
        case 0x36: case 0x37: case 0x38: case 0x39: case 0x3A: case 0x3B:
            apply_tile_36_3b(city, x, y);
            break;
        case 0x3C: case 0x3D: case 0x3E: case 0x3F:
            apply_tile_3c_3f(city, x, y);
            break;
        case 0x40:
            apply_tile_40(city, x, y);
            break;
        case 0xE0: case 0xE1:
            apply_temple(city, x, y, TempleVariant::Variant1);
            break;
        case 0xE2: case 0xE3:
            apply_temple(city, x, y, TempleVariant::Variant2);
            break;
        case 0xE4: case 0xE5:
            apply_temple(city, x, y, TempleVariant::Variant3);
            break;
        case 0xE6: case 0xE7:
            apply_temple(city, x, y, TempleVariant::Variant4);
            break;
        case 0xE8: case 0xEA:
            apply_bath_houses(city, x, y);
            break;
        case 0xEB:
            apply_tile_ea(city, x, y);
            break;
        case 0xEC: case 0xED:
            apply_hospital(city, x, y);
            break;
        case 0xEE:
            apply_school(city, x, y);
            break;
        case 0xEF:
            apply_oracle(city, x, y);
            break;
        case 0xF0:
            apply_theater(city, x, y);
            break;
        case 0xF1:
            apply_coliseum(city, x, y);
            break;
        case 0xF2:
            apply_hippodrome(city, x, y);
            break;
        case 0xF3:
            apply_plaza(city, x, y);
            break;
        case 0xF4:
            apply_barracks(city, x, y);
            break;
        case 0xF5: case 0xF6:
            apply_prefecture(city, x, y);
            break;
        default:
            break;  // 0x00-0x15 (housing, handled separately), 0xE9 (undocumented default), everything else: no-op
    }
}

}  // namespace gaius::systems::service
