// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/military.hpp"

namespace gaius::systems::military {

namespace {

// The engine's arithmetic is on 16-bit words; the Cohort fields are bytes.
int16_t w16(int32_t v) { return static_cast<int16_t>(static_cast<uint16_t>(v & 0xFFFF)); }

int g(const model::CityState& state, uint16_t ds) { return model::global_word(state, ds); }
void set(model::CityState& state, uint16_t ds, int v) { model::set_global_word(state, ds, w16(v)); }

bool is_cohort(const model::Actor& a) { return a.active() != 0 && a.type() == kCohortType; }

// One pool's share for one Cohort (0x28B87-0x28BBB, repeated per pool).
void assign(uint8_t& field, int share, int& remainder) {
    if (static_cast<int8_t>(field) >= share) {
        field = static_cast<uint8_t>(share);
        if (remainder <= 0) return;
        --remainder;
    }
    ++field;
}

}  // namespace

int regulars_after_year(int regulars, int wages) {
    regulars = w16(regulars);
    wages = w16(wages);
    if (w16((regulars + 1) * 8) <= wages) {
        ++regulars;
    } else if (w16(regulars * 8) > wages) {
        --regulars;
    }
    return regulars < 0 ? 0 : w16(regulars);
}

int irregulars_target(int population_units, int conscription) {
    // 0x289D2-0x28A14: a long multiply and a signed long divide.
    const int32_t men = static_cast<int32_t>(w16(population_units)) * w16(conscription * 4);
    return static_cast<int>(men / 10000);
}

int irregulars_after_year(int irregulars, int target) {
    irregulars = w16(irregulars);
    if (irregulars < target) {
        ++irregulars;
    } else if (irregulars > target) {
        irregulars -= 4;
    }
    return irregulars < 0 ? 0 : w16(irregulars);
}

void assign_centuries(model::CityState& state) {
    int cohorts = 0;
    for (const model::Actor& a : state.objects) {
        if (is_cohort(a) && a.state() != kCohortDemobilized) ++cohorts;
    }
    if (cohorts <= 0) cohorts = 1;

    const int regulars = g(state, kRegulars), irregulars = g(state, kIrregulars), auxiliaries = g(state, kAuxiliaries);
    const int regular_share = regulars / cohorts, irregular_share = irregulars / cohorts,
              auxiliary_share = auxiliaries / cohorts;
    int regular_rest = regulars % cohorts, irregular_rest = irregulars % cohorts, auxiliary_rest = auxiliaries % cohorts;

    for (model::Actor& a : state.objects) {
        if (!is_cohort(a)) continue;
        if (a.state() == kCohortDemobilized) {
            a.raw[kCohortRegulars] = 0;
            a.raw[kCohortIrregulars] = 0;
            a.raw[kCohortAuxiliaries] = 0;
            continue;
        }
        assign(a.raw[kCohortRegulars], regular_share, regular_rest);
        assign(a.raw[kCohortIrregulars], irregular_share, irregular_rest);
        assign(a.raw[kCohortAuxiliaries], auxiliary_share, auxiliary_rest);
    }
}

void run_year(model::CityState& state) {
    set(state, kRegularsLastYear, g(state, kRegulars));
    set(state, kIrregularsLastYear, g(state, kIrregulars));
    set(state, kAuxiliariesLastYear, g(state, kAuxiliaries));

    const int target = irregulars_target(g(state, 0x6C10), g(state, kConscription));
    // 0x28A17 compares and changes the irregulars first, then the regulars;
    // the two don't read each other.
    set(state, kIrregulars, irregulars_after_year(g(state, kIrregulars), target));
    set(state, kRegulars, regulars_after_year(g(state, kRegulars), g(state, kArmyWages)));

    assign_centuries(state);
}

}  // namespace gaius::systems::military
