// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius CLI tool: playtest
//
// A bot that plays a career the way the viewer's start screen and build tools
// do, to find out whether the game can actually be played through. It uses the
// same glue as apps/viewer/main.cpp (place_tool's pleb gate, funds check and
// charge; the month loop through run_frame; the promotion and province hooks)
// and the same library calls underneath, and logs what a player would see.
//
// Usage: playtest <game folder> probe   [funding [difficulty]]    the new career's city and province
//        playtest <game folder> play    [funding [difficulty [years]]]  the bot plays a whole career, to Caesar
//        playtest <game folder> fuzz    [seed [months [funding [difficulty]]]]  a player who clicks at random
//        playtest <game folder> inspect <save>   houses by grade, services and last year's accounts
//        playtest <game folder> tutorial [funding [difficulty [years]]]  a newcomer who does what the viewer's tutorial says
// The bot is tuned for funding 0 (8000 Dn) on Easy or Medium. Environment knobs: PT_QUIET, PT_TRACE, PT_DUTIES=<year>,
// PT_NO_TRIBUNE (never touch the Tribune's duties or the welfare: what an unattended new game is like; with PT_ASSIST the
// viewer's Tribune: Automatic, apps/viewer/tribune_assist.hpp, does it), PT_PROVINCE, PT_GRADES, PT_HOUSES, PT_TAX, PT_ITAX, PT_UNEMP, PT_MARGIN, PT_PROVINCE_AFTER, PT_ORACLE_AFTER, PT_RANK,
// PT_SAVE_DIR (milestone saves for the viewer), PT_CHAOS=<seed> [PT_CHAOS_N, PT_CHAOS_GENTLE] (random clicks on top).

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "formats/empire2/empire2.hpp"
#include "formats/save/save.hpp"
#include "model/city_state.hpp"
#include "apps/viewer/tribune_assist.hpp"
#include "apps/viewer/tutorial.hpp"
#include "systems/actors.hpp"
#include "systems/administration.hpp"
#include "systems/battle.hpp"
#include "systems/campaign.hpp"
#include "systems/construction.hpp"
#include "systems/economy.hpp"
#include "systems/forum.hpp"
#include "systems/housing.hpp"
#include "systems/messages.hpp"
#include "systems/military.hpp"
#include "systems/month.hpp"
#include "systems/plebs.hpp"
#include "systems/province.hpp"

using namespace gaius;
namespace fs = std::filesystem;
namespace construction = systems::construction;
namespace economy = systems::economy;
namespace admin = systems::administration;
namespace msg = systems::messages;
using construction::CommandId;

namespace {

int gw(const model::CityState& s, uint16_t ds) { return model::global_word(s, ds); }

struct Pt {
    int x, y;
    bool operator==(const Pt& o) const { return x == o.x && y == o.y; }
};

// ---------------------------------------------------------------------------
// The two sides of a battle round as 0x2308E and 0x229A3 add them up before the shift, the generator's words at their averages.
int min_barbarian_strength(const model::CityState& st) {
    int best = 99;
    for (int t = 0; t < 4; ++t) best = std::min(best, systems::battle::tactic_strength(st, static_cast<systems::battle::Tactic>(t)));
    return best;
}
double romans_strength(const model::Actor& c) {
    const int morale = c.raw[0x2B];
    const int mod = morale <= 1 ? -4 : morale <= 3 ? -2 : morale >= 8 ? 2 : morale >= 6 ? 1 : 0;
    return 3.0 * c.raw[0x20] + 2.0 * c.raw[0x21] + c.raw[0x1D] / 2 + 1.5 + mod;
}
double barbarians_strength(int strength, int size) { return 2.0 * strength + size + 3.5; }

// ---------------------------------------------------------------------------
// The game, as the viewer holds it.

struct Game {
    std::string dir;
    model::CityState state;
    systems::month::SimState sim;
    int shore_variant = 0;
    int forum_grade = 0, workshop_goods = 0;
    construction::DragState drag;
    systems::construction::DragState province_drag;

    // What happened, for the report.
    struct Event {
        int year, month;
        std::string text;
    };
    std::vector<Event> events;
    std::map<std::string, int> refusals;
    bool promotion_pending = false, promotion_to_caesar = false;
    int battles = 0, battles_won = 0, battles_lost = 0, battles_retreated = 0, invasions = 0;
    bool dismissed = false;
    bool verbose = false;

    void note(const std::string& text) {
        events.push_back({sim.year, sim.month + 1, text});
        if (verbose) std::printf("  [%d/%d] %s\n", sim.year, sim.month + 1, text.c_str());
    }

    void install_hooks() {
        sim.on_promotion = [&](model::CityState&, bool to_caesar) {
            promotion_pending = true;
            promotion_to_caesar = to_caesar;
            return 0;  // unanswered, like the viewer: it opens the promotion screen
        };
        sim.on_battle = [&](model::CityState&, int cohort, int army) { fight(cohort, army); };
    }

    // A battle as a player would fight it: the tactic the province's race is
    // weakest against, round after round, until one side is gone.
    void fight(int cohort, int army) {
        ++battles;
        systems::battle::load_race(state);
        char buf[200];
        {
            const model::Actor& c = state.objects[static_cast<size_t>(cohort)];
            const model::Actor& a = state.objects[static_cast<size_t>(army)];
            std::snprintf(buf, sizeof buf,
                          "battle: the Cohort (regulars %d irregulars %d auxiliaries %d morale %d) meets an army of size %d; strengths A%d F%d C%d T%d",
                          c.raw[0x20], c.raw[0x21], c.raw[0x1D], c.raw[0x2B], a.raw[0x30],
                          systems::battle::tactic_strength(state, systems::battle::Tactic::Assault),
                          systems::battle::tactic_strength(state, systems::battle::Tactic::Flank),
                          systems::battle::tactic_strength(state, systems::battle::Tactic::Charge),
                          systems::battle::tactic_strength(state, systems::battle::Tactic::Tortoise));
        }
        note(buf);
        for (int round = 0; round < 60; ++round) {
            {
                const model::Actor& c = state.objects[static_cast<size_t>(cohort)];
                const model::Actor& a = state.objects[static_cast<size_t>(army)];
                if (romans_strength(c) - barbarians_strength(min_barbarian_strength(state), a.raw[0x30]) < -4.0 &&
                    static_cast<int>(c.raw[0x20]) + c.raw[0x21] > 0) {
                    systems::battle::retreat(state, cohort);
                    ++battles_retreated;
                    note("battle: the Cohort retreats, the odds are against it");
                    return;
                }
            }
            // pick the tactic with the lowest barbarian strength
            int best = 0, best_strength = 1 << 30;
            for (int t = 0; t < 4; ++t) {
                const int strength = systems::battle::tactic_strength(state, static_cast<systems::battle::Tactic>(t));
                if (strength < best_strength) {
                    best_strength = strength;
                    best = t;
                }
            }
            sim.random.advance();
            const auto r = systems::battle::fight_round(state, cohort, army, static_cast<systems::battle::Tactic>(best),
                                                        sim.random);
            if (r.victory) {
                ++battles_won;
                note("battle won");
                return;
            }
            if (r.defeat) {
                ++battles_lost;
                note("battle LOST");
                return;
            }
        }
        note("battle: 60 rounds without a result, retreating");
        systems::battle::retreat(state, cohort);
    }

    bool start(int funding, int difficulty) {
        state = model::blank_state();
        sim.difficulty = difficulty;
        const int province = systems::campaign::begin_new_game(state, sim.random, funding, difficulty);
        std::printf("new game: funding level %d, difficulty %d, first province %d\n", funding, difficulty, province);
        if (province < 0 || !start_province()) return false;
        sim.messages.post(msg::plain(msg::Id::NoCity));
        return true;
    }

    // apps/viewer/main.cpp start_new_province.
    bool start_province() {
        const int province = gw(state, 0x6CA6);
        char name[16];
        std::snprintf(name, sizeof name, "EMPIRE2.%03d", province);
        fs::path path = fs::path(dir) / name;
        formats::empire2::EmpireMap map;
        try {
            map = formats::empire2::load(path.string());
        } catch (const std::exception& e) {
            std::printf("can't start the new province, %s: %s\n", name, e.what());
            return false;
        }
        systems::month::Random random = sim.random;
        const int difficulty = sim.difficulty;
        systems::campaign::start_province(state, map, random, difficulty, shore_variant);
        model::set_global_word(state, 0x6CB8, difficulty);
        const int speed = sim.speed;
        sim = systems::month::sim_state_from_save(state);
        sim.speed = speed;
        sim.random = random;
        install_hooks();
        std::printf("province %d (%s), funds %d\n", province, name, gw(state, economy::kFunds));
        return true;
    }

    // The viewer's place_tool.
    bool place_tool(CommandId tool, int x, int y) {
        if (!economy::enough_plebs(state, static_cast<int>(tool))) {
            sim.messages.post(msg::plain(msg::Id::ConstructionPlebs));
            ++refusals["too few plebs"];
            return false;
        }
        if (tool == CommandId::Forum && gw(state, 0x6CA0) >= 30) {
            ++refusals["30 forums"];
            return false;
        }
        if (tool == CommandId::Workshop && gw(state, 0x6C9E) >= 30) {
            ++refusals["30 workshops"];
            return false;
        }
        const int cost = economy::construction_cost(tool, forum_grade);
        if (!economy::can_afford(state, cost)) {
            if (economy::grant_emergency_funds(state))
                note("Rome sends 500 Dn in emergency funds");
            ++refusals["not enough funds"];
            return false;
        }
        bool placed = false;
        switch (tool) {
            case CommandId::Road: placed = construction::place_road(state.city, drag, x, y); break;
            case CommandId::ReservoirPipe: placed = construction::place_pipe(state.city, drag, x, y); break;
            case CommandId::Wall: placed = construction::place_wall(state.city, drag, x, y); break;
            case CommandId::Plaza: placed = construction::place_plaza(state.city, x, y); break;
            case CommandId::ClearArea: placed = construction::clear_area(state, sim.random, x, y); break;
            case CommandId::Forum: placed = construction::place_forum(state, forum_grade, x, y); break;
            case CommandId::Workshop: placed = construction::place_workshop(state, workshop_goods, x, y); break;
            default: placed = construction::place(state.city, tool, x, y); break;
        }
        if (placed) {
            economy::charge(state, cost);
        } else {
            ++refusals[std::string("refused: ") + construction::command_name(tool)];
        }
        return placed;
    }

    // The viewer's province_place: one province construction command on a cell.
    bool province_place(int id, int x, int y) {
        namespace province = systems::province;
        if (x < 0 || y < 0 || x >= province::kMapW || y >= province::kMapW) return false;
        if (!economy::enough_plebs(state, id)) {
            ++refusals["province: too few plebs"];
            return false;
        }
        const uint8_t tile = state.empire.cells[static_cast<size_t>(y) * province::kMapW + x] & 0x7F;
        const int cost = economy::kConstructionCost[static_cast<size_t>(id)] << economy::province_cost_shift(id, tile);
        if (!economy::can_afford(state, cost)) {
            if (economy::grant_emergency_funds(state)) note("Rome sends 500 Dn in emergency funds");
            ++refusals["province: not enough funds"];
            return false;
        }
        province::Built built = province::Built::Refused;
        switch (id) {
            case 35: built = province::clear_province(state, x, y); break;
            case 36: built = province::place_province_road(state, province_drag, x, y); break;
            case 37: built = province::place_great_wall(state, province_drag, x, y); break;
            case 41: built = province::place_great_tower(state, x, y); break;
            case 42: built = province::place_highway(state, province_drag, x, y); break;
            default: break;
        }
        if (built == province::Built::Charged) economy::charge(state, cost);
        if (built == province::Built::Refused) ++refusals["province: refused id " + std::to_string(id)];
        return built != province::Built::Refused;
    }

    // One frame of the main loop, with the viewer's bookkeeping around it.
    // Returns false once the game can't go on (dismissed).
    bool frame() {
        const int month_before = sim.month;
        const int msg_timer = sim.messages.timer;
        systems::month::run_frame(state, sim);
        if (sim.messages.timer > msg_timer && sim.messages.current.id != msg::Id::None) {
            std::string t = sim.messages.current.text;
            for (char& c : t)
                if (c == 10) c = ' ';
            while (!t.empty() && t.back() == ' ') t.pop_back();
            if (t.find("entering") != std::string::npos) {
                ++invasions;
                if (std::getenv("PT_INV")) {
                    std::printf("   INVASION at %d/%d:", sim.year, sim.month + 1);
                    for (int i = 0; i < model::kActorCount; ++i) {
                        const model::Actor& a = state.objects[static_cast<size_t>(i)];
                        if (!a.active()) continue;
                        if (a.type() == systems::province::kCohortType)
                            std::printf(" [cohort %d at (%d,%d) state %d men %d/%d]", i, a.screen_x() >> 4, a.screen_y() >> 4, a.state(), a.raw[0x20], a.raw[0x21]);
                        else if (a.type() == systems::province::kArmyType || a.type() == systems::province::kSeaArmyType)
                            std::printf(" [%s %d at (%d,%d) state %d size %d]", a.type() == systems::province::kSeaArmyType ? "SEA army" : "army", i, a.screen_x() >> 4, a.screen_y() >> 4, a.state(), a.raw[0x30]);
                    }
                    std::putchar(10);
                }
            }
            note("message: " + t);
        }
        if (sim.funds_warning) {
            sim.funds_warning = false;
            note("FUNDS WARNING screen");
        }
        if (sim.dismissed) {
            sim.dismissed = false;
            dismissed = true;
            note("DISMISSED: three tributes missed");
            return false;
        }
        (void)month_before;
        return true;
    }

    // Called every few frames: what a player would do the moment something shows up on the province map.
    std::function<void()> frame_hook;
    void* hook_owner = nullptr;
    int hook_every = 4;

