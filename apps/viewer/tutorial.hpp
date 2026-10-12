// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — apps/viewer/tutorial.hpp
//
// The tutorial: Gaius's own guide through a career's first rank, from the empty site to the promotion, with a last
// page wishing the player luck as the second province begins. The original has none in the program (its manual walks
// through a first city); the wording here is Gaius's, and every rule it states is the transcribed simulation's:
//   - houses grow on land value and services, in the order water, the Forum's citizens, a market, baths, a school, a
//     show (systems/housing.cpp); a well reaches the plots that touch it, bath houses three cells, and an Aventine
//     Forum taxes the houses within six (systems/service.cpp);
//   - the share of the people without work sets the land value every house starts from (month::run_economy);
//   - Peace rises by 2 a year, Culture is counted per head (an Oracle is 18 points: 12 for 2000 people), Prosperity and
//     Culture are capped by the population (2000 people for a Prosperity of 12) and Prosperity rises only in a year
//     whose accounts, construction included, close in profit; Empire is the highway (20), the linked towns (up to
//     20 each) and the straight roads (systems/administration.cpp);
//   - a workshop's output counts the people housed within three cells of it and falls with each other workshop of
//     the same goods (actors::workshop_level); the population tax is free of ill effects up to 6 percent
//     (month::run_economy's tables); Rome takes 60 percent of a year's profit while the treasury holds more than
//     500 Dn (economy::settle_accounts);
//   - a town counts as linked through the Imperial Highway too, which a road joins by crossing it
//     (province::kTownRoads); a province road wears away in a month whose construction duty is short.
// `playtest <game> tutorial` plays the pages as a newcomer would, to check that doing what they say reaches the
// promotion (after 8 game years at the easiest funding, 17 at 5000 Dn).
//   - Citizen to Equitus asks 12 of each rating and an average of 35 (administration::kPromotion[1]).
//
// A step is a page of text (ui::Page, time stopped while it is read) and a goal. The goal is read off the city and the
// province themselves -- a Forum stands, twelve cells are road, a town is linked -- so the tutorial follows whatever
// the player does, in any order: a goal already met is passed over without its page. While a step waits, its goal is a
// line above the control bar (a click on it opens the page again) and a gold frame marks the button to press next.
//
// The step reached is kept in gaius.cfg (ui::Settings::tutorial_step), not in the save, whose format is the
// original's. Nothing here touches the simulation.
//
// No SDL, so the steps, the goals and the pages are tested (test_tutorial).

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "apps/viewer/game_folder_note.hpp"
#include "apps/viewer/screens.hpp"
#include "model/city_state.hpp"
#include "systems/administration.hpp"
#include "systems/construction.hpp"
#include "systems/forum.hpp"
#include "systems/housing.hpp"
#include "systems/province.hpp"
#include "ui/panel.hpp"
#include "ui/strings.hpp"
#include "ui/toolbar.hpp"

