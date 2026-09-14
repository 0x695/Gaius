// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/actors.hpp"

#include <algorithm>

#include "systems/construction.hpp"
#include "systems/housing.hpp"
#include "systems/province.hpp"

namespace gaius::systems::actors {

namespace {

using namespace field;
using model::CityState;

constexpr int kSlots = static_cast<int>(model::kActorCount);
constexpr int kEdge = 0x630;  // 99 * 16: the last column or row, in pixels

// One actor record, read and written the way the engine does: bytes and
// words at fixed offsets.
struct Rec {
    std::array<uint8_t, model::kActorRecordSize>& r;
    uint8_t b(int o) const { return r[static_cast<size_t>(o)]; }
    int sb(int o) const { return static_cast<int8_t>(r[static_cast<size_t>(o)]); }
    int w(int o) const { return static_cast<int16_t>(r[static_cast<size_t>(o)] | (r[static_cast<size_t>(o) + 1] << 8)); }
    void setb(int o, int v) { r[static_cast<size_t>(o)] = static_cast<uint8_t>(v); }
    void setw(int o, int v) {
        r[static_cast<size_t>(o)] = static_cast<uint8_t>(v & 0xFF);
        r[static_cast<size_t>(o) + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    }
};

Rec rec(CityState& s, int slot) { return Rec{s.objects[static_cast<size_t>(slot)].raw}; }

// Cell layers addressed by the engine's flat index. The engine doesn't bound
// these; out of the grid Gaius reads and writes a scratch byte instead.
template <typename T>
T& cell_at(model::CityGrid<T>& g, int cell) {
    static T scratch;
    if (cell < 0 || cell >= model::kCityW * model::kCityH) {
        scratch = T{};
        return scratch;
    }
    return g[static_cast<size_t>(cell / model::kCityW)][static_cast<size_t>(cell % model::kCityW)];
}

// Global words, growing an empty block so a fresh CityState works too.
int gw(const CityState& s, uint16_t ds) { return model::global_word(s, ds); }
void set_gw(CityState& s, uint16_t ds, int v) {
    if (s.global_words_128.size() < 256) s.global_words_128.resize(256, 0);
    if (s.final_state.size() < 68) s.final_state.resize(68, 0);
    model::set_global_word(s, ds, v);
}

int table_word(std::vector<uint8_t>& t, size_t o) {
    if (t.size() < o + 2) t.resize(o + 2, 0);
    return static_cast<int16_t>(t[o] | (t[o + 1] << 8));
}
void set_table_word(std::vector<uint8_t>& t, size_t o, int v) {
    if (t.size() < o + 2) t.resize(o + 2, 0);
    t[o] = static_cast<uint8_t>(v & 0xFF);
    t[o + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

// The type counters (0x5C39 / 0x5DC7).
struct Band {
    uint16_t ds;
    int limit;
};
std::optional<Band> band(int type) {
    if (type <= 2) return Band{0x6C1A, 30};
    if (type <= 4) return Band{0x6C18, 8};
    if (type <= 7) return Band{0x6C16, 12};
    if (type <= 9) return Band{0x6C14, 10};
    if (type <= 12) return Band{0x6C16, 12};
    if (type == 13) return Band{0x6C12, 10};
    return std::nullopt;
}

// 3496:1A10: the map-edge point a spawned walker heads for, by facing.
constexpr std::array<std::pair<int, int>, 8> kEdgeTarget = {
    {{50, 0}, {99, 0}, {99, 50}, {99, 99}, {50, 99}, {0, 99}, {0, 50}, {0, 0}}};
// Facings 0-7 clockwise from north (0x26CEE; 3496:1DF4 lists the same).
constexpr std::array<int, 8> kDx = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr std::array<int, 8> kDy = {-1, -1, 0, 1, 1, 1, 0, -1};
// 3496:1878: ticks between a forum's walkers, by grade.
constexpr std::array<int8_t, 8> kForumDelay = {7, 6, 5, 5, 4, 3, 2, 2};
// 3496:1880: a workshop's base level, 50 x 8 -- by the current province
// (DS:0x6CA6, 0-49; findings section 22) and then the goods type.
constexpr std::array<int8_t, 400> kWorkshopBase = {
    0, -2, 1, -3, 0, 0, 2, 0,  // province 0
    0, -2, 2, -3, 0, 0, 1, 0,  // province 1
    1, -2, 1, -3, 1, 0, 0, 0,  // province 2
    2, -2, 2, -3, 1, 0, 0, 0,  // province 3
    -2, -3, 0, 1, 0, 0, 2, 0,  // province 4
    -2, -2, 0, 0, 2, 0, 1, 0,  // province 5
    0, -2, 1, -2, 2, 0, 0, 0,  // province 6
    -2, 0, 1, -2, 2, 0, 1, 0,  // province 7
    0, -2, 1, -3, 2, 0, 0, 0,  // province 8
    0, 0, -2, 2, 0, 1, 0, 0,  // province 9
    0, -2, -2, 1, 0, 0, 1, 0,  // province 10
    -2, 1, 0, 0, 1, 0, 0, -2,  // province 11
    -2, 0, 1, -2, 0, 1, 0, -2,  // province 12
    -2, 1, 0, 1, 2, -2, 0, -2,  // province 13
    0, -2, 0, 0, 1, -2, 1, -2,  // province 14
    -2, 1, 0, 0, 2, -2, 0, -2,  // province 15
    -2, 0, 1, 0, 0, -3, 1, -3,  // province 16
    -2, 2, 0, 0, 0, -3, 1, -3,  // province 17
    -2, -2, 2, -2, 1, -2, 1, -2,  // province 18
    -2, 0, 0, 0, 1, -3, 1, -3,  // province 19
    -3, 2, 0, 2, -3, -3, 0, -3,  // province 20
    -3, 2, 0, 0, -3, -3, 1, -3,  // province 21
    -3, 1, 1, 2, -3, -3, 0, -3,  // province 22
    -3, 0, 1, 0, -3, -3, 1, -3,  // province 23
    1, 0, 1, 0, -2, -3, 1, -3,  // province 24
    0, -3, 1, 1, 1, -3, 0, -3,  // province 25
    0, -2, 2, -3, 1, -3, 1, -3,  // province 26
    -3, -2, 0, -3, 1, -3, 1, -3,  // province 27
    -2, -3, -2, -3, 2, -2, 2, -2,  // province 28
    0, -2, -2, -2, 1, -2, 1, -2,  // province 29
    0, -3, 2, -3, 0, -2, 1, 0,  // province 30
    -2, -3, 1, -3, 0, -2, 2, 0,  // province 31
    -2, -3, 0, -3, 1, -3, 1, -2,  // province 32
    0, -3, 0, -3, 1, 0, -2, 1,  // province 33
    -2, -3, 1, -3, 0, 0, -2, 1,  // province 34
    -3, -3, 1, -2, 0, 0, -2, 1,  // province 35
    -3, -3, -2, -2, 1, 0, -2, 1,  // province 36
    -3, -2, -2, 1, 0, 0, -2, 2,  // province 37
    0, -3, -2, -2, 1, 0, 1, 1,  // province 38
    -2, -3, -3, -2, -3, 1, 0, 2,  // province 39
    1, -3, 0, -3, 1, 0, 1, 1,  // province 40
    0, -3, -3, -2, -2, 2, -2, 2,  // province 41
    2, -3, -3, -3, -2, 2, 1, 2,  // province 42
    0, -3, -3, -3, -2, 1, 1, 1,  // province 43
    -2, -3, -2, -2, -2, 2, 2, 1,  // province 44
    -3, -3, -3, 0, -2, 1, 2, 1,  // province 45
    -3, -3, -3, 1, -2, 1, 1, 1,  // province 46
    -3, -3, -3, 2, 0, 1, 0, 1,  // province 47
    -3, -3, -2, 1, 1, 0, 1, 1,  // province 48
    -2, -3, -2, -2, 1, -2, 2, -2,  // province 49
};

bool hostile(int type) { return type == 5 || type == 6 || type == 7 || type == 10; }

// ---------------------------------------------------------------------------
// Movement

struct Ctx {
    CityState& s;
    month::Random& random;
    int tick;
    int slot;
};

int cell_of(const Rec& a) { return (a.w(kY) >> 4) * 100 + (a.w(kX) >> 4); }

// 0x26B3C
int direction(int x, int y, int tx, int ty) {
    if (x > tx) return y > ty ? 7 : (y == ty ? 6 : 5);
    if (x == tx) return y > ty ? 0 : (y == ty ? 8 : 4);
    return y > ty ? 1 : (y == ty ? 2 : 3);
}

// 0x26CEE
void step_pixel(Rec& a) {
    const int d = a.sb(kFacing);
    if (d < 0 || d > 7) return;
    a.setw(kX, a.w(kX) + kDx[static_cast<size_t>(d)]);
    a.setw(kY, a.w(kY) + kDy[static_cast<size_t>(d)]);
}

// 0x25484
void reset_path(Rec& a) {
    a.setb(kPixelsLeft, 0);
    a.setb(kFlipCountdown, 0);
    a.setb(kFlipInterval, 0);
    a.setb(kHand, 0);
    a.setb(kStatus, 0);
    a.setb(kBlockedClass, 0);
}

// 0x25438
void reset_search(Rec& a) {
    a.setb(kFlipCountdown, 0);
    a.setb(kFlipInterval, 0);
    a.setb(kHand, 0);
    a.setb(kStatus, 0);
    a.setb(kBlockedClass, 0);
}

// 0x26BB0: snap to the cell and stop.
void settle(Ctx& c, Rec& a) {
    cell_at(c.s.city.operational_state, a.w(kCell)) &= 0xBF;
    a.setw(kX, a.w(kX) & 0xFFF0);
    a.setw(kY, a.w(kY) & 0xFFF0);
    a.setw(kCell, cell_of(a));
    cell_at(c.s.city.operational_state, a.w(kCell)) |= 0x40;
    reset_path(a);
    a.setb(kStatus, a.b(kStatus) | kStatusStopped);
}

// 0x25909: 1 if facing `dir` leads onto a passable cell; otherwise records
// the obstacle and returns 0. Off the map edge it returns 0 recording nothing.
int try_step(Ctx& c, Rec& a, int dir, const std::array<uint8_t, 256>& table) {
    const int x = a.w(kX), y = a.w(kY);
    const int dx = kDx[static_cast<size_t>(dir)], dy = kDy[static_cast<size_t>(dir)];
    if ((dy < 0 && !(y > 0)) || (dy > 0 && !(y < kEdge)) || (dx > 0 && !(x < kEdge)) || (dx < 0 && !(x > 0))) return 0;
    const int next = a.w(kCell) + dy * 100 + dx;
    const uint8_t v = table[cell_at(c.s.city.tile, next)];
    if (v == 0) return 1;
    const int bx = (x >> 4) + dx, by = (y >> 4) + dy;
    a.setw(kBlockedX, bx);
    a.setw(kBlockedY, by);
    a.setb(kFollowX, bx);
    a.setb(kFollowY, by);
    a.setw(kBlockedCell, next);
    a.setb(kBlockedClass, (v >= 2 && v <= 8) ? (1 << (v - 2)) : 0);  // 0x26A8F
    return 0;
}

// 0x254DF: one pixel of walking. At each cell centre the walker picks the
// facing toward its destination; when that's blocked it keeps an obstacle
// on one hand, turning from the last blocked cell, and swaps hands after an
// interval that grows by 2 each swap.
void walk(Ctx& c, Rec& a, const std::array<uint8_t, 256>& table) {
    auto& op = c.s.city.operational_state;
    int moved = 0;
    a.setb(kStatus, a.b(kStatus) & ~kStatusCentre);
    a.setb(kPixelsLeft, a.b(kPixelsLeft) - 1);
    if (a.sb(kPixelsLeft) > 0) {
        step_pixel(a);
        return;
    }
    a.setb(kStatus, a.b(kStatus) | kStatusCentre);
    a.setb(kBlockedClass, 0);
    a.setb(kPixelsLeft, 16);
    cell_at(op, a.w(kCell)) &= 0xBF;
    a.setw(kCell, cell_of(a));
    const int cx = a.w(kX) >> 4, cy = a.w(kY) >> 4;
    const int direct = direction(cx, cy, a.sb(kDestX), a.sb(kDestY));
    if (direct >= 8) {
        settle(c, a);
        return;
    }
    int dir = direct;
    if (a.sb(kFollowing) == 1) dir = direction(cx, cy, a.sb(kFollowX), a.sb(kFollowY));
    if (dir == direct) a.setb(kFollowing, 0);
    if (a.sb(kFollowing) == 0) moved = try_step(c, a, dir, table);
    if (moved == 0) {
        if (a.sb(kFollowing) == 0) {
            a.setb(kFollowX, a.b(kDestX));
            a.setb(kFollowY, a.b(kDestY));
            a.setb(kFollowing, 1);
        }
        for (int k = 0; k < 8; ++k) {
            if (a.sb(kHand) == 0) dir = (dir + 1) & 7;
            if (a.sb(kHand) == 1) dir = dir == 0 ? 7 : dir - 1;
            moved = try_step(c, a, dir, table);
            if (moved == 1) break;
        }
    }
    if (moved != 1) {
        settle(c, a);
        return;
    }
    a.setb(kFacing, dir);
    cell_at(op, a.w(kCell)) &= 0xBF;
    a.setw(kCell, cell_of(a));
    a.setb(kStatus, a.b(kStatus) & ~kStatusShared);
    if (cell_at(op, a.w(kCell)) & 0x40) a.setb(kStatus, a.b(kStatus) | kStatusShared);
    cell_at(op, a.w(kCell)) |= 0x40;
    step_pixel(a);

    const int left = (dir + 1) & 7, right = dir == 0 ? 7 : dir - 1;
    if (direct == dir || direct == left || direct == right) {
        a.setb(kFollowing, 0);
        return;
    }
    a.setb(kFlipCountdown, a.b(kFlipCountdown) - 1);
    if (a.sb(kFlipCountdown) > 0) return;
    a.setb(kFlipInterval, a.b(kFlipInterval) + 2);
    a.setb(kFlipCountdown, a.b(kFlipInterval));
    a.setb(kHand, a.b(kHand) ^ 1);
    a.setb(kFollowing, 0);
}

// 0x2520E: a hostile within 160 px becomes the walker's target.
bool find_hostile(Ctx& c, Rec& a) {
    const int x = a.w(kX), y = a.w(kY);
    for (int j = 0; j < kSlots; ++j) {
        Rec t = rec(c.s, j);
        if (t.b(kActive) == 0 || !hostile(t.sb(kType))) continue;
        if (t.w(kX) < x - 160 || t.w(kX) >= x + 160 || t.w(kY) < y - 160 || t.w(kY) >= y + 160) continue;
        a.setw(kHome, j);
        reset_search(a);
        return true;
    }
    return false;
}

// 0x26D9D: a hostile, other than the walker itself, within 16 px.
int touching_hostile(Ctx& c, Rec& a) {
    const int x = a.w(kX), y = a.w(kY);
    for (int j = 0; j < kSlots; ++j) {
        Rec t = rec(c.s, j);
        if (t.b(kActive) == 0 || t.w(kIndex) == a.w(kIndex)) continue;
        if (t.w(kX) < x - 16 || t.w(kX) >= x + 16 || t.w(kY) < y - 16 || t.w(kY) >= y + 16) continue;
        if (!hostile(t.sb(kType))) continue;
        a.setw(kContact, j);
        return j;
    }
    return -1;
}

void demolish(Ctx& c, Rec& a) {
    construction::demolish(c.s, c.random, a.w(kBlockedX), a.w(kBlockedY));
}

// 0x243B5: an invader against a wall piece (walk class 4, tiles 0x92-0x95 and
// 0x9E-0xA1) breaches it now and then.
void breach_wall(Ctx& c, Rec& a) {
    if (c.random.walk > 6) return;
    const int cell = a.w(kBlockedCell);
    auto& tile = c.s.city.tile;
    const int piece = static_cast<int>(cell_at(tile, cell)) - 0x92;
    auto tower = [](int t) { return t > 0x99 && t < 0xA0; };
    switch (piece) {
        case 1:
        case 3:
        case 15:  // 0x243F3
            if (!(tower(cell_at(tile, cell - 100)) || tower(cell_at(tile, cell + 100))) || c.random.walk <= 1)
                cell_at(tile, cell) = 0xA3;
            break;
        case 0:
        case 2:
        case 14:  // 0x2448D
            if (!(tower(cell_at(tile, cell - 1)) || tower(cell_at(tile, cell + 1))) || c.random.walk <= 1)
                cell_at(tile, cell) = 0xA2;
            break;
        case 12:  // 0x2450A -- the walk is never below 1, so never
            if (c.random.walk < 1) cell_at(tile, cell) = 0xA2;
            break;
        case 13:  // 0x24532 -- likewise
            if (c.random.walk < 1) cell_at(tile, cell) = 0xA3;
            break;
        default:
            break;
    }
}

bool inside_margin(const Rec& a) { return a.w(kX) >= 16 && a.w(kX) < kEdge && a.w(kY) >= 16 && a.w(kY) < kEdge; }

// ---------------------------------------------------------------------------
// States (DS:0x1384)

void run_state(Ctx& c, Rec& a) {
    auto& city = c.s.city;
    switch (a.b(kState)) {
        case kCitizenWalk:  // 0x2417C
            if ((a.b(kStatus) & kStatusShared) && ((c.tick % 32) & 1)) return;
            walk(c, a, road_table());
            if (!(a.b(kStatus) & kStatusCentre) || !inside_margin(a)) return;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) cell_at(city.service_flags, a.w(kCell) + dy * 100 + dx) |= 0x12;
            return;
        case kRemove:  // 0x242EF
            release(c.s, c.slot);
            return;
        case kInvade:  // 0x242FA
            walk(c, a, ground_table());
            if (a.b(kStatus) & kStatusStopped) {
                a.setb(kState, kRemove);
                return;
            }
            if (!(a.b(kStatus) & kStatusCentre)) return;
            switch (a.b(kBlockedClass)) {
                case 1:
                case 2: demolish(c, a); break;
                case 4: breach_wall(c, a); break;
                case 8: cell_at(city.tile, a.w(kBlockedCell)) = 0x1D; break;
                default: break;
            }
            return;
        case kTrade:  // 0x24563 -- note the skip is the reverse of state 1's
            if (!(a.b(kStatus) & kStatusShared) && ((c.tick % 32) & 1)) return;
            walk(c, a, road_table());
            if (!(a.b(kStatus) & kStatusCentre) || !(cell_at(city.service_flags, a.w(kCell)) & 0x08)) return;
            a.setb(kState, kRemove);
            {
                const size_t o = static_cast<size_t>(a.w(kHome)) * 24 + 0x0E;
                const int sales = table_word(c.s.table_720, o);
                if (sales <= 1) set_table_word(c.s.table_720, o, sales + 1);
            }
            return;
        case kRiot:  // 0x245FF
            walk(c, a, ground_table());
            if (a.b(kStatus) & kStatusStopped) {
                a.setb(kState, kRiotPause);
                a.setw(kTimer, 0);
                const size_t d = static_cast<size_t>(c.random.low7 & 7);
                a.setb(kDestX, a.b(kDestX) + kDx[d]);
                a.setb(kDestY, a.b(kDestY) + kDy[d]);
                if (a.sb(kDestX) < 0) a.setb(kDestX, 0);
                if (a.sb(kDestX) > 99) a.setb(kDestX, 99);
                if (a.sb(kDestY) < 0) a.setb(kDestY, 0);
                if (a.sb(kDestY) > 99) a.setb(kDestY, 99);
                return;
            }
            if (!(a.b(kStatus) & kStatusCentre)) return;
            if (a.b(kBlockedClass) == 1 || a.b(kBlockedClass) == 2) {
                demolish(c, a);
            } else if (a.b(kBlockedClass) == 8) {
                cell_at(city.tile, a.w(kBlockedCell)) = 0x1D;
            }
            return;
        case kRiotPause:  // 0x2478B (the type handler then overwrites the frame)
            a.setw(kFrame, (((c.tick % 32) & 2) + 0x54) >> 1);
            a.setw(kTimer, a.w(kTimer) + 1);
            if (a.w(kTimer) > 8) {
                a.setb(kState, kRiot);
                reset_path(a);
            }
            return;
        case kPatrol:  // 0x247DA
            walk(c, a, road_table());
            if (!(a.b(kStatus) & kStatusCentre) || !inside_margin(a)) return;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int8_t& lv = cell_at(city.land_value, a.w(kCell) + dy * 100 + dx);
                    lv = static_cast<int8_t>(static_cast<uint8_t>(lv) + 0xFE);
                }
            }
            a.setw(kTimer, a.w(kTimer) + 1);
            if (a.w(kTimer) <= 4) return;
            a.setw(kTimer, 0);
            if (find_hostile(c, a)) a.setb(kState, kChase);
            return;
        case kChase: {  // 0x24A6B
            const int target = a.w(kHome);
            if (target < 0 || target >= kSlots) return;
            Rec t = rec(c.s, target);
            if (t.b(kActive) == 0 || !hostile(t.sb(kType))) {
                if (!find_hostile(c, a)) a.setb(kState, kRemove);
                return;
            }
            a.setb(kDestX, t.w(kX) >> 4);
            a.setb(kDestY, t.w(kY) >> 4);
            walk(c, a, ground_table());
            if (!(a.b(kStatus) & kStatusCentre)) return;
            const int hit = touching_hostile(c, a);
            if (hit >= 0) rec(c.s, hit).setb(kState, kRemove);
            return;
        }
        case 15:  // 0x24EA4, a province state, but city-safe
            a.setw(kTimer, a.w(kTimer) + 1);
            if (a.w(kTimer) > 16) release(c.s, c.slot);
            return;
        default:  // 0: nothing; 9-14: province-map states, not transcribed
            return;
    }
}

// Per-type handler parameters (DS:0x134C): walking-frame base, age limit and
// the counter period aging uses.
struct TypeInfo {
    bool city;
    int frame_base;
    int age_limit;
    int period;
};
constexpr std::array<TypeInfo, 11> kTypes = {{
    {true, 0, 10, 16},    // 0  0x23CA1
    {true, 12, 20, 16},   // 1  0x23CFE
    {true, 24, 40, 16},   // 2  0x23D5C
    {false, 0, 0, 0},     // 3  0x23DBA (returns)
    {true, 36, 50, 16},   // 4  0x23DBB
    {true, 48, 120, 64},  // 5  0x23E19
    {true, 60, 120, 64},  // 6  0x23E77
    {true, 72, 120, 64},  // 7  0x23ED5
    {true, 0, 30, 16},    // 8  0x23F33
    {false, 0, 0, 0},     // 9  0x23FC5 (returns)
    {true, 0, 20, 16},    // 10 0x23FC6
}};

// 0x25196: the walking frame -- north-facing +3, east +9, south +0, west +6,
// plus the stride (0, 1 or 2) from the pixels left in the cell.
void set_walk_frame(Rec& a, int base) {
    const int d = a.sb(kFacing);
    static constexpr std::array<int, 8> kFacingSet = {3, 3, 9, 0, 0, 0, 6, 3};
    int frame = base + ((d >= 0 && d <= 7) ? kFacingSet[static_cast<size_t>(d)] : 0);
    const int stride = a.sb(kPixelsLeft) & 6;
    if (stride == 0 || stride == 4) frame += 1;
    if (stride == 6) frame += 2;
    a.setw(kFrame, frame);
}

// The ring search the spawners share (0x2D4BF / 0x2D558 / 0x2D5F1).
std::optional<std::pair<int, int>> road_beside(const model::CityMap& city, const month::Random& random, int size,
                                               int x, int y) {
    const int n = 4 * size + 4;
    const int start = random.low7 & 0xF;
    for (int i = 0; i < n; ++i) {
        const auto [dx, dy] = ring_offset(size, (start + i) % n);
        if (spawnable(city, x + dx, y + dy)) return std::make_pair(x + dx, y + dy);
    }
    return std::nullopt;
}

void send_to_edge(CityState& s, int slot, int facing, int home, State state) {
    Rec a = rec(s, slot);
    a.setb(kDestX, kEdgeTarget[static_cast<size_t>(facing)].first);
    a.setb(kDestY, kEdgeTarget[static_cast<size_t>(facing)].second);
    a.setb(kFacing, facing);
    a.setw(kHome, home);
    a.setb(kState, state);
}

std::array<uint8_t, 256> make_table(bool ground) {
    std::array<uint8_t, 256> t{};
    auto fill = [&t](int lo, int hi, uint8_t v) {
        for (int i = lo; i <= hi; ++i) t[static_cast<size_t>(i)] = v;
    };
    fill(0x00, 0xC7, 1);
    fill(0x36, 0x43, 0);
    fill(0x5E, 0x61, 0);
    fill(0x82, 0x89, 0);
    fill(0xC8, 0xD7, 2);
    fill(0xD8, 0xFF, 3);
    if (ground) {
        fill(0x1D, 0x2D, 0);
        fill(0x2E, 0x35, 5);
        fill(0x44, 0x49, 5);
        fill(0x92, 0x95, 4);
        fill(0x9E, 0xA1, 4);
        for (int v : {0xA2, 0xA3, 0xA7, 0xAA, 0xAD, 0xB0}) t[static_cast<size_t>(v)] = 0;
    }
    return t;
}

}  // namespace

const std::array<uint8_t, 256>& road_table() {
    static const std::array<uint8_t, 256> t = make_table(false);
    return t;
}

const std::array<uint8_t, 256>& ground_table() {
    static const std::array<uint8_t, 256> t = make_table(true);
    return t;
}

bool spawnable(const model::CityMap& city, int x, int y) {
    if (x < 0 || x >= 100 || y < 0 || y >= 100) return false;
    const uint8_t t = city.tile[static_cast<size_t>(y)][static_cast<size_t>(x)];
    return (t >= 0x36 && t <= 0x43) || (t >= 0x5E && t <= 0x61);
}

std::pair<int, int> ring_offset(int size, int k) {
    if (k <= size + 1) return {k - 1, -1};
    if (k <= 2 * size + 2) return {size, k - (size + 2)};
    if (k <= 3 * size + 3) return {size - 1 - (k - (2 * size + 3)), size};
    return {-1, size - 1 - (k - (3 * size + 4))};
}

int spawn(CityState& state, int type, int x, int y) {
    for (int slot = 0; slot < kSlots; ++slot) {
        Rec a = rec(state, slot);
        if (a.b(kActive) != 0) continue;
        const auto bd = band(type);
        if (!bd) return -1;
        const int count = gw(state, bd->ds);
        if (count >= bd->limit) return -1;
        set_gw(state, bd->ds, count + 1);
        a.setw(kCell, type >= 11 ? y * 40 + x : y * 100 + x);
        a.setb(kType, type);
        a.setb(kDestX, x);
        a.setb(kDestY, y);
        a.setb(0x2C, 0);
        a.setb(0x2D, 0);
        a.setb(kActive, 1);
        a.setw(kX, x << 4);
        a.setw(kY, y << 4);
        a.setw(kIndex, slot);
        return slot;
    }
    return -1;
}

void release(CityState& state, int slot) {
    if (slot < 0 || slot >= kSlots) return;
    Rec a = rec(state, slot);
    const int type = a.sb(kType);
    if (const auto bd = band(type)) set_gw(state, bd->ds, gw(state, bd->ds) - 1);
    if (type < 11) cell_at(state.city.operational_state, a.w(kCell)) &= 0xBF;
    a.r.fill(uint8_t{0});
}

void update(CityState& state, month::Random& random, int tick, const province::Hooks* hooks) {
    for (int slot = 0; slot < kSlots; ++slot) {
        Rec a = rec(state, slot);
        if (a.b(kActive) != 1) continue;
        const int type = a.sb(kType);
        if (type < 0) {
            release(state, slot);
            continue;
        }
        if (type >= 11 && type <= 13) {
            province::update_actor(state, random, tick, slot, hooks);
            continue;
        }
        if (type >= static_cast<int>(kTypes.size())) continue;
        const TypeInfo& info = kTypes[static_cast<size_t>(type)];
        if (!info.city) continue;
        Ctx c{state, random, tick, slot};
        run_state(c, a);
        // As in the engine, this runs even when the state just freed the
        // record, leaving a frame and perhaps an age in the empty slot.
        set_walk_frame(a, info.frame_base);
        if (tick % info.period != 0) continue;
        a.setb(kAge, a.b(kAge) + 1);
        if (a.sb(kAge) < info.age_limit) continue;
        a.setb(kState, kRemove);
        if (type == 8) {  // the trader never sold: the workshop's sales drop
            const size_t o = static_cast<size_t>(a.w(kHome)) * 24 + 0x0E;
            const int sales = table_word(state.table_720, o);
            if (sales >= -1) set_table_word(state.table_720, o, sales - 1);
        }
    }
}

int workshop_level(CityState& state, int record) {
    auto& t = state.table_720;
    const size_t r = static_cast<size_t>(record) * 24;
    const int col = table_word(t, r), row = table_word(t, r + 2), goods = table_word(t, r + 4);

    // 0x2D19A: housing population and heavy industry in the 9x9 window from
    // (col - 3, row - 3), clipped to the map.
    auto window = [](int v, int* start, int* count) {
        if (v - 3 < 0) {
            *start = 0;
            *count = v + 6;
        } else if (v + 6 >= 100) {
            *start = v - 3;
            *count = 100 - (v + 6) + 9;
        } else {
            *start = v - 3;
            *count = 9;
        }
    };
    int r0, rn, c0, cn;
    window(row, &r0, &rn);
    window(col, &c0, &cn);
    int population = 0, industry = 0;
    for (int y = r0; y < r0 + rn; ++y) {
        for (int x = c0; x < c0 + cn; ++x) {
            const uint8_t tile = cell_at(state.city.tile, y * 100 + x);
            if (tile == 0xF3) industry = 2;
            if (tile >= 0xC8 && tile <= 0xD7) population += housing::kPopulationUnitsPerCell[tile - 0xC8];
        }
    }
    set_table_word(t, r + 0x0C, population);
    set_table_word(t, r + 0x14, industry);

    const int base_index = gw(state, 0x6CA6) * 8 + goods;
    int level = (base_index >= 0 && base_index < 400) ? kWorkshopBase[static_cast<size_t>(base_index)] : 0;
    level += (static_cast<int16_t>(population) >> 4) - 4;
    level += gw(state, 0x6BF4) + table_word(t, r + 0x0E) + industry;
    const int m = gw(state, 0x6BFC);
    if (m < -30) level += 2;
    else if (m < -10) level += 1;
    else if (m <= 0) level += 0;
    else if (m < 3) level -= 1;
    else if (m < 5) level -= 2;
    else if (m < 8) level -= 3;
    else if (m < 12) level -= 4;
    else if (m < 16) level -= 5;
    else if (m < 20) level -= 6;
    else if (m < 30) level -= 7;
    else if (m < 200) level -= 8;
    if (goods >= 0 && goods < 8) {
        if (state.table_8.size() < 8) state.table_8.resize(8, 0);
        const int same = static_cast<int8_t>(state.table_8[static_cast<size_t>(goods)]);
        if (same > 1) --level;
        if (same > 3) --level;
        if (same > 5) --level;
    }
    return std::clamp(level, 0, 7);
}

void run_spawners(CityState& state, const month::Random& random, int step) {
    const bool populated = gw(state, 0x6C10) > 10;

    // 0x2D2F5: forums, at steps 0 and 50. Record (DS:0x5BA4, 16 bytes): +0
    // column, +2 row, +4 grade, +6 timer, +8 active, +A facing.
    if (step == 0 || step == 50) {
        auto& t = state.table_480;
        for (int i = 0; i < 30; ++i) {
            const size_t r = static_cast<size_t>(i) * 16;
            if (table_word(t, r + 8) == 0) continue;
            set_table_word(t, r + 6, table_word(t, r + 6) - 1);
            if (table_word(t, r + 6) >= 0) continue;
            const int grade = table_word(t, r + 4);
            set_table_word(t, r + 6, (grade >= 0 && grade < 8) ? kForumDelay[static_cast<size_t>(grade)] : 0);
            const int facing = (table_word(t, r + 0x0A) + 1) & 7;
            set_table_word(t, r + 0x0A, facing);
            if (!populated) continue;
            const int size = grade <= 2 ? 2 : (grade <= 6 ? 3 : 4);
            const auto at = road_beside(state.city, random, size, table_word(t, r), table_word(t, r + 2));
            if (!at) continue;
            const int roll = random.low7 & 0xF;
            const int slot = spawn(state, roll < 6 ? 0 : (roll <= 12 ? 1 : 2), at->first, at->second);
            if (slot >= 0) send_to_edge(state, slot, facing, i, kCitizenWalk);
        }
    }

    // 0x2CE7C: one workshop record per step, steps 25-54. Record (DS:0x585C,
    // 24 bytes): +0 column, +2 row, +4 goods, +6 timer, +8 active, +A facing,
    // +C population nearby, +E sales, +10 level, +12 last level, +14 industry.
    if (step >= 25 && step < 55) {
        const int i = step - 25;
        auto& t = state.table_720;
        const size_t r = static_cast<size_t>(i) * 24;
        if (table_word(t, r + 8) != 0) {
            const int level = workshop_level(state, i);
            set_table_word(t, r + 0x12, table_word(t, r + 0x10));
            set_table_word(t, r + 0x10, level);
            set_table_word(t, r + 6, table_word(t, r + 6) - 1);
            if (table_word(t, r + 6) < 0) {
                set_table_word(t, r + 6, 4);
                const int facing = (table_word(t, r + 0x0A) + 1) & 7;
                set_table_word(t, r + 0x0A, facing);
                if (populated) {
                    if (const auto at = road_beside(state.city, random, 3, table_word(t, r), table_word(t, r + 2))) {
                        const int slot = spawn(state, 8, at->first, at->second);
                        if (slot >= 0) send_to_edge(state, slot, facing, i, kTrade);
                    }
                }
            }
        }
    }

    // 0x2CD1C: one barracks record per step, steps 75-84. Record (DS:0x5B2C,
    // 12 bytes): +0 column, +2 row, +4 timer, +6 active, +8 facing.
    if (step >= 75 && step < 85) {
        const int i = step - 75;
        auto& t = state.table_120;
        const size_t r = static_cast<size_t>(i) * 12;
        if (table_word(t, r + 6) != 0) {
            set_table_word(t, r + 4, table_word(t, r + 4) - 1);
            if (table_word(t, r + 4) < 0) {
                set_table_word(t, r + 4, 6);
                const int facing = (table_word(t, r + 8) + 1) & 7;
                set_table_word(t, r + 8, facing);
                if (populated) {
                    if (const auto at = road_beside(state.city, random, 3, table_word(t, r), table_word(t, r + 2))) {
                        const int slot = spawn(state, 4, at->first, at->second);
                        if (slot >= 0) send_to_edge(state, slot, facing, i, kPatrol);
                    }
                }
            }
        }
    }
}

void spawn_rioter(CityState& state, int x, int y) {
    const int slot = spawn(state, 10, x, y);
    if (slot < 0) return;
    Rec a = rec(state, slot);
    a.setb(kDestX, x);
    a.setb(kDestY, y + 1);
    a.setb(kState, kRiot);
    a.setb(kFacing, 4);
    set_gw(state, 0x6C3C, std::max(0, gw(state, 0x6C3C) - 2));
    set_gw(state, 0x6C84, 2);
}

}  // namespace gaius::systems::actors