    // Runs until the calendar turns a month (or a promotion waits for an answer).
    bool month() {
        const int m = sim.month;
        int guard = 0;
        while (sim.month == m && !promotion_pending) {
            if (!frame()) return false;
            if (frame_hook && guard % hook_every == 0) frame_hook();
            if (++guard > 200000) {
                note("month never ended (200000 frames)");
                return false;
            }
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
// What the player sees.

bool grass(const model::CityState& s, int x, int y) {
    return x >= 0 && y >= 0 && x < model::kCityW && y < model::kCityH &&
           construction::is_buildable_terrain(s.city.tile[y][x]);
}

struct Census {
    int houses_cells = 0, roads = 0, forums = 0, workshops = 0, reservoirs = 0, markets = 0, oracles = 0, temples = 0,
        baths = 0, schools = 0, theaters = 0, rubble = 0, fire = 0;
    std::array<int, 16> grade{};
    int unrest_max = -999;
    int unrest_over_20 = 0;
};

Census census(const model::CityState& s) {
    Census c;
    for (int y = 0; y < model::kCityH; ++y) {
        for (int x = 0; x < model::kCityW; ++x) {
            const uint8_t t = s.city.tile[y][x];
            if (t >= 0x36 && t <= 0x43) ++c.roads;
            else if (t >= 0xC8 && t <= 0xD7) {
                ++c.houses_cells;
                ++c.grade[t - 0xC8];
                c.unrest_max = std::max(c.unrest_max, static_cast<int>(static_cast<int8_t>(s.city.unrest[y][x])));
                if (static_cast<int8_t>(s.city.unrest[y][x]) > 20) ++c.unrest_over_20;
            } else if (t >= 0xE0 && t <= 0xE7) ++c.forums;
            else if (t == 0xF5 || t == 0xF6) ++c.workshops;
            else if (t == 0xA4) ++c.reservoirs;
            else if (t == 0xF4) ++c.markets;
            else if (t == 0xEB) ++c.oracles;
            else if (t >= 0xD8 && t <= 0xDF) ++c.temples;
            else if (t == 0xE8 || t == 0xEA) ++c.baths;
            else if (t == 0xEC || t == 0xED) ++c.schools;
            else if (t >= 0xF0 && t <= 0xF2) ++c.theaters;
            else if (t == 0xA7 || t == 0xAA || t == 0xAD || t == 0xB0) ++c.rubble;
            else if (t == 0xA8 || t == 0xAB || t == 0xAE || t == 0xB1) ++c.fire;
        }
    }
    c.forums /= 4;  // cells, roughly (2x2 grade 0)
    c.workshops /= 9;
    c.markets /= 4;
    c.oracles /= 2;
    c.schools /= 4;
    c.theaters /= 2;
    return c;
}

void print_province_status(const Game& g) {
    const auto& s = g.state;
    int roads = 0, highway = 0, towns[4] = {0, 0, 0, 0};
    for (uint8_t c : s.empire.cells) {
        const int t = c & 0x7F;
        if (t >= 0x36 && t <= 0x41) ++roads;
        if (t >= 0x6D && t <= 0x78) ++highway;
        if (t == 0x61) ++towns[0];
        if (t == 0x79) ++towns[1];
        if (t == 0x7A) ++towns[2];
        if (t == 0x4C) ++towns[3];
    }
    std::printf("      province: road cells %d, highway cells %d, towns small %d/%d/%d/%d (61/79/7A/4C) | road score 0x6C86=%d "
                "highway linked 0x6C8C=%d linked towns %d | plebs on construction %d need %d\n",
                roads, highway, towns[0], towns[1], towns[2], towns[3], gw(s, 0x6C86), gw(s, 0x6C8C), g.sim.linked_towns,
                gw(s, 0x6C5C), gw(s, 0x6C3E));
}

void print_accounts(const model::CityState& s) {
    std::printf("      last year: pop tax %d, industrial tax %d, construction %d, operating %d, tribute %d, profit %d | "
                "tax rates %d/%d, per head %d.%02d\n",
                gw(s, 0x6BB2), gw(s, 0x6BB0), gw(s, 0x6BAE), gw(s, 0x6BAC), gw(s, 0x6BAA), gw(s, 0x6BB4), gw(s, 0x6C04),
                gw(s, 0x6C02), gw(s, 0x6BCA), gw(s, 0x6BC8));
}

// What holds the houses back: for each grade, the land value spread and how many have each service bit.
void print_house_detail(const model::CityState& s) {
    struct Acc {
        int n = 0, lv_min = 127, lv_max = -128, lv_sum = 0, water = 0, net = 0, market = 0, bath = 0, school = 0, ent = 0, tax = 0;
    };
    std::array<Acc, 16> acc{};
    for (int y = 0; y < model::kCityH; ++y)
        for (int x = 0; x < model::kCityW; ++x) {
            const uint8_t t = s.city.tile[y][x];
            if (t < 0xC8 || t > 0xD7 || (s.city.operational_state[y][x] & 0x0F) != 0) continue;  // anchors only
            Acc& a = acc[static_cast<size_t>(t - 0xC8)];
            const int lv = static_cast<int8_t>(s.city.land_value[y][x]);
            const uint8_t f = s.city.service_flags[y][x];
            ++a.n;
            a.lv_min = std::min(a.lv_min, lv);
            a.lv_max = std::max(a.lv_max, lv);
            a.lv_sum += lv;
            a.water += (f & 1) != 0;
            a.net += (f & 2) != 0;
            a.bath += (f & 4) != 0;
            a.market += (f & 8) != 0;
            a.tax += (f & 0x20) != 0;
            a.school += (f & 0x40) != 0;
            a.ent += (f & 0x80) != 0;
        }
    for (int i = 0; i < 16; ++i) {
        const Acc& a = acc[static_cast<size_t>(i)];
        if (!a.n) continue;
        std::printf("      %X x%-4d land value %d..%d avg %.1f | water %d net %d market %d bath %d school %d ent %d taxed %d\n",
                    0xC8 + i, a.n, a.lv_min, a.lv_max, static_cast<double>(a.lv_sum) / a.n, a.water, a.net, a.market,
                    a.bath, a.school, a.ent, a.tax);
    }
}

// A save at a milestone, for the viewer: PT_SAVE_DIR names the folder.
void save_milestone(const Game& g, const std::string& name) {
    const char* dir = std::getenv("PT_SAVE_DIR");
    if (!dir) return;
    std::error_code ec;
    fs::create_directories(dir, ec);
    try {
        formats::save::write(model::serialize(g.state), (fs::path(dir) / name).string());
        std::printf("   [saved %s]\n", name.c_str());
    } catch (const std::exception& e) {
        std::printf("   [could not save %s: %s]\n", name.c_str(), e.what());
    }
}

void print_status(const Game& g, const char* tag) {
    const auto& s = g.state;
    const Census c = census(s);
    if (std::getenv("PT_HOUSES")) print_house_detail(s);
    if (std::getenv("PT_GRADES")) {
        std::printf("      grades:");
        for (int i = 0; i < 16; ++i)
            if (c.grade[static_cast<size_t>(i)]) std::printf(" %X:%d", 0xC8 + i, c.grade[static_cast<size_t>(i)]);
        std::printf(" | unrest>20: %d reservoirs %d markets %d oracles %d rubble %d fire %d\n", c.unrest_over_20,
                    c.reservoirs, c.markets, c.oracles, c.rubble, c.fire);
    }
    std::printf(
        "%s year %d/%d rank %d | pop %d (units %d) funds %d | ratings P%d C%d Pr%d E%d avg %d | houses %d cells "
        "roads %d forums %d wksp %d | plebs %d | unrest max %d\n",
        tag, g.sim.year, g.sim.month + 1, gw(s, 0x6C30), gw(s, 0x6C0E), gw(s, 0x6C10), gw(s, economy::kFunds),
        gw(s, admin::kPeace), gw(s, admin::kCulture), gw(s, admin::kProsperity), gw(s, admin::kEmpire),
        gw(s, admin::kAverage), c.houses_cells, c.roads, c.forums, c.workshops, gw(s, 0x6C56), c.unrest_max);
}

char city_char(const model::CityState& s, int x, int y) {
    const uint8_t t = s.city.tile[y][x];
    if (t == 0) return '~';
    if (t < 0x1D) return ',';
    if (t <= 0x35) return '.';
    if (t >= 0x36 && t <= 0x43) return '#';
    if (t >= 0x44 && t < 0x5E) return '=';
    if (t == 0xA4 || t == 0xB8) return 'w';
    if (t >= 0xC8 && t <= 0xCB) return 'h';
    if (t >= 0xCC && t <= 0xD7) return 'H';
    if (t >= 0xE0 && t <= 0xE7) return 'F';
    if (t == 0xF4) return 'M';
    if (t == 0xEB) return 'O';
    if (t == 0xA7 || t == 0xAA || t == 0xAD || t == 0xB0) return 'x';
    if (t == 0xA8 || t == 0xAB || t == 0xAE || t == 0xB1) return '*';
    return 'B';
}

void print_city(const model::CityState& s, int x0 = 0, int y0 = 0, int x1 = model::kCityW, int y1 = model::kCityH) {
    for (int y = std::max(0, y0); y < std::min(model::kCityH, y1); ++y) {
        for (int x = std::max(0, x0); x < std::min(model::kCityW, x1); ++x) std::putchar(city_char(s, x, y));
        std::putchar(10);
    }
}

void print_workshops(const model::CityState& st) {
    const auto& t = st.table_720;
    auto word = [&](size_t o) { return static_cast<int16_t>(t[o] | (t[o + 1] << 8)); };
    std::printf("workshops: prospects(0x6BF4)=%d pressure(0x6BFC)=%d empire=%d units=%d", gw(st, 0x6BF4), gw(st, 0x6BFC),
                gw(st, admin::kEmpire), gw(st, 0x6C10));
    std::putchar(10);
    for (int i = 0; i < 30; ++i) {
        const size_t r = static_cast<size_t>(i) * 24;
        if (r + 24 > t.size() || word(r + 8) == 0) continue;
        std::printf("   wks %2d at (%d,%d) goods %d base %d | window pop %d sales %d industry %d | level %d", i, word(r), word(r + 2),
                    word(r + 4), systems::actors::workshop_base(gw(st, 0x6CA6), word(r + 4)), word(r + 0x0C), word(r + 0x0E),
                    word(r + 0x14), word(r + 0x10));
        std::putchar(10);
    }
}

void print_province(const model::CityState& s) {
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x) {
            const uint8_t v = s.empire.cells[static_cast<size_t>(y) * 40 + x] & 0x7F;
            char c = '.';
            if (v == 0x00) c = ' ';
            else if (v == 0x4A || v == 0x4B) c = 'C';
            else if (v == 0x4C) c = 'T';
            else if (v == 0x61 || v == 0x79 || v == 0x7A) c = 't';
            else if (v == 0x78) c = 'H';
            else if (v >= 0x4E && v <= 0x50) c = '~';
            else if (v >= 0x51 && v <= 0x60) c = '^';
            else if (v >= 0x36 && v <= 0x41) c = '#';
            else if (v >= 0x6D && v <= 0x77) c = '=';
            std::putchar(c);
        }
        std::putchar(10);
    }
}

// ---------------------------------------------------------------------------
// The bot.

struct Block {
    int y0, h;
    char type;  // 'H' housing, 'I' industry
    int k;      // 0 the forum's own rows, positive south, negative north
};

struct Bot {
    Game& g;
    model::CityState& s;
    int fx = -1, fy = -1;  // the first forum's anchor
    int max_RW = 46;
    int RW = std::getenv("PT_RW") ? std::atoi(std::getenv("PT_RW")) : 22;  // half-width of the plan
    std::vector<Block> blocks;
    std::vector<int> road_rows;
    std::vector<std::vector<char>> role;  // r road, h house, w reservoir, M market anchor, W workshop anchor, k reserved
    std::vector<Pt> house_queue, workshop_queue, market_queue, reservoir_queue, forum_queue, oracle_queue;
    std::vector<Pt> culture_cells, prefecture_queue;
    std::vector<std::vector<char>> culture_region;
    size_t house_next = 0, workshop_next = 0, forum_next = 0;
    int stage = 0;
    int first_year = 0;
    int reach = std::getenv("PT_EAGER_ROADS") ? 9 : 100;  // the built-up area's reach from the forum
    int oracles = 0, forums_extra = 0;
    int built_houses = 0, built_workshops = 0;
    std::map<std::string, int> spent;

    explicit Bot(Game& game) : g(game), s(game.state) {
        // the Legion is watched all month long
        g.frame_hook = [this] { military(); };
        g.hook_owner = this;
    }
    ~Bot() {
        if (g.hook_owner == this) g.frame_hook = nullptr;
    }

    int funds() const { return gw(s, economy::kFunds); }
    bool within(int x, int y, int r) const { return std::abs(x - fx) <= r && std::abs(y - fy) <= r; }
    bool is_road(int x, int y) const {
        const uint8_t t = s.city.tile[y][x];
        return t >= 0x36 && t <= 0x43;
    }

    bool choose_site() {
        int best = -1, bx = 0, by = 0;
        for (int y = 16; y < model::kCityH - 16; ++y) {
            for (int x = 16; x < model::kCityW - 16; ++x) {
                if (!grass(s, x, y) || !grass(s, x + 1, y) || !grass(s, x, y + 1) || !grass(s, x + 1, y + 1)) continue;
                int open = 0;
                for (int dy = -15; dy <= 15; ++dy)
                    for (int dx = -15; dx <= 15; ++dx) open += grass(s, x + dx, y + dy);
                const int dist = std::abs(x - 50) + std::abs(y - 50);
                const int score = open * 4 - dist;
                if (score > best) {
                    best = score;
                    bx = x;
                    by = y;
                }
            }
        }
        if (best < 0) return false;
        fx = bx;
        fy = by;
        std::printf("site: forum at (%d,%d), open ground score %d\n", fx, fy, best);
        return true;
    }

    // Which strips carry the workshops. A workshop holds land value to 3 within three cells of itself (0x2C4F5), so the houses
    // beside one stay tenements; PT_IND=0 keeps the old pattern (every third strip), otherwise only the second strip each side.
    // Rows in a housing strip: 2 fits pairs and 2 x 2 houses; 3 lets them grow into the 3 x 3 top grades.
    static int strip_rows() {
        static const int rows = std::getenv("PT_STRIP") ? std::atoi(std::getenv("PT_STRIP")) : 2;
        return rows;
    }
    // Strips from this one outward are the culture ground: no roads, the buildings that need none (oracles, hippodromes, schools).
    static int culture_from() {
        static const int k = std::getenv("PT_CULTURE_FROM") ? std::atoi(std::getenv("PT_CULTURE_FROM")) : 8;
        return k;
    }
    static bool industry_strip(int k) {
        static const int mode = std::getenv("PT_IND") ? std::atoi(std::getenv("PT_IND")) : 2;
        if (mode == 0) return k % 3 == 2;
        if (mode == 1) return k == 2;
        if (mode == 2) return k == 2 || k == 6;
        if (mode == 3) return k == 4 || k == 8;
        if (mode == 4) return k == 5 || k == 9;
        return k == 6 || k == 10;
    }

    // Rows of blocks outward from the forum's own: two houses rows, a road, then the pattern H H I H H I ...
    // (an industry block is three rows tall, for the 3 x 3 workshops).
    void plan() {
        blocks.clear();
        road_rows.clear();
        blocks.push_back({fy, 2, 'H', 0});
        road_rows.push_back(fy - 1);
        road_rows.push_back(fy + 2);
        int cur = fy + 2;
        const int last_housing = culture_from() - 1;
        for (int k = 1; k < 20; ++k) {
            const char type = k > last_housing ? 'C' : industry_strip(k) ? 'I' : 'H';
            const int h = type == 'I' ? 3 : type == 'C' ? 2 : strip_rows();
            const int y0 = type == 'C' && k > culture_from() ? cur : cur + 1;
            if (y0 + h >= model::kCityH) break;
            blocks.push_back({y0, h, type, k});
            cur = y0 + h;
            if (type != 'C') road_rows.push_back(cur);
        }
        cur = fy - 1;
        for (int k = 1; k < 20; ++k) {
            const char type = k > last_housing ? 'C' : industry_strip(k) ? 'I' : 'H';
            const int h = type == 'I' ? 3 : type == 'C' ? 2 : strip_rows();
            const int y0 = type == 'C' && k > culture_from() ? cur - h + 1 : cur - h;
            if (y0 < 1) break;
            blocks.push_back({y0, h, type, -k});
            cur = y0 - 1;
            if (type != 'C') road_rows.push_back(cur);
        }
        std::sort(blocks.begin(), blocks.end(), [](const Block& a, const Block& b) { return a.y0 < b.y0; });

        role.assign(model::kCityH, std::vector<char>(model::kCityW, 0));
        const int x_lo = std::max(0, fx - RW), x_hi = std::min(model::kCityW - 1, fx + RW);
        plan_x_lo = x_lo;
        plan_x_hi = x_hi;
        auto spine = [&](int x) { return is_spine(x); };
        int y_lo = model::kCityH, y_hi = 0;
        for (int r : road_rows) {
            y_lo = std::min(y_lo, r);
            y_hi = std::max(y_hi, r);
        }
        for (int r : road_rows)
            for (int x = x_lo; x <= x_hi; ++x)
                if (grass(s, x, r)) role[r][x] = 'r';
        for (const Block& b : blocks) {
            for (int y = b.y0; y < b.y0 + b.h; ++y)
                for (int x = x_lo; x <= x_hi; ++x) {
                    if (!grass(s, x, y)) continue;
                    if (spine(x)) role[y][x] = 'r';
                    else if (b.type == 'H') role[y][x] = 'h';
                }
        }
        for (int y = y_lo; y <= y_hi; ++y)
            for (int x = x_lo; x <= x_hi; ++x)
                if (spine(x) && grass(s, x, y) && role[y][x] == 0) role[y][x] = 'r';
        for (int y = fy; y <= fy + 1; ++y)
            for (int x = fx; x <= fx + 1; ++x) role[y][x] = 'F';

        for (const Block& b : blocks) {
            if (b.type == 'H') {
                // wells along the bottom row, every 3 columns, markets in odd blocks every 12
                const int ry = b.y0 + b.h / 2;
                for (int x = x_lo; x <= x_hi; ++x) {
                    static const int step = std::getenv("PT_WELL_STEP") ? std::atoi(std::getenv("PT_WELL_STEP")) : 3;
                    const int dx = ((x - (fx + 3 * (std::abs(b.k) & 1))) % step + step) % step;
                    if (dx == 0 && role[ry][x] == 'h') role[ry][x] = 'w';
                }
                if ((std::abs(b.k) & 1) == 1) {
                    for (int x = x_lo; x + 1 <= x_hi; ++x) {
                        const int dx = ((x - (fx + 5 + 3 * (std::abs(b.k) % 4))) % 12 + 12) % 12;
                        if (dx != 0) continue;
                        bool ok = true;
                        for (int dy = 0; dy < 2; ++dy)
                            for (int ddx = 0; ddx < 2; ++ddx) ok = ok && role[b.y0 + dy][x + ddx] == 'h';
                        if (!ok) continue;
                        for (int dy = 0; dy < 2; ++dy)
                            for (int ddx = 0; ddx < 2; ++ddx) role[b.y0 + dy][x + ddx] = 'k';
                        role[b.y0][x] = 'M';
                    }
                }
            } else {
                // industry: carve 3-wide slots out of each run of open columns
                int slot = 0;
                int x = x_lo;
                while (x <= x_hi) {
                    auto open3 = [&](int xx) {
                        for (int dy = 0; dy < 3; ++dy)
                            for (int ddx = 0; ddx < 3; ++ddx)
                                if (xx + ddx > x_hi || !grass(s, xx + ddx, b.y0 + dy) || spine(xx + ddx)) return false;
                        return true;
                    };
                    if (open3(x)) {
                        if (slot % 6 == 5) {
                            role[b.y0][x] = 'M';
                            role[b.y0][x + 1] = role[b.y0 + 1][x] = role[b.y0 + 1][x + 1] = 'k';
                        } else {
                            for (int dy = 0; dy < 3; ++dy)
                                for (int ddx = 0; ddx < 3; ++ddx) role[b.y0 + dy][x + ddx] = 'k';
                            role[b.y0][x] = 'W';
                        }
                        ++slot;
                        x += 3;
                    } else {
                        ++x;
                    }
                }
            }
        }
        // the culture ground, nearest strips first
        culture_cells.clear();
        culture_region.assign(model::kCityH, std::vector<char>(model::kCityW, 0));
        for (const Block& b : blocks) {
            if (b.type != 'C') continue;
            for (int y = b.y0; y < b.y0 + b.h; ++y)
                for (int x = x_lo; x <= x_hi; ++x)
                    if (grass(s, x, y)) {
                        culture_region[y][x] = 1;
                        culture_cells.push_back({x, y});
                    }
        }
        std::sort(culture_cells.begin(), culture_cells.end(), [&](const Pt& a, const Pt& b) {
            const int ka = std::abs(a.y - fy) * 100 + std::abs(a.x - fx), kb = std::abs(b.y - fy) * 100 + std::abs(b.x - fx);
            return ka < kb;
        });
        // prefectures (one cell) in the housing strips, every eight columns: each holds the unrest down within three cells (-2 a
        // month), collects the tax within four, and is what lets the conscription rate rise
        prefecture_queue.clear();
        for (const Block& b : blocks) {
            if (b.type != 'H') continue;
            const int kk = std::abs(b.k);
            for (int x = x_lo; x <= x_hi; ++x) {
                if ((((x - (fx + 1 + (kk * 3) % 8)) % 8) + 8) % 8 != 0) continue;
                const int y = b.y0 + (kk % 2);
                if (role[y][x] != 'h') continue;
                role[y][x] = 'p';
                prefecture_queue.push_back({x, y});
            }
        }
        std::sort(prefecture_queue.begin(), prefecture_queue.end(), [&](const Pt& a, const Pt& b) {
            return std::abs(a.x - fx) + 3 * std::abs(a.y - fy) < std::abs(b.x - fx) + 3 * std::abs(b.y - fy);
        });
        // forums (2 x 2) in the housing strips, every 22 columns and staggered strip by strip: their walkers carry the network
        // flag along the road rows beside them, their radius collects the tax, and each is 30 jobs for 60 Dn
        for (const Block& b : blocks) {
            if (b.type != 'H') continue;
            const int kk = std::abs(b.k);
            for (int x = x_lo; x + 1 <= x_hi; ++x) {
                if ((((x - (fx + 3 + (kk * 5) % 12)) % 12) + 12) % 12 != 0) continue;
                bool ok = true;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) ok = ok && role[b.y0 + dy][x + dx] == 'h';
                if (!ok) continue;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) role[b.y0 + dy][x + dx] = 'u';
                role[b.y0][x] = 'U';
            }
        }
        // queues
        house_queue.clear();
        oracle_queue.clear();
        workshop_queue.clear();
        market_queue.clear();
        reservoir_queue.clear();
        forum_queue.clear();
        for (int y = 0; y < model::kCityH; ++y)
            for (int x = 0; x < model::kCityW; ++x) {
                if (role[y][x] == 'h') house_queue.push_back({x, y});
                else if (role[y][x] == 'W') workshop_queue.push_back({x, y});
                else if (role[y][x] == 'M') market_queue.push_back({x, y});
                else if (role[y][x] == 'w') reservoir_queue.push_back({x, y});
                else if (role[y][x] == 'U') forum_queue.push_back({x, y});

            }
        // The city grows outward in a wide oval, rows a third as far apart as columns are long: a new strip means a whole
        // new road row, so the strips already open are filled first.
        static const int row_weight = std::getenv("PT_ROWW") ? std::atoi(std::getenv("PT_ROWW")) : 0;
        std::vector<int>& strip_of = strip_rank;
        strip_of.assign(model::kCityH, 99);
        for (const Block& b : blocks)
            for (int y = b.y0; y < b.y0 + b.h; ++y) strip_of[static_cast<size_t>(y)] = std::abs(b.k);
        auto key = [&](const Pt& a) {
            const int dx = std::abs(a.x - fx), dy = std::abs(a.y - fy) * (row_weight ? row_weight : 3);
            if (row_weight == 0) return strip_of[static_cast<size_t>(a.y)] * 100000 + dx * 10 + (a.y < fy ? 1 : 0);  // a strip at a time
            return std::max(dx, dy) * 1000 + dx + dy;
        };
        auto by_dist = [&](const Pt& a, const Pt& b) { return key(a) < key(b); };
        std::sort(house_queue.begin(), house_queue.end(), by_dist);
        std::sort(workshop_queue.begin(), workshop_queue.end(), by_dist);
        std::sort(market_queue.begin(), market_queue.end(), by_dist);
        std::sort(reservoir_queue.begin(), reservoir_queue.end(), by_dist);
        std::sort(forum_queue.begin(), forum_queue.end(), by_dist);
        std::printf("plan: %zu house cells, %zu workshop slots, %zu markets, %zu reservoirs, %zu culture cells, %zu blocks\n",
                    house_queue.size(), workshop_queue.size(), market_queue.size(), reservoir_queue.size(),
                    culture_cells.size(), blocks.size());
    }

