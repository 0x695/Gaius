// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — apps/viewer/screens.hpp
//
// The viewer's full-screen pages: the Forum's advisors, the promotion offer and
// the battle. Each is a ui::Page built from the game state, and its buttons'
// actions are applied back through systems::forum, administration and battle.
//
// The layouts are Gaius's own; the names shown are the executable's own
// strings where they exist (province names DS:0x2CAE, Cohort emblems
// DS:0x45BE, Cohort states DS:0x4772, the rank titles, the barbarian races),
// transcribed below. That each list is indexed the way it's used here (the
// province by DS:0x6CA6/0x6CA4, the emblem by the Cohort's number, the state
// word by +0x31) is STRONG INFERENCE: the Military Advisor capture shows the
// Prima Cohors, number 0, as "EAGLE" and "WAITING" in state 10.

#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "model/city_state.hpp"
#include "systems/administration.hpp"
#include "systems/battle.hpp"
#include "systems/campaign.hpp"
#include "systems/economy.hpp"
#include "systems/forum.hpp"
#include "systems/military.hpp"
#include "systems/plebs.hpp"
#include "ui/panel.hpp"

namespace gaius::viewer {

// DS:0x2CAE, 16-character fields.
inline constexpr std::array<const char*, 50> kProvinceNames = {
    "Sicilia",       "Campania",      "Latium",        "Cisalpine Gaul", "Corsica",      "Sardinia",
    "Alpes Maritimae", "Narbonensis", "Hispania Inf.", "Baetica",        "Lusitania Inf.", "Lusitania Sup.",
    "Tarraconensis", "Aquitania Inf.", "Hispania Sup.", "Aquitania Sup.", "Lugdunensis",  "Belgica",
    "Gallia Sup.",   "Gallia Inf.",   "W. Britannia",  "E. Britannia",   "Britannia Sup.", "Caledonia",
    "Germania Inf.", "Germania Sup.", "Pannonia",      "Dacia",          "Illyricum",    "Dalmatia",
    "Macedonia",     "Achaea",        "Creta",         "Thracia",        "Asia",         "Pamphylia",
    "Cappadocia",    "Assyria",       "Syria",         "Mesopotamia",    "Judea",        "Arabia",
    "Aegyptus",      "Cyrenaica",     "Africa",        "Numidia",        "Mauretania",   "Caeariensis",
    "Tingitania",    "Moesia"};

// DS:0x45BE, 12-character fields.
inline constexpr std::array<const char*, 10> kCohortEmblems = {"Eagle", "Rabbit", "Snake", "Fish",     "Horse",
                                                              "Pig",   "Wolf",   "Hero",  "Explorer", "Protector"};

inline const char* province_name(int province) {
    return province >= 0 && province < 50 ? kProvinceNames[static_cast<size_t>(province)] : "?";
}

// The words DS:0x4772 (16-character fields, indexed by state) gives states 10-14.
inline const char* cohort_state_name(int state) {
    switch (state) {
        case 10: return "waiting";
        case 11: return "patrolling";
        case 12: return "attacking";
        case 13: return "retiring";
        case 14: return "demobilized";
        default: return "nothing";
    }
}

inline const char* rank_name(int rank) {
    return rank >= 0 && rank < static_cast<int>(systems::administration::kRankNames.size())
               ? systems::administration::kRankNames[static_cast<size_t>(rank)]
               : "?";
}

inline std::string year_text(int year) {
    return year < 0 ? "BC " + std::to_string(-year) : "AD " + std::to_string(year);
}

// ---------------------------------------------------------------------------
// Action ids

enum ForumTab { kTreasurer, kTribune, kLegion, kRatings, kGovernor, kIndustry, kHistory, kForumTabCount, kStatue };

inline constexpr int kActionTab = 100;       // + ForumTab
inline constexpr int kActionControl = 200;   // + 2 x forum::Control, +1 for up
inline constexpr int kActionDuty = 300;      // + 2 x forum::Duty, +1 for up
inline constexpr int kActionPrevCohort = 400, kActionNextCohort = 401, kActionMobilize = 402, kActionDonate = 403,
                     kActionClose = 404;
inline constexpr int kActionAccept = 500, kActionWait9 = 501, kActionWait24 = 502;
inline constexpr int kActionTactic = 600;    // + battle::Tactic
inline constexpr int kActionRetreat = 610, kActionContinue = 611, kActionQuit = 612;
inline constexpr int kActionOpenSave = 405, kActionOpenLoad = 406;
inline constexpr int kActionSpeedDown = 407, kActionSpeedUp = 408;
inline constexpr int kActionEmpireMap = 409;
inline constexpr int kActionFundingDown = 700, kActionFundingUp = 701, kActionDifficultyDown = 702,
                     kActionDifficultyUp = 703, kActionBegin = 704, kActionChooseName = 705;
inline constexpr int kActionSlot = 710;  // + slot
inline constexpr int kActionBack = 720;
inline constexpr int kSaveSlots = 8;
inline constexpr int kActionHint = 800;  // + ratings column (Peace, Culture, Prosperity, Empire)
inline constexpr int kActionRankDown = 810, kActionRankUp = 811;

namespace detail {

inline int g(const model::CityState& s, uint16_t ds) { return model::global_word(s, ds); }

inline ui::PanelRow control_row(const model::CityState& s, const char* label, systems::forum::Control c,
                                const std::string& suffix) {
    const int base = kActionControl + 2 * static_cast<int>(c);
    return {label, std::to_string(g(s, systems::forum::control_word(c))) + suffix, base, base + 1};
}

inline ui::PanelRow duty_row(const model::CityState& s, const char* label, systems::forum::Duty d, int need_word) {
    const int base = kActionDuty + 2 * static_cast<int>(d);
    std::string value = std::to_string(g(s, systems::forum::duty_word(d)));
    if (need_word) value += " (" + std::to_string(g(s, static_cast<uint16_t>(need_word))) + ")";
    return {label, value, base, base + 1};
}

}  // namespace detail

// `speed`, when not negative, adds the game speed (DS:0x5292) to the governor's
// page -- the original keeps it on its options screen (0x0F0D8). `hint` is the
// ratings advice on show (forum::kRatingHints), 0 for none. `empire_map` adds
// the governor's map button (0x0C060) when the map's files are there.
inline ui::Page forum_page(const model::CityState& s, ForumTab tab, int speed = -1, int hint = 0,
                           bool empire_map = false) {
    namespace forum = systems::forum;
    using detail::g;
    ui::Page page;
    for (int i = 0; i < kForumTabCount; ++i) {
        static constexpr const char* kTabs[] = {"Money", "Plebs", "Legion", "Ratings", "Governor", "Industry", "History"};
        page.tabs.push_back({kTabs[i], kActionTab + i});
    }
    page.selected_tab = tab < kForumTabCount ? tab : -1;
    page.buttons.push_back({"Save", kActionOpenSave});
    page.buttons.push_back({"Load", kActionOpenLoad});
    page.buttons.push_back({"Close", kActionClose});
    const auto dn = [](int v) { return std::to_string(v) + " Dn"; };
    switch (tab) {
        case kTreasurer: {
            page.title = "The Treasurer, " + year_text(g(s, 0x6C32));
            page.rows.push_back({"Funds", dn(g(s, systems::economy::kFunds))});
            page.rows.push_back(detail::control_row(s, "Population tax", forum::Control::PopulationTax, " %"));
            page.rows.push_back(detail::control_row(s, "Industrial tax", forum::Control::IndustrialTax, " %"));
            page.rows.push_back({"City population", std::to_string(g(s, 0x6C0E))});
            const int cents = g(s, 0x6BC8);
            page.rows.push_back({"Tax per head",
                                 std::to_string(g(s, 0x6BCA)) + "." + (cents < 10 ? "0" : "") + std::to_string(cents) +
                                     " Dn"});
            // FONT1 has no colon or slash (DS:0F64 draws ':' as '0', '/' as nothing).
            page.rows.push_back({"Last year in - population tax", dn(g(s, 0x6BC6))});
            page.rows.push_back({"  industrial tax", dn(g(s, 0x6BC4))});
            page.rows.push_back({"Out - construction", dn(g(s, 0x6BC2))});
            page.rows.push_back({"  operating costs", dn(g(s, 0x6BBE))});
            page.rows.push_back({"  tribute to Rome", dn(g(s, 0x6BAA))});
            const int profit = static_cast<int16_t>(g(s, 0x6BB6));
            page.rows.push_back({profit < 0 ? "Overall loss" : "Overall profit", dn(profit < 0 ? -profit : profit)});
            break;
        }
        case kTribune: {
            using systems::plebs::kFireNeed;
            page.title = "The Tribune of the Plebs";
            page.rows.push_back({"Pleb groups", std::to_string(g(s, systems::plebs::kPlebs))});
            page.rows.push_back(detail::control_row(s, "Welfare", forum::Control::Welfare, " Dn"));
            page.rows.push_back(detail::duty_row(s, "Fire prevention", forum::Duty::FirePrevention, kFireNeed));
            page.rows.push_back(detail::duty_row(s, "Building upkeep", forum::Duty::BuildingMaintenance,
                                                 systems::plebs::kBuildingNeed));
            page.rows.push_back(
                detail::duty_row(s, "Road upkeep", forum::Duty::RoadMaintenance, systems::plebs::kRoadNeed));
            // The construction need DS:0x6C3E isn't saved (systems::plebs::set_needs).
            page.rows.push_back(detail::duty_row(s, "Construction", forum::Duty::Construction, 0));
            page.rows.push_back(detail::duty_row(s, "Army duty", forum::Duty::ArmyDuty, 0));
            page.rows.push_back({"Auxiliary Centuries", std::to_string(g(s, systems::military::kAuxiliaries))});
            page.rows.push_back({"Unassigned", std::to_string(g(s, systems::plebs::kUnassigned))});
            break;
        }
        case kLegion: {
            namespace military = systems::military;
            const int number = g(s, forum::kSelectedCohort);
            const int slot = forum::selected_cohort_slot(s);
            page.title = "The Legion";
            if (slot >= 0) {
                const model::Actor& a = s.objects[static_cast<size_t>(slot)];
                const char* emblem = number >= 0 && number < 10 ? kCohortEmblems[static_cast<size_t>(number)] : "?";
                page.rows.push_back({"Cohort " + std::to_string(number + 1) + ", " + emblem,
                                     cohort_state_name(a.state()), kActionPrevCohort, kActionNextCohort});
                page.rows.push_back({"  Morale", std::to_string(a.raw[military::kCohortMorale])});
                page.rows.push_back({"  Regulars", std::to_string(a.raw[military::kCohortRegulars])});
                page.rows.push_back({"  Irregulars", std::to_string(a.raw[military::kCohortIrregulars])});
                page.rows.push_back({"  Auxiliaries", std::to_string(a.raw[military::kCohortAuxiliaries])});
                page.buttons.insert(page.buttons.begin(),
                                    {a.state() == military::kCohortDemobilized ? "Mobilize" : "Demobilize",
                                     kActionMobilize});
            } else {
                page.rows.push_back({"No Cohort", "", kActionPrevCohort, kActionNextCohort});
            }
            const auto with_last = [&](uint16_t now, uint16_t last) {
                return std::to_string(g(s, now)) + " (" + std::to_string(g(s, last)) + ")";
            };
            page.rows.push_back({"Regular Centuries", with_last(military::kRegulars, military::kRegularsLastYear)});
            page.rows.push_back(
                {"Irregular Centuries", with_last(military::kIrregulars, military::kIrregularsLastYear)});
            page.rows.push_back(
                {"Auxiliary Centuries", with_last(military::kAuxiliaries, military::kAuxiliariesLastYear)});
            page.rows.push_back(detail::control_row(s, "Wages bill", forum::Control::ArmyWages, " Dn"));
            page.rows.push_back(detail::control_row(s, "Conscription", forum::Control::Conscription, " %"));
            break;
        }
        case kRatings: {
            namespace admin = systems::administration;
            const int rank = g(s, admin::kRank);
            page.title = std::string(rank_name(rank)) + " of " + province_name(g(s, 0x6CA6));
            page.rows.push_back({"Peace", std::to_string(g(s, admin::kPeace))});
            page.rows.push_back({"Culture", std::to_string(g(s, admin::kCulture))});
            page.rows.push_back({"Prosperity", std::to_string(g(s, admin::kProsperity))});
            page.rows.push_back({"Empire", std::to_string(g(s, admin::kEmpire))});
            page.rows.push_back({"Average", std::to_string(g(s, admin::kAverage))});
            if (rank >= 0 && rank < static_cast<int>(admin::kPromotion.size()) && rank < 20) {
                const admin::Requirement& need = admin::kPromotion[static_cast<size_t>(rank)];
                page.rows.push_back({"To become " + std::string(rank_name(rank + 1)), ""});
                page.rows.push_back({"  average of", std::to_string(need.average)});
                page.rows.push_back({"  and each at least", std::to_string(need.each)});
            }
            page.rows.push_back({"Year", year_text(g(s, 0x6C32))});
            // 0x0CF12: the original's advice comes from clicking a column.
            if (hint > 0 && hint < static_cast<int>(forum::kRatingHints.size()))
                page.rows.push_back({forum::kRatingHints[static_cast<size_t>(hint)], ""});
            static constexpr const char* kAsk[] = {"Peace ?", "Culture ?", "Prosperity ?", "Empire ?"};
            for (int c = 3; c >= 0; --c) page.buttons.insert(page.buttons.begin(), {kAsk[c], kActionHint + c});
            break;
        }
        case kIndustry: {
            // 0x09D22, the man in green.
            const forum::IndustryReport r = forum::industry_report(s);
            page.title = std::string("Industry Report on ") + province_name(r.province);
            page.rows.push_back({"Overall Industry Rating", forum::kGradeNames[static_cast<size_t>(r.overall)]});
            page.rows.push_back({"Prospects for Expansion", forum::kGradeNames[static_cast<size_t>(r.prospects)]});
            page.rows.push_back({"Industry type", "suitability  factories"});
            for (size_t i = 0; i < r.rows.size(); ++i) {
                const forum::IndustryRow& row = r.rows[i];
                const std::string grade = row.grade >= 0 ? forum::kGradeNames[static_cast<size_t>(row.grade)] : "";
                page.rows.push_back({std::string("  ") + forum::kGoodsNames[i], grade + "   " + std::to_string(row.factories)});
            }
            break;
        }
        case kHistory: {
            // 0x0ACA7, the man in the blue robe.
            page.title = forum::history_years(g(s, 0x6C32));
            const std::array<const std::vector<uint8_t>*, 4> tables = {&s.table_60_a, &s.table_60_b, &s.table_60_c,
                                                                       &s.table_60_d};
            for (size_t i = 0; i < forum::kHistoryGraphs.size(); ++i) {
                const forum::HistoryGraph& graph = forum::kHistoryGraphs[i];
                const forum::HistoryBars bars =
                    forum::history_bars(*tables[i], g(s, graph.index_word), graph.start_scale, graph.max_height);
                ui::PanelChart chart;
                chart.label = graph.label;
                chart.caption = bars.doublings < 3 ? graph.ranges[static_cast<size_t>(bars.doublings)] : "";
                chart.max = graph.max_height;
                for (size_t k = bars.height.size(); k-- > 0;) chart.bars.push_back(bars.height[k]);
                page.charts.push_back(chart);
            }
            break;
        }
        case kStatue: {
            // 0x0DF9B, the statue's hidden page.
            page.title = "The statue";
            page.rows.push_back({"Rank", rank_name(g(s, forum::kRank)), kActionRankDown, kActionRankUp});
            break;
        }
        case kGovernor:
        case kForumTabCount: {
            page.title = "The Governor";
            page.rows.push_back({"Rank", rank_name(g(s, systems::administration::kRank))});
            page.rows.push_back({"Province", province_name(g(s, 0x6CA6))});
            page.rows.push_back(detail::control_row(s, "Salary", forum::Control::Salary, " Dn"));
            page.rows.push_back({"Personal savings", dn(g(s, 0x6C2E))});
            page.rows.push_back(detail::control_row(s, "Donation", forum::Control::Donation, " Dn"));
            if (speed >= 0) page.rows.push_back({"Game speed", std::to_string(speed), kActionSpeedDown, kActionSpeedUp});
            page.buttons.insert(page.buttons.begin(), {"Donate", kActionDonate});
            if (empire_map) page.buttons.insert(page.buttons.begin() + 1, {"Empire", kActionEmpireMap});
            break;
        }
    }
    return page;
}

// Applies a Forum page's action. Returns false when the action closes the Forum.
inline bool apply_forum_action(model::CityState& s, int action, ForumTab& tab) {
    namespace forum = systems::forum;
    if (action == kActionClose) return false;
    if (action >= kActionTab && action < kActionTab + kForumTabCount) {
        tab = static_cast<ForumTab>(action - kActionTab);
        if (tab == kIndustry) forum::open_industry_report(s);
    } else if (action == kActionRankUp) {
        forum::raise_rank(s);
    } else if (action == kActionRankDown) {
        forum::lower_rank(s);
    } else if (action >= kActionControl && action < kActionControl + 14) {
        const int i = action - kActionControl;
        forum::adjust(s, static_cast<forum::Control>(i / 2), i % 2 ? 1 : -1);
    } else if (action >= kActionDuty && action < kActionDuty + 10) {
        const int i = action - kActionDuty;
        if (i % 2) {
            forum::raise_duty(s, static_cast<forum::Duty>(i / 2));
        } else {
            forum::lower_duty(s, static_cast<forum::Duty>(i / 2));
        }
    } else if (action == kActionPrevCohort) {
        forum::previous_cohort(s);
    } else if (action == kActionNextCohort) {
        forum::next_cohort(s);
    } else if (action == kActionMobilize) {
        forum::toggle_mobilized(s);
    } else if (action == kActionDonate) {
        // 0x0C26E: the amount DS:0x6C28 from the savings.
        systems::economy::donate_savings(s, detail::g(s, 0x6C28));
    }
    return true;
}

// The promotion screen (0x291C3) or, at rank 19, the offer of the title of
// Caesar (0x290DF).
inline ui::Page promotion_page(const model::CityState& s, bool to_caesar) {
    using detail::g;
    ui::Page page;
    const int rank = g(s, systems::administration::kRank);
    if (to_caesar) {
        page.title = "Rome offers you the throne";
        page.rows.push_back({"Your ratings have earned", ""});
        page.rows.push_back({"the title of", rank_name(20)});
        page.buttons.push_back({"Accept", kActionAccept});
    } else {
        page.title = "Promotion";
        page.rows.push_back({"Your ratings have earned you", ""});
        page.rows.push_back({"the rank of", rank_name(rank + 1)});
        page.rows.push_back({"and the province of", province_name(g(s, 0x6CA4))});
        page.rows.push_back({"Ratings average", std::to_string(g(s, systems::administration::kAverage))});
        page.buttons.push_back({"Accept", kActionAccept});
        page.buttons.push_back({"Wait 9 years", kActionWait9});
        page.buttons.push_back({"Wait 24 years", kActionWait24});
    }
    return page;
}

// The start screen (0x27DDF): the funding level and the difficulty, each with
// its arrows (0x27F54/0x27F60, 0x27F6C/0x27F78), and the governor's name, which
// the engine keeps at DS:0x5858 ("Octavian" until the player types one).
inline std::string trimmed(const std::string& t) {
    const size_t a = t.find_first_not_of(' ');
    if (a == std::string::npos) return std::string();
    return t.substr(a, t.find_last_not_of(' ') - a + 1);
}

// The start screen (0x27DDF). `name` is the governor's 12-character name.
inline ui::Page start_page(int funding_level, int difficulty, const std::string& name = "  Octavian  ") {
    namespace campaign = systems::campaign;
    ui::Page page;
    page.title = "A new career";
    page.rows.push_back({"Governor", trimmed(name)});
    const size_t level = static_cast<size_t>(std::clamp(funding_level, 0, 9));
    page.rows.push_back({"Funding", std::string(campaign::kFundingNames[level]) + ", " +
                                        std::to_string(campaign::kStartingFunding[level]) + " Dn",
                         kActionFundingDown, kActionFundingUp});
    page.rows.push_back({"Difficulty", campaign::kDifficultyNames[static_cast<size_t>(std::clamp(difficulty, 0, 2))],
                         kActionDifficultyDown, kActionDifficultyUp});
    page.buttons.push_back({"Begin", kActionBegin});
    page.buttons.push_back({"Choose name", kActionChooseName});
    page.buttons.push_back({"Load a game", kActionOpenLoad});
    return page;
}

// The save and load slots: one button per slot (the caller labels them and
// disables empty ones when loading), and Back.
inline ui::Page files_page(bool saving, const std::vector<ui::PanelButton>& slots) {
    ui::Page page;
    page.title = saving ? "Save the game" : "Load a game";
    page.rows.push_back({saving ? "Choose a slot to write" : "Choose a saved game", ""});
    page.buttons = slots;
    return page;
}

// A full-screen notice: its first line the title, the rest rows, and Continue.
// The funds warning (0x084B1) is one.
inline ui::Page notice_page(const char* const* lines, size_t count) {
    const auto trim = [](std::string t) {
        const size_t a = t.find_first_not_of(' ');
        if (a == std::string::npos) return std::string();
        return t.substr(a, t.find_last_not_of(' ') - a + 1);
    };
    ui::Page page;
    if (count > 0) page.title = trim(lines[0]);
    for (size_t i = 1; i < count; ++i) page.rows.push_back({trim(lines[i]), ""});
    page.buttons.push_back({"Continue", kActionContinue});
    return page;
}

// The end of a career: dismissed after three missed tributes (the settlement's
// DS:0x6D6A = 0x3C), or hailed as Caesar (0x29100). The texts are Gaius's own.
inline ui::Page ending_page(const model::CityState& s, bool caesar) {
    ui::Page page;
    if (caesar) {
        page.title = "Ave, Caesar";
        page.rows.push_back({"Rome hails you as", rank_name(20)});
        page.rows.push_back({"Last governed", province_name(detail::g(s, 0x6CA6))});
        page.rows.push_back({"Year", year_text(detail::g(s, 0x6C32))});
    } else {
        page.title = "Dismissed";
        page.rows.push_back({"Three tributes to Rome in a row", ""});
        page.rows.push_back({"went unpaid.", ""});
        page.rows.push_back({"Rome has ordered your arrest.", ""});
    }
    page.buttons.push_back({"Continue", kActionContinue});
    page.buttons.push_back({"Quit", kActionQuit});
    return page;
}

struct BattleView {
    int cohort = -1, army = -1;
    bool has_round = false;
    systems::battle::Round last;
    bool retreated = false;
};

inline ui::Page battle_page(const model::CityState& s, const BattleView& b) {
    namespace military = systems::military;
    namespace battle = systems::battle;
    using detail::g;
    ui::Page page;
    const int race = g(s, 0x6BD6);
    const char* race_name =
        race >= 0 && race < static_cast<int>(battle::kRaces.size()) ? battle::kRaces[static_cast<size_t>(race)].name : "?";
    page.title = std::string("Battle against the ") + race_name;
    const model::Actor& c = s.objects[static_cast<size_t>(b.cohort)];
    const model::Actor& a = s.objects[static_cast<size_t>(b.army)];
    const int number = static_cast<int8_t>(c.raw[0x2A]);
    const bool ended = b.last.victory || b.last.defeat || b.retreated;
    page.rows.push_back({"Cohort " + std::to_string(number + 1) + ", " +
                             (number >= 0 && number < 10 ? kCohortEmblems[static_cast<size_t>(number)] : "?"),
                         ""});
    page.rows.push_back({"  Regulars", std::to_string(c.raw[military::kCohortRegulars])});
    page.rows.push_back({"  Irregulars", std::to_string(c.raw[military::kCohortIrregulars])});
    page.rows.push_back({"  Auxiliaries", std::to_string(c.raw[military::kCohortAuxiliaries])});
    page.rows.push_back({"  Morale", std::to_string(c.raw[military::kCohortMorale])});
    page.rows.push_back({"Barbarian army", b.last.victory ? "destroyed" : std::to_string(a.raw[battle::kArmySize])});
    if (b.has_round) {
        page.rows.push_back({"Last round - Romans", std::to_string(b.last.romans)});
        page.rows.push_back({"  barbarians", std::to_string(b.last.barbarians)});
    }
    if (ended) {
        page.rows.push_back({b.last.victory ? "Victory !" : b.last.defeat ? "The Cohort is destroyed" : "You retreat",
                             ""});
        page.buttons.push_back({"Continue", kActionContinue});
    } else {
        page.buttons.push_back({"Tortoise", kActionTactic + static_cast<int>(battle::Tactic::Tortoise)});
        page.buttons.push_back({"Assault", kActionTactic + static_cast<int>(battle::Tactic::Assault)});
        page.buttons.push_back({"Flank", kActionTactic + static_cast<int>(battle::Tactic::Flank)});
        page.buttons.push_back({"Charge", kActionTactic + static_cast<int>(battle::Tactic::Charge)});
        page.buttons.push_back({"Retreat", kActionRetreat});
    }
    return page;
}

}  // namespace gaius::viewer
