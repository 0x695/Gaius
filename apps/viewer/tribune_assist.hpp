// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — apps/viewer/tribune_assist.hpp
//
// "Tribune: Automatic" (Settings > Game). The original leaves the Tribune of the Plebs to the player. A new game starts
// with 10 pleb groups on each duty and a welfare of 88, which covers a city of up to about 160 buildings; past that the
// fire, building and road duties are short of plebs, and a duty's shortfall is the chance, every month, of a fire, a
// collapse or a worn road (systems/plebs.hpp: the share of the need covered is the threshold the month's rolls are made
// against). A player who has not found the Tribune's page sees fires and collapses every month and nothing to do about them.
//
// This does what a careful player does with that page, once a month, with the page's own arrows (systems::forum):
//   * the fire, building-maintenance and road duties, then the province construction duty, are raised from the plebs
//     nobody has been given work, to what the city needs plus a fifth (the city grows between two months), the
//     construction duty also for a road's worth of pieces not laid yet;
//   * welfare is set so the plebs keep coming until the duties, army duty and the 50 kept back are all covered, and then
//     so they stay where they are.
// It never lowers a duty and never takes plebs from army duty (the auxiliaries are the player's choice), and it changes
// nothing in systems/: the simulation stays the original's, and a player who switches it off plays the original's rule.

#pragma once

#include "model/city_state.hpp"
#include "systems/forum.hpp"
#include "systems/plebs.hpp"

namespace gaius::viewer {

// Plebs kept beyond what the duties need, so a month's growth does not outrun them.
inline constexpr int kTribuneSpare = 20;

inline int tribune_target(int need) { return need + need / 5 + 1; }

// The province's roads are counted and rolled against in one step of the month (step 105: the count, the needs, the
// assignment, the thresholds, the rolls), so a road laid this month is in the need its own month's roll uses, before
// this assistant can answer. The construction duty is therefore staffed ahead, for this many more pieces than stand
// (a long road's worth): without it every new stretch left one month short, and a short month can wear a piece of the
// Imperial Highway away (seen in `playtest tutorial`, 2026-10-09).
inline constexpr int kTribuneRoadsAhead = 40;

// plebs::set_needs' divisor for the construction duty: a pleb group for every 8 pieces at rank 1, every 4 at ranks 2
// and 3, every 2 above, and one for each on the hard difficulty.
inline int tribune_roads_ahead(const model::CityState& state, int difficulty) {
    const int rank = model::global_word(state, 0x6C30);
    const int shift = difficulty == 2 ? 1 : rank <= 1 ? 4 : rank <= 3 ? 3 : 2;
    return kTribuneRoadsAhead >> (shift - 1);
}

inline void tribune_assist(model::CityState& state, int difficulty) {
    namespace plebs = systems::plebs;
    using systems::forum::Duty;
    const auto word = [&](uint16_t ds) { return model::global_word(state, ds); };

    // The needs, from the counts the month's scan has just published.
    const int construction_need = plebs::set_needs(state, difficulty);
    struct Target {
        Duty duty;
        int want;
    };
    const Target targets[] = {{Duty::FirePrevention, tribune_target(word(plebs::kFireNeed))},
                              {Duty::BuildingMaintenance, tribune_target(word(plebs::kBuildingNeed))},
                              {Duty::RoadMaintenance, tribune_target(word(plebs::kRoadNeed))},
                              {Duty::Construction,
                               tribune_target(construction_need) + tribune_roads_ahead(state, difficulty)}};
    int wanted = 0;
    for (const Target& t : targets) {
        wanted += t.want;
        // One pleb group a click, from the unassigned only: raise_duty would otherwise take army duty's.
        while (word(systems::forum::duty_word(t.duty)) < t.want && word(plebs::kUnassigned) > 0 &&
               systems::forum::raise_duty(state, t.duty)) {
        }
    }

    // Welfare: ten above what the plebs expect while there are fewer of them than the duties need (they grow by 5 a
    // month), one above once there are enough (no change in either direction).
    const int needed = plebs::kPlebsKeptBack + wanted + word(plebs::kArmyDuty) + kTribuneSpare;
    const int expected = plebs::expected_welfare(state);
    const int welfare = word(plebs::kPlebs) < needed ? expected + 10 : expected + 1;
    model::set_global_word(state, plebs::kWelfare, welfare < 0 ? 0 : welfare > 9999 ? 9999 : welfare);
}

}  // namespace gaius::viewer