    // ---- placement helpers, paid for the way a player pays ----
    int plan_x_lo = 0, plan_x_hi = 0;
    std::vector<int> strip_rank;  // |k| of the strip each row belongs to
    int houses_strip_max = 0;     // the farthest strip with a house in it
    bool strip_open(int y) const { return strip_rank[static_cast<size_t>(y)] <= houses_strip_max + 1; }
    const char* road_label = "?";
    bool lazy_roads = std::getenv("PT_EAGER_ROADS") == nullptr;
    bool is_spine(int x) const { return ((x - (fx - 1)) % 12 + 12) % 12 == 0 || x == fx + 2; }

    void lay_road(int cx, int cy) {
        if (cx < 0 || cy < 0 || cx >= model::kCityW || cy >= model::kCityH) return;
        if (role[cy][cx] != 'r' || is_road(cx, cy)) return;
        const int before = funds();
        if (g.place_tool(CommandId::Road, cx, cy)) spent[std::string("roads for ") + road_label] += before - funds();
    }

    // Roads laid just ahead of the buildings, as a tree: the trunk is the spine beside the forum, each strip's road rows grow out
    // from it as far as the buildings do, and nothing is laid where nobody builds. A house needs the road row beside its own
    // row (the walkers' flag reaches one cell), a forum or a workshop a road beside it for its walkers, a market none at all.
    // `w` is the building's width.
    void roads_for(int x, int y, int w, CommandId tool) {
        if (tool == CommandId::Market) return;
        const Block* bk = nullptr;
        for (const Block& b : blocks)
            if (y >= b.y0 && y < b.y0 + b.h) {
                bk = &b;
                break;
            }
        if (!bk || bk->type == 'C') return;
        const int trunk = fx + 2;
        const int above = bk->y0 - 1, below = bk->y0 + bk->h;
        int row = above;
        if (tool == CommandId::Housing) {
            row = (y - bk->y0) * 2 >= bk->h ? below : above;
        } else if (tool == CommandId::Forum || tool == CommandId::Workshop) {
            // the side that already has road under it, else the one nearer the forum's own rows
            auto built = [&](int r) {
                for (int cx = x; cx < x + w; ++cx)
                    if (r >= 0 && r < model::kCityH && is_road(cx, r)) return true;
                return false;
            };
            if (built(above)) row = above;
            else if (built(below)) row = below;
            else row = bk->y0 > fy ? above : below;
        }
        const int lo = std::min(trunk, x - 1), hi = std::max(trunk, x + w);
        for (int cx = lo; cx <= hi; ++cx) lay_road(cx, row);
        // the trunk, from the forum's own rows to this row
        if (row > fy + 1) {
            for (int cy = fy + 2; cy <= row; ++cy) lay_road(trunk, cy);
        } else if (row < fy) {
            for (int cy = row; cy <= fy - 1; ++cy) lay_road(trunk, cy);
        }
        // and the forum's own rows out to the trunk
        for (int cx = std::min(fx, trunk); cx <= std::max(fx, trunk); ++cx) {
            lay_road(cx, fy - 1);
            lay_road(cx, fy + 2);
        }
    }

    // Wells (water to the cells next to them) along the strip's bottom row, every third column. A reservoir only goes on
    // water and pipes carry its water to fountains, so a city's own water is wells unless a lake is at hand.
    void water_for(int x, int y) {
        const Block* bk = nullptr;
        for (const Block& b : blocks)
            if (y >= b.y0 && y < b.y0 + b.h) {
                bk = &b;
                break;
            }
        if (!bk || bk->type != 'H') return;
        const int ry = bk->y0 + bk->h / 2;  // the middle row of a three-row strip, the lower of two
        for (int cx = std::max(0, x - 6); cx <= std::min(model::kCityW - 1, x + 6); ++cx) {
            if (role[ry][cx] != 'w' || s.city.tile[ry][cx] == 0xB8 || !grass(s, cx, ry)) continue;
            const int before = funds();
            if (g.place_tool(CommandId::Well, cx, ry)) spent["wells"] += before - funds();
        }
    }

    bool put(CommandId tool, int x, int y, const char* what) {
        if (lazy_roads && tool != CommandId::Road && tool != CommandId::Well && tool != CommandId::Plaza &&
            tool != CommandId::Oracle && tool != CommandId::Hippodrome && tool != CommandId::School && tool != CommandId::Theater &&
            tool != CommandId::Coliseum) {
            road_label = what;
            roads_for(x, y, std::max(1, tool == CommandId::Forum ? 2 : construction::placement_spec(tool).width), tool);
            if (tool == CommandId::Housing) water_for(x, y);
        }
        const int before = funds();
        const bool ok = g.place_tool(tool, x, y);
        if (ok) {
            spent[what] += before - funds();
            if (tool == CommandId::Market || tool == CommandId::Forum || tool == CommandId::Workshop || tool == CommandId::Housing)
                houses_strip_max = std::max(houses_strip_max, strip_rank[static_cast<size_t>(y)]);
        }
        return ok;
    }

    int roads_in(int r) {
        int placed = 0;
        for (int y = std::max(0, fy - r); y <= std::min(model::kCityH - 1, fy + r); ++y)
            for (int x = std::max(0, fx - r); x <= std::min(model::kCityW - 1, fx + r); ++x) {
                if (role[y][x] != 'r' || is_road(x, y)) continue;
                if (put(CommandId::Road, x, y, "roads")) ++placed;
            }
        // columns, so a vertical run is dragged top to bottom
        return placed;
    }

    // Plazas: a road with the 0x10 flag gives land value to its neighbours (a plain road gives none).
    int plazas_in(int r) {
        int placed = 0;
        for (int y = std::max(0, fy - r); y <= std::min(model::kCityH - 1, fy + r); ++y)
            for (int x = std::max(0, fx - r); x <= std::min(model::kCityW - 1, fx + r); ++x) {
                if (role[y][x] != 'r' || !is_road(x, y) || (s.city.operational_state[y][x] & 0x10)) continue;
                if (put(CommandId::Plaza, x, y, "plazas")) ++placed;
            }
        return placed;
    }

    int reservoirs_in(int r) {
        int n = 0;
        for (const Pt& p : reservoir_queue)
            if (within(p.x, p.y, r) && s.city.tile[p.y][p.x] != 0xB8 && grass(s, p.x, p.y))
                n += put(CommandId::Well, p.x, p.y, "wells");
        return n;
    }

    int markets_in(int r) {
        int n = 0;
        for (const Pt& p : market_queue)
            if (within(p.x, p.y, r) && grass(s, p.x, p.y) && construction::can_place(s.city, CommandId::Market, p.x, p.y))
                n += put(CommandId::Market, p.x, p.y, "markets");
        return n;
    }