namespace gaius::viewer {

// What a step waits for.
enum class TutorialGoal {
    Read,        // nothing: Continue moves on
    Forum,       // a Forum stands
    Roads,       // kTutorialRoadCells cells of road
    Housing,     // kTutorialHouses housing plots
    Water,       // kTutorialWells wells, or a fountain
    Work,        // a market and kTutorialWorkshops workshops
    BathHouse,   // bath houses
    VisitForum,  // the Forum screen was opened during the step
    Settled,     // kTutorialSettled people
    Highway,     // the Imperial Highway reaches the city
    People,      // kTutorialPeople people
    Oracle,      // an oracle
    Town,        // kTutorialTowns towns are linked to the city
    Promotion,   // rank 2
};

// The numbers the goals ask for. The steps' lines name them, and test_tutorial checks that they agree.
inline constexpr int kTutorialRoadCells = 12, kTutorialHouses = 12, kTutorialWells = 3, kTutorialWorkshops = 2,
                     kTutorialSettled = 500, kTutorialPeople = 2000, kTutorialTowns = 2;

// When a step's last paragraph is replaced by `alt`.
enum class TutorialAlt { None, Touch, TribuneByHand };

struct TutorialStep {
    TutorialGoal goal;
    const char* line;     // the goal as the map shows it; "" for a page that is only read
    const char* title;
    const char* text[3];  // paragraphs, wrapped to the page; nullptr for none
    const char* alt = nullptr;
    TutorialAlt alt_when = TutorialAlt::None;
};

// FONT1 draws no colon, semicolon or slash (lang files are checked for them: scripts/extract_strings.py --check).
// The order is the order money allows: a city that pays its way first, then the Imperial Highway while the starting
// funds still cover it, then the city's growth and the towns' roads out of what is left and what comes in. (Until
// 2026-10-11 the Highway came after 2000 people; the first person to play it reached that step with 23 Dn.)
inline constexpr TutorialStep kTutorialSteps[] = {
    {TutorialGoal::Read, "", "Welcome, governor",
     {"Rome has given you a province. Build its capital, keep the peace, and Rome will promote you.",
      "Rome judges four ratings - Peace, Culture, Prosperity and Empire. To rise from Citizen to Equitus each must reach 12 and their average 35.",
      "This tutorial leads you there. A gold frame marks the button to press next."}},
    {TutorialGoal::Forum, "Build a Forum", "A Forum",
     {"A city begins with its Forum. It gathers the taxes of the houses around it and sends its citizens along the roads.",
      "Find open land near water, choose Forum, take the first type (Aventine, 60 Dn) and place it.",
      "The arrow keys scroll the map, and so does the pointer at the screen's edge. The right button puts a tool away."},
     "Drag a finger to scroll the map. Tap the map to see what would be built there, tap it again to build.",
     TutorialAlt::Touch},
    {TutorialGoal::Roads, "Lay 12 cells of road", "Roads",
     {"Houses grow only where the Forum's citizens pass, and those keep to the roads.",
      "Choose Road and drag it from beside the Forum, some twenty cells. Leave room on both sides for houses.",
      "The right button during a drag takes it back. Clear Area removes what was built by mistake."},
     "Drag a finger to lay it. Undo takes the last drag back. Clear Area removes what was built by mistake.",
     TutorialAlt::Touch},
    {TutorialGoal::Housing, "Place 12 houses", "Housing",
     {"Choose Housing and place a dozen plots along the road, within six cells of the Forum - further out they pay no tax.",
      "Each plot starts as a tent. Where life is good it grows, step by step, into a villa that holds more people and pays more tax. Where it is not, the tent folds.",
      nullptr}},
    {TutorialGoal::Water, "Dig 3 wells", "Water",
     {"No house grows past a pair of tents without water.",
      "Choose Well and dig one among the houses. A well serves only the plots that touch it, so dig one for every few houses.",
      "Later a reservoir on the water's edge, a pipe and a Fountain water a whole district. Pipe each Fountain to a reservoir - one fed through another runs dry every other month."}},
    {TutorialGoal::Work, "Build a Market and 2 Workshops", "Work",
     {"People need work. Where many are idle, the houses decay.",
      "Build a Market beside the road among the houses. Then two Workshops at the edge of the houses - they want people living within three cells, and a Market along their road.",
      "Give each different goods, from the best named above."}},
    {TutorialGoal::BathHouse, "Build Bath Houses", "Bath Houses",
     {"Houses ask for more as they grow - water, then a market, then baths, later a school and a theater.",
      "Choose Bath Houses and build them beside the road among the houses. They serve the plots within three cells.",
      nullptr}},
    {TutorialGoal::VisitForum, "Visit the Forum", "Your advisors",
     {"Go to Forum shows your advisors. With the Treasurer, keep the people's tax at 6 percent and the industry tax at 5 - no higher.",
      "At the year's end Rome takes a tribute, and most of the year's profit if more than 500 Dn lie in the treasury. Miss three tributes and you are dismissed.",
      "The Tribune's plebs fight fires and mend buildings and roads. He staffs them by himself."},
     "The Tribune's plebs fight fires and mend buildings and roads. Give each duty the number in brackets.",
     TutorialAlt::TribuneByHand},
    {TutorialGoal::Settled, "Grow to 500 people", "A growing city",
     {"Taxes are your income. Add houses along new roads, with water, a market and baths in reach, and work for the newcomers - a Workshop employs 80 people, a Market 50, a Forum 120.",
      "A second Forum further along carries the taxes and the citizens to new streets.",
      "Mind the treasury - the province's roads are still to pay for. A line above the goal says how much to keep."}},
    {TutorialGoal::Highway, "Link the Imperial Highway", "The province",
     {"Go to Province shows the land you govern - your city, four towns and, at one edge, the junction of the Imperial Highway, marked in gold.",
      "Choose Highway and drag it from the junction to the city (50 Dn a cell, more on rough ground). It is worth 20 to your Empire rating, and straight roads add more.",
      "Should a piece of road wear away, lay it again - a gap breaks the link."}},
    {TutorialGoal::People, "Grow to 2000 people", "A bigger city",
     {"Prosperity and Culture rise no higher than the city's size allows. Rome wants 2000 people at the least.",
      "Build what pays - houses with water, a market and baths in reach, and work for them. Schools and shows can wait.",
      "A year costs some 200 Dn in welfare, wages and tribute whatever you build, so every slow year is paid from the treasury."}},
    {TutorialGoal::Oracle, "Build an Oracle", "Culture",
     {"Culture is counted per head - temples, oracles, schools and shows for the people you have.",
      "An Oracle (200 Dn) is the quickest way. One is enough for about 2000 people, so build more as the city grows.",
      "Temples are cheap, calm the streets and add a little."}},
    {TutorialGoal::Town, "Link 2 towns by road", "The towns",
     {"A town joined to your city by road trades with it and grows. Each is worth up to 20 to your Empire rating.",
      "Choose Road (30 Dn a cell) and drag it from a town marked in gold to the city - or just across the Highway, which a road that crosses it shares. Link two now and the others as the funds allow.",
      "The standard beside the city is your Cohort. Cohort Patrol sends it out when barbarians are sighted."}},
    {TutorialGoal::Promotion, "Earn Rome's promotion", "Promotion",
     {"In brackets, what an Equitus needs. Reach the average and Rome promotes you at the month's end - in a first province it overlooks a weak rating. Prosperity rises only in years that end in profit.",
      nullptr, nullptr}},
    {TutorialGoal::Read, "", "Well done, Equitus",
     {"Rome has promoted you. A new province and an empty site for its capital are yours, and all you have learned applies again.",
      "From now on Rome asks more with every rank and the barbarians come sooner. Your advisors in the Forum know where you stand.",
      "Good luck, governor!"}},
};
inline constexpr int kTutorialStepCount = static_cast<int>(sizeof(kTutorialSteps) / sizeof(kTutorialSteps[0]));
// The last page: shown as rank 2 begins, and the tutorial is over when it is closed.
inline constexpr int kTutorialFinal = kTutorialStepCount - 1;

// A page's rows are 38 characters wide (ui/panel.cpp at 1x: 304 pixels of 8), and 14 of them fit above the buttons.
inline constexpr size_t kTutorialWidth = 38;
inline constexpr int kTutorialRows = 14;

// What the goals are read from, counted off the city and the province.
struct TutorialCensus {
    int rank = 0;                     // DS:0x6C30
    int forums = 0, workshops = 0;    // DS:0x6CA0, DS:0x6C9E
    int road_cells = 0, house_cells = 0, wells = 0, fountains = 0, markets = 0, bath_houses = 0, oracles = 0;
    int population = 0;               // 4 x the housing units, as the game counts it (systems/housing.hpp)
    bool highway_junction = false;    // the province has the Imperial Highway's entry (DS:0x6C90, DS:0x6C8E)
    bool highway_linked = false;      // as province::connect_highway finds it
    int highway_cells = 0;            // from the junction to the city along the grid (0 without a junction)
    int towns = 0, towns_linked = 0;  // as province::develop_towns counts them
    int peace = 0, culture = 0, prosperity = 0, empire = 0, average = 0;  // the ratings (administration.hpp)
    int people_tax = 0, industry_tax = 0;  // DS:0x6C04, DS:0x6C02, percent
    int best_workshop = 0;                 // the highest production level among the workshops, 0-7
};

inline bool tutorial_town_tile(uint8_t tile) { return tile == 0x61 || tile == 0x79 || tile == 0x7A || tile == 0x4C; }

inline TutorialCensus tutorial_census(const model::CityState& state) {
    namespace province = systems::province;
    TutorialCensus c;
    c.rank = model::global_word(state, systems::administration::kRank);
    c.forums = model::global_word(state, 0x6CA0);
    c.workshops = model::global_word(state, 0x6C9E);
    c.peace = model::global_word(state, systems::administration::kPeace);
    c.culture = model::global_word(state, systems::administration::kCulture);
    c.prosperity = model::global_word(state, systems::administration::kProsperity);
    c.empire = model::global_word(state, systems::administration::kEmpire);
    c.average = model::global_word(state, systems::administration::kAverage);
    c.people_tax = model::global_word(state, 0x6C04);
    c.industry_tax = model::global_word(state, 0x6C02);
    // A workshop record is 24 bytes: +0x08 active, +0x10 its production level (actors::workshop_level).
    for (size_t r = 0; r + 24 <= state.table_720.size(); r += 24) {
        const int active = state.table_720[r + 8] | (state.table_720[r + 9] << 8);
        if (active != 0) c.best_workshop = std::max(c.best_workshop, state.table_720[r + 0x10] & 7);
    }
    int market_cells = 0, oracle_cells = 0, units = 0;
    for (const auto& row : state.city.tile) {
        for (const uint8_t t : row) {
            if (t >= 0x36 && t <= 0x43) ++c.road_cells;
            else if (t == 0xB8) ++c.wells;
            else if (t >= 0xB9 && t <= 0xBD) ++c.fountains;
            else if (t >= 0xC8 && t <= 0xD7) {
                ++c.house_cells;
                units += systems::housing::kPopulationUnitsPerCell[static_cast<size_t>(t - 0xC8)];
            } else if (t >= 0xE8 && t <= 0xEA) ++c.bath_houses;
            else if (t == 0xEB) ++oracle_cells;
            else if (t == 0xF4) ++market_cells;
        }
    }
    c.markets = market_cells / 4;
    c.oracles = oracle_cells / 2;
    c.population = 4 * units;
    if (state.empire.cells.size() < static_cast<size_t>(province::kMapW) * province::kMapW) return c;
    const int hx = model::global_word(state, 0x6C90), hy = model::global_word(state, 0x6C8E);
    c.highway_junction = hx >= 0 && hx < province::kMapW && hy >= 0 && hy < province::kMapW &&
                         (state.empire.cells[static_cast<size_t>(hy) * province::kMapW + hx] & 0x7F) == 0x78;
    c.highway_linked = c.highway_junction && province::connected(state.empire, hx, hy, province::kHighwayRoads);
    for (int y = 0; y < province::kMapW; ++y) {
        for (int x = 0; x < province::kMapW; ++x) {
            const uint8_t cell = state.empire.cells[static_cast<size_t>(y) * province::kMapW + x] & 0x7F;
            if (cell == province::kCityTile && c.highway_junction) {
                const int away = std::abs(x - hx) + std::abs(y - hy);
                if (c.highway_cells == 0 || away < c.highway_cells) c.highway_cells = away;
            }
            if (!tutorial_town_tile(cell)) continue;
            ++c.towns;
            if (province::connected(state.empire, x, y, province::kTownRoads)) ++c.towns_linked;
        }
    }
    return c;
}

inline bool tutorial_goal_met(TutorialGoal goal, const TutorialCensus& c, bool forum_visited) {
    switch (goal) {
        case TutorialGoal::Read: return false;
        case TutorialGoal::Forum: return c.forums >= 1;
        case TutorialGoal::Roads: return c.road_cells >= kTutorialRoadCells;
        case TutorialGoal::Housing: return c.house_cells >= kTutorialHouses;
        case TutorialGoal::Water: return c.wells >= kTutorialWells || c.fountains >= 1;
        case TutorialGoal::Work: return c.markets >= 1 && c.workshops >= kTutorialWorkshops;
        case TutorialGoal::BathHouse: return c.bath_houses >= 1;
        case TutorialGoal::VisitForum: return forum_visited;
        case TutorialGoal::Highway: return c.highway_linked || !c.highway_junction;
        case TutorialGoal::Town: return c.towns_linked >= std::min(c.towns, kTutorialTowns);
        case TutorialGoal::Oracle: return c.oracles >= 1;
        case TutorialGoal::Settled: return c.population >= kTutorialSettled;
        case TutorialGoal::People: return c.population >= kTutorialPeople;
        case TutorialGoal::Promotion: return c.rank >= 2;
    }
    return false;
}

inline const TutorialStep& tutorial_step(int step) {
    return kTutorialSteps[static_cast<size_t>(std::clamp(step, 0, kTutorialFinal))];
}

// The step the tutorial is at: `step`, or the first one after it whose goal is not met yet. A page that is only read
// is left by its Continue, not here.
inline int tutorial_progress(int step, const TutorialCensus& c, bool forum_visited) {
    step = std::clamp(step, 0, kTutorialFinal);
    while (step < kTutorialFinal && tutorial_goal_met(kTutorialSteps[step].goal, c, forum_visited)) ++step;
    return step;
}

// The goal as the map shows it, with how far it has come: "Lay 12 cells of road (5)". Empty for a page that is only read.
inline std::string tutorial_line(int step, const TutorialCensus& c) {
    const TutorialStep& s = tutorial_step(step);
    if (s.line[0] == '\0') return std::string();
    std::string line = ui::tr(s.line);
    int have = -1;
    switch (s.goal) {
        case TutorialGoal::Roads: have = c.road_cells; break;
        case TutorialGoal::Housing: have = c.house_cells; break;
        case TutorialGoal::Water: have = c.wells; break;
        case TutorialGoal::Settled:
        case TutorialGoal::People: have = c.population; break;
        case TutorialGoal::Town: have = c.towns_linked; break;
        case TutorialGoal::Promotion: have = c.average; break;  // of the 35 the tutorial's rule asks
        default: break;
    }
    if (have > 0) line += " (" + std::to_string(have) + ")";
    return line;
}

// The tutorial's own rule for the first promotion, Gaius's and not the original's: Rome offers it once the average
// is reached, whatever the single ratings. The original also asks 12 of each (administration::check_promotion), and
// the first person to play the tutorial through stood at an average of 40 for years with a Prosperity of 6 --
// Prosperity rises only in a year that closes in profit, which a player still building never has. The original's
// rule stands with the tutorial off, and from the second rank always.
inline bool tutorial_promotion_due(const model::CityState& state) {
    namespace admin = systems::administration;
    return model::global_word(state, admin::kRank) == 1 && model::global_word(state, 0x6C24) == 0 &&
           model::global_word(state, admin::kAverage) >= admin::kPromotion[1].average;
}

// The goods this province suits best, by the industry report's own table (3496:1880): the two best, and a third
// when it is rated Good or better, joined by commas -- "Wheat, Pottery".
inline std::string tutorial_best_goods(const model::CityState& state) {
    const systems::forum::IndustryReport report = systems::forum::industry_report(state);
    std::vector<int> order = {0, 1, 2, 3, 4, 5, 6, 7};
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return report.rows[static_cast<size_t>(a)].suitability > report.rows[static_cast<size_t>(b)].suitability;
    });
    std::string names;
    for (size_t i = 0; i < order.size() && i < 3; ++i) {
        const int goods = order[i];
        if (i >= 2 && report.rows[static_cast<size_t>(goods)].suitability < 1) break;
        if (!names.empty()) names += ", ";
        names += systems::forum::kGoodsNames[static_cast<size_t>(goods)];
    }
    return names;
}

