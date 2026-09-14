// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — systems/administration.hpp
//
// The Administrative Advisor's four ratings, promotion, and the yearly
// notice. Transcribed from the US-build CSR.EXE (2026-09-14): the yearly
// routine 0x28238 ends with 0x28C43 (ratings), 0x29023 (promotion) and
// 0x2933B (notice). docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 30.
//
// The ratings, each 0-100, named by the manual's "peace, culture, prosperity
// and empire" in the order the Advisor draws them (STRONG INFERENCE from what
// each one's arithmetic reads):
//   DS:0x6C3C  Peace       +2 a year; rioters (-2) and invasions (-4/-10) lower it
//   DS:0x6C3A  Culture     recomputed yearly from temples, the oracle,
//                          entertainment and schools, per head of population
//   DS:0x6C38  Prosperity  accumulates: tax per head, population, a profit
//   DS:0x6C36  Empire      recomputed yearly from the province's roads and towns
//   DS:0x6C34  the average of the four, which promotion reads
// Culture and Prosperity can't exceed twice the number of 50-unit (and
// 100-unit) population steps (population_cap).
//
// Checked against real saves: the average in all 17; Culture's two raw scores
// (DS:0x6C82, 0x6C80) in the 14 saves whose buildings haven't changed since
// the year turned; Peace and Prosperity across CAESARXS -> CAESARXR, the only
// consecutive years (test_administration_matches_saves).

#pragma once

#include <array>
#include <cstdint>

#include "model/city_state.hpp"
#include "systems/month.hpp"

namespace gaius::systems::administration {

inline constexpr uint16_t kPeace = 0x6C3C, kCulture = 0x6C3A, kProsperity = 0x6C38, kEmpire = 0x6C36,
                          kAverage = 0x6C34, kRank = 0x6C30;

// 3496:01C6, by rank: the average needed and the minimum for each rating.
struct Requirement {
    int8_t average;
    int8_t each;
};
extern const std::array<Requirement, 22> kPromotion;

// The rank titles, DS string table after the province toolbar's names, in
// rank order (the saves' cities are rank 1). STRONG INFERENCE that the index is
// the rank word DS:0x6C30.
extern const std::array<const char*, 21> kRankNames;

// 0x28FEF: a rating can't exceed 2 x b, for the first b (0-49) with
// b x per_step x 50 above the population units DS:0x6C10.
int population_cap(int value, int per_step, int units);

void peace_year(model::CityState& state);       // 0x28C70
void culture_year(model::CityState& state);     // 0x28C90
void prosperity_year(model::CityState& state);  // 0x28E63
void empire_year(model::CityState& state);      // 0x28F3C
void set_average(model::CityState& state);      // 0x28C57

// 0x28C43: the four in that order, then the average.
void run_ratings(model::CityState& state);

enum class Offer {
    None,       // not deserved this year
    Deferred,   // the player asked to wait: DS:0x6C24 counts down
    Promotion,  // the promotion screen (0x291C3)
    Caesar,     // at rank 19, the last one (0x290DF)
};

// 0x29023: with no waiting years left (DS:0x6C24), a rank whose requirements
// the ratings meet is offered a new province -- picked by drawing until the
// generator's walk / 2 is a province not yet given (0x2898E, DS:0x6CA4,
// table_50) -- or, at rank 19, the title of Caesar.
Offer check_promotion(model::CityState& state, month::Random& random);

// The promotion screen's three buttons (DS:0x14E2):
// 0x29280, accept: the governor's savings are cut to 4000 from rank 9, or
// else to rank x 200 + 2300; the rank rises; DS:0x6C2A grows by 5 (1 at the
// start; read by a Forum screen, 0x09A1E); the picked province becomes the
// current one and is marked given (0x289B0); the Easy difficulty becomes
// Medium; DS:0x6C26 = 1 tells the main loop to move to it (0x0F81B).
void accept_promotion(model::CityState& state, int& difficulty);
// 0x292EA / 0x292FD: wait 9 or 24 years before the next offer.
void defer_promotion(model::CityState& state, int years);
// 0x29100: the rank becomes 20, Caesar.
void become_caesar(model::CityState& state);

struct Notice {
    enum class Kind { None, News, Advice } kind = Kind::None;
    int topic = 0;          // DS:0x6C96 for news, DS:0x6C94 for advice
    bool alternate = false; // advice whose condition holds shows its other text
};

// 0x2933B: when the year reaches DS:0x6C98, the next notice is set (walk & 7)
// + 1 years on and the screen 0x09AFD shows, by the last draw's low bit, news
// (topic += low7 & 4, back to 0 past 15) or advice (topic cycles 0-4; its
// alternate text for: the highway linked, more than 2 linked towns, more than
// 5 Cohorts, construction fully staffed or no province roads, a positive road
// score -- and topic 4 with no road score at all becomes 1). Nothing else
// changes. `province_wear_threshold` is DS:0x6BDE, which the save doesn't keep.
Notice yearly_notice(model::CityState& state, const month::Random& random, int province_wear_threshold,
                     int linked_towns);

}  // namespace gaius::systems::administration