    // goods in the order the province suits them
    std::vector<int> goods_order() {
        auto rep = systems::forum::industry_report(s);
        std::vector<int> order = {0, 1, 2, 3, 4, 5, 6, 7};
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return rep.rows[static_cast<size_t>(a)].suitability > rep.rows[static_cast<size_t>(b)].suitability;
        });
        return order;
    }

    int workshops(int count, int r) {
        int n = 0;
        const auto order = goods_order();
        while (n < count && workshop_next < workshop_queue.size()) {
            const Pt p = workshop_queue[workshop_next];
            if (!within(p.x, p.y, r) || !strip_open(p.y)) break;  // by distance: the rest is farther
            ++workshop_next;
            // spread over the three best goods, round robin
            g.workshop_goods = order[static_cast<size_t>(built_workshops % 3)];
            if (put(CommandId::Workshop, p.x, p.y, "workshops")) {
                ++n;
                ++built_workshops;
            }
        }
        return n;
    }

    int houses(int count, int) {
        int n = 0;
        size_t i = 0;
        for (; i < house_queue.size() && n < count; ++i) {
            const Pt p = house_queue[i];
            if (!grass(s, p.x, p.y)) continue;
            if (put(CommandId::Housing, p.x, p.y, "houses")) {
                ++n;
                ++built_houses;
                houses_strip_max = std::max(houses_strip_max, strip_rank[static_cast<size_t>(p.y)]);
            } else {
                break;  // no money, no plebs: the next ones would fail too
            }
        }
        return n;
    }
    bool house_cell_free() const {
        for (const Pt& p : house_queue)
            if (grass(s, p.x, p.y)) return true;
        return false;
    }

    // What fire, collapse and invaders took: rubble is cleared for a Dn a cell, and the roads laid before are laid again.
    std::vector<std::vector<char>> road_was;
    int repairs_done = 0;
    void repair_city() {
        if (road_was.empty()) road_was.assign(model::kCityH, std::vector<char>(model::kCityW, 0));
        int budget = 60;
        for (int y = 0; y < model::kCityH && budget > 0; ++y)
            for (int x = 0; x < model::kCityW && budget > 0; ++x) {
                const uint8_t t = s.city.tile[y][x];
                if (t >= 0x36 && t <= 0x43) {
                    road_was[static_cast<size_t>(y)][static_cast<size_t>(x)] = 1;
                } else if ((t == 0xA7 || t == 0xAA || t == 0xAD || t == 0xB0) && funds() > 50) {
                    if (g.place_tool(CommandId::ClearArea, x, y)) {
                        --budget;
                        ++repairs_done;
                    }
                } else if (road_was[static_cast<size_t>(y)][static_cast<size_t>(x)] && t == 0x1D && funds() > 100) {
                    // a road piece that wore away or was torn up: the plan's road, if the cell is one
                    if (role[y][x] == 'r') {
                        const int before = funds();
                        if (g.place_tool(CommandId::Road, x, y)) {
                            spent["roads repaired"] += before - funds();
                            --budget;
                            ++repairs_done;
                        }
                    }
                }
            }
    }

    // ---- the province ----
    uint8_t ptile(int x, int y) const { return s.empire.cells[static_cast<size_t>(y) * 40 + x] & 0x7F; }
    static bool in_map(int x, int y) { return x >= 0 && y >= 0 && x < 40 && y < 40; }
    bool road_land(int x, int y) const {
        const uint8_t t = ptile(x, y);
        return (t >= 0x1D && t <= 0x35) || (t >= 0x36 && t <= 0x41);
    }
    bool highway_land(int x, int y) const {
        const uint8_t t = ptile(x, y);
        return (t >= 0x1D && t <= 0x35) || (t >= 0x6D && t <= 0x77);
    }

    // Cheapest straight-ish path over land from cells next to a `from` tile to cells next to a `to` tile.
    std::vector<Pt> province_path(const std::vector<uint8_t>& from, const std::vector<uint8_t>& to, bool highway,
                                  const std::vector<Pt>& skip_towns = {}) const {
        auto is_in = [](const std::vector<uint8_t>& set, uint8_t t) { return std::find(set.begin(), set.end(), t) != set.end(); };
        auto passable = [&](int x, int y) { return highway ? highway_land(x, y) : road_land(x, y); };
        static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
        auto next_to = [&](int x, int y, const std::vector<uint8_t>& set, bool is_goal) {
            for (int d = 0; d < 4; ++d) {
                const int nx = x + dx[d], ny = y + dy[d];
                if (!in_map(nx, ny) || !is_in(set, ptile(nx, ny))) continue;
                if (is_goal) {
                    bool skipped = false;
                    for (const Pt& t : skip_towns) skipped = skipped || (t.x == nx && t.y == ny);
                    if (skipped) continue;
                }
                return true;
            }
            return false;
        };
        struct Node {
            int cost, x, y, dir;
            bool operator>(const Node& o) const { return cost > o.cost; }
        };
        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
        std::vector<int> best(40 * 40 * 5, 1 << 30);
        std::vector<int> prev(40 * 40 * 5, -1);
        auto id = [](int x, int y, int d) { return (y * 40 + x) * 5 + d; };
        auto step_cost = [&](int x, int y) {
            const uint8_t t = ptile(x, y);
            if (t >= 0x36 && t <= 0x41) return 1;
            if (t >= 0x6D && t <= 0x77) return 1;
            return 4 << economy::province_cost_shift(highway ? 42 : 36, t);
        };
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 40; ++x)
                if (passable(x, y) && next_to(x, y, from, false)) {
                    best[id(x, y, 4)] = step_cost(x, y);
                    open.push({step_cost(x, y), x, y, 4});
                }
        while (!open.empty()) {
            const Node n = open.top();
            open.pop();
            if (n.cost > best[id(n.x, n.y, n.dir)]) continue;
            if (next_to(n.x, n.y, to, true)) {
                std::vector<Pt> path;
                int cur = id(n.x, n.y, n.dir);
                while (cur >= 0) {
                    const int cell = cur / 5;
                    path.push_back({cell % 40, cell / 40});
                    cur = prev[static_cast<size_t>(cur)];
                }
                std::reverse(path.begin(), path.end());
                return path;
            }
            for (int d = 0; d < 4; ++d) {
                const int nx = n.x + dx[d], ny = n.y + dy[d];
                if (!in_map(nx, ny) || !passable(nx, ny)) continue;
                const int c = n.cost + step_cost(nx, ny) + ((n.dir != 4 && n.dir != d) ? 6 : 0);
                if (c < best[id(nx, ny, d)]) {
                    best[id(nx, ny, d)] = c;
                    prev[static_cast<size_t>(id(nx, ny, d))] = id(n.x, n.y, n.dir);
                    open.push({c, nx, ny, d});
                }
            }
        }
        return {};
    }

    int path_cost(const std::vector<Pt>& path, int command) const {
        int total = 0;
        for (const Pt& p : path) {
            const uint8_t t = ptile(p.x, p.y);
            if ((t >= 0x36 && t <= 0x41) || (t >= 0x6D && t <= 0x77)) continue;
            total += economy::kConstructionCost[static_cast<size_t>(command)] << economy::province_cost_shift(command, t);
        }
        return total;
    }

    struct Route {
        int command;
        std::vector<Pt> cells;
    };
    std::vector<Route> routes;
    bool highway_done = false;
    std::vector<Pt> town_done;

    // Province road, wall and highway pieces as the monthly count sees them (0x2E0BE).
    int province_cells() const {
        int n = 0;
        for (uint8_t c : s.empire.cells) {
            const int t = c & 0x7F;
            if ((t >= 0x36 && t <= 0x49) || (t >= 0x62 && t <= 0x77)) ++n;
        }
        return n;
    }
    int construction_need_now_with(int extra) const {
        const int rank = gw(s, 0x6C30);
        int shift = rank <= 1 ? 4 : rank <= 3 ? 3 : 2;
        if (g.sim.difficulty == 2) shift = 1;
        return std::max(1, (province_cells() + extra) >> (shift - 1));
    }
    int construction_need_now() const {
        const int rank = gw(s, 0x6C30);
        int shift = rank <= 1 ? 4 : rank <= 3 ? 3 : 2;
        if (g.sim.difficulty == 2) shift = 1;
        return std::max(1, province_cells() >> (shift - 1));
    }

    // Worn pieces (a road that wore away is open ground again) are laid again.
    void province_repair() {
        for (const Route& r : routes)
            for (const Pt& c : r.cells) {
                if (ptile(c.x, c.y) > 0x35 || ptile(c.x, c.y) < 0x1D) continue;
                const int cost = economy::kConstructionCost[static_cast<size_t>(r.command)]
                                 << economy::province_cost_shift(r.command, ptile(c.x, c.y));
                if (funds() < cost + 100) return;
                if (g.province_place(r.command, c.x, c.y)) ++repairs;
            }
    }
    int repairs = 0;
    std::vector<Pt> highway_cells;
    std::vector<uint8_t> highway_tiles;

    // The province's roads, laid cell by cell as the money comes in: the highway first, then a road to each town.
    int cur_cmd = 0;
    std::vector<Pt> cur_cells;
    size_t cur_next = 0;
    bool province_finished = false;

    bool province_plan_next() {
        if (!highway_done) {
            highway_done = true;
            const auto path = province_path({0x78}, {0x4A, 0x4B}, true);
            if (path.empty()) {
                g.note("province: no route for the Imperial Highway");
            } else {
                cur_cmd = 42;
                cur_cells = path;
                cur_next = 0;
                highway_cells = path;
                return true;
            }
        }
        const auto path = province_path({0x4A, 0x4B}, {0x61}, false, town_done);
        if (path.empty()) return false;
        cur_cmd = 36;
        cur_cells = path;
        cur_next = 0;
        return true;
    }

    void finish_piece() {
        static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
        routes.push_back({cur_cmd, cur_cells});
        char b[160];
        if (cur_cmd == 42) {
            highway_tiles.clear();
            for (const Pt& q : cur_cells) highway_tiles.push_back(ptile(q.x, q.y));
            std::snprintf(b, sizeof b, "province: Imperial Highway laid, %zu cells; entry (%d,%d)", cur_cells.size(),
                          gw(s, 0x6C90), gw(s, 0x6C8E));
        } else {
            const Pt last = cur_cells.back();
            for (int d = 0; d < 4; ++d)
                if (in_map(last.x + dx[d], last.y + dy[d]) && ptile(last.x + dx[d], last.y + dy[d]) == 0x61)
                    town_done.push_back({last.x + dx[d], last.y + dy[d]});
            std::snprintf(b, sizeof b, "province: road to a town laid, %zu cells", cur_cells.size());
        }
        g.note(b);
        cur_cells.clear();
        cur_next = 0;
    }

    // Lays what `budget` pays for; returns what it spent.
    int province_step(int budget) {
        if (province_finished) return 0;
        if (cur_cells.empty()) {
            if (!province_plan_next()) {
                province_finished = true;
                return 0;
            }
        }
        const int before = funds();
        // the construction duty first, so no piece wears while the plebs catch up
        pending_roads = static_cast<int>(std::min<size_t>(cur_cells.size() - cur_next, 40));
        tribune();
        while (cur_next < cur_cells.size()) {
            const Pt p = cur_cells[cur_next];
            const uint8_t t = ptile(p.x, p.y);
            int cost = 0;
            if (!((t >= 0x36 && t <= 0x41) || (t >= 0x6D && t <= 0x77)))
                cost = economy::kConstructionCost[static_cast<size_t>(cur_cmd)] << economy::province_cost_shift(cur_cmd, t);
            if (before - funds() + cost > budget || funds() < cost + 100) break;
            g.province_place(cur_cmd, p.x, p.y);
            ++cur_next;
        }
        pending_roads = 0;
        tribune();
        if (cur_next >= cur_cells.size()) finish_piece();
        return before - funds();
    }

    // For the harness: lays everything there is, if the money lasts.
    void province_work() {
        for (int guard = 0; guard < 100 && !province_finished; ++guard) {
            province_step(1 << 20);
            if (!cur_cells.empty()) break;  // out of money mid-piece: the rest next month
        }
    }

    // ---- the Legion ----
    int armies_seen = 0;
    bool patrol_set = false;
    Pt patrol_a{-1, -1}, patrol_b{-1, -1};

    void pick_patrol() {
        // the city on the province map, then the two nearest open cells four or five cells away on either side
        int cx = -1, cy = -1;
        for (int y = 0; y < 40 && cx < 0; ++y)
            for (int x = 0; x < 40; ++x)
                if (ptile(x, y) == 0x4A) {
                    cx = x;
                    cy = y;
                    break;
                }
        if (cx < 0) return;
        auto open = [&](int x, int y) {
            return in_map(x, y) && ptile(x, y) >= 0x1D && ptile(x, y) <= 0x35 &&
                   systems::province::kLandClass[static_cast<size_t>(ptile(x, y))] == 0;
        };
        Pt best_a{-1, -1}, best_b{-1, -1};
        for (int d = 3; d <= 7 && (best_a.x < 0 || best_b.x < 0); ++d) {
            for (int dy = -d; dy <= d; ++dy)
                for (int dx = -d; dx <= d; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != d || !open(cx + dx, cy + dy)) continue;
                    if (dx <= 0 && best_a.x < 0) best_a = {cx + dx, cy + dy};
                    if (dx > 0 && best_b.x < 0) best_b = {cx + dx, cy + dy};
                }
        }
        patrol_a = best_a;
        patrol_b = best_b;
    }

    // A Fort (500 Dn) is another Cohort. The Legion is shared out among them, so they come only when the money is plentiful
    // and the Legion has grown; but the armies come in twos and threes, and one Cohort chasing one of them leaves the rest
    // to walk into the city.
    int forts_wanted() const {
        const int regulars = gw(s, systems::military::kRegulars);
        static const int per_cohort = std::getenv("PT_REGS_PER_COHORT") ? std::atoi(std::getenv("PT_REGS_PER_COHORT")) : 6;
        return std::clamp(regulars / per_cohort, 1, 4);
    }
    void maybe_build_fort() {
        namespace province = systems::province;
        const int cohorts = gw(s, 0x6C12);
        if (cohorts >= forts_wanted() || funds() < 1500 + reserve_funds() || !viable()) return;
        int city_x = -1, city_y = -1;
        for (int y = 0; y < 40 && city_x < 0; ++y)
            for (int x = 0; x < 40; ++x)
                if (ptile(x, y) == 0x4A) {
                    city_x = x;
                    city_y = y;
                    break;
                }
        if (city_x < 0) return;
        for (int d = 2; d <= 7; ++d)
            for (int dy = -d; dy <= d; ++dy)
                for (int dx = -d; dx <= d; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != d) continue;
                    const int x = city_x + dx, y = city_y + dy;
                    if (!in_map(x, y) || ptile(x, y) < 0x1D || ptile(x, y) > 0x35 ||
                        province::kLandClass[static_cast<size_t>(ptile(x, y))] != 0)
                        continue;
                    // not beside another fort
                    bool crowded = false;
                    for (int ddy = -2; ddy <= 2 && !crowded; ++ddy)
                        for (int ddx = -2; ddx <= 2 && !crowded; ++ddx)
                            if (in_map(x + ddx, y + ddy) && ptile(x + ddx, y + ddy) == 0x4D) crowded = true;
                    if (crowded) continue;
                    if (economy::can_afford(s, 500) && economy::enough_plebs(s, 29) && province::place_fort(s, x, y) >= 0) {
                        economy::charge(s, 500);
                        g.note("province: a Fort raised for another Cohort");
                        return;
                    }
                }
    }

    // A Cohort that cannot take the armies stays home: a battle lost costs its Centuries, and the Legion grows by one of each
    // kind a year, so a Cohort that fights every army it is too weak for never gets stronger. One that can takes the
    // province road on patrol, which turns on any army it meets, and is sent after the ones on the march that it can beat.
    void military() {
        namespace province = systems::province;
        if (patrol_a.x < 0) pick_patrol();
        const int min_str = min_barbarian_strength(s);
        static const double margin = std::getenv("PT_MIL_MARGIN") ? std::atof(std::getenv("PT_MIL_MARGIN")) : 3.0;
        for (int i = 0; i < model::kActorCount; ++i) {
            const model::Actor& a = s.objects[static_cast<size_t>(i)];
            if (a.active() == 0 || a.type() != province::kCohortType) continue;
            const bool strong = romans_strength(a) - barbarians_strength(min_str, 6) >= margin;
            if (strong && a.state() == province::kHalt && patrol_a.x >= 0 && patrol_b.x >= 0) {
                if (province::order_patrol(s, i, patrol_a.x, patrol_a.y, patrol_b.x, patrol_b.y) && !patrol_set) {
                    patrol_set = true;
                    g.note("the Prima Cohors put on patrol near the city");
                }
            } else if (!strong && a.state() == province::kPatrol) {
                province::order_go_home(s, i);
            }
        }
        // armies on the march: the one nearest the city is the most urgent, and the Cohorts go after them in that order, the
        // strongest first, wherever they can win; a Cohort chasing one that is not the nearest turns to the one that is
        int city_x = -1, city_y = -1;
        for (int y = 0; y < 40 && city_x < 0; ++y)
            for (int x = 0; x < 40; ++x)
                if (ptile(x, y) == 0x4A) {
                    city_x = x;
                    city_y = y;
                    break;
                }
        struct Threat {
            int slot, distance;
        };
        std::vector<Threat> threats;
        for (int ai = 0; ai < model::kActorCount; ++ai) {
            const model::Actor& army = s.objects[static_cast<size_t>(ai)];
            if (army.active() == 0 || army.type() != province::kArmyType) continue;  // a ship can't be attacked until it lands
            if (army.state() != province::kMarch) continue;
            threats.push_back({ai, std::abs((army.screen_x() >> 4) - city_x) + std::abs((army.screen_y() >> 4) - city_y)});
        }
        std::sort(threats.begin(), threats.end(), [](const Threat& a, const Threat& b) { return a.distance < b.distance; });
        std::vector<bool> taken(model::kActorCount, false);
        for (const Threat& th : threats) {
            const model::Actor& army = s.objects[static_cast<size_t>(th.slot)];
            int best = -1;
            double best_strength = -1e9;
            for (int ci = 0; ci < model::kActorCount; ++ci) {
                const model::Actor& c = s.objects[static_cast<size_t>(ci)];
                if (taken[static_cast<size_t>(ci)] || c.active() == 0 || c.type() != province::kCohortType ||
                    c.state() == province::kDemobilized)
                    continue;
                const double r = romans_strength(c);
                if (r > best_strength) {
                    best_strength = r;
                    best = ci;
                }
            }
            if (best < 0) break;
            if (best_strength - barbarians_strength(min_str, army.raw[0x30]) < margin) continue;
            taken[static_cast<size_t>(best)] = true;
            const model::Actor& c = s.objects[static_cast<size_t>(best)];
            const int current = c.raw[0x14] | (c.raw[0x15] << 8);
            if (c.state() == province::kAttack && current == th.slot) continue;  // already on its way
            // a Cohort already chasing another army only turns for one clearly nearer the city
            if (c.state() == province::kAttack && current >= 0 && current < model::kActorCount) {
                const model::Actor& now = s.objects[static_cast<size_t>(current)];
                if (now.active() != 0 && now.type() == province::kArmyType) {
                    const int d = std::abs((now.screen_x() >> 4) - city_x) + std::abs((now.screen_y() >> 4) - city_y);
                    if (d <= th.distance + 3) continue;
                }
            }
            if (province::order_attack(s, best, th.slot)) ++armies_seen;
        }
    }

    // ---- slots chosen by what they do for the houses around them ----
    // A summed-area table, so a box of cells is counted in one step.
    template <typename F>
    std::vector<int> make_sat(F f) const {
        std::vector<int> t(static_cast<size_t>(model::kCityH + 1) * (model::kCityW + 1), 0);
        const size_t W = model::kCityW + 1;
        for (int y = 0; y < model::kCityH; ++y)
            for (int x = 0; x < model::kCityW; ++x)
                t[static_cast<size_t>(y + 1) * W + x + 1] = f(x, y) + t[static_cast<size_t>(y) * W + x + 1] +
                                                           t[static_cast<size_t>(y + 1) * W + x] - t[static_cast<size_t>(y) * W + x];
        return t;
    }
    static int box_sum(const std::vector<int>& t, int x0, int y0, int x1, int y1) {  // inclusive, clipped
        x0 = std::max(0, x0);
        y0 = std::max(0, y0);
        x1 = std::min(model::kCityW - 1, x1);
        y1 = std::min(model::kCityH - 1, y1);
        if (x0 > x1 || y0 > y1) return 0;
        const size_t W = model::kCityW + 1;
        return t[static_cast<size_t>(y1 + 1) * W + x1 + 1] - t[static_cast<size_t>(y0) * W + x1 + 1] -
               t[static_cast<size_t>(y1 + 1) * W + x0] + t[static_cast<size_t>(y0) * W + x0];
    }
    bool free_rect(int x, int y, int w, int h) const {
        for (int dy = 0; dy < h; ++dy)
            for (int dx = 0; dx < w; ++dx)
                if (!grass(s, x + dx, y + dy)) return false;
        return true;
    }
    bool is_house_tile(uint8_t t) const { return t >= 0xC8 && t <= 0xD7; }

    // The free slot of `queue` (width w, height h) whose box, grown by `radius`, holds the most of what `sat` counts; -1 if
    // there is no free slot. Ties go to the one nearest the forum.
    int best_slot(const std::vector<Pt>& queue, int w, int h, int radius, const std::vector<int>& sat) const {
        int best = -1;
        long long best_score = -(1LL << 40);
        for (size_t i = 0; i < queue.size(); ++i) {
            const Pt& p = queue[i];
            if (!strip_open(p.y) || !free_rect(p.x, p.y, w, h)) continue;
            const long long score = static_cast<long long>(box_sum(sat, p.x - radius, p.y - radius, p.x + w - 1 + radius, p.y + h - 1 + radius)) * 1000 -
                               (std::abs(p.x - fx) + 3 * std::abs(p.y - fy));
            if (score > best_score) {
                best_score = score;
                best = static_cast<int>(i);
            }
        }
        return best;
    }
    bool any_slot(const std::vector<Pt>& queue, int w, int h) const {
        for (const Pt& p : queue)
            if (strip_open(p.y) && free_rect(p.x, p.y, w, h)) return true;
        return false;
    }

    // The best free 2 x 2 on cells the plan keeps for houses, in the open strips, for when the reserved slots are used up.
    bool find_house_square(const std::vector<int>& sat, int radius, Pt& out) const {
        long long best_score = -(1LL << 40);
        bool found = false;
        for (const Pt& p : house_queue) {
            if (!strip_open(p.y) || p.x + 1 >= model::kCityW || p.y + 1 >= model::kCityH) continue;
            if (role[p.y][p.x] != 'h' || role[p.y][p.x + 1] != 'h' || role[p.y + 1][p.x] != 'h' || role[p.y + 1][p.x + 1] != 'h') continue;
            if (!free_rect(p.x, p.y, 2, 2)) continue;
            // nothing but houses' own ground: keep to the strips that already have houses
            if (strip_rank[static_cast<size_t>(p.y)] > houses_strip_max + 1) continue;
            const long long score = static_cast<long long>(box_sum(sat, p.x - radius, p.y - radius, p.x + 1 + radius, p.y + 1 + radius)) * 1000 -
                                    (std::abs(p.x - fx) + 3 * std::abs(p.y - fy));
            if (score > best_score) {
                best_score = score;
                out = p;
                found = true;
            }
        }
        return found;
    }

    // A forum goes where the most houses are still outside the tax (and the network).
    int more_forums(int count, int) {
        int n = 0;
        g.forum_grade = forum_grade_choice();
        const int fw = g.forum_grade <= 2 ? 2 : g.forum_grade <= 6 ? 3 : 4;
        for (int i = 0; i < count; ++i) {
            const auto sat = make_sat([&](int x, int y) {
                if (!is_house_tile(s.city.tile[y][x])) return 0;
                const uint8_t f = s.city.service_flags[y][x];
                return ((f & 0x20) ? 0 : 3) + ((f & 0x02) ? 0 : 1);
            });
            const int at = best_slot(forum_queue, fw, fw, 6, sat);
            Pt p{};
            if (at >= 0) p = forum_queue[static_cast<size_t>(at)];
            else if (!find_house_square(sat, 6, p)) break;
            if (!put(CommandId::Forum, p.x, p.y, "forums")) break;
            ++n;
        }
        return n;
    }

    // The Tribune's duties. The assignment runs fire -> building -> road -> construction -> army, each cut to what is
    // left of the plebs above 50, so a short supply starves the province roads first; the player takes every duty down
    // and refills them in the order that matters. (DS:0x6C3E, the construction need, is not saved: the Tribune page
    // shows 0 for it, so the player has to work it out from the province roads.)
    int unemp_limit = std::getenv("PT_UNEMP") ? std::atoi(std::getenv("PT_UNEMP")) : 8;
    int pleb_deficit = 0;
    int pending_roads = 0;  // province pieces about to be laid: their upkeep is staffed first
    void tribune() {
        using systems::forum::Duty;
        if (std::getenv("PT_NO_TRIBUNE")) {  // a player who never opens the Tribune: the new game's duties stay as they are
            if (std::getenv("PT_ASSIST")) viewer::tribune_assist(s, g.sim.difficulty);  // ... unless the viewer's Tribune: Automatic does it
            return;
        }
        const int construction_need =
            std::max(systems::plebs::set_needs(s, g.sim.difficulty), (construction_need_now_with(pending_roads)));
        struct D { Duty duty; uint16_t have; int target; };
        // The needs the next scan will find, from the map as it stands (what has just been built counts), and a little over:
        // the shares that set the chance of a fire or a collapse are taken against whatever is built by then.
        int building_cells = 0, road_cells = 0;
        for (int y = 0; y < model::kCityH; ++y)
            for (int x = 0; x < model::kCityW; ++x) {
                const uint8_t t = s.city.tile[y][x];
                if ((t >= 0xC8 && t <= 0xE8) || (t >= 0xEA && t <= 0xF3) || t == 0xF5 || t == 0xF6) ++building_cells;
                else if (t >= 0x36 && t <= 0x40) ++road_cells;
            }
        const int rank = gw(s, 0x6C30);
        int shift = rank <= 1 ? 4 : rank <= 3 ? 3 : 2;
        if (g.sim.difficulty == 2) shift = 1;
        const int extra = rank == 3 ? 16 : 0;
        auto over = [](int need) { return need + need / 20 + 1; };
        const int fire_need = std::max(1, (building_cells + extra) >> shift);
        const int building_need = std::max(1, (building_cells + extra + 16) >> (shift + 1));
        const int road_need = std::max(1, (road_cells + extra) >> shift);
        D duties[] = {{Duty::Construction, systems::plebs::kConstruction, construction_need},
                      {Duty::FirePrevention, systems::plebs::kFirePrevention, over(std::max(fire_need, gw(s, systems::plebs::kFireNeed)))},
                      {Duty::BuildingMaintenance, systems::plebs::kBuildingMaintenance,
                       over(std::max(building_need, gw(s, systems::plebs::kBuildingNeed)))},
                      {Duty::RoadMaintenance, systems::plebs::kRoadMaintenance, over(std::max(road_need, gw(s, systems::plebs::kRoadNeed)))}};
        int total_need = 0;
        for (const D& d : duties) total_need += d.target;
        pleb_deficit = total_need - (gw(s, systems::plebs::kPlebs) - 50);
        for (const D& d : duties) {
            int guard = 0;
            while (gw(s, d.have) > 0 && guard++ < 4000) systems::forum::lower_duty(s, d.duty);
        }
        for (const D& d : duties) {
            int guard = 0;
            while (gw(s, d.have) < d.target && gw(s, systems::plebs::kUnassigned) > 0 && guard++ < 4000)
                if (!systems::forum::raise_duty(s, d.duty)) break;
        }
    }

    // Welfare buys plebs: pay_welfare (0x2DDFC) moves the count by up to 5 a month towards where welfare equals
    // (plebs - unassigned / 4 + plebs / 20 x rank + 150) / 3. The bot keeps welfare 10 above that while it is short of the
    // plebs its duties need (plus the 50 kept back and a margin), and level with it otherwise.
    int welfare_margin = std::getenv("PT_MARGIN") ? std::atoi(std::getenv("PT_MARGIN")) : 60;
    void welfare() {
        if (std::getenv("PT_NO_TRIBUNE")) return;  // ... nor changes the welfare
        const int rank = gw(s, 0x6C30);
        const int plebs = gw(s, systems::plebs::kPlebs);
        const int needs = pleb_deficit + (plebs - 50);  // total need, from tribune()
        const int want = needs + 50 + welfare_margin;
        const int unassigned = gw(s, systems::plebs::kUnassigned);
        const int expected = (plebs - (unassigned >> 2) + (plebs / 20) * rank + 150) / 3;
        const int target = std::max(0, plebs < want ? expected + 10 : expected + 1);
        int guard = 0;
        while (gw(s, systems::plebs::kWelfare) < target && guard++ < 3000)
            systems::forum::adjust(s, systems::forum::Control::Welfare, 1);
        while (gw(s, systems::plebs::kWelfare) > target && guard++ < 6000)
            systems::forum::adjust(s, systems::forum::Control::Welfare, -1);
    }

    // Two free house cells side by side in a housing block, the nearest to the forum.
    bool find_pair(int& ox, int& oy, int min_dist) {
        int best = 1 << 30;
        for (const Pt& p : house_queue) {
            if (!grass(s, p.x, p.y) || !grass(s, p.x + 1, p.y)) continue;
            if (role[p.y][p.x + 1] != 'h') continue;
            const int d = std::abs(p.x - fx) + std::abs(p.y - fy);
            if (d < min_dist || d >= best) continue;
            best = d;
            ox = p.x;
            oy = p.y;
        }
        return best < (1 << 30);
    }

    int tax_rate = std::getenv("PT_TAXRATE") ? std::atoi(std::getenv("PT_TAXRATE")) : 6;
    void setup() {
        g.place_tool(CommandId::Forum, fx, fy);
        const int r = lazy_roads ? 0 : roads_in(reach);
        const int w = lazy_roads ? 0 : reservoirs_in(reach);
        const int m = markets_n(6, reach);
        const int k = workshops_n(4, reach);
        const int h = houses(40, reach);
        std::printf("setup: roads %d reservoirs %d markets %d workshops %d houses %d", r, w, m, k, h);
        std::putchar(10);
        int guard = 0;
        static const int conscription = std::getenv("PT_CONSRATE") ? std::atoi(std::getenv("PT_CONSRATE")) : 10;
        while (gw(s, 0x6C06) > conscription && guard++ < 4000) systems::forum::adjust(s, systems::forum::Control::Conscription, -1);
        while (gw(s, 0x6C06) < conscription && guard++ < 400) systems::forum::adjust(s, systems::forum::Control::Conscription, 1);
        // the governor's savings go to the city (90 %), and the salary stops
        guard = 0;
        while (gw(s, 0x6C28) < gw(s, 0x6C2E) && guard++ < 500) systems::forum::adjust(s, systems::forum::Control::Donation, 1);
        economy::donate_savings(s, gw(s, 0x6C28));
        guard = 0;
        while (gw(s, 0x6C2C) > 0 && guard++ < 500) systems::forum::adjust(s, systems::forum::Control::Salary, -1);
        // 6 % on the people costs nothing in land value or unrest (the tables at 5 % and 6 % agree) and pays a fifth more
        guard = 0;
        while (gw(s, 0x6C04) < tax_rate && guard++ < 50) systems::forum::adjust(s, systems::forum::Control::PopulationTax, 1);
        stage = 1;
    }

    // ---------------------------------------------------------------------------------------------------------------
    // What the city is made of, counted from the map, and what its jobs and culture come to.
    struct Counts {
        int house_cells = 0, units_now = 0, units_potential = 0;
        int markets = 0, forums = 0, workshops = 0, oracles = 0, schools = 0, theaters = 0, coliseums = 0,
            hippodromes = 0;
        int religion = 0, entertainment = 0;  // the culture points, as administration::culture_year counts them
        int road_cells = 0;
    };

    Counts count() const {
        Counts c;
        int market_cells = 0, oracle_cells = 0, school_cells = 0, theater_cells = 0, coliseum_cells = 0, hippo_cells = 0;
        for (int y = 0; y < model::kCityH; ++y)
            for (int x = 0; x < model::kCityW; ++x) {
                const uint8_t t = s.city.tile[y][x];
                if (t >= 0x36 && t <= 0x43) {
                    ++c.road_cells;
                } else if (t >= 0xC8 && t <= 0xD7) {
                    const int u = systems::housing::kPopulationUnitsPerCell[t - 0xC8];
                    ++c.house_cells;
                    c.units_now += u;
                    c.units_potential += t <= 0xCB ? std::max(u, 3) : u;
                } else if (t >= 0xD8 && t <= 0xDF) {
                    ++c.religion;
                } else if (t == 0xEB) {
                    ++oracle_cells;
                    c.religion += 9;
                } else if (t == 0xF4) {
                    ++market_cells;
                } else if (t == 0xEC || t == 0xED) {
                    ++school_cells;
                } else if (t == 0xF0) {
                    ++theater_cells;
                    c.entertainment += 2;
                } else if (t == 0xF1) {
                    ++coliseum_cells;
                    c.entertainment += 3;
                } else if (t == 0xF2) {
                    ++hippo_cells;
                    c.entertainment += 4;
                }
            }
        c.markets = market_cells / 4;
        c.oracles = oracle_cells / 2;
        c.schools = school_cells / 4;
        c.theaters = theater_cells / 2;
        c.coliseums = coliseum_cells / 6;
        c.hippodromes = hippo_cells / 8;
        c.forums = gw(s, 0x6CA0);
        c.workshops = gw(s, 0x6C9E);
        return c;
    }

    // The monthly jobs sum of the economy step (0x28694), in population units; the multiplier is the saved yearly average.
    int jobs_units(const Counts& c) const {
        const int mult = gw(s, 0x6BE8) / 4 + 1;
        return (c.workshops * 20 + c.markets * 12) * mult + c.forums * 30;
    }
    // The share of the population (as it will be once the houses have grown) with no work, net of the conscripts.
    double unemployment_if(const Counts& c) const {
        const int cons = gw(s, 0x6C06);
        const double need = c.units_potential * (100 - cons) / 100.0;
        const double jobs = jobs_units(c);
        return std::max(0.0, need - jobs) / std::max(1, c.units_potential);
    }

    // Culture as the year-end routine scores it (0x28C90), with planning arithmetic in doubles.
    double culture_value(int religion, int entertainment, int schools, int units) const {
        const double d = units / 12 + 5;
        double rel = religion * 34.0 / d;
        if (rel > 34) rel = 34;
        double sum = rel + (entertainment >> 1) * 34.0 / d;
        if (sum > 68) sum = 68;
        sum += schools * 34.0 / d;
        const int cap = systems::administration::population_cap(100, 1, units);
        return std::min({sum, static_cast<double>(cap), 100.0});
    }

    // ---- the money: shares of what is spare each month are put aside for the province and for culture ----
    double credit_province = 0, credit_culture = 0;
    double share_province = std::getenv("PT_SHARE_PROVINCE") ? std::atof(std::getenv("PT_SHARE_PROVINCE")) : 0.45;
    double share_culture_base = std::getenv("PT_SHARE_CULTURE") ? std::atof(std::getenv("PT_SHARE_CULTURE")) : 0.4;
    double share_culture_now() const { return final_stage ? 0.8 : share_culture_base; }
    // What the city can count on in a year (the taxes as the yearly routine would charge them now) against what it owes
    // (welfare, salary, wages and the tribute): a cushion of two years of any shortfall stays untouched.
    int estimated_taxes() const {
        const int rate = gw(s, 0x6C04), industrial_rate = gw(s, 0x6C02);
        const auto pop = economy::population_tax(s.city, rate, 4 * gw(s, 0x6C10));
        return pop.tax + economy::industrial_tax(s, industrial_rate);
    }
    int yearly_costs() const { return gw(s, 0x6C46) + gw(s, 0x6C2C) + gw(s, 0x6C08) + gw(s, 0x6BBA); }
    int reserve_funds() const {
        const int deficit = yearly_costs() - estimated_taxes();
        static const int base = std::getenv("PT_RESERVE") ? std::atoi(std::getenv("PT_RESERVE")) : 200;
        return std::min(1000, base + std::max(0, deficit));
    }
    double years_in() const { return gw(s, 0x6C32) - first_year + gw(s, 0x6C1C) / 12.0; }

    // ---- growth: jobs, then the houses they pay for ----
    int pick_goods() {
        // A workshop's level is its base for the province and the goods, plus what the neighbourhood and the Empire add
        // (about 6), less a penalty that grows with the number of the same goods (0x2CFB5: over 1, 3 and 5), clamped to
        // 0-7. So the best few goods get all the workshops, up to the point where the penalty has stopped growing.
        auto& t8 = s.table_8;
        if (t8.size() < 8) t8.resize(8, 0);
        auto penalty = [](int n) { return (n > 1) + (n > 3) + (n > 5); };
        auto level = [&](int base, int n) { return std::clamp(base + 6 - penalty(n), 0, 7); };
        int best = 0;
        double best_gain = -1e9;
        for (int gd = 0; gd < 8; ++gd) {
            const int base = systems::actors::workshop_base(gw(s, 0x6CA6), gd);
            const int n = static_cast<int8_t>(t8[static_cast<size_t>(gd)]);
            const double gain = (n + 1) * level(base, n + 1) - n * level(base, n) + 0.01 * base;
            if (gain > best_gain) {
                best_gain = gain;
                best = gd;
            }
        }
        return best;
    }

    // A workshop's level rises with the people in the nine by nine around it (0x2CFB5): it goes where there are most.
    int workshops_n(int count_wanted, int) {
        int n = 0;
        for (int i = 0; i < count_wanted; ++i) {
            const auto sat = make_sat([&](int x, int y) {
                const uint8_t t = s.city.tile[y][x];
                return is_house_tile(t) ? systems::housing::kPopulationUnitsPerCell[t - 0xC8] : 0;
            });
            const int at = best_slot(workshop_queue, 3, 3, 3, sat);
            if (at < 0) break;
            const Pt p = workshop_queue[static_cast<size_t>(at)];
            g.workshop_goods = pick_goods();
            if (!put(CommandId::Workshop, p.x, p.y, "workshops")) break;
            ++n;
            ++built_workshops;
        }
        return n;
    }

    // A market goes where the most houses are outside its reach; it is 12 jobs wherever it stands.
    int markets_n(int count_wanted, int) {
        int n = 0;
        for (int i = 0; i < count_wanted; ++i) {
            const auto sat = make_sat([&](int x, int y) {
                if (!is_house_tile(s.city.tile[y][x])) return 0;
                return (s.city.service_flags[y][x] & 0x08) ? 0 : 1;
            });
            const int at = best_slot(market_queue, 2, 2, 6, sat);
            Pt p{};
            if (at >= 0) p = market_queue[static_cast<size_t>(at)];
            else if (!find_house_square(sat, 6, p)) break;
            if (!put(CommandId::Market, p.x, p.y, "markets")) break;
            ++n;
        }
        return n;
    }

    bool slot_left(const std::vector<Pt>& queue, int w, int h) const { return any_slot(queue, w, h); }

    // Forums come first: each one collects the tax from the houses around it (radius 6), sends the walkers that carry the
    // network flag, and is 30 jobs for 60 Dn. One for every dozen house cells, and two to begin with.
    double unemployment_limit = std::getenv("PT_UNEMP_LIMIT") ? std::atof(std::getenv("PT_UNEMP_LIMIT")) : 0.08;
    int forum_grade_choice() const {
        static const int g0 = std::getenv("PT_FORUM_GRADE") ? std::atoi(std::getenv("PT_FORUM_GRADE")) : 0;
        return g0;
    }
    int forums_wanted(const Counts& c) const {
        static const int per = std::getenv("PT_FORUM_PER") ? std::atoi(std::getenv("PT_FORUM_PER")) : 24;
        return std::min(30, 2 + c.house_cells / per);
    }
    // A workshop is worth building while the ones there are producing: a level of 3 is 38 Dn a year for the 50 it cost, and
    // each new one eats into every other's share of the market (0x28621), so the number follows the average level.
    int workshops_wanted(const Counts& c) const {
        static const int need_empire = std::getenv("PT_WKS_EMPIRE") ? std::atoi(std::getenv("PT_WKS_EMPIRE")) : 0;
        const double average = economy::average_workshop_level(s) + 0.0;
        if (gw(s, admin::kEmpire) < need_empire) return std::min(c.workshops, 4);
        if (c.workshops < 4) return 4;
        if (average >= 3.0) return std::min(30, c.workshops + 1);
        return c.workshops;
    }

    // One batch of jobs. `exhausted` is set when no slot of any kind is left in reach (the ring has to grow), as opposed
    // to a slot that could not be paid for.
    int add_jobs(const Counts& c, bool& exhausted) {
        const bool want_workshop = c.workshops < workshops_wanted(c);
        int n = 0;
        if (want_workshop) n = workshops_n(1, reach);
        if (n == 0) n = markets_n(1, reach);
        if (n == 0 && c.forums < 30) n = more_forums(1, reach);
        if (n == 0 && c.workshops < 30) n = workshops_n(1, reach);
        exhausted = n == 0 && !(c.workshops < 30 && slot_left(workshop_queue, 3, 3)) &&
                    !(c.forums < 30 && slot_left(forum_queue, 2, 2)) && !slot_left(market_queue, 2, 2);
        if (exhausted) exhausted = false;  // house ground can always be given over to a market
        return n;
    }

    int add_houses(int n) { return houses(n, reach); }

    // The next ring of the plan: its roads and water, when the money is there.
    bool expand_ring() {
        if (lazy_roads) return false;  // the roads follow the houses; nothing to widen
        if (reach >= 3 * RW) return false;
        if (funds() < 400 + reserve_funds()) return false;
        reach += 3;
        const int r = roads_in(reach), w = reservoirs_in(reach);
        char b[100];
        std::snprintf(b, sizeof b, "expanding to reach %d: roads %d wells %d", reach, r, w);
        if (std::getenv("PT_QUIET") == nullptr) std::puts(b);
        return true;
    }

    // plebs needed per new house cell, for the rank's duties (fire, upkeep of buildings and roads)
    double pleb_per_cell() const {
        const int rank = gw(s, 0x6C30);
        int shift = rank <= 1 ? 4 : rank <= 3 ? 3 : 2;
        if (g.sim.difficulty == 2) shift = 1;
        return 1.8 / static_cast<double>(1 << shift);
    }

    int grow(int floor_funds) {
        int placed_total = 0;
        double room = -pleb_deficit - 4.0;  // plebs to spare
        const double per_cell = pleb_per_cell();
        Counts c = count();
        int failures = 0;
        for (int iter = 0; iter < 300 && failures < 3; ++iter) {
            if (funds() < floor_funds + 30 || gw(s, systems::plebs::kPlebs) < 50) break;
            int did = 0;
            bool exhausted = false;
            // in order of what pays: forums for the tax and the walkers, workshops, jobs when there are none, then houses;
            // whatever cannot be done (no slot, no money) gives way to the next
            if (c.forums < forums_wanted(c)) did = more_forums(1, reach);
            if (did == 0 && c.workshops < workshops_wanted(c)) did = workshops_n(1, reach);
            if (did == 0 && unemployment_if(c) > unemployment_limit) {
                did = add_jobs(c, exhausted);
            } else if (did == 0 && room > per_cell) {
                did = add_houses(4);
                room -= did * per_cell;
                exhausted = did == 0 && !house_cell_free();
            } else if (did == 0) {
                break;  // the plebs for more upkeep aren't there yet
            }
            if (std::getenv("PT_DEBUG2"))
                std::printf("   grow: forums %d/%d unemployment %.2f room %.1f did %d exhausted %d funds %d floor %d", c.forums, forums_wanted(c), unemployment_if(c), room, did, (int)exhausted, funds(), floor_funds), std::putchar(10);
            if (did == 0) {
                if (!exhausted) break;  // slots there, but no money for them
                ++failures;
                // the plan is full: widen it, a wider city needs more forums to carry the network to its edges
                if (RW < max_RW) {
                    RW = std::min(max_RW, RW + 12);
                    plan();
                    continue;
                }
                if (!expand_ring()) break;
                continue;
            }
            placed_total += did;
            c = count();
        }
        return placed_total;
    }

    // ---- culture ----
    struct Candidate {
        CommandId cmd;
        int cost, religion, entertainment, schools;
        const char* what;
    };

    // The nearest free ground in the culture strips that holds the building's footprint.
    bool find_culture_site(CommandId cmd, Pt& out) const {
        const auto spec = construction::placement_spec(cmd);
        for (const Pt& p : culture_cells) {
            bool ok = true;
            for (int dy = 0; dy < spec.height && ok; ++dy)
                for (int dx = 0; dx < spec.width && ok; ++dx) {
                    const int cx = p.x + dx, cy = p.y + dy;
                    ok = cx < model::kCityW && cy < model::kCityH && culture_region[static_cast<size_t>(cy)][static_cast<size_t>(cx)];
                }
            if (ok && construction::can_place(s.city, cmd, p.x, p.y)) {
                out = p;
                return true;
            }
        }
        return false;
    }

    // After the last promotion has been earned, the title waits while the record is made as good as it can be.
    bool final_stage = false;

    double culture_goal() const {
        if (final_stage) return 100.0;
        // money that would only pile up (the funds word is 16 bits) is better spent on culture, which no rating punishes
        if (funds() > 15000) return 100.0;
        const int rank = gw(s, 0x6C30);
        const admin::Requirement& need = admin::kPromotion[static_cast<size_t>(std::min(rank, 20))];
        // the average is the sum of the four: what the others will be, by the time Peace is there
        const double p = std::min(100.0, gw(s, admin::kPeace) + 2.0 * 3);
        const double e = 100, pr = std::min(100.0, gw(s, admin::kProsperity) + 5.0 * 3);
        return std::min(100.0, std::max<double>(need.each + 4, 4.0 * need.average - p - e - pr + 3));
    }

    // Builds the culture building that adds the most per Dn, while the credit pays for one.
    int culture_step(const Counts& c) {
        const int units = std::max(gw(s, 0x6C10), c.units_now);
        if (units < 150) return 0;
        const double goal = culture_goal();
        int built = 0;
        for (int guard = 0; guard < 10; ++guard) {
            const Counts now = count();
            const double have = culture_value(now.religion, now.entertainment, now.schools, units);
            if (have >= goal) break;
            const Candidate cands[] = {{CommandId::Oracle, 200, 18, 0, 0, "oracles"},
                                       {CommandId::Hippodrome, 300, 0, 32, 0, "hippodromes"},
                                       {CommandId::School, 60, 0, 0, 1, "schools"}};
            int best = -1;
            double best_value = 0;
            for (int i = 0; i < 3; ++i) {
                const Candidate& k = cands[i];
                if (k.schools > 0 && goal <= 68) continue;  // a school is worth a tenth of a point
                if (credit_culture < k.cost || funds() < k.cost + reserve_funds()) continue;
                const double after = culture_value(now.religion + k.religion, now.entertainment + k.entertainment,
                                                   now.schools + k.schools, units);
                const double value = (after - have) / k.cost;
                if (value > best_value) {
                    best_value = value;
                    best = i;
                }
            }
            if (best < 0) break;
            const Candidate& k = cands[best];
            Pt site;
            if (!find_culture_site(k.cmd, site)) break;
            if (!put(k.cmd, site.x, site.y, k.what)) break;
            credit_culture -= k.cost;
            ++built;
        }
        return built;
    }

    // ---- the Legion's pay: one more regular Century a year if the wages are there for it ----
    void military_policy() {
        const int regulars = gw(s, systems::military::kRegulars);
        static const int max_regs = std::getenv("PT_MAX_REGS") ? std::atoi(std::getenv("PT_MAX_REGS")) : 24;
        static const double wage_share = std::getenv("PT_WAGE_SHARE") ? std::atof(std::getenv("PT_WAGE_SHARE")) : 0.2;
        const int affordable = static_cast<int>(wage_share * estimated_taxes());
        int want = std::min({8 * max_regs, 8 * (regulars + 2), std::max(8 * 3, affordable)});
        want = std::max(want, 8 * regulars - 8);  // never cut below what keeps the Centuries there, unless it has to be
        int guard = 0;
        while (gw(s, systems::military::kArmyWages) < want && guard++ < 1200)
            systems::forum::adjust(s, systems::forum::Control::ArmyWages, 1);
        while (gw(s, systems::military::kArmyWages) > want + 8 && guard++ < 2400)
            systems::forum::adjust(s, systems::forum::Control::ArmyWages, -1);
    }

    // ---- the people's tax: as high as the houses will bear ----
    // Each house cell adds the tax table's value to the land value of its neighbours, and each month the tax raises the unrest
    // of the cells the forums cover (0x28800, 0x28826). Up to 10 % costs one point of land value, and the unrest it adds is
    // held down by the conscription rate; so the rate follows the worst unrest among the small houses, which collapse.
    int tax_limit = std::getenv("PT_TAXMAX") ? std::atoi(std::getenv("PT_TAXMAX")) : 6;
    int months_since_tax_change = 0;
    long long last_spent_total = 0;
    int months_idle = 0;
    int last_worst_unrest = -999;
    void tax_controller() {
        ++months_since_tax_change;
        int worst = -999;
        for (int y = 0; y < model::kCityH; ++y)
            for (int x = 0; x < model::kCityW; ++x) {
                const uint8_t t = s.city.tile[y][x];
                if (t >= 0xC8 && t <= 0xCB) worst = std::max(worst, static_cast<int>(static_cast<int8_t>(s.city.unrest[y][x])));
            }
        const int rate = gw(s, 0x6C04);
        const int unemployed = gw(s, 0x6BCC);
        last_worst_unrest = worst;
        if ((worst > 24 || (unemployed >= 10 && rate > 6)) && rate > 4 && months_since_tax_change >= 2) {
            systems::forum::adjust(s, systems::forum::Control::PopulationTax, -1);
            months_since_tax_change = 0;
        } else if (worst < 6 && unemployed < 8 && rate < tax_limit && months_since_tax_change >= 8) {
            systems::forum::adjust(s, systems::forum::Control::PopulationTax, 1);
            months_since_tax_change = 0;
        }
    }

    // ---- the conscription rate: every conscript is a man with no need of work (0x28694), so it is the cheapest job there is;
    // its price is the unrest it adds (0x2882C), which the prefectures carry off ----
    int conscription_max = std::getenv("PT_CONS_MAX") ? std::atoi(std::getenv("PT_CONS_MAX")) : 30;
    int months_since_conscription_change = 0;
    int prefectures_built = 0;
    int prefectures_wanted(const Counts& c) const {
        const int rate = gw(s, 0x6C06);
        if (rate <= 10) return 0;
        return std::min(static_cast<int>(prefecture_queue.size()), c.house_cells / 20 + (rate - 10) / 5 * 8);
    }
    void conscription_controller(const Counts& c, int worst_unrest) {
        ++months_since_conscription_change;
        const int rate = gw(s, 0x6C06);
        const int unemployed = gw(s, 0x6BCC);
        if (worst_unrest > 20 && rate > 10 && months_since_conscription_change >= 2) {
            systems::forum::adjust(s, systems::forum::Control::Conscription, -1);
            months_since_conscription_change = 0;
            return;
        }
        // build the prefectures first, then raise the rate
        if (prefectures_built < prefectures_wanted(c) && funds() > reserve_funds() + 200) {
            for (const Pt& p : prefecture_queue) {
                if (!grass(s, p.x, p.y)) continue;
                if (put(CommandId::Prefecture, p.x, p.y, "prefectures")) ++prefectures_built;
                break;
            }
        }
        const bool covered = prefectures_built >= std::max(1, c.house_cells / 20);
        if (unemployed >= 7 && rate < conscription_max && worst_unrest < 8 && months_since_conscription_change >= 6 &&
            (rate < 10 || covered)) {
            for (int i = 0; i < 5 && gw(s, 0x6C06) < conscription_max; ++i)
                systems::forum::adjust(s, systems::forum::Control::Conscription, 1);
            months_since_conscription_change = 0;
        }
    }

    // The governor's salary is paid into his savings each year, to 25000; in the last stage, with money to spare, it is raised so
    // that the savings fill, and stopped when they are full or the city's cash runs short.
    void salary_policy() {
        if (!final_stage) return;
        const int savings = gw(s, 0x6C2E);
        int target = 0;
        if (savings < 25000 && funds() > 7000 && viable()) target = std::min(9999, (funds() - 5000) / 2);
        int guard = 0;
        while (gw(s, 0x6C2C) < target && guard++ < 10000) systems::forum::adjust(s, systems::forum::Control::Salary, 1);
        while (gw(s, 0x6C2C) > target && guard++ < 20000) systems::forum::adjust(s, systems::forum::Control::Salary, -1);
    }

    // One decision for the month just begun.
    int province_after_years = std::getenv("PT_PROVINCE_AFTER") ? std::atoi(std::getenv("PT_PROVINCE_AFTER")) : 2;
    int culture_lead_years = std::getenv("PT_CULTURE_LEAD") ? std::atoi(std::getenv("PT_CULTURE_LEAD")) : 5;

    // The city carries itself: what the taxes bring in covers what it costs, with something over.
    bool viable() const {
        static const int margin = std::getenv("PT_VIABLE") ? std::atoi(std::getenv("PT_VIABLE")) : 60;
        return estimated_taxes() >= yearly_costs() + margin;
    }
    bool province_active(const Counts& c) const {
        if (!viable() && years_in() < 12) return false;
        static const int gate = std::getenv("PT_PROV_GATE") ? std::atoi(std::getenv("PT_PROV_GATE")) : 1;
        return years_in() >= province_after_years && !province_finished && (gate == 0 || c.workshops >= 8 || years_in() >= 3);
    }
    bool culture_active(const Counts& c) const {
        if (!viable()) return false;
        const int rank = gw(s, 0x6C30);
        const admin::Requirement& need = admin::kPromotion[static_cast<size_t>(std::min(rank, 20))];
        const double years_to_peace = (need.each - gw(s, admin::kPeace)) / 2.0;
        if (c.units_now < 400) return false;
        const int units = std::max(gw(s, 0x6C10), c.units_now);
        if (culture_value(c.religion, c.entertainment, c.schools, units) >= culture_goal()) return false;
        return years_to_peace <= culture_lead_years || years_in() >= 8;
    }

    void act() {
        if (stage == 0) return setup();
        // what costs nothing, every month
        tribune();
        welfare();
        military_policy();
        military();
        province_repair();
        salary_policy();
        maybe_build_fort();
        repair_city();
        tax_controller();
        const Counts c = count();
        conscription_controller(c, last_worst_unrest);
        const int reserve = reserve_funds();
        // earmark shares of the spare money
        const double free_funds = std::max(0, funds() - reserve - static_cast<int>(credit_province + credit_culture));
        if (province_active(c)) credit_province += share_province * free_funds;
        if (culture_active(c)) credit_culture += share_culture_now() * free_funds;
        if (!province_active(c)) credit_province = 0;
        if (!culture_active(c)) credit_culture = 0;
        if (province_active(c) && credit_province >= 30) {
            const int before = funds();
            province_step(static_cast<int>(credit_province));
            credit_province = std::max(0.0, credit_province - (before - funds()));
        }
        if (culture_active(c)) culture_step(c);
        if (std::getenv("PT_DEBUG"))
            std::printf("  dbg %d/%d funds %d credit prov %.0f cult %.0f | route %zu/%zu hw %d fin %d active %d | plebs %d deficit %d | units %d pot %d jobs %d unemp %.2f"
                        " | reach %d forums %d wksp %d houses %d | welfare %d wages %d reserve %d est_tax %d costs %d",
                        gw(s, 0x6C32), gw(s, 0x6C1C), funds(), credit_province, credit_culture, cur_next, cur_cells.size(),
                        (int)highway_done, (int)province_finished, (int)province_active(c), gw(s, 0x6C56), pleb_deficit,
                        c.units_now, c.units_potential, jobs_units(c), unemployment_if(c), reach, c.forums, c.workshops, c.house_cells, gw(s, 0x6C46), gw(s, 0x6C08), reserve_funds(), estimated_taxes(), yearly_costs());
        if (std::getenv("PT_DEBUG")) std::putchar(10);
        if (std::getenv("PT_DEBUG"))
            std::printf("  dbg2 oracles %d hippo %d schools %d religion %d ent %d culture_est %.1f goal %.1f rating C%d active %d viable %d",
                        c.oracles, c.hippodromes, c.schools, c.religion, c.entertainment,
                        culture_value(c.religion, c.entertainment, c.schools, std::max(gw(s, 0x6C10), c.units_now)), culture_goal(),
                        gw(s, admin::kCulture), (int)culture_active(c), (int)viable()),
                std::putchar(10);
        // the rest of the money grows the city
        const int floor_funds = reserve + static_cast<int>(credit_province + credit_culture);
        if (funds() > floor_funds + 30) grow(floor_funds);
        tribune();  // the duties for what has just been built
        // a stall: money in hand and nothing built for a year
        {
            long long total = 0;
            for (const auto& kv : spent) total += kv.second;
            if (total != last_spent_total) {
                last_spent_total = total;
                months_idle = 0;
            } else if (++months_idle >= 12 && months_idle % 12 == 0 && funds() > 1500) {
                std::printf("   STALL %d/%d: funds %d floor %d | units %d pot %d jobs %d unemp %.2f room %.1f | forums %d/%d wksp %d houses %d | "
                            "house cell free %d, plebs %d need-deficit %d | viable %d",
                            gw(s, 0x6C32), gw(s, 0x6C1C), funds(), floor_funds, c.units_now, c.units_potential, jobs_units(c),
                            unemployment_if(c), -pleb_deficit - 4.0, c.forums, forums_wanted(c), c.workshops, c.house_cells,
                            (int)house_cell_free(), gw(s, systems::plebs::kPlebs), pleb_deficit, (int)viable());
                std::putchar(10);
            }
        }
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// playtest <dir> maps [rank [years [first [last]]]]: the province logic alone, on each of the 50 maps, with money to burn.
int run_maps(Game& game, int rank, int years, int first, int last) {
    int bad = 0;
    for (int p = first; p <= last; ++p) {
        game.verbose = false;
        game.events.clear();
        game.dismissed = false;
        if (!game.start(0, 0)) return 1;
        model::set_global_word(game.state, 0x6C30, rank);
        model::set_global_word(game.state, 0x6CA6, p);
        game.state.table_50.assign(50, 0);
        if (!game.start_province()) return 1;
        auto bot = std::make_unique<Bot>(game);
        bot->first_year = game.sim.year;
        if (!bot->choose_site()) return 1;
        bot->plan();
        bot->setup();
        int linked_max = 0;
        for (int m = 0; m < years * 12 && !game.dismissed; ++m) {
            model::set_global_word(game.state, economy::kFunds, 30000);
            model::set_global_word(game.state, systems::plebs::kWelfare, 200);
            bot->tribune();
            bot->welfare();
            bot->military();
            bot->province_repair();
            bot->province_work();
            if (!game.month()) break;
            linked_max = std::max(linked_max, game.sim.linked_towns);
        }
        int towns = 0, towns_total = 0;
        for (uint8_t c : game.state.empire.cells) {
            const int t = c & 0x7F;
            if (t == 0x61 || t == 0x79 || t == 0x7A || t == 0x4C) ++towns_total;
            if (t == 0x4C) ++towns;
        }
        std::printf("province %2d race %2d | empire %3d highway %d linked towns %d (max %d) of %d | road score %d | battles %d won %d lost %d | peace %d\n",
                    p, systems::battle::kProvinceRace[static_cast<size_t>(p)], gw(game.state, admin::kEmpire),
                    gw(game.state, 0x6C8C), game.sim.linked_towns, linked_max, towns_total, gw(game.state, 0x6C86),
                    game.battles, game.battles_won, game.battles_lost, gw(game.state, admin::kPeace));
        if (gw(game.state, admin::kEmpire) < 90) {
            ++bad;
            for (const auto& e : game.events)
                if (e.text.rfind("province", 0) == 0) std::printf("      [%d/%d] %s\n", e.year, e.month, e.text.c_str());
        }
        game.battles = game.battles_won = game.battles_lost = 0;
    }
    std::printf("%d provinces below 90 empire\n", bad);
    return 0;
}

// ---------------------------------------------------------------------------
// The fuzzer: a player who clicks at random. Everything goes through the same glue the viewer uses; the checks are that
// nothing crashes or hangs, the grids stay legal, and a save of the state reads back to the same state.

#include <random>

bool g_gentle = false;

void fuzz_actions(Game& game, std::mt19937& rng, int actions) {
    auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<unsigned>(n)); };
    const CommandId tools[] = {CommandId::Road,     CommandId::Wall,     CommandId::Plaza,      CommandId::ClearArea,
                               CommandId::Forum,    CommandId::Workshop, CommandId::Housing,    CommandId::Well,
                               CommandId::Fountain, CommandId::ReservoirPipe, CommandId::Temple, CommandId::BathHouses,
                               CommandId::Hospital, CommandId::School,   CommandId::Oracle,     CommandId::Theater,
                               CommandId::Coliseum, CommandId::Hippodrome, CommandId::Barracks, CommandId::Prefecture,
                               CommandId::Market,   CommandId::HeavyIndustry, CommandId::Tower};
    const systems::forum::Control controls[] = {
        systems::forum::Control::PopulationTax, systems::forum::Control::IndustrialTax, systems::forum::Control::Conscription,
        systems::forum::Control::ArmyWages,     systems::forum::Control::Welfare,       systems::forum::Control::Salary,
        systems::forum::Control::Donation};
    const systems::forum::Duty duties[] = {systems::forum::Duty::FirePrevention, systems::forum::Duty::BuildingMaintenance,
                                           systems::forum::Duty::RoadMaintenance, systems::forum::Duty::Construction,
                                           systems::forum::Duty::ArmyDuty};
        for (int k = 0; k < actions; ++k) {
            const int x = pick(model::kCityW), y = pick(model::kCityH);
            switch (pick(10)) {
                case 0: case 1: case 2: case 3: {
                    // a drag: a run of cells with one tool
                    CommandId tool = tools[pick(static_cast<int>(sizeof tools / sizeof tools[0]))];
                    if (g_gentle && (tool == CommandId::ClearArea || tool == CommandId::Wall)) tool = CommandId::Housing;
                    game.forum_grade = pick(8);
                    game.workshop_goods = pick(8);
                    const int len = 1 + pick(12);
                    const bool horizontal = pick(2) == 0;
                    for (int i = 0; i < len; ++i)
                        game.place_tool(tool, std::min(model::kCityW - 1, x + (horizontal ? i : 0)),
                                        std::min(model::kCityH - 1, y + (horizontal ? 0 : i)));
                    break;
                }
                case 4: case 5: {
                    static const int ids[] = {35, 36, 37, 41, 42, 29};
                    const int id = ids[pick(6)];
                    if (g_gentle && (id == 35 || id == 29)) break;
                    const int len = 1 + pick(10);
                    const int px = pick(40), py = pick(40);
                    for (int i = 0; i < len; ++i) {
                        const int cx = std::min(39, px + (pick(2) ? i : 0)), cy = std::min(39, py + (pick(2) ? 0 : i));
                        if (id == 29) {
                            if (economy::can_afford(game.state, 500) && economy::enough_plebs(game.state, id) &&
                                gw(game.state, 0x6C12) < 10 && systems::province::place_fort(game.state, cx, cy) >= 0)
                                economy::charge(game.state, 500);
                        } else {
                            game.province_place(id, cx, cy);
                        }
                    }
                    break;
                }
                case 6: {
                    int cohort = -1, army = -1;
                    for (int i = 0; i < model::kActorCount; ++i) {
                        const model::Actor& a = game.state.objects[static_cast<size_t>(i)];
                        if (!a.active()) continue;
                        if (a.type() == systems::province::kCohortType && (cohort < 0 || pick(2))) cohort = i;
                        if ((a.type() == systems::province::kArmyType || a.type() == systems::province::kSeaArmyType) &&
                            (army < 0 || pick(2)))
                            army = i;
                    }
                    if (cohort >= 0) {
                        switch (pick(4)) {
                            case 0: systems::province::order_halt(game.state, cohort); break;
                            case 1: systems::province::order_go_home(game.state, cohort); break;
                            case 2: systems::province::order_patrol(game.state, cohort, pick(40), pick(40), pick(40), pick(40)); break;
                            default:
                                if (army >= 0) systems::province::order_attack(game.state, cohort, army);
                        }
                    }
                    break;
                }
                case 7: {
                    const auto c = controls[pick(7)];
                    if (g_gentle && (c == systems::forum::Control::Donation || c == systems::forum::Control::Salary)) break;
                    const int times = g_gentle ? 1 : 1 + pick(30);
                    for (int i = 0; i < times; ++i) systems::forum::adjust(game.state, c, pick(2) ? 1 : -1);
                    break;
                }
                case 8: {
                    const auto d = duties[pick(5)];
                    const int times = 1 + pick(20);
                    for (int i = 0; i < times; ++i) {
                        if (pick(2)) systems::forum::raise_duty(game.state, d);
                        else systems::forum::lower_duty(game.state, d);
                    }
                    break;
                }
                default: {
                    // the Cohort advisor's buttons
                    if (pick(2)) systems::forum::next_cohort(game.state);
                    else systems::forum::toggle_mobilized(game.state);
                }
            }
        }
}