// Pages that open once in a career while the tutorial runs, when the player does one of the things that quietly
// ruin a first city. Each was read off a played session (2026-10-11): the people's tax raised to 7 percent emptied
// a city of 2200 with a quarter of its people idle (month::run_economy's tables), the industry tax at 6 percent
// took a level a year from every workshop until none earned anything (economy::industrial_tax_pressure), and
// twenty years of building closed every year at a loss, so Prosperity never moved (administration::prosperity_year).
enum class TutorialAdvice { None, Treasury, PeopleTax, IndustryTax, Losses, Workshops };
inline constexpr int kTutorialAdviceCount = 6;
inline constexpr int kTutorialLossYears = 3;
struct TutorialAdviceText {
    const char* title;
    const char* text[2];
};
inline constexpr TutorialAdviceText kTutorialAdviceTexts[] = {
    {"", {nullptr, nullptr}},
    {"The treasury", {nullptr, nullptr}},  // tutorial_treasury_page
    {"The people's tax",
     {"A people's tax above 6 percent breeds unrest. Where many are out of work the houses then fall, street by street.",
      "Set it back to 6 at the Treasurer's. Workshops, Markets and Forums give the idle work."}},
    {"The industry tax",
     {"An industry tax above 5 percent costs every Workshop a little of its output each year, until none earns anything.",
      "Set it to 5 at the Treasurer's. Lower still for some years, and the Workshops recover."}},
    {"Years at a loss",
     {"Three years have closed at a loss. Prosperity rises only in a year that ends in profit, and all you build counts as spending.",
      "Stop building for a year or two and let the taxes come in."}},
    {"Idle workshops",
     {"Your Workshops produce nothing. A Workshop counts the people housed within three cells of it - give it sixteen full plots or more.",
      "Build the best goods for this province, each of them once, and keep the industry tax at 5 percent."}},
};
// Which advice the city calls for now, the first that applies and has not been given (`given` by TutorialAdvice).
// `loss_years` is the count of years in a row that closed at a loss; `reserve` what the steps ahead cost.
inline TutorialAdvice tutorial_advice_due(const TutorialCensus& c, int funds, int reserve, int loss_years,
                                          const bool given[kTutorialAdviceCount]) {
    const auto open = [&](TutorialAdvice a) { return !given[static_cast<size_t>(a)]; };
    if (open(TutorialAdvice::PeopleTax) && c.people_tax > 6) return TutorialAdvice::PeopleTax;
    if (open(TutorialAdvice::IndustryTax) && c.industry_tax > 5) return TutorialAdvice::IndustryTax;
    if (open(TutorialAdvice::Treasury) && funds < reserve) return TutorialAdvice::Treasury;
    if (open(TutorialAdvice::Losses) && loss_years >= kTutorialLossYears) return TutorialAdvice::Losses;
    if (open(TutorialAdvice::Workshops) && c.workshops >= kTutorialWorkshops && c.population >= 1000 &&
        c.best_workshop == 0)
        return TutorialAdvice::Workshops;
    return TutorialAdvice::None;
}

