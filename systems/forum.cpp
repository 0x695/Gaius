// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/forum.hpp"

#include <algorithm>
#include <array>

#include "systems/actors.hpp"
#include "systems/administration.hpp"
#include "systems/economy.hpp"
#include "systems/military.hpp"
#include "systems/plebs.hpp"

namespace gaius::systems::forum {

namespace {

int g(const model::CityState& state, uint16_t ds) { return model::global_word(state, ds); }
void set(model::CityState& state, uint16_t ds, int v) { model::set_global_word(state, ds, v); }

// The duties in table order, then the unassigned.
constexpr std::array<uint16_t, 5> kDutyWords = {plebs::kFirePrevention, plebs::kBuildingMaintenance,
                                                plebs::kRoadMaintenance, plebs::kConstruction, plebs::kArmyDuty};

void recompute_auxiliaries(model::CityState& state) {
    set(state, military::kAuxiliaries, g(state, plebs::kArmyDuty) / 16);
}

}  // namespace

uint16_t control_word(Control control) {
    switch (control) {
        case Control::PopulationTax: return 0x6C04;
        case Control::IndustrialTax: return 0x6C02;
        case Control::Conscription: return military::kConscription;
        case Control::ArmyWages: return military::kArmyWages;
        case Control::Welfare: return plebs::kWelfare;
        case Control::Salary: return 0x6C2C;
        case Control::Donation: return 0x6C28;
    }
    return 0;
}

int adjust(model::CityState& state, Control control, int delta) {
    const uint16_t word = control_word(control);
    int maximum = 0;
    switch (control) {
        case Control::PopulationTax:
        case Control::IndustrialTax: maximum = 25; break;
        case Control::Conscription: maximum = 50; break;
        case Control::ArmyWages: maximum = 999; break;
        case Control::Welfare:
        case Control::Salary: maximum = 9999; break;
        case Control::Donation: maximum = g(state, 0x6C2E); break;
    }
    int v = g(state, word);
    // Each handler steps only while inside its limit (the welfare's clamps
    // after stepping, which comes to the same).
    if (delta > 0 && v < maximum) ++v;
    if (delta < 0 && v > 0) --v;
    set(state, word, v);
    return v;
}

uint16_t duty_word(Duty duty) { return kDutyWords[static_cast<size_t>(duty)]; }

bool raise_duty(model::CityState& state, Duty duty) {
    const int d = static_cast<int>(duty);
    uint16_t source = 0;
    if (g(state, plebs::kUnassigned) > 0) {
        source = plebs::kUnassigned;
    } else {
        for (int i = 4; i > d; --i) {
            if (g(state, kDutyWords[static_cast<size_t>(i)]) > 0) {
                source = kDutyWords[static_cast<size_t>(i)];
                break;
            }
        }
    }
    if (source == 0) return false;
    set(state, source, g(state, source) - 1);
    set(state, kDutyWords[static_cast<size_t>(d)], g(state, kDutyWords[static_cast<size_t>(d)]) + 1);
    recompute_auxiliaries(state);
    return true;
}

bool lower_duty(model::CityState& state, Duty duty) {
    const uint16_t word = duty_word(duty);
    if (g(state, word) <= 0) return false;
    set(state, word, g(state, word) - 1);
    set(state, plebs::kUnassigned, g(state, plebs::kUnassigned) + 1);
    recompute_auxiliaries(state);
    return true;
}

int selected_cohort_slot(const model::CityState& state) {
    const int number = g(state, kSelectedCohort);
    for (int i = 0; i < model::kActorCount; ++i) {
        const model::Actor& a = state.objects[static_cast<size_t>(i)];
        if (a.active() != 0 && a.type() == military::kCohortType && static_cast<int8_t>(a.raw[0x2A]) == number)
            return i;
    }
    return -1;
}

void next_cohort(model::CityState& state) {
    int number = g(state, kSelectedCohort);
    if (number >= g(state, 0x6C12)) return;
    bool found = false;
    while (!found && number < 11) {
        ++number;
        set(state, kSelectedCohort, number);
        found = selected_cohort_slot(state) >= 0;
    }
    if (number >= 10) set(state, kSelectedCohort, 0);
}

void previous_cohort(model::CityState& state) {
    int number = g(state, kSelectedCohort);
    bool found = false;
    while (!found && number > 0) {
        --number;
        set(state, kSelectedCohort, number);
        found = selected_cohort_slot(state) >= 0;
    }
}

bool toggle_mobilized(model::CityState& state) {
    const int slot = selected_cohort_slot(state);
    if (slot < 0) return false;
    uint8_t& s = state.objects[static_cast<size_t>(slot)].raw[0x31];
    s = s == military::kCohortDemobilized ? military::kCohortMobilized : military::kCohortDemobilized;
    return true;
}

void raise_rank(model::CityState& state) {
    if (g(state, kRank) < 19) set(state, kRank, g(state, kRank) + 1);
    if (g(state, kRank) > 1 && g(state, kDifficulty) == 0) set(state, kDifficulty, 1);
}

void lower_rank(model::CityState& state) {
    if (g(state, kRank) > 1) set(state, kRank, g(state, kRank) - 1);
    if (g(state, kRank) == 1 && g(state, kDifficulty) == 1) set(state, kDifficulty, 0);
}

namespace {

int grade_of_level(int level) {
    if (level <= 1) return 0;
    if (level <= 3) return 1;
    if (level <= 5) return 2;
    if (level <= 6) return 3;
    return 4;
}

int grade_of_prospects(int p) {
    if (p <= -2) return 0;
    if (p <= 0) return 1;
    if (p <= 2) return 2;
    if (p <= 4) return 3;
    return 4;
}

int grade_of_suitability(int s) {
    switch (s) {
        case -3: return 0;
        case -2: return 1;
        case 0: return 2;
        case 1: return 3;
        case 2: return 4;
        default: return -1;
    }
}

int word_at(const std::vector<uint8_t>& t, size_t o) {
    if (t.size() < o + 2) return 0;
    return static_cast<int16_t>(t[o] | (t[o + 1] << 8));
}

}  // namespace

IndustryReport open_industry_report(model::CityState& state) {
    IndustryReport r = industry_report(state);
    set(state, 0x6BE8, r.average_level);
    return r;
}

IndustryReport industry_report(const model::CityState& state) {
    IndustryReport r;
    r.province = g(state, 0x6CA6);
    r.average_level = economy::average_workshop_level(state);
    r.overall = grade_of_level(r.average_level);
    r.prospects = grade_of_prospects(g(state, 0x6BF4));
    for (size_t i = 0; i < 8; ++i) {
        IndustryRow& row = r.rows[i];
        row.suitability = actors::workshop_base(r.province, static_cast<int>(i));
        row.grade = grade_of_suitability(row.suitability);
        row.factories = i < state.table_8.size() ? static_cast<int8_t>(state.table_8[i]) : 0;
    }
    return r;
}

HistoryBars history_bars(const std::vector<uint8_t>& table, int newest, int start_scale, int max_height) {
    const auto value = [&](int k) {
        const int i = ((newest - k) % 15 + 15) % 15;
        return word_at(table, static_cast<size_t>(i) * 4 + 2);
    };
    HistoryBars bars;
    int scale = start_scale;
    for (int k = 0; k < 14; ++k) {
        if (value(k) / scale > max_height) {
            scale *= 2;
            ++bars.doublings;
            k = -1;
        }
    }
    for (int k = 0; k < 14; ++k) {
        const int v = value(k);
        bars.value[static_cast<size_t>(k)] = v;
        bars.height[static_cast<size_t>(k)] = std::min(v / scale, max_height);
    }
    return bars;
}

std::string history_years(int year) {
    const int from = year - 15, to = year - 1;
    return std::string(from >= 0 ? "A.D. " : "B.C. ") + std::to_string(from < 0 ? -from : from) + " - " +
           std::to_string(to < 0 ? -to : to);
}

int rating_hint(const model::CityState& state, int x, int cycle, int linked_towns, int random_walk) {
    if (x > 0 && x < 0x50 && g(state, administration::kPeace) < 100) {
        if (g(state, 0x6C84) == 1 && cycle == 0) return 1;
        if (g(state, 0x6C84) == 2 && cycle == 1) return 2;
        return 3;
    }
    if (x > 0x50 && x < 0xA0 && g(state, administration::kCulture) < 100) {
        if (g(state, 0x6C80) < 36 && cycle == 0) return 4;
        if (g(state, 0x6C82) < 36 && cycle == 1) return 5;
        return random_walk < 50 ? 6 : 7;
    }
    if (x > 0xA0 && x < 0xF0 && g(state, administration::kProsperity) < 100) {
        if (g(state, 0x6C10) < 1200 && cycle == 0) return 8;
        if (g(state, 0x6C7E) != 0 && cycle == 1) return 9;
        return 10;
    }
    if (x > 0xF0 && x < 0x140 && g(state, administration::kEmpire) < 100) {
        if (g(state, 0x6C8C) == 0 && cycle == 0) return 11;
        if (linked_towns < 4 && cycle == 1) return 12;
        return 13;
    }
    return 14;
}

}  // namespace gaius::systems::forum