int run_fuzz(Game& game, unsigned seed, int months, int funding, int difficulty) {
    std::mt19937 rng(seed);
    auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<unsigned>(n)); };
    game.verbose = false;
    if (!game.start(funding, difficulty)) return 1;
    int problems = 0;
    auto problem = [&](const std::string& what) {
        ++problems;
        std::printf("FUZZ seed %u, %d/%d: %s\n", seed, game.sim.year, game.sim.month + 1, what.c_str());
    };
    const CommandId tools[] = {CommandId::Road,     CommandId::Wall,     CommandId::Plaza,      CommandId::ClearArea,
                               CommandId::Forum,    CommandId::Workshop, CommandId::Housing,    CommandId::Well,
                               CommandId::Fountain, CommandId::ReservoirPipe, CommandId::Temple, CommandId::BathHouses,
                               CommandId::Hospital, CommandId::School,   CommandId::Oracle,     CommandId::Theater,
                               CommandId::Coliseum, CommandId::Hippodrome, CommandId::Barracks, CommandId::Prefecture,
                               CommandId::Market,   CommandId::HeavyIndustry, CommandId::Tower};
    const systems::forum::Control controls[] = {
        systems::forum::Control::PopulationTax, systems::forum::Control::IndustrialTax, systems::forum::Control::Conscription,
        systems::forum::Control::ArmyWages,     systems::forum::Control::Welfare,       systems::forum::Control::Salary,
        systems::forum::Control::Donation};
    const systems::forum::Duty duties[] = {systems::forum::Duty::FirePrevention, systems::forum::Duty::BuildingMaintenance,
                                           systems::forum::Duty::RoadMaintenance, systems::forum::Duty::Construction,
                                           systems::forum::Duty::ArmyDuty};
    int accepted = 0;
    for (int m = 0; m < months; ++m) {
        fuzz_actions(game, rng, 1 + pick(40));
        if (!game.month() && !game.dismissed) {
            problem("month() gave up");
            break;
        }
        if (game.dismissed) break;
        if (game.promotion_pending) {
            game.promotion_pending = false;
            const int choice = pick(4);
            if (game.promotion_to_caesar) {
                if (choice == 0) {
                    admin::become_caesar(game.state);
                    break;
                }
                admin::defer_promotion(game.state, 9);
            } else if (choice < 2) {
                int difficulty_now = game.sim.difficulty;
                admin::accept_promotion(game.state, difficulty_now);
                game.sim.difficulty = difficulty_now;
                if (gw(game.state, 0x6C26) == 1 && !game.start_province()) problem("start_province failed");
                ++accepted;
            } else {
                admin::defer_promotion(game.state, choice == 2 ? 9 : 24);
            }
        }
        // the grids and words stay legal
        for (int y = 0; y < model::kCityH; ++y)
            for (int x = 0; x < model::kCityW; ++x) {
                const uint8_t t = game.state.city.tile[y][x];
                if (t > 0xFF || (t >= 0x5E && t < 0x82 && false)) problem("tile out of range");
            }
        const int funds = gw(game.state, economy::kFunds);
        if (funds < 0 || funds > 32767) problem("funds out of range: " + std::to_string(funds));
        if (gw(game.state, 0x6C0E) < 0) problem("negative population");
        for (uint16_t rating : {admin::kPeace, admin::kCulture, admin::kProsperity, admin::kEmpire, admin::kAverage})
            if (gw(game.state, rating) < 0 || gw(game.state, rating) > 100)
                problem("rating " + std::to_string(rating) + " out of 0-100: " + std::to_string(gw(game.state, rating)));
        // a save of the state reads back to the same state, once a year
        if (game.sim.month == 0) {
            try {
                const auto saved = model::serialize(game.state);
                const model::CityState back = model::load(saved);
                const auto again = model::serialize(back);
                if (saved.raw != again.raw) problem("save -> load -> save differs");
            } catch (const std::exception& e) {
                problem(std::string("save/load threw: ") + e.what());
            }
        }
    }
    std::printf("fuzz seed %u: %d months, year %d, rank %d, %d promotions taken, %d problems%s\n", seed, months, game.sim.year,
                gw(game.state, 0x6C30), accepted, problems, game.dismissed ? ", dismissed" : "");
    return problems != 0;
}

