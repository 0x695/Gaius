// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — systems/forum.hpp
//
// What the Forum screens' buttons do: the arrow buttons on the rates, wages,
// welfare, salary and donation, the Tribune of the Plebs' duty arrows, and the
// Military Advisor's Cohort buttons. Transcribed from the US-build CSR.EXE
// (2026-09-15); docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 32.
//
// Every arrow moves its word by one per click, within the limits of section
// 25.2. The screens themselves (layout, texts, art) are Gaius's own; which
// screen shows which word is STRONG INFERENCE from the manual and from the
// words each screen's draw routine reads (0x0E6E3 the Treasurer's rates,
// 0x0E822 the Tribune's duties, 0x0B345 the Legion).

#pragma once

#include <array>
#include <string>
#include <vector>

#include "model/city_state.hpp"

namespace gaius::systems::forum {

enum class Control {
    PopulationTax,  // DS:0x6C04, 0-25 (0x0E7AE / 0x0E7C0)
    IndustrialTax,  // DS:0x6C02, 0-25 (0x0E7D2 / 0x0E7E4)
    Conscription,   // DS:0x6C06, 0-50 (0x0E650 / 0x0E65C)
    ArmyWages,      // DS:0x6C08, 0-999 (0x0E637 / 0x0E644)
    Welfare,        // DS:0x6C46, 0-9999 (0x0EB13 / 0x0EB2C)
    Salary,         // DS:0x6C2C, 0-9999 (0x0C164 / 0x0C171)
    Donation,       // DS:0x6C28, 0 up to the savings DS:0x6C2E (0x0C2C3 / 0x0C2D1)
};

uint16_t control_word(Control control);

// One click on the control's up (`delta` > 0) or down arrow. Returns the new
// value.
int adjust(model::CityState& state, Control control, int delta);

// The Tribune's five duties, in the table order of systems::plebs.
enum class Duty { FirePrevention, BuildingMaintenance, RoadMaintenance, Construction, ArmyDuty };

uint16_t duty_word(Duty duty);

// 0x0EB45-0x0EC88, a duty's up arrow: one pleb group from the unassigned, or,
// with none unassigned, from the last duty after this one that has any (army
// duty first, then construction, ...). Army duty takes only unassigned plebs.
// Returns whether a group moved. Army duty's arrows recompute the auxiliaries
// (DS:0x6C52 = army duty / 16), as the screen's draw routine does (0x0E85A).
bool raise_duty(model::CityState& state, Duty duty);
// 0x0EB8F-0x0EC8D, the down arrow: one group back to the unassigned.
bool lower_duty(model::CityState& state, Duty duty);

// The Military Advisor's Cohort on display, DS:0x6C0A (its number, 0-9).
inline constexpr uint16_t kSelectedCohort = 0x6C0A;

// 0x0E54C: the actor slot of the active Cohort numbered DS:0x6C0A, or -1.
int selected_cohort_slot(const model::CityState& state);

// 0x0E59C, the next Cohort: while the number is below the Cohort count
// (DS:0x6C12), step it up until a Cohort has it (at most to 11); 10 or more
// wraps to 0.
void next_cohort(model::CityState& state);
// 0x0E60E, the previous one: step down until a Cohort has it, not below 0.
void previous_cohort(model::CityState& state);
// 0x0E5D5: the Cohort on display switches between mobilized (state 10) and
// demobilized (state 14). Returns whether there was one.
bool toggle_mobilized(model::CityState& state);

// ---------------------------------------------------------------------------
// The Forum picture's other figures (CONTFRM.GD8 regions 1, 4 and 8) and the
// ratings screen's advice. Transcribed 2026-09-16; findings section 36.

// The statue, region 1 (0x0DF9B): a hidden rank cheat. It opens only when the
// keyboard handler's key words 2EF9:0031 and 2EF9:002F hold 0x63 and 0x42
// ("c" and "B" if both keep typed characters -- INFERENCE: what 2EF9:0031
// keeps isn't traced). Its page shows the rank's title with two arrows. The up
// arrow (0x0E066) raises the rank to at most 19 and, above rank 1, moves
// difficulty 0 to 1; the down arrow (0x0E086) lowers it to at least 1 and, at
// rank 1, moves difficulty 1 back to 0.
inline constexpr uint16_t kRank = 0x6C30, kDifficulty = 0x6CB8;
inline constexpr int kCheatKey1 = 0x63, kCheatKey2 = 0x42;
void raise_rank(model::CityState& state);
void lower_rank(model::CityState& state);

// The five words the industry report grades with (DS:0x30C2-0x30E3).
inline constexpr std::array<const char*, 5> kGradeNames = {"Terrible", "Poor", "Average", "Good", "Excellent"};
// DS:0x46F1, 16-character fields: the goods types in record order.
inline constexpr std::array<const char*, 8> kGoodsNames = {"Glass", "Tin",   "Pottery", "Copper",
                                                          "Wine",  "Ivory", "Wheat",   "Spices"};

struct IndustryRow {
    int suitability = 0;  // 3496:1880[province * 8 + goods], -3..2
    int grade = -1;       // kGradeNames index: -3, -2, 0, 1, 2 name a grade; -1 shows nothing
    int factories = 0;    // DS:0x5816[goods], the workshops making it
};

struct IndustryReport {
    int province = 0;       // DS:0x6CA6
    int average_level = 0;  // 0x09FF1: the active workshops' average level
    int overall = 0;        // the average's grade: <=1, <=3, <=5, 6, >=7
    int prospects = 0;      // DS:0x6BF4's grade: <=-2, <=0, <=2, <=4, >=5
    std::array<IndustryRow, 8> rows{};
};

// The man in green, region 8 (0x09D22): "Industry Report on" the province,
// "Overall Industry Rating -", "Prospects for Expansion -", then a row per
// goods.
IndustryReport industry_report(const model::CityState& state);
// Opening the report also stores the average in DS:0x6BE8, as the original
// does (the yearly accounts compute that word the same way).
IndustryReport open_industry_report(model::CityState& state);

// The histories graphed by the man in the blue robe, region 4 (0x0ACA7): four
// panels of 14 bars, newest on the right, 8 pixels apart, from the yearly
// history buffers (month's record_history: 15 records of year, value).
struct HistoryGraph {
    const char* label;                  // drawn under the panel (DS:0x7022-0x7030)
    uint16_t index_word;                // the buffer's newest record
    int start_scale;                    // DS:0x6D7B
    int max_height;                     // DS:0x6D7D, pixels
    int right_x, base_y;                // the newest bar's x, the bars' bottom row
    std::array<const char*, 3> ranges;  // the caption after 0, 1 or 2 doublings
};
// In table order: table_60_a, _b, _c, _d.
inline constexpr std::array<HistoryGraph, 4> kHistoryGraphs = {{
    {"population tax", 0x6B36, 25, 36, 0x80, 0x4C, {"0 - 750 dn", "0 - 1500 dn", "0 - 2250 dn"}},
    {"industry tax", 0x6B34, 25, 36, 0x120, 0x4C, {"0 - 750 dn", "0 - 1500 dn", "0 - 2250 dn"}},
    {"city funds", 0x6B32, 200, 68, 0x80, 0xAC, {"0 - 12500 dn", "0 - 25000 dn", "0 - 37500 dn"}},
    {"population", 0x6B30, 25, 68, 0x120, 0xAC, {"0 - 6000", "0 - 12000", "0 - 18000"}},
}};

struct HistoryBars {
    std::array<int, 14> height{};  // newest first; 0 or less draws nothing
    std::array<int, 14> value{};
    int doublings = 0;             // the caption is ranges[doublings] when below 3, else none
};

// 1F6F:236C: the scale starts at start_scale and doubles until no bar is taller
// than max_height; each bar is value / scale pixels (signed division), capped.
// The bars are POINTERS.PL8 frames 0x36/0x37 (0x37 where x is a multiple of
// 16), cut to their height by 303E:15B7 (which rows of the frame show is
// INFERENCE).
HistoryBars history_bars(const std::vector<uint8_t>& table, int newest, int start_scale, int max_height);

// The panels' caption (0x0B04C): "A.D." or "B.C." by the sign of year - 15,
// then |year - 15| and |year - 1|.
std::string history_years(int year);

// The ratings screen's advice (0x0CC21, picker 0x0CF12). A click with the
// pointer's y in 0x78-0xB3 advances a cycle 0 -> 1 -> 2 -> 0 (DS:0x6D2A) and
// picks a text by the column under x and that column's rating below 100; the
// text shows for 90 frames at the bottom, in place of "Average Rating %".
inline constexpr std::array<const char*, 15> kRatingHints = {
    "Average Rating",                        // 0: the resting line, with DS:0x6C34
    "Stop barbarians from reaching city",    // Peace, DS:0x6C84 == 1, cycle 0
    "Building temples helps prevent riots",  // Peace, DS:0x6C84 == 2, cycle 1
    "Reduce taxes, keep everyone happy !!",  // Peace, otherwise
    "Prehaps you need a grand spectacle",    // Culture, DS:0x6C80 < 36, cycle 0
    "Is everyone served by religion ?",      // Culture, DS:0x6C82 < 36, cycle 1
    "Increase your population",              // Culture, the random walk 2EF9:0286 < 50
    "Are people healthy and educated ?",     // Culture, otherwise
    "Increase the population",               // Prosperity, DS:0x6C10 < 1200, cycle 0
    "Check everyone is paying you tax !",    // Prosperity, DS:0x6C7E set, cycle 1
    "Upgrade slums to high grade housing",   // Prosperity, otherwise
    "Build an Imperial highway to Rome",     // Empire, DS:0x6C8C == 0, cycle 0
    "Develop links with local villages",     // Empire, DS:0x6C92 < 4, cycle 1
    "Straighten those roads !",              // Empire, otherwise
    "you need no help here",                 // the rating is 100 or more, or no column
};
inline constexpr int kRatingHintTop = 0x78, kRatingHintBottom = 0xB4, kRatingHintFrames = 90;

inline int next_hint_cycle(int cycle) { return cycle >= 2 ? 0 : cycle + 1; }
// A column's centre on the original screen: Peace 1-79, Culture 81-159,
// Prosperity 161-239, Empire 241-319 (x = 0, 80, 160, 240 give text 14).
inline int rating_column_x(int column) { return column * 80 + 40; }

// 0x0CF12's choice for a click at x, after the cycle has advanced.
// `linked_towns` is DS:0x6C92, which the save doesn't keep; `random_walk` is
// 2EF9:0286 (month::Random::walk).
int rating_hint(const model::CityState& state, int x, int cycle, int linked_towns, int random_walk);

}  // namespace gaius::systems::forum