// What the steps still ahead will cost, roughly: the Imperial Highway at 50 Dn a cell and a quarter more for rough
// ground and turns, 180 Dn for each of the two towns' roads (a spur onto the Highway: `playtest <game> tutorial` pays
// 150-180), and the Oracle's 200. Shown above the goal from the step that asks for people, so the player keeps it.
inline constexpr int kTutorialTownRoad = 180, kTutorialOracle = 200;
inline int tutorial_reserve(int step, const TutorialCensus& c) {
    const TutorialGoal goal = tutorial_step(step).goal;
    if (goal != TutorialGoal::Settled && goal != TutorialGoal::Highway && goal != TutorialGoal::People &&
        goal != TutorialGoal::Oracle && goal != TutorialGoal::Town)
        return 0;
    int need = 0;
    if (c.highway_junction && !c.highway_linked) need += c.highway_cells * 50 * 5 / 4;
    need += std::max(0, std::min(c.towns, kTutorialTowns) - c.towns_linked) * kTutorialTownRoad;
    if (c.oracles < 1) need += kTutorialOracle;
    return need;
}
// The line above the goal: "Still to pay for - 1630 Dn". Empty when nothing is.
inline std::string tutorial_reserve_line(int step, const TutorialCensus& c) {
    const int need = tutorial_reserve(step, c);
    if (need <= 0) return std::string();
    return std::string(ui::tr("Still to pay for")) + " - " + std::to_string(need) + " Dn";
}
// The page that opens once when the treasury falls below that: what is short, and what the game does to a city
// without money (economy::grant_emergency_funds, economy::settle_accounts' welfare cut, systems/plebs.hpp).
inline constexpr const char* kTutorialTreasuryText[] = {
    "Stop building for now. Taxes come in at each year's end - let a few years pass, and see that the people's tax is 6 percent.",
    "Should the treasury run dry, Rome sends 500 Dn once. After that the welfare is cut, the plebs leave, and fires and collapses take the city.",
};
inline ui::Page tutorial_treasury_page(int funds, int need) {
    ui::Page page;
    page.title = ui::tr("The treasury");
    page.rows.push_back({ui::tr("In the treasury"), std::to_string(funds) + " Dn"});
    page.rows.push_back({ui::tr("Still to pay for"), std::to_string(need) + " Dn"});
    for (const char* text : kTutorialTreasuryText) {
        page.rows.push_back({"", ""});
        for (const std::string& line : wrap_note(ui::tr(text), kTutorialWidth)) page.rows.push_back({line, ""});
    }
    page.buttons.push_back({ui::tr("Continue"), kActionContinue});
    return page;
}