// ---------------------------------------------------------------------------
// playtest <dir> tutorial [funding [difficulty [years]]]: a newcomer who does what the viewer's tutorial pages say
// (apps/viewer/tutorial.hpp), a step at a time and no more cleverly than the pages put it, with the Tribune left on
// Automatic as a new player's is. It shows that each step's goal is reached by doing what its page asks, and what the
// advice comes to: the ratings year by year, and when Rome offers the promotion.
//
// The city is laid out in districts, one to a Forum: a road above and below it joined beside it, houses along both
// sides of each road within six cells of the Forum with a well every third plot, a market next to the Forum, bath
// houses either side and room for an oracle at the road's end. The workshops stand along a street of their own just
// below the first row of districts, with houses on its other side ("at the edge of the houses", page 5), each of the
// goods the industry advisor rates best among those not yet made twice. PT_NOVICE_FAR=1 puts them out of the way west
// of the city instead, to see what the page's advice is worth.
struct Novice {
    Game& g;
    model::CityState& s;
    Bot planner;  // for the province's paths only: it plays no part
    int fx = 0, fy = 0;
    std::vector<Pt> districts;  // each Forum's anchor, in the order they were founded
    std::vector<Pt> towns_done;
    int step = 0;
    bool forum_visited = false;
    int workshop_slot = 0, oracles = 0;
    bool far_workshops = std::getenv("PT_NOVICE_FAR") != nullptr;
    int reserve = std::getenv("PT_NOVICE_RESERVE") ? std::atoi(std::getenv("PT_NOVICE_RESERVE")) : 200;
    int enough_people = std::getenv("PT_NOVICE_PEOPLE") ? std::atoi(std::getenv("PT_NOVICE_PEOPLE")) : 2400;

