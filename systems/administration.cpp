// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/administration.hpp"

namespace gaius::systems::administration {

const std::array<Requirement, 22> kPromotion = {{
    {30, 10}, {35, 12}, {40, 16}, {50, 25}, {56, 30}, {60, 35}, {64, 40}, {68, 45}, {72, 50}, {74, 55}, {78, 60},
    {80, 65}, {82, 70}, {83, 72}, {84, 74}, {85, 75}, {86, 76}, {87, 77}, {88, 78}, {89, 79}, {90, 80}, {0, 0},
}};

const std::array<const char*, 21> kRankNames = {
    "Plebian",    "Citizen",      "Equitus",    "Taberllarius", "Decurian", "Iuridicus", "Procurator",
    "Magistrate", "Logistas",     "Praefectus", "Magister",     "Cubicularius", "Legate", "Quaestor",
    "Senator",    "Praetor",      "Consul",     "Proconsul",    "Princeps", "Imperator", "Caesar",
};

namespace {

int16_t w16(int32_t v) { return static_cast<int16_t>(static_cast<uint16_t>(v & 0xFFFF)); }
int g(const model::CityState& state, uint16_t ds) { return model::global_word(state, ds); }
void set(model::CityState& state, uint16_t ds, int v) { model::set_global_word(state, ds, w16(v)); }
int clamp100(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

// 3496:0172: culture points per cell of tiles 0xD8-0xF2, (religion, entertainment).
struct CulturePoints {
    int8_t religion, entertainment;
};
constexpr std::array<CulturePoints, 27> kCulturePoints = {{
    {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},  // 0xD8-0xDF temples
    {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},  // 0xE0-0xE7 forums
    {0, 0}, {0, 0}, {0, 0}, {9, 0},                                  // 0xE8-0xEA, 0xEB oracle
    {0, 0}, {0, 0}, {0, 0}, {0, 0},                                  // 0xEC-0xEF
    {0, 2}, {0, 3}, {0, 4},                                          // 0xF0-0xF2 theater, coliseum, hippodrome
}};

// 3496:00DE (rank 1) and 00F6 (other ranks): Prosperity by tax per head / 3 hundredths.
constexpr std::array<int8_t, 24> kTaxRank1 = {-2, -1, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7};
constexpr std::array<int8_t, 24> kTaxOther = {-3, -2, -1, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7};
// 3496:010E: Prosperity by population units / 50.
constexpr std::array<int8_t, 40> kPopulation = {-4, -3, -2, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1,
                                                1,  1,  1,  1,  1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3};

// The long multiply and divide at 0x28D62: value x 34 / (units / 12 + 5).
int per_head(int value, int units) {
    const int32_t divisor = w16(units) / 12 + 5;
    return w16(static_cast<int32_t>(w16(value)) * 34 / divisor);
}

}  // namespace

int population_cap(int value, int per_step, int units) {
    for (int b = 0; b < 50; ++b) {
        if (w16(b * per_step * 50) > units) {
            if (b * 2 < value) value = b * 2;
            break;
        }
    }
    return value;
}

void peace_year(model::CityState& state) { set(state, kPeace, clamp100(g(state, kPeace) + 2)); }

void culture_year(model::CityState& state) {
    int religion = 0, entertainment = 0;
    for (const auto& row : state.city.tile) {
        for (uint8_t t : row) {
            if (t < 0xD8 || t > 0xF2) continue;
            religion += kCulturePoints[static_cast<size_t>(t - 0xD8)].religion;
            entertainment += kCulturePoints[static_cast<size_t>(t - 0xD8)].entertainment;
        }
    }
    if (entertainment < 0) entertainment = 0;
    if (g(state, 0x6BEC) < 0) set(state, 0x6BEC, 0);
    const int units = g(state, 0x6C10);

    int culture = per_head(religion, units);
    set(state, 0x6C82, culture);
    if (culture > 34) culture = 34;
    const int shows = per_head(entertainment >> 1, units);
    set(state, 0x6C80, shows);
    culture = w16(culture + shows);
    if (culture > 68) culture = 68;
    culture = w16(culture + per_head(g(state, 0x6BEC), units));
    set(state, kCulture, clamp100(population_cap(culture, 1, units)));
}

void prosperity_year(model::CityState& state) {
    const int rank = g(state, kRank);
    int prosperity = g(state, kProsperity);
    int index = w16(g(state, 0x6BCA) * 100 + g(state, 0x6BC8)) / 3;
    if (index > 23) index = 23;
    if (index < 0) index = 0;  // the engine would read before the table; tax per head is never negative
    prosperity += (rank == 1 ? kTaxRank1 : kTaxOther)[static_cast<size_t>(index)];
    int units = g(state, 0x6C10);
    if (units > 1999) units = 1999;
    prosperity += kPopulation[static_cast<size_t>((units < 0 ? 0 : units) / 50)];
    const bool profit = g(state, 0x6BB6) > 0;
    prosperity += profit && rank == 1 ? 4 : profit && rank > 1 ? 2 : -2;
    set(state, kProsperity, clamp100(population_cap(w16(prosperity), 2, g(state, 0x6C10))));
}

void empire_year(model::CityState& state) {
    int empire = 0;
    const int roads = g(state, 0x6C86);
    if (roads < 0) empire -= 5;
    if (roads > 0) empire += 5;
    if (roads > 10) empire += 10;
    if (g(state, 0x6C8C) != 0) empire += 20;
    for (uint8_t cell : state.empire.cells) {
        const int t = cell & 0x7F;
        if (t == 0x4C) empire += 20;
        if (t == 0x7A) empire += 10;
        if (t == 0x79) empire += 5;
    }
    set(state, kEmpire, clamp100(empire));
}

void set_average(model::CityState& state) {
    set(state, kAverage, (g(state, kPeace) + g(state, kCulture) + g(state, kProsperity) + g(state, kEmpire)) / 4);
}

void run_ratings(model::CityState& state) {
    peace_year(state);
    culture_year(state);
    prosperity_year(state);
    empire_year(state);
    set_average(state);
}

Offer check_promotion(model::CityState& state, month::Random& random) {
    set(state, 0x6C26, 0);
    if (g(state, 0x6C24) != 0) {
        set(state, 0x6C24, g(state, 0x6C24) - 1);
        return Offer::Deferred;
    }
    const int rank = g(state, kRank);
    if (rank < 0 || rank >= static_cast<int>(kPromotion.size())) return Offer::None;
    const Requirement& need = kPromotion[static_cast<size_t>(rank)];
    if (need.average > g(state, kAverage)) return Offer::None;
    for (uint16_t rating : {kPeace, kCulture, kEmpire, kProsperity}) {
        if (need.each > g(state, rating)) return Offer::None;
    }
    // 0x2898E: draw until a province not yet given. (A game that had given all
    // 50 would loop forever; Gaius stops after 1000 draws.)
    if (state.table_50.size() < 50) state.table_50.resize(50, 0);
    for (int i = 0; i < 1000; ++i) {
        random.advance();
        const int province = random.walk >> 1;
        if (province >= 0 && province < 50 && state.table_50[static_cast<size_t>(province)] == 0) {
            set(state, 0x6CA4, province);
            break;
        }
    }
    if (rank == 19) return Offer::Caesar;
    if (rank > 19) return Offer::None;
    return Offer::Promotion;
}

void accept_promotion(model::CityState& state, int& difficulty) {
    set(state, 0x6C24, 0);
    set(state, 0x6C26, 1);
    const int rank = g(state, kRank);
    const int savings = g(state, 0x6C2E);
    if (rank >= 9 && savings > 4000) {
        set(state, 0x6C2E, 4000);
    } else if (w16(rank * 200 + 2300) < savings) {
        set(state, 0x6C2E, rank * 200 + 2300);
    }
    set(state, kRank, rank + 1);
    set(state, 0x6C2A, g(state, 0x6C2A) + 5);
    const int province = g(state, 0x6CA4);  // 0x289B0
    set(state, 0x6CA6, province);
    if (state.table_50.size() < 50) state.table_50.resize(50, 0);
    if (province >= 0 && province < 50) state.table_50[static_cast<size_t>(province)] = 1;
    if (difficulty == 0) difficulty = 1;
}

void defer_promotion(model::CityState& state, int years) {
    set(state, 0x6C24, years);
    set(state, 0x6C26, 0);
}

void become_caesar(model::CityState& state) { set(state, kRank, 20); }

Notice yearly_notice(model::CityState& state, const month::Random& random, int province_wear_threshold,
                     int linked_towns) {
    Notice n;
    if (g(state, 0x6C32) != g(state, 0x6C98)) return n;
    set(state, 0x6C98, g(state, 0x6C98) + (random.walk & 7) + 1);
    if ((random.draw & 1) == 0) {
        int topic = g(state, 0x6C96) + (random.low7 & 4);
        if (topic > 15) topic = 0;
        set(state, 0x6C96, topic);
        n.kind = Notice::Kind::News;
        n.topic = topic;
        return n;
    }
    int topic = g(state, 0x6C94) + 1;
    if (topic > 4) topic = 0;
    switch (topic) {
        case 0: n.alternate = g(state, 0x6C8C) != 0; break;
        case 1: n.alternate = linked_towns > 2; break;
        case 2: n.alternate = g(state, 0x6C12) > 5; break;
        case 3: n.alternate = province_wear_threshold == 100 || g(state, 0x6C8A) == 0; break;
        case 4:
            n.alternate = g(state, 0x6C86) > 0;
            if (g(state, 0x6C86) == 0) topic = 1;
            break;
        default: break;
    }
    set(state, 0x6C94, topic);
    n.kind = Notice::Kind::Advice;
    n.topic = topic;
    return n;
}

}  // namespace gaius::systems::administration