inline ui::Page tutorial_advice_page(TutorialAdvice advice, const model::CityState& state, int funds, int need) {
    if (advice == TutorialAdvice::Treasury) return tutorial_treasury_page(funds, need);
    const TutorialAdviceText& t = kTutorialAdviceTexts[static_cast<size_t>(advice)];
    ui::Page page;
    page.title = ui::tr(t.title);
    if (advice == TutorialAdvice::Workshops) page.rows.push_back({ui::tr("Best goods here"), tutorial_best_goods(state)});
    for (const char* text : t.text) {
        if (!text) continue;
        if (!page.rows.empty()) page.rows.push_back({"", ""});
        for (const std::string& line : wrap_note(ui::tr(text), kTutorialWidth)) page.rows.push_back({line, ""});
    }
    page.buttons.push_back({ui::tr("Continue"), kActionContinue});
    return page;
}

// The command a step asks the player to choose in the city (NoAction for none), and on the province map (the
// executable's id: 42 the highway, 36 a road; 0 for none).
inline systems::construction::CommandId tutorial_city_command(int step, const TutorialCensus& c) {
    using C = systems::construction::CommandId;
    switch (tutorial_step(step).goal) {
        case TutorialGoal::Forum: return C::Forum;
        case TutorialGoal::Roads: return C::Road;
        case TutorialGoal::Housing: return C::Housing;
        case TutorialGoal::Water: return C::Well;
        case TutorialGoal::Work: return c.markets < 1 ? C::Market : C::Workshop;
        case TutorialGoal::BathHouse: return C::BathHouses;
        case TutorialGoal::VisitForum: return C::GoToForum;
        case TutorialGoal::Highway:
        case TutorialGoal::Town: return C::GoToProvince;
        case TutorialGoal::Oracle: return C::Oracle;
        default: return C::NoAction;
    }
}
inline int tutorial_province_command(int step) {
    const TutorialGoal goal = tutorial_step(step).goal;
    return goal == TutorialGoal::Highway ? 42 : goal == TutorialGoal::Town ? 36 : 0;
}