    explicit Novice(Game& game) : g(game), s(game.state), planner(game) {
        g.frame_hook = nullptr;  // the Legion is left to itself, as a newcomer leaves it
        g.hook_owner = nullptr;
    }

    int funds() const { return gw(s, economy::kFunds); }
    viewer::TutorialCensus count() const { return viewer::tutorial_census(s); }
    viewer::TutorialGoal goal() const { return viewer::tutorial_step(step).goal; }
    bool put(CommandId tool, int x, int y) { return grass(s, x, y) && g.place_tool(tool, x, y); }
    void road(int x0, int x1, int y0, int y1) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) put(CommandId::Road, x, y);
    }
    // The step the tutorial is at after what was just done.
    void sync(const char* did) {
        const int before = step;
        step = viewer::tutorial_progress(step, count(), forum_visited);
        if (step != before) {
            std::printf("%d/%d  %-22s -> step %d, \"%s\" (funds %d)\n", g.sim.year, g.sim.month + 1, did, step,
                        viewer::tutorial_step(step).title, funds());
            forum_visited = false;
        }
    }

    // A district's rows: the Forum's own two, a road above and below, and a row of houses beyond each road. Districts
    // south of the first leave room for the workshops' street between them.
    Pt district(int i, int j) const { return {fx + 18 * i, fy + (j >= 0 ? 10 : 6) * j}; }
    // A district's plots, the nearest to the Forum first: the two rows along the road below it, then (once the road
    // above is laid) the two along that. Every third column is left for a well.
    static bool well_column(int dx) { return ((dx + 6) % 3 + 3) % 3 == 1; }
    std::vector<Pt> plots(Pt f, bool wells, bool upper) const {
        std::vector<Pt> out;
        for (int dy : {1, 3, 0, -2}) {
            if (!upper && (dy == 0 || dy == -2)) continue;
            for (int k = 0; k < 14; ++k) {
                const int dx = k % 2 ? (k + 1) / 2 : -k / 2;  // 0, 1, -1, 2, -2 ... 7, -6
                if (dx < -6 || dx > 7 || well_column(dx) != wells) continue;
                if ((dy == 0 || dy == 1) && dx >= -1 && dx <= 4) continue;  // the Forum, the joining roads, the market
                if (dy == 1 && (dx == -4 || dx == 6) && !wells) continue;   // the bath houses
                out.push_back({f.x + dx, f.y + dy});
            }
        }
        return out;
    }
    bool is_house(int x, int y) const {
        return x >= 0 && y >= 0 && x < model::kCityW && y < model::kCityH && s.city.tile[y][x] >= 0xC8 && s.city.tile[y][x] <= 0xD7;
    }
    int houses(Pt f, int want, bool upper) {
        int n = 0;
        for (const Pt& p : plots(f, false, upper))
            if (n < want && put(CommandId::Housing, p.x, p.y)) ++n;
        return n;
    }
    // A well on each well plot that touches a house.
    int wells(Pt f) {
        int n = 0;
        for (const Pt& p : plots(f, true, true)) {
            bool touches = false;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) touches = touches || is_house(p.x + dx, p.y + dy);
            if (touches && put(CommandId::Well, p.x, p.y)) ++n;
        }
        return n;
    }
    bool found_district(Pt f) {
        if (funds() < 300 + reserve) return false;
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx)
                if (!grass(s, f.x + dx, f.y + dy)) return false;
        g.forum_grade = 0;  // Aventine
        if (!g.place_tool(CommandId::Forum, f.x, f.y)) return false;
        districts.push_back(f);
        return true;
    }
    void lower_road(Pt f) { road(f.x - 8, f.x + 9, f.y + 2, f.y + 2); }
    void upper_road(Pt f) {
        road(f.x - 8, f.x + 9, f.y - 1, f.y - 1);
        road(f.x - 1, f.x - 1, f.y, f.y + 1);
        road(f.x + 2, f.x + 2, f.y, f.y + 1);
    }
    bool market(Pt f) { return put(CommandId::Market, f.x + 3, f.y); }
    int baths(Pt f) { return put(CommandId::BathHouses, f.x - 4, f.y + 1) + put(CommandId::BathHouses, f.x + 6, f.y + 1); }

    // The goods for the next workshop: the best the industry advisor rates (page 5 sends the player to him) among
    // those made by fewer than two workshops yet -- "different goods for each".
    int next_goods() const {
        const systems::forum::IndustryReport r = systems::forum::industry_report(s);
        int best = -1;
        for (int i = 0; i < 8; ++i) {
            const auto& row = r.rows[static_cast<size_t>(i)];
            if (row.factories >= 2) continue;
            if (best < 0 || row.suitability > r.rows[static_cast<size_t>(best)].suitability) best = i;
        }
        return best < 0 ? 0 : best;
    }
    // The workshops' street: below the first row of districts (and then the second), a workshop every four columns
    // with its road under it, joined to the district's road at its west end. A place on rough ground or under a
    // district not founded yet is passed over.
    bool workshop() {
        if (gw(s, 0x6C9E) >= 30 || funds() < 50 + reserve) return false;
        g.workshop_goods = next_goods();
        for (int tries = 0; tries < 12 && workshop_slot < 52; ++tries) {
            const int slot = workshop_slot++;
            int x, y;
            if (far_workshops) {  // out of the way, west of the first district
                y = fy + 6 * (slot / 4) + 3;
                x = fx - 13 - 4 * (slot % 4);
                road(x - 1, fx - 9, y - 1, y - 1);
                if (slot / 4 > 0) road(fx - 9, fx - 9, fy + 2, y - 1);
                if (g.place_tool(CommandId::Workshop, x, y)) return true;
                continue;
            }
            const int street = slot / 13, m = slot % 13;
            x = fx - 8 + 4 * m;
            y = fy + 10 * street + 4;
            if (std::find(districts.begin(), districts.end(), district(m * 4 / 18, street)) == districts.end()) continue;
            if (!g.place_tool(CommandId::Workshop, x, y)) continue;
            road(fx - 9, x + 3, y + 3, y + 3);            // its street
            road(fx - 9, fx - 9, y - 2, y + 3);           // up to the district's road
            return true;
        }
        return false;
    }
    bool oracle() {
        if (funds() < 200 + reserve) return false;
        for (const Pt& f : districts)
            for (int dx : {8, -8})
                for (int dy : {0, 1})
                    if (grass(s, f.x + dx, f.y + dy) && grass(s, f.x + dx + 1, f.y + dy) &&
                        g.place_tool(CommandId::Oracle, f.x + dx, f.y + dy)) {
                        ++oracles;
                        return true;
                    }
        return false;
    }
    // The next district: east of the first, then below, then above, wherever the Forum's ground is open.
    bool next_district() {
        static const Pt kOrder[] = {{1, 0}, {0, 1}, {1, 1}, {0, -1}, {1, -1}, {2, 0}, {2, 1}, {2, -1},
                                    {0, 2}, {1, 2}, {2, 2}, {0, -2}, {1, -2}, {2, -2}};
        for (const Pt& o : kOrder) {
            const Pt f = district(o.x, o.y);
            if (f.x + 10 >= model::kCityW || f.x - 9 < 0 || f.y - 3 < 0 || f.y + 9 >= model::kCityH) continue;
            if (std::find(districts.begin(), districts.end(), f) != districts.end()) continue;
            if (!found_district(f)) continue;
            lower_road(f);
            upper_road(f);
            road(fx - 9, fx - 9, std::min(fy, f.y) - 1, std::max(fy, f.y) + 2);  // one street joins the rows of districts
            road(fx - 9, fx - 8, f.y + 2, f.y + 2);
            market(f);
            baths(f);
            return true;
        }
        return false;
    }

    // What the pages give as the work each building offers.
    int jobs(const viewer::TutorialCensus& c) const { return 80 * c.workshops + 48 * c.markets + 120 * c.forums; }

    // The province: the highway, then a road for each town, laid a cell at a time as far as the month's funds go above
    // the reserve ("as the funds allow"). A town's road runs to the city or, where that is shorter, onto the Highway,
    // which it crosses to share (page 11).
    int laying = 0;           // the command of the road being laid: 42 the highway, 36 a town's, 0 none
    std::vector<Pt> route;    // its cells
    size_t route_next = 0;
    int cell_cost(int command, Pt p) const {
        const uint8_t tile = planner.ptile(p.x, p.y);
        if ((tile >= 0x36 && tile <= 0x41) || tile == 0x7B || tile == 0x7C) return 0;  // a road already
        if (command == 42 && tile >= 0x6D && tile <= 0x77) return 0;
        return economy::kConstructionCost[static_cast<size_t>(command)] << economy::province_cost_shift(command, tile);
    }
    // Lays what the funds allow of the route. True once it is whole.
    std::vector<std::pair<int, Pt>> laid;  // every piece laid, with its command: what a worn piece is laid again from
    int relaid = 0;
    bool lay_route() {
        while (route_next < route.size()) {
            const Pt p = route[route_next];
            if (funds() < cell_cost(laying, p) + reserve) return false;
            g.province_place(laying, p.x, p.y);
            laid.push_back({laying, p});
            ++route_next;
        }
        laying = 0;
        return true;
    }
    // Page 10: a piece that has worn away (open ground again) is laid again.
    void repair() {
        for (const auto& [command, p] : laid) {
            const uint8_t tile = planner.ptile(p.x, p.y);
            if (tile < 0x1D || tile > 0x35 || funds() < cell_cost(command, p) + 50) continue;
            if (g.province_place(command, p.x, p.y)) ++relaid;
        }
    }
    bool highway() {
        if (laying != 42) {
            route = planner.province_path({0x78}, {0x4A, 0x4B}, true);
            if (route.empty()) return false;
            laying = 42;
            route_next = 0;
        }
        return lay_route();
    }
    bool town_road() {
        if (laying != 36) {
            static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
            route = planner.province_path({0x4A, 0x4B}, {0x61}, false, towns_done);
            // Onto the Highway instead: from a cell beside one of its straight pieces, with the piece itself as the
            // road's first cell (laid over it, a road makes a crossing).
            auto spur = planner.province_path({0x6D, 0x6E}, {0x61}, false, towns_done);
            if (!spur.empty() && !std::getenv("PT_NOVICE_NO_SPUR") &&
                (route.empty() || planner.path_cost(spur, 36) + 30 < planner.path_cost(route, 36))) {
                for (int d = 0; d < 4; ++d) {
                    const Pt h{spur.front().x + dx[d], spur.front().y + dy[d]};
                    if (Bot::in_map(h.x, h.y) && (planner.ptile(h.x, h.y) == 0x6D || planner.ptile(h.x, h.y) == 0x6E)) {
                        spur.insert(spur.begin(), h);
                        route = spur;
                        break;
                    }
                }
            }
            if (route.empty()) return false;
            for (int d = 0; d < 4; ++d) {
                const Pt town{route.back().x + dx[d], route.back().y + dy[d]};
                if (Bot::in_map(town.x, town.y) && planner.ptile(town.x, town.y) == 0x61) towns_done.push_back(town);
            }
            laying = 36;
            route_next = 0;
            std::printf("%d/%d  a town's road planned, %zu cells, %d Dn\n", g.sim.year, g.sim.month + 1, route.size(),
                        planner.path_cost(route, 36));
        }
        return lay_route();
    }
    // A dozen more plots where there is room, with their wells; a new district when there is none.
    void more_houses() {
        int placed = 0;
        for (const Pt& f : districts) {
            if (placed >= 12) break;
            placed += houses(f, 12 - placed, false);
            if (placed < 12) {
                upper_road(f);
                placed += houses(f, 12 - placed, true);
            }
            wells(f);
        }
        if (placed == 0) next_district();
    }

    // One month's play: the step's own task, then what the pages say to keep doing.
    void act() {
        using viewer::TutorialGoal;
        const Pt first{fx, fy};
        switch (goal()) {
            case TutorialGoal::Read:
                if (step == 0) step = 1;  // Continue
                break;
            case TutorialGoal::Forum: found_district(first); break;
            case TutorialGoal::Roads: lower_road(first); break;
            case TutorialGoal::Housing: houses(first, 12, false); break;
            case TutorialGoal::Water: wells(first); break;
            case TutorialGoal::Work:
                market(first);
                while (count().workshops < viewer::kTutorialWorkshops && workshop()) {
                }
                break;
            case TutorialGoal::BathHouse: baths(first); break;
            case TutorialGoal::VisitForum:
                forum_visited = true;
                // Page 7: the people's tax from 5 to 6 percent.
                while (gw(s, 0x6C04) < 6) systems::forum::adjust(s, systems::forum::Control::PopulationTax, 1);
                break;
            case TutorialGoal::Highway: highway(); break;
            case TutorialGoal::Town: town_road(); break;
            case TutorialGoal::Oracle: oracle(); break;
            default: break;
        }
        sync("the step's task");
        if (step < 8) return;
        repair();
        // Pages 8 and 9, from then on: work for the people there are, an oracle for every 2000, houses while there is
        // work -- and (page 12) once the city stands and the towns are linked, nothing more, so the years close in profit.
        viewer::TutorialCensus c = count();
        const bool standing = c.population >= enough_people && c.highway_linked && c.towns_linked >= c.towns;
        if (!standing) {
            for (int guard = 0; guard < 4 && c.population > jobs(c) && workshop(); ++guard) c = count();
            if (step > 9 && oracles < (c.population + 1999) / 2000) oracle();
            if (c.population < enough_people && funds() > 50 + reserve && jobs(c) >= c.population &&
                c.population * 11 >= c.house_cells * 100)
                more_houses();
            if (step > 11 && c.highway_linked && (laying == 36 || static_cast<int>(towns_done.size()) < c.towns)) town_road();
        }
        sync("keeping the city");
    }
};

