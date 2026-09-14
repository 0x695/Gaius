// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/plebs.hpp"

#include <iterator>

namespace gaius::systems::plebs {

namespace {

int16_t w16(int32_t v) { return static_cast<int16_t>(static_cast<uint16_t>(v & 0xFFFF)); }
int g(const model::CityState& state, uint16_t ds) { return model::global_word(state, ds); }
void set(model::CityState& state, uint16_t ds, int v) { model::set_global_word(state, ds, w16(v)); }

}  // namespace

int set_needs(model::CityState& state, int difficulty) {
    const int rank = g(state, 0x6C30);
    int shift = rank <= 1 ? 4 : rank <= 3 ? 3 : 2;
    if (difficulty == 2) shift = 1;
    const int extra = rank == 3 ? 16 : 0;
    const int buildings = g(state, 0x6BF2), roads = g(state, 0x6BF0), province_roads = g(state, 0x6C8A);
    auto at_least_one = [](int v) { return v > 0 ? v : 1; };
    set(state, kFireNeed, at_least_one(w16(buildings + extra) >> shift));
    set(state, kBuildingNeed, at_least_one(w16(buildings + extra + 16) >> (shift + 1)));
    set(state, kRoadNeed, at_least_one(w16(roads + extra) >> shift));
    const int construction = at_least_one(province_roads >> (shift - 1));
    return construction;
}

void pay_welfare(model::CityState& state) {
    const int plebs = g(state, kPlebs), welfare = g(state, kWelfare);
    const int expected_extra = w16((plebs / 20) * g(state, 0x6C30) + 150);
    const int expected = w16(plebs - (g(state, kUnassigned) >> 2) + expected_extra) / 3;
    if (expected > welfare) {
        int loss = expected - welfare;
        if (loss > 10) loss = 10;
        const int left = plebs - loss;
        set(state, kPlebs, left < 0 ? 0 : left);
    } else if (expected < welfare) {
        int gain = welfare - expected;
        if (gain > 10) gain = 10;
        const int grown = plebs + (gain >> 1);
        set(state, kPlebs, grown > 2000 ? 2000 : grown);
    }
}

void assign(model::CityState& state) {
    const int plebs = g(state, kPlebs);
    const uint16_t duties[] = {kFirePrevention, kBuildingMaintenance, kRoadMaintenance, kConstruction, kArmyDuty};
    if (plebs <= 50) {
        set(state, 0x6C64, plebs);
        set(state, kUnassigned, 0);
        for (uint16_t d : duties) set(state, d, 0);
        return;
    }
    int left = plebs - 50;
    for (size_t i = 0; i < std::size(duties); ++i) {
        const int assigned = g(state, duties[i]);
        if (assigned < left) {
            left -= assigned;
            continue;
        }
        set(state, duties[i], left);
        for (size_t j = i + 1; j < std::size(duties); ++j) set(state, duties[j], 0);
        set(state, kUnassigned, 0);
        if (duties[i] == kArmyDuty) set(state, 0x6C52, left / 16);
        return;
    }
    set(state, kUnassigned, left);
}

int set_thresholds(model::CityState& state, int construction_need) {
    auto share = [&](uint16_t assigned, int n) {
        const int a = g(state, assigned);
        if (a >= n || n <= 0) return 100;
        return static_cast<int>(w16(a * 100) / n);
    };
    set(state, 0x6BE4, share(kFirePrevention, g(state, kFireNeed)));
    set(state, 0x6BE2, share(kBuildingMaintenance, g(state, kBuildingNeed)));
    set(state, 0x6BE0, share(kRoadMaintenance, g(state, kRoadNeed)));
    const int province = share(kConstruction, construction_need);
    if (province == 100) set(state, 0x6C88, -1);
    return province;
}

}  // namespace gaius::systems::plebs
