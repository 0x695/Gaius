// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/battle.hpp"

#include <fstream>

#include "systems/actors.hpp"
#include "systems/military.hpp"

namespace gaius::systems::battle {

// 3496:1790 (6 bytes of each 8) with the names at DS:0x2BAD.
const std::array<Race, 16> kRaces = {{
    {"Carthaginians", 40, 0, 5, 4, 6, 3}, {"Mauri", 41, 1, 4, 3, 7, 4},     {"Blemmyes", 42, 2, 6, 5, 3, 5},
    {"Sassanids", 43, 0, 3, 8, 2, 6},     {"Huns", 41, 2, 7, 4, 6, 2},      {"Ostrogoths", 42, 1, 5, 6, 3, 8},
    {"Visigoths", 43, 1, 4, 6, 2, 4},     {"Alamanni", 40, 1, 6, 8, 2, 3},  {"Saxons", 40, 2, 5, 6, 6, 6},
    {"Picts", 41, 0, 4, 7, 4, 7},         {"Celts", 42, 0, 6, 1, 6, 6},     {"Celtiberians", 43, 2, 7, 5, 6, 5},
    {"Helvetii", 40, 2, 3, 8, 2, 3},      {"Ligurians", 41, 1, 8, 3, 7, 5}, {"Illyrians", 42, 0, 2, 9, 3, 4},
    {"Volcae", 43, 1, 8, 4, 6, 2},
}};

const std::array<uint8_t, 50> kProvinceRace = {0,  0,  12, 12, 0, 0, 13, 15, 11, 1,  1,  11, 11, 15, 11, 10, 10,
                                               8,  15, 10, 9,  9, 9, 9,  8,  7,  6,  5,  14, 14, 6,  0,  0,  6,
                                               6,  3,  4,  4,  3, 3, 3,  3,  2,  2,  1,  1,  1,  1,  1,  6};

namespace {

int16_t w16(int32_t v) { return static_cast<int16_t>(static_cast<uint16_t>(v & 0xFFFF)); }
int g(const model::CityState& state, uint16_t ds) { return model::global_word(state, ds); }
void set(model::CityState& state, uint16_t ds, int v) { model::set_global_word(state, ds, w16(v)); }

int8_t sbyte(uint8_t v) { return static_cast<int8_t>(v); }
int word(const model::Actor& a, size_t at) { return static_cast<int16_t>(a.raw[at] | (a.raw[at + 1] << 8)); }
void set_word(model::Actor& a, size_t at, int v) {
    a.raw[at] = static_cast<uint8_t>(v & 0xFF);
    a.raw[at + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

inline constexpr size_t kX = 0x02, kY = 0x04, kPixelsLeft = 0x0F, kDestX = 0x12, kDestY = 0x13, kCell = 0x18,
                        kState = 0x31;

// 0x226BC / 0x225AD: morale back into 0-9.
void clamp_morale(model::Actor& a) {
    const int m = sbyte(a.raw[military::kCohortMorale]);
    if (m > 9) a.raw[military::kCohortMorale] = 9;
    if (m < 0) a.raw[military::kCohortMorale] = 0;
}

void add_morale(model::Actor& a, int delta) {
    a.raw[military::kCohortMorale] = static_cast<uint8_t>(a.raw[military::kCohortMorale] + delta);
}

// Stop where it stands: the destination becomes the current cell (x / 16).
void stop_here(model::Actor& a, bool arithmetic_shift) {
    a.raw[kState] = military::kCohortMobilized;
    // 0x230BE shifts; 0x22D5D divides. They differ only for negative
    // positions, which the province map never has.
    const int x = word(a, kX), y = word(a, kY);
    a.raw[kDestX] = static_cast<uint8_t>(arithmetic_shift ? (x >> 4) : (x / 16));
    a.raw[kDestY] = static_cast<uint8_t>(arithmetic_shift ? (y >> 4) : (y / 16));
}

}  // namespace

void load_race(model::CityState& state) {
    const int province = g(state, 0x6CA6);
    const int race = (province >= 0 && province < 50) ? static_cast<int8_t>(kProvinceRace[province]) : 0;
    set(state, 0x6BD6, race);
    const Race& r = kRaces[static_cast<size_t>(race) & 15];
    set(state, 0x6BD8, r.banner);
    set(state, 0x6BDA, r.invader_type);
    set(state, 0x6BD4, r.assault);
    set(state, 0x6BD2, r.flank);
    set(state, 0x6BD0, r.charge);
    set(state, 0x6BCE, r.tortoise);
}

int tactic_strength(const model::CityState& state, Tactic tactic) {
    switch (tactic) {
        case Tactic::Tortoise: return g(state, 0x6BCE);
        case Tactic::Assault: return g(state, 0x6BD4);
        case Tactic::Flank: return g(state, 0x6BD2);
        case Tactic::Charge: return g(state, 0x6BD0);
    }
    return 0;
}

Round fight_round(model::CityState& state, int cohort, int army, Tactic tactic, const month::Random& random) {
    model::Actor& c = state.objects[static_cast<size_t>(cohort)];
    model::Actor& a = state.objects[static_cast<size_t>(army)];
    Round r;

    int barbarians = w16(tactic_strength(state, tactic) * 2 + sbyte(a.raw[kArmySize]) + (random.walk & 7));
    int romans = w16(sbyte(c.raw[military::kCohortRegulars]) * 3 + sbyte(c.raw[military::kCohortIrregulars]) * 2 +
                     sbyte(c.raw[military::kCohortAuxiliaries]) / 2 + (random.low7 & 3));
    const int morale = sbyte(c.raw[military::kCohortMorale]);
    if (morale <= 1) {
        romans -= 4;
    } else if (morale <= 3) {
        romans -= 2;
    } else if (morale >= 8) {
        romans += 2;
    } else if (morale >= 6) {
        romans += 1;
    }
    r.barbarians = barbarians = w16(barbarians) >> 2;
    r.romans = romans = w16(romans) >> 2;

    if (barbarians == romans) {
        r.outcome = Outcome::Even;
    } else if (barbarians < romans) {
        r.outcome = Outcome::RomansWon;
        if (random.walk & 1) add_morale(c, 1);
        a.raw[kArmySize] = static_cast<uint8_t>(a.raw[kArmySize] - static_cast<uint8_t>(romans - barbarians));
        if (sbyte(a.raw[kArmySize]) <= 0) {
            win(state, cohort, army);
            r.victory = true;
            return r;
        }
    } else {
        r.outcome = Outcome::BarbariansWon;
        if (random.walk & 1) add_morale(c, -1);
        const struct {
            size_t field;
            uint16_t pool;
        } kinds[] = {{military::kCohortAuxiliaries, military::kAuxiliaries},
                     {military::kCohortIrregulars, military::kIrregulars},
                     {military::kCohortRegulars, military::kRegulars}};
        for (const auto& k : kinds) {
            if (sbyte(c.raw[k.field]) > 0) {
                --c.raw[k.field];
                set(state, k.pool, g(state, k.pool) - 1);
            }
        }
        if (sbyte(c.raw[military::kCohortRegulars]) <= 0 && sbyte(c.raw[military::kCohortIrregulars]) <= 0 &&
            sbyte(c.raw[military::kCohortAuxiliaries]) <= 0) {
            lose(state, cohort);
            r.defeat = true;
            return r;
        }
    }
    clamp_morale(c);
    return r;
}

void retreat(model::CityState& state, int cohort) {
    model::Actor& c = state.objects[static_cast<size_t>(cohort)];
    add_morale(c, -2);
    stop_here(c, true);
    clamp_morale(c);
}

void win(model::CityState& state, int cohort, int army) {
    model::Actor& c = state.objects[static_cast<size_t>(cohort)];
    add_morale(c, 2);
    if (c.raw[kCohortPatrolX] == 0) {
        stop_here(c, false);
    } else {
        c.raw[kState] = 11;
        c.raw[kDestX] = c.raw[kCohortResumeX];
        c.raw[kDestY] = c.raw[kCohortResumeY];
    }
    actors::release(state, army);
    clamp_morale(c);
}

void lose(model::CityState& state, int cohort) {
    model::Actor& c = state.objects[static_cast<size_t>(cohort)];
    add_morale(c, -3);
    c.raw[military::kCohortRegulars] = 0;
    c.raw[military::kCohortIrregulars] = 0;
    c.raw[military::kCohortAuxiliaries] = 0;
    c.raw[kState] = military::kCohortMobilized;
    const int cell = word(c, kCell);
    if (cell >= 0 && cell < static_cast<int>(state.empire.cells.size())) {
        state.empire.cells[static_cast<size_t>(cell)] &= 0x7F;
    }
    const int home_x = sbyte(c.raw[kCohortHomeX]), home_y = sbyte(c.raw[kCohortHomeY]);
    set_word(c, kX, w16(home_x << 4));
    set_word(c, kY, w16(home_y << 4));
    c.raw[kDestX] = static_cast<uint8_t>(home_x);
    c.raw[kDestY] = static_cast<uint8_t>(home_y);
    c.raw[kPixelsLeft] = 0;
    clamp_morale(c);
}

void hand_over(model::CityState& state, int cohort, int army) {
    const model::Actor& c = state.objects[static_cast<size_t>(cohort)];
    const model::Actor& a = state.objects[static_cast<size_t>(army)];
    set(state, kHandoverPending, 1);
    set(state, kHandoverOutcome, 0);
    set(state, kHandoverCohort, cohort);
    set(state, kHandoverArmy, army);
    set(state, kHandoverRegularsBefore, sbyte(c.raw[military::kCohortRegulars]));
    set(state, kHandoverIrregularsBefore, sbyte(c.raw[military::kCohortIrregulars]));
    set(state, kHandoverAuxiliariesBefore, sbyte(c.raw[military::kCohortAuxiliaries]));
    set(state, kHandoverRegulars, sbyte(c.raw[military::kCohortRegulars]));
    set(state, kHandoverIrregulars, sbyte(c.raw[military::kCohortIrregulars]));
    set(state, kHandoverAuxiliaries, sbyte(c.raw[military::kCohortAuxiliaries]));
    set(state, kHandoverMorale, sbyte(c.raw[military::kCohortMorale]));
    set(state, kHandoverNumber, sbyte(c.raw[0x2A]));
    set(state, kHandoverArmyBefore, sbyte(a.raw[kArmySize]));
    set(state, kHandoverArmySize, sbyte(a.raw[kArmySize]));
    set(state, kHandoverRace, g(state, 0x6BD6));
    set(state, kHandoverYear, g(state, 0x6C32));
    set(state, kHandoverMonth, g(state, 0x6C1C));
    set(state, kHandoverProvince, g(state, 0x6CA6));
    // DS:0x6DE7 is never written in the US build (0 in its data); the date
    // line 0x2793E reads it to choose "AD" (1), "aJC" or "nCH" (2), so it is
    // most likely the international build's language (INFERENCE).
    set(state, kHandoverLanguage, 0);
}

void take_back(model::CityState& state) {
    set(state, kHandoverPending, 0);
    const int cohort = g(state, kHandoverCohort), army = g(state, kHandoverArmy);
    if (cohort < 0 || cohort >= static_cast<int>(state.objects.size()) || army < 0 ||
        army >= static_cast<int>(state.objects.size())) {
        return;  // Gaius's guard: the original indexes whatever the words hold
    }
    model::Actor& c = state.objects[static_cast<size_t>(cohort)];
    c.raw[military::kCohortRegulars] = static_cast<uint8_t>(g(state, kHandoverRegulars));
    c.raw[military::kCohortIrregulars] = static_cast<uint8_t>(g(state, kHandoverIrregulars));
    c.raw[military::kCohortAuxiliaries] = static_cast<uint8_t>(g(state, kHandoverAuxiliaries));
    c.raw[military::kCohortMorale] = static_cast<uint8_t>(g(state, kHandoverMorale));
    set(state, military::kRegulars,
        g(state, military::kRegulars) - (g(state, kHandoverRegularsBefore) - g(state, kHandoverRegulars)));
    set(state, military::kIrregulars,
        g(state, military::kIrregulars) - (g(state, kHandoverIrregularsBefore) - g(state, kHandoverIrregulars)));
    set(state, military::kAuxiliaries,
        g(state, military::kAuxiliaries) - (g(state, kHandoverAuxiliariesBefore) - g(state, kHandoverAuxiliaries)));
    state.objects[static_cast<size_t>(army)].raw[kArmySize] = static_cast<uint8_t>(g(state, kHandoverArmySize));

    if (g(state, kHandoverOutcome) == kCohort2Victory) {
        // 0x23309: as 0x22D5D without the morale.
        if (c.raw[kCohortPatrolX] == 0) {
            stop_here(c, false);
        } else {
            c.raw[kState] = 11;
            c.raw[kDestX] = c.raw[kCohortResumeX];
            c.raw[kDestY] = c.raw[kCohortResumeY];
        }
        actors::release(state, army);  // 0x05DC7
        return;
    }
    // 0x233BB: as 0x22EDB, keeping the Centuries.
    add_morale(c, -3);
    c.raw[kState] = military::kCohortMobilized;
    const int cell = word(c, kCell);
    if (cell >= 0 && cell < static_cast<int>(state.empire.cells.size())) {
        state.empire.cells[static_cast<size_t>(cell)] &= 0x7F;
    }
    const int home_x = sbyte(c.raw[kCohortHomeX]), home_y = sbyte(c.raw[kCohortHomeY]);
    set_word(c, kX, w16(home_x << 4));
    set_word(c, kY, w16(home_y << 4));
    c.raw[kDestX] = static_cast<uint8_t>(home_x);
    c.raw[kDestY] = static_cast<uint8_t>(home_y);
    c.raw[kPixelsLeft] = 0;
}

std::string handover_file_bytes() { return std::string("csr0.dat\0start", 14); }

bool write_handover_file(const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const std::string bytes = handover_file_bytes();
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

std::string read_handover_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    char bytes[14] = {};
    f.read(bytes, sizeof bytes);
    if (f.gcount() <= 0) return {};
    std::string name(bytes, static_cast<size_t>(f.gcount()));
    const size_t nul = name.find('\0');
    return nul == std::string::npos ? name : name.substr(0, nul);
}

}  // namespace gaius::systems::battle