// The button of the city's bar to press on the way to `want`: its own when it is showing; on the original's paged bar
// the button that turns to its page, or the arrow back to the main bar. -1 for none.
inline int tutorial_bar_button(const ui::Toolbar& bar, systems::construction::CommandId want) {
    using ui::BarKind;
    const auto is_want = [&](const ui::BarButton& b) {
        return (b.kind == BarKind::Tool || b.kind == BarKind::Go) && b.command == want;
    };
    if (want == systems::construction::CommandId::NoAction) return -1;
    for (int i = 0; i < bar.count(); ++i)
        if (is_want(bar.entry(i))) return bar.button(i).w > 0 ? i : -1;
    if (!bar.original_bar()) return -1;
    int page = -1;
    for (int p = 0; p < ui::kBarProvincePage && page < 0; ++p)
        for (const ui::BarButton& b : ui::original_bar_page(p))
            if (is_want(b)) page = p;
    if (page < 0) return -1;
    for (int i = 0; i < bar.count(); ++i) {
        const ui::BarButton b = bar.entry(i);
        if (b.kind == BarKind::Page && b.page == page) return i;
    }
    for (int i = 0; i < bar.count(); ++i)
        if (bar.entry(i).kind == BarKind::Back) return i;
    return -1;
}

// The cells of the province map a step points at: the highway's junction, or every town not linked yet.
inline std::vector<std::pair<int, int>> tutorial_province_marks(const model::CityState& state, int step) {
    namespace province = systems::province;
    std::vector<std::pair<int, int>> marks;
    const TutorialGoal goal = tutorial_step(step).goal;
    if (state.empire.cells.size() < static_cast<size_t>(province::kMapW) * province::kMapW) return marks;
    if (goal == TutorialGoal::Highway) {
        const int x = model::global_word(state, 0x6C90), y = model::global_word(state, 0x6C8E);
        if (x >= 0 && x < province::kMapW && y >= 0 && y < province::kMapW) marks.push_back({x, y});
        // The city's two cells are the other end.
        for (int cy = 0; cy < province::kMapW; ++cy)
            for (int cx = 0; cx < province::kMapW; ++cx) {
                const uint8_t t = state.empire.cells[static_cast<size_t>(cy) * province::kMapW + cx] & 0x7F;
                if (t == province::kCityTile) marks.push_back({cx, cy});
            }
    } else if (goal == TutorialGoal::Town) {
        for (int y = 0; y < province::kMapW; ++y)
            for (int x = 0; x < province::kMapW; ++x)
                if (tutorial_town_tile(state.empire.cells[static_cast<size_t>(y) * province::kMapW + x] & 0x7F) &&
                    !province::connected(state.empire, x, y, province::kTownRoads))
                    marks.push_back({x, y});
    }
    return marks;
}

