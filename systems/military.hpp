// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — systems/military.hpp
//
// The Legion: how many Centuries of regulars, irregulars and auxiliaries the
// province has, and how they're assigned to its Cohorts. Transcribed from the
// US-build CSR.EXE (2026-09-14): the yearly routine 0x28238 calls 0x289C0,
// which recruits and falls through into 0x28A8F, the assignment.
// docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 26.
//
// The words, with the manual's names for them (the Military Advisor's "very
// large numbers in the center of the screen", drawn by 0x0B345-0x0B436 with
// last year's "in brackets" beside each). The names are STRONG INFERENCE: each
// word's arithmetic is transcribed and matches the manual's description.
//   DS:0x6C4E  regular Centuries      DS:0x6C4C  last year's
//   DS:0x6C4A  irregular Centuries    DS:0x6C48  last year's
//   DS:0x6C52  auxiliary Centuries    DS:0x6C50  last year's
//   DS:0x6C06  conscription rate, 0-50 %   DS:0x6C08  army wages, 0-999
// Regulars follow the wages (8 Dn a Century a year), irregulars the city's
// population and the conscription rate. Auxiliaries are plebs on army duty,
// DS:0x6C5A / 16, set by the Tribune of the Plebs' screen (0x0E85A) and the
// step-105 pleb routine (0x2DDFC), which Gaius doesn't model yet.
//
// A Cohort is a province actor of type 13 (placed in state 10 by 0x062D4 for
// the Prima Cohors and 0x1566C for a new fort's). Its record holds its
// Centuries at +0x20 regulars, +0x21 irregulars and +0x1D auxiliaries, and its
// morale at +0x2B (5 when formed); battles take their casualties from both the
// record and the Legion's words (0x22C16-0x22C87).
//
// Checked against real saves: in all 17, recruiting from last year's figures
// with the year-end population (table_60_d) gives the saved regulars and
// irregulars, and assigning the saved Legion leaves every Cohort as saved
// (test_military_year_matches_saves).

#pragma once

#include <cstddef>
#include <cstdint>

#include "model/city_state.hpp"

namespace gaius::systems::military {

inline constexpr uint16_t kRegulars = 0x6C4E;
inline constexpr uint16_t kRegularsLastYear = 0x6C4C;
inline constexpr uint16_t kIrregulars = 0x6C4A;
inline constexpr uint16_t kIrregularsLastYear = 0x6C48;
inline constexpr uint16_t kAuxiliaries = 0x6C52;
inline constexpr uint16_t kAuxiliariesLastYear = 0x6C50;
inline constexpr uint16_t kConscription = 0x6C06;
inline constexpr uint16_t kArmyWages = 0x6C08;

inline constexpr uint8_t kCohortType = 13;
// The Military Advisor's mobilize/demobilize button (0x0E5D4) switches the
// selected Cohort (DS:0x6C0A) between these two states. A demobilized Cohort
// loses its Centuries at the year's end and the rest share them.
inline constexpr uint8_t kCohortMobilized = 10;
inline constexpr uint8_t kCohortDemobilized = 14;

// Offsets in a Cohort's actor record.
inline constexpr size_t kCohortAuxiliaries = 0x1D;
inline constexpr size_t kCohortRegulars = 0x20;
inline constexpr size_t kCohortIrregulars = 0x21;
inline constexpr size_t kCohortMorale = 0x2B;

// 0x28A46: one more regular Century if the wages pay 8 Dn for each of them,
// one fewer if they no longer pay for those there are; not below 0.
int regulars_after_year(int regulars, int wages);

// 0x289D2: the irregulars the city can supply, population units x 4 x
// conscription % / 10000 -- the population (4 per unit) times the rate, in
// hundreds of men.
int irregulars_target(int population_units, int conscription);

// 0x28A17: one more irregular Century below the target, four fewer above it;
// not below 0.
int irregulars_after_year(int irregulars, int target);

// 0x28A8F: shares the Legion among the mobilized Cohorts. Each gets the
// quotient of each pool by their number, the first ones one more while the
// remainder lasts. A Cohort below its share gains only one Century a year (the
// manual: "it will take a while for them to be assigned"); one above it drops
// to it at once. Demobilized Cohorts are emptied.
void assign_centuries(model::CityState& state);

// 0x289C0, run when the year turns after the histories: last year's figures
// are kept, regulars and irregulars recruited with the population units
// DS:0x6C10, then assign_centuries.
void run_year(model::CityState& state);

}  // namespace gaius::systems::military