int run_tutorial(Game& game, int funding, int difficulty, int years) {
    game.verbose = false;
    if (!game.start(funding, difficulty)) return 1;
    Novice novice(game);
    if (!novice.planner.choose_site()) return 1;
    novice.fx = novice.planner.fx;
    novice.fy = novice.planner.fy;
    const int first_year = game.sim.year;
    int last_year = game.sim.year;
    std::puts("  year  step  people  houses  wksp forums  funds   peace culture prosperity empire average  towns highway");
    while (game.sim.year < first_year + years && !game.dismissed && !game.promotion_pending) {
        viewer::tribune_assist(game.state, game.sim.difficulty);  // Settings > Game > Tribune: Automatic, each month
        novice.act();
        if (!game.month()) break;
        if (game.sim.year == last_year) continue;
        last_year = game.sim.year;
        const viewer::TutorialCensus c = novice.count();
        std::printf("  %-5d %-5d %-7d %-7d %-4d %-6d  %-7d %-5d %-7d %-10d %-6d %-7d  %d/%d   %d\n", game.sim.year, novice.step,
                    c.population, c.house_cells, c.workshops, c.forums, novice.funds(), gw(game.state, admin::kPeace),
                    gw(game.state, admin::kCulture), gw(game.state, admin::kProsperity), gw(game.state, admin::kEmpire),
                    gw(game.state, admin::kAverage), c.towns_linked, c.towns, static_cast<int>(c.highway_linked));
        if (std::getenv("PT_ACCOUNTS")) {
            print_accounts(game.state);
            std::printf("      workshops' average level %d, without work %d%%\n", economy::average_workshop_level(game.state),
                        gw(game.state, 0x6BCC));
            namespace plebs = systems::plebs;
            std::printf("      plebs %d (unassigned %d, welfare %d): fire %d/%d building %d/%d road %d/%d province %d/%d army %d"
                        " | province pieces %d, wear threshold %d\n",
                        gw(game.state, plebs::kPlebs), gw(game.state, plebs::kUnassigned), gw(game.state, plebs::kWelfare),
                        gw(game.state, plebs::kFirePrevention), gw(game.state, plebs::kFireNeed),
                        gw(game.state, plebs::kBuildingMaintenance), gw(game.state, plebs::kBuildingNeed),
                        gw(game.state, plebs::kRoadMaintenance), gw(game.state, plebs::kRoadNeed),
                        gw(game.state, plebs::kConstruction), plebs::set_needs(game.state, game.sim.difficulty),
                        gw(game.state, plebs::kArmyDuty), gw(game.state, 0x6C8A), game.sim.province_wear_threshold);
        }
    }
    if (std::getenv("PT_MAP")) print_city(game.state, novice.fx - 30, novice.fy - 16, novice.fx + 50, novice.fy + 28);
    for (const auto& [k, v] : game.refusals) std::printf("refusal: %s x%d\n", k.c_str(), v);
    if (novice.relaid) std::printf("province pieces laid again after wearing away: %d\n", novice.relaid);
    if (game.promotion_pending) {
        int difficulty_now = game.sim.difficulty;
        admin::accept_promotion(game.state, difficulty_now);
        novice.sync("the promotion accepted");
        std::printf("PROMOTED after %d years: the tutorial is at its last page (%s)\n", game.sim.year - first_year,
                    novice.step == viewer::kTutorialFinal ? "yes" : "NO");
        return novice.step == viewer::kTutorialFinal ? 0 : 1;
    }
    std::printf("no promotion in %d years%s; the tutorial stands at step %d\n", years, game.dismissed ? " (dismissed)" : "",
                novice.step);
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: %s <game folder> probe [funding [difficulty]]\n"
                     "       %s <game folder> play  [funding [difficulty [years]]]\n",
                     argv[0], argv[0]);
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Game game;
    game.dir = argv[1];
    const std::string command = argv[2];
    const int funding = argc > 3 ? std::atoi(argv[3]) : 0;
    const int difficulty = argc > 4 ? std::atoi(argv[4]) : 0;
    if (command == "probe") {
        if (!game.start(funding, difficulty)) return 1;
        print_city(game.state);
        print_province(game.state);
        return 0;
    }
    if (command == "maps") {
        const int rank = argc > 3 ? std::atoi(argv[3]) : 3;
        const int years = argc > 4 ? std::atoi(argv[4]) : 6;
        const int first = argc > 5 ? std::atoi(argv[5]) : 0;
        const int last = argc > 6 ? std::atoi(argv[6]) : 49;
        return run_maps(game, rank, years, first, last);
    }
    if (command == "fuzz") {
        // playtest <dir> fuzz [seed [months [funding [difficulty]]]]
        const unsigned seed = argc > 3 ? static_cast<unsigned>(std::atoi(argv[3])) : 1;
        const int months = argc > 4 ? std::atoi(argv[4]) : 600;
        const int f = argc > 5 ? std::atoi(argv[5]) : static_cast<int>(seed % 10);
        const int d = argc > 6 ? std::atoi(argv[6]) : static_cast<int>(seed % 3);
        return run_fuzz(game, seed, months, f, d);
    }
    if (command == "tribune" && argc > 3) {
        // playtest <dir> tribune <save> [years [mode]]: what the Tribune's duties do to a city left alone. A real save runs
        // for `years` with the duties as saved (mode "saved"), put back to a new game's (10 on each, welfare 88: "default"),
        // or kept by the viewer's Tribune: Automatic ("assist"). Prints the share of the months a fire, a collapse or a road
        // wear roll could hit (the thresholds the pleb coverage sets: a roll fires when the generator's walk, 1-99, beats
        // it, so 100 is never), and what the city is at the end.
        const int years = argc > 4 ? std::atoi(argv[4]) : 5;
        const std::string mode = argc > 5 ? argv[5] : "saved";
        game.state = model::load(formats::save::load(argv[3]));
        game.sim = systems::month::sim_state_from_save(game.state);
        game.install_hooks();
        if (mode == "default") {
            using systems::plebs::kArmyDuty;
            for (uint16_t d : {systems::plebs::kFirePrevention, systems::plebs::kBuildingMaintenance,
                               systems::plebs::kRoadMaintenance, systems::plebs::kConstruction, kArmyDuty})
                model::set_global_word(game.state, d, 10);
            model::set_global_word(game.state, systems::plebs::kWelfare, 88);
        }
        const Census before = census(game.state);
        double fire = 0, collapse = 0, wear = 0;
        int months = 0;
        for (int m = 0; m < years * 12; ++m) {
            if (mode == "assist") viewer::tribune_assist(game.state, game.sim.difficulty);
            if (!game.month()) break;
            if (game.promotion_pending) break;
            // the chance of a roll's hit this month, from the thresholds the month set: (99 - threshold) / 99
            fire += (99.0 - std::min(99, gw(game.state, 0x6BE4))) / 99.0;
            collapse += (99.0 - std::min(99, gw(game.state, 0x6BE2))) / 99.0;
            wear += (99.0 - std::min(99, gw(game.state, 0x6BE0))) / 99.0;
            ++months;
        }
        const Census after = census(game.state);
        std::printf("%s, %d years (%d months): chance of a fire a month %.0f%%, a collapse %.0f%%, road wear %.0f%% | "
                    "houses %d -> %d cells, rubble %d, plebs %d, welfare %d, funds %d\n", mode.c_str(), years, months,
                    100.0 * fire / std::max(1, months), 100.0 * collapse / std::max(1, months),
                    100.0 * wear / std::max(1, months), before.houses_cells, after.houses_cells, after.rubble,
                    gw(game.state, 0x6C56), gw(game.state, 0x6C46), gw(game.state, economy::kFunds));
        return 0;
    }
    if (command == "tutorial") {
        // playtest <dir> tutorial [funding [difficulty [years]]]: a newcomer following the viewer's tutorial (Novice, above)
        return run_tutorial(game, funding, difficulty, argc > 5 ? std::atoi(argv[5]) : 20);
    }
    if (command == "inspect" && argc > 3) {
        // a real save, through the same houses report: usage `playtest <dir> inspect <save>`
        model::CityState st = model::load(formats::save::load(argv[3]));
        game.state = st;
        game.sim = systems::month::sim_state_from_save(st);
        print_house_detail(game.state);
        print_status(game, "save ");
        print_accounts(game.state);
        const Census c = census(game.state);
        std::printf("roads %d, houses %d cells\n", c.roads, c.houses_cells);
        return 0;
    }
    if (command == "play") {
        const int years = argc > 5 ? std::atoi(argv[5]) : 2500;
        game.verbose = std::getenv("PT_QUIET") == nullptr;
        if (!game.start(funding, difficulty)) return 1;
        if (const char* r = std::getenv("PT_START_RANK")) {
            // begin at a later rank, on a chosen province (default: the next one the game would give)
            model::set_global_word(game.state, 0x6C30, std::atoi(r));
            if (const char* pr = std::getenv("PT_START_PROVINCE")) model::set_global_word(game.state, 0x6CA6, std::atoi(pr));
            if (const char* yr = std::getenv("PT_START_YEAR")) model::set_global_word(game.state, 0x6C32, std::atoi(yr));
            if (const char* sd = std::getenv("PT_SEED"))
                for (int i = 0; i < std::atoi(sd) * 61 + 7; ++i) game.sim.random.advance();
            if (!game.start_province()) return 1;
        }
        if (const char* t = std::getenv("PT_TAX")) {
            int guard = 0;
            while (gw(game.state, 0x6C04) < std::atoi(t) && guard++ < 30)
                systems::forum::adjust(game.state, systems::forum::Control::PopulationTax, 1);
            while (gw(game.state, 0x6C04) > std::atoi(t) && guard++ < 60)
                systems::forum::adjust(game.state, systems::forum::Control::PopulationTax, -1);
        }
        if (const char* t = std::getenv("PT_CONS")) {
            int guard = 0;
            while (gw(game.state, 0x6C06) < std::atoi(t) && guard++ < 60)
                systems::forum::adjust(game.state, systems::forum::Control::Conscription, 1);
            while (gw(game.state, 0x6C06) > std::atoi(t) && guard++ < 120)
                systems::forum::adjust(game.state, systems::forum::Control::Conscription, -1);
        }
        if (const char* t = std::getenv("PT_ITAX")) {
            int guard = 0;
            while (gw(game.state, 0x6C02) < std::atoi(t) && guard++ < 30)
                systems::forum::adjust(game.state, systems::forum::Control::IndustrialTax, 1);
            while (gw(game.state, 0x6C02) > std::atoi(t) && guard++ < 60)
                systems::forum::adjust(game.state, systems::forum::Control::IndustrialTax, -1);
        }
        // PT_CHAOS=<seed>: the bot plays, and a monkey clicks at random on top of it (PT_CHAOS_N actions a month)
        std::unique_ptr<std::mt19937> chaos;
        int chaos_actions = std::getenv("PT_CHAOS_N") ? std::atoi(std::getenv("PT_CHAOS_N")) : 3;
        g_gentle = std::getenv("PT_CHAOS_GENTLE") != nullptr;
        if (const char* c = std::getenv("PT_CHAOS")) chaos = std::make_unique<std::mt19937>(static_cast<unsigned>(std::atoi(c)));
        const int target_rank = std::getenv("PT_RANK") ? std::atoi(std::getenv("PT_RANK")) : 20;
        struct RankRecord {
            int rank, province, start_year, end_year, peace, culture, prosperity, empire, pop, funds, invasions, battles;
        };
        std::vector<RankRecord> career;
        int record_province = gw(game.state, 0x6CA6);
        int record_invasions = 0, record_battles = 0;
        const int max_deferrals = std::getenv("PT_DEFERRALS") ? std::atoi(std::getenv("PT_DEFERRALS")) : 8;
        int deferrals = 0, last_total = -1;
        const int max_savings_deferrals = std::getenv("PT_SAVINGS_DEFERRALS") ? std::atoi(std::getenv("PT_SAVINGS_DEFERRALS")) : 5;
        auto bot = std::make_unique<Bot>(game);
        auto begin_city = [&]() -> bool {
            bot = std::make_unique<Bot>(game);
            bot->first_year = game.sim.year;
            if (!bot->choose_site()) return false;
            bot->plan();
            return true;
        };
        if (!begin_city()) return 1;
        const int end_year = game.sim.year + years;
        int last_year = game.sim.year - 1;
        const bool trace = std::getenv("PT_TRACE") != nullptr;
        int province_start_year = game.sim.year;
        std::map<std::string, int> prev_spent;
        while (game.sim.year < end_year && !game.dismissed && gw(game.state, 0x6C30) < target_rank) {
            if (game.promotion_pending) {
                // The promotion screen: the player accepts (the bot always does).
                game.promotion_pending = false;
                print_status(game, "PROMOTION OFFERED");
                if (std::getenv("PT_WKS")) print_workshops(game.state);
                if (std::getenv("PT_MAP")) print_city(game.state, bot->fx - 40, bot->fy - 45, bot->fx + 41, bot->fy + 46);
                save_milestone(game, "PROMO" + std::to_string(gw(game.state, 0x6C30)) + ".SAV");
                std::printf("   ratings needed: average %d, each %d; years in the province: %d\n",
                            admin::kPromotion[static_cast<size_t>(gw(game.state, 0x6C30))].average,
                            admin::kPromotion[static_cast<size_t>(gw(game.state, 0x6C30))].each,
                            game.sim.year - province_start_year);
                career.push_back({gw(game.state, 0x6C30), record_province, province_start_year, game.sim.year,
                                  gw(game.state, admin::kPeace), gw(game.state, admin::kCulture), gw(game.state, admin::kProsperity),
                                  gw(game.state, admin::kEmpire), gw(game.state, 0x6C0E), gw(game.state, economy::kFunds),
                                  game.invasions - record_invasions, game.battles - record_battles});
                record_invasions = game.invasions;
                record_battles = game.battles;
                if (game.promotion_to_caesar) {
                    // The title is there for the asking. First make the record as good as it gets: wait while the four ratings
                    // are still rising, to a hundred each.
                    const int total = gw(game.state, admin::kPeace) + gw(game.state, admin::kCulture) +
                                      gw(game.state, admin::kProsperity) + gw(game.state, admin::kEmpire);
                    // then, with the ratings full, a few years more for the governor's savings to fill (the salary is paid into
                    // them each year, to 25000)
                    const int savings = gw(game.state, 0x6C2E);
                    const bool want_savings = total >= 400 && savings < 24900 && deferrals < max_savings_deferrals;
                    if ((total < 400 || want_savings) && deferrals < max_deferrals) {
                        std::printf("   the title of Caesar is put off for nine years: ratings %d + %d + %d + %d\n",
                                    gw(game.state, admin::kPeace), gw(game.state, admin::kCulture),
                                    gw(game.state, admin::kProsperity), gw(game.state, admin::kEmpire));
                        admin::defer_promotion(game.state, 9);
                        last_total = total;
                        ++deferrals;
                        bot->final_stage = true;
                        career.pop_back();
                        continue;
                    }
                    admin::become_caesar(game.state);
                    std::printf("you are Caesar\n");
                    break;
                }
                int difficulty_now = game.sim.difficulty;
                admin::accept_promotion(game.state, difficulty_now);
                game.sim.difficulty = difficulty_now;
                std::printf("promotion accepted: %s\n", admin::kRankNames[static_cast<size_t>(gw(game.state, 0x6C30))]);
                if (gw(game.state, 0x6C26) == 1) {
                    if (!game.start_province()) break;
                    record_province = gw(game.state, 0x6CA6);
                    if (gw(game.state, 0x6C30) >= target_rank) break;
                    province_start_year = game.sim.year;
                    if (!begin_city()) return 1;
                    last_year = game.sim.year - 1;
                }
                continue;
            }
            if (std::getenv("PT_RICH")) model::set_global_word(game.state, economy::kFunds, std::getenv("PT_RICH_FUNDS") ? std::atoi(std::getenv("PT_RICH_FUNDS")) : 30000);
            bot->act();
            if (chaos) fuzz_actions(game, *chaos, chaos_actions);
            if (!game.month()) break;
            if (std::getenv("PT_DUTIES") && game.sim.year >= std::atoi(std::getenv("PT_DUTIES"))) {
                const Census c = census(game.state);
                std::printf("   %d/%d plebs %d (welfare %d, unassigned %d) | fire %d/%d building %d/%d road %d/%d province %d/%d | "
                            "thresholds fire %d collapse %d road %d | buildings %d roads %d | houses %d forums %d wksp %d rubble %d "
                            "fire %d | funds %d\n",
                            game.sim.year, game.sim.month, gw(game.state, 0x6C56), gw(game.state, 0x6C46),
                            gw(game.state, 0x6C58), gw(game.state, 0x6C62), gw(game.state, 0x6C44), gw(game.state, 0x6C60),
                            gw(game.state, 0x6C42), gw(game.state, 0x6C5E), gw(game.state, 0x6C40), gw(game.state, 0x6C5C),
                            bot->construction_need_now(), gw(game.state, 0x6BE4), gw(game.state, 0x6BE2),
                            gw(game.state, 0x6BE0), gw(game.state, 0x6BF2), gw(game.state, 0x6BF0), c.houses_cells, c.forums,
                            c.workshops, c.rubble, c.fire, gw(game.state, economy::kFunds));
            }
            if (std::getenv("PT_MIL")) {
                for (int i = 0; i < model::kActorCount; ++i) {
                    const model::Actor& a = game.state.objects[static_cast<size_t>(i)];
                    if (!a.active()) continue;
                    if (a.type() == systems::province::kCohortType)
                        std::printf("   mil %d/%d cohort slot %d at (%d,%d) state %d men %d/%d/%d morale %d", game.sim.year,
                                    game.sim.month + 1, i, a.screen_x() >> 4, a.screen_y() >> 4, a.state(), a.raw[0x20],
                                    a.raw[0x21], a.raw[0x1D], a.raw[0x2B]);
                    else if (a.type() == systems::province::kArmyType || a.type() == systems::province::kSeaArmyType)
                        std::printf("   mil %d/%d army slot %d at (%d,%d) state %d size %d", game.sim.year, game.sim.month + 1, i,
                                    a.screen_x() >> 4, a.screen_y() >> 4, a.state(), a.raw[0x30]);
                    else
                        continue;
                    std::putchar(10);
                }
            }
            if (std::getenv("PT_HWTRACE")) {
                int hw = 0, rd = 0;
                for (uint8_t c : game.state.empire.cells) {
                    const int t = c & 0x7F;
                    if (t >= 0x6D && t <= 0x78) ++hw;
                    if (t >= 0x36 && t <= 0x41) ++rd;
                }
                std::printf("   %d/%d province: highway cells %d road cells %d | 0x6C8C %d wear target 0x6C88 %d | duty %d need %d "
                            "plebs %d unassigned %d\n",
                            game.sim.year, game.sim.month, hw, rd, gw(game.state, 0x6C8C), gw(game.state, 0x6C88),
                            gw(game.state, 0x6C5C), systems::plebs::set_needs(game.state, game.sim.difficulty),
                            gw(game.state, 0x6C56), gw(game.state, 0x6C58));
            }
            if (trace) {
                const Census c = census(game.state);
                std::printf("   m%d houses %d units %d | base %d band %d unemployed %d%% growth %d | markets %d forums %d "
                            "wksp %d | funds %d\n",
                            game.sim.month, c.houses_cells, gw(game.state, 0x6C10), gw(game.state, 0x6BF8),
                            gw(game.state, 0x6BFA), gw(game.state, 0x6BCC), gw(game.state, 0x6BF6),
                            gw(game.state, 0x6BEE), gw(game.state, 0x6CA0), gw(game.state, 0x6C9E),
                            gw(game.state, economy::kFunds));
            }
            if (game.sim.year != last_year) {
                last_year = game.sim.year;
                if ((game.sim.year - province_start_year) % 5 == 0)
                    save_milestone(game, "R" + std::to_string(gw(game.state, 0x6C30)) + "Y" +
                                             std::to_string(game.sim.year - province_start_year) + ".SAV");
                print_status(game, "year");
                print_accounts(game.state);
                if (std::getenv("PT_SPENT")) {
                    std::printf("      spent this year:");
                    for (const auto& [k, v] : bot->spent) {
                        const int d = v - prev_spent[k];
                        if (d) std::printf(" %s %d", k.c_str(), d);
                    }
                    std::printf("\n");
                    prev_spent = bot->spent;
                }
                if (std::getenv("PT_PROVINCE")) print_province_status(game);
            }
        }
        print_status(game, "end  ");
        std::printf("invasions %d, battles %d (won %d, lost %d, retreated %d)", game.invasions, game.battles, game.battles_won, game.battles_lost, game.battles_retreated);
        std::putchar(10);
        if (std::getenv("PT_MAP")) print_city(game.state, bot->fx - 50, bot->fy - 48, bot->fx + 51, bot->fy + 49);
        else print_city(game.state, bot->fx - 40, bot->fy - 20, bot->fx + 41, bot->fy + 21);
        if (std::getenv("PT_WKS")) print_workshops(game.state);
        if (std::getenv("PT_LVMAP")) {
            // land value (0-9, a-z for 10-35, - for negative, . for none), network flag as upper case where missing on a house
            const auto& st = game.state;
            std::puts("land value (house cells), '_' no house; digits/capitals = network flag present, a-j (0-9) and k-z (10+) = none:");
            for (int y = std::max(0, bot->fy - 22); y < std::min(model::kCityH, bot->fy + 23); ++y) {
                for (int x = std::max(0, bot->fx - 40); x < std::min(model::kCityW, bot->fx + 41); ++x) {
                    const uint8_t t = st.city.tile[y][x];
                    char ch = '_';
                    if (t >= 0xC8 && t <= 0xD7) {
                        const int lv = static_cast<int8_t>(st.city.land_value[y][x]);
                        const bool net = (st.city.service_flags[y][x] & 2) != 0;
                        if (lv < 0) ch = '-';
                        else if (net) ch = lv < 10 ? static_cast<char>('0' + lv) : static_cast<char>('A' + std::min(25, lv - 10));
                        else ch = lv < 10 ? static_cast<char>('a' + lv) : static_cast<char>('k' + std::min(15, lv - 10));
                    } else if (t >= 0x36 && t <= 0x43) {
                        ch = '#';
                    } else if (t != 0x1D && t >= 0xE0) {
                        ch = '*';
                    }
                    std::putchar(ch);
                }
                std::putchar(10);
            }
        }
        for (const auto& [k, v] : game.refusals) std::printf("refusal: %s x%d\n", k.c_str(), v);
        for (const auto& [k, v] : bot->spent) std::printf("spent on %s: %d Dn\n", k.c_str(), v);
        if (!career.empty()) {
            std::puts("CAREER SUMMARY");
            std::puts("  rank  title          province  years   in office  peace culture prosperity empire  pop     funds  invasions battles");
            for (const RankRecord& r : career) {
                std::printf("  %-4d  %-13s  %-8d  %d..%-4d %-9d  %-5d %-7d %-10d %-6d %-7d %-6d %-9d %d", r.rank,
                            admin::kRankNames[static_cast<size_t>(std::min(r.rank, 20))], r.province, r.start_year, r.end_year,
                            r.end_year - r.start_year, r.peace, r.culture, r.prosperity, r.empire, r.pop, r.funds, r.invasions, r.battles);
                std::putchar(10);
            }
            std::printf("final: rank %d (%s), year %d; peace %d, culture %d, prosperity %d, empire %d (average %d); savings %d Dn, imperial favour %d%%",
                        gw(game.state, 0x6C30), admin::kRankNames[static_cast<size_t>(std::min(gw(game.state, 0x6C30), 20))],
                        game.sim.year, gw(game.state, admin::kPeace), gw(game.state, admin::kCulture), gw(game.state, admin::kProsperity),
                        gw(game.state, admin::kEmpire), gw(game.state, admin::kAverage), gw(game.state, 0x6C2E), gw(game.state, 0x6C2A));
            std::putchar(10);
            std::printf("battles %d won, %d lost; invasions %d; dismissed %d", game.battles_won, game.battles_lost, game.invasions,
                        (int)game.dismissed);
            std::putchar(10);
        }
        return 0;
    }
    std::fprintf(stderr, "unknown command %s\n", command.c_str());
    return 2;
}