// A step's page: its title, the paragraphs wrapped to the page with a line between them, and Continue. The promotion
// step leads with the ratings as they stand, each with what the next rank asks in brackets and what raises it.
inline ui::Page tutorial_page(int step, const model::CityState& state, bool touch = false, bool tribune_auto = true) {
    namespace admin = systems::administration;
    step = std::clamp(step, 0, kTutorialFinal);
    const TutorialStep& s = kTutorialSteps[step];
    ui::Page page;
    const bool numbered = step > 0 && step < kTutorialFinal;
    page.title = (numbered ? std::to_string(step) + ". " : std::string()) + ui::tr(s.title);
    if (s.goal == TutorialGoal::Promotion) {
        const int rank = std::clamp(model::global_word(state, admin::kRank), 0, 20);
        const admin::Requirement& need = admin::kPromotion[static_cast<size_t>(rank)];
        const auto rating = [&](const char* name, uint16_t word, int asked, const char* how) {
            page.rows.push_back({std::string(ui::tr(name)) + " " + std::to_string(model::global_word(state, word)) + " (" +
                                     std::to_string(asked) + ")",
                                 how});
        };
        rating("Peace", admin::kPeace, need.each, ui::tr("2 a quiet year"));
        rating("Culture", admin::kCulture, need.each, ui::tr("oracles, temples"));
        rating("Prosperity", admin::kProsperity, need.each, ui::tr("years in profit"));
        rating("Empire", admin::kEmpire, need.each, ui::tr("Highway, towns"));
        rating("Average", admin::kAverage, need.average, "");
    }
    if (s.goal == TutorialGoal::Work) page.rows.push_back({ui::tr("Best goods here"), tutorial_best_goods(state)});
    const bool use_alt = s.alt && ((s.alt_when == TutorialAlt::Touch && touch) ||
                                   (s.alt_when == TutorialAlt::TribuneByHand && !tribune_auto));
    int last = -1;
    for (int i = 0; i < 3; ++i)
        if (s.text[i]) last = i;
    for (int i = 0; i <= last; ++i) {
        if (!s.text[i]) continue;
        if (!page.rows.empty()) page.rows.push_back({"", ""});
        for (const std::string& line : wrap_note(ui::tr(i == last && use_alt ? s.alt : s.text[i]), kTutorialWidth))
            page.rows.push_back({line, ""});
    }
    page.buttons.push_back({ui::tr("Continue"), kActionContinue});
    if (step < kTutorialFinal) page.buttons.push_back({ui::tr("Skip tutorial"), kActionTutorialSkip});
    return page;
}

// A frame `thick` pixels wide just outside a rectangle: the mark on the button to press, and on a province cell.
inline void draw_tutorial_frame(std::vector<uint8_t>& rgb, int w, int h, ui::Rect r, bool bright, int thick = 2,
                                int max_y = -1) {
    const formats::RGB c = bright ? formats::RGB{255, 226, 110} : formats::RGB{196, 140, 36};
    if (max_y < 0 || max_y > h) max_y = h;
    for (int y = r.y - thick; y < r.y + r.h + thick; ++y) {
        if (y < 0 || y >= max_y) continue;
        for (int x = r.x - thick; x < r.x + r.w + thick; ++x) {
            if (x < 0 || x >= w) continue;
            if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) continue;
            const size_t i = (static_cast<size_t>(y) * w + x) * 3;
            rgb[i] = c.r;
            rgb[i + 1] = c.g;
            rgb[i + 2] = c.b;
        }
    }
}

}  // namespace gaius::viewer
