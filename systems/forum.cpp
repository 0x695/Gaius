// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/forum.hpp"

#include <algorithm>
#include <array>

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

}  // namespace gaius::systems::forum
