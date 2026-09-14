// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/province.hpp"

#include "systems/actors.hpp"

namespace gaius::systems::province {

const std::array<uint8_t, 140> kLandClass = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0x00
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 5,  // 0x14
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 8, 8, 8, 8, 8, 8,  // 0x28
    8, 8, 8, 8, 8, 8, 4, 4, 7, 7, 4, 4, 4, 4, 0, 0, 6, 7, 1, 1,  // 0x3C
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 3, 3,  // 0x50
    3, 3, 3, 3, 4, 4, 4, 4, 4, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,  // 0x64
    0, 6, 6, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0x78
};

const std::array<uint8_t, 140> kSeaClass = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0x00
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0x14
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0x28
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,  // 0x3C
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0x50
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0x64
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0x78
};

const std::array<Cell, 40> kCityEntry = {{
    {50, 0},  {25, 0},  {75, 0},  {5, 0},   {5, 0},    // 0
    {98, 0},  {80, 0},  {98, 20}, {60, 0},  {98, 40},  // 1
    {98, 50}, {98, 25}, {98, 75}, {98, 5},  {98, 95},  // 2
    {98, 98}, {98, 80}, {80, 98}, {98, 60}, {60, 98},  // 3
    {50, 98}, {25, 98}, {75, 98}, {5, 98},  {95, 98},  // 4
    {0, 98},  {20, 98}, {0, 80},  {40, 98}, {0, 60},   // 5
    {0, 50},  {0, 25},  {0, 75},  {0, 5},   {0, 5},    // 6
    {0, 0},   {0, 20},  {20, 0},  {0, 40},  {40, 0},   // 7
}};

namespace {

using model::CityState;

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

constexpr int kFrame = 0x00, kX = 0x02, kY = 0x04, kActive = 0x06, kType = 0x07, kIndex = 0x08, kFacing = 0x0A;
constexpr int kFollowing = 0x0B, kHand = 0x0C, kFollowX = 0x0D, kFollowY = 0x0E, kPixelsLeft = 0x0F;
constexpr int kFlipCountdown = 0x10, kFlipInterval = 0x11, kDestX = 0x12, kDestY = 0x13, kTarget = 0x14;
constexpr int kStatus = 0x17, kCell = 0x18, kBlockedCell = 0x1A, kBlockedClass = 0x1E, kAge = 0x1F;
constexpr int kResumeX = 0x22, kResumeY = 0x24, kContact = 0x26, kTimer = 0x28, kNumber = 0x2A;
constexpr int kOtherX = 0x2C, kOtherY = 0x2D, kArmySize = 0x30, kState = 0x31;

constexpr uint8_t kStopped = 0x01, kShared = 0x02, kCentre = 0x04, kBlocked = 0x08;

constexpr int kEdge = 0x270;  // 39 * 16
constexpr std::array<int, 8> kDx = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr std::array<int, 8> kDy = {-1, -1, 0, 1, 1, 1, 0, -1};

uint8_t& map_cell(CityState& s, int cell) {
    static uint8_t scratch;
    if (cell < 0 || cell >= kMapW * kMapW) {
        scratch = 0;
        return scratch;
    }
    return s.empire.cells[static_cast<size_t>(cell)];
}

int16_t w16(int32_t v) { return static_cast<int16_t>(static_cast<uint16_t>(v & 0xFFFF)); }

int cell_of(const Rec& a) { return (a.w(kY) >> 4) * kMapW + (a.w(kX) >> 4); }

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

// 0x26C4F: snap to the cell and stop.
void settle(CityState& s, Rec& a) {
    map_cell(s, a.w(kCell)) &= 0x7F;
    a.setw(kX, a.w(kX) & 0xFFF0);
    a.setw(kY, a.w(kY) & 0xFFF0);
    a.setw(kCell, cell_of(a));
    map_cell(s, a.w(kCell)) |= kOccupied;
    reset_path(a);
    a.setb(kStatus, a.b(kStatus) | kStopped);
}

// 0x26454: 1 if facing `dir` leads onto a cell the table passes.
int try_step(CityState& s, Rec& a, int dir, const std::array<uint8_t, 140>& classes) {
    const int x = a.w(kX), y = a.w(kY);
    const int dx = kDx[static_cast<size_t>(dir)], dy = kDy[static_cast<size_t>(dir)];
    if ((dy < 0 && !(y > 0)) || (dy > 0 && !(y < kEdge)) || (dx > 0 && !(x < kEdge)) || (dx < 0 && !(x > 0))) return 0;
    const int next = a.w(kCell) + dy * kMapW + dx;
    const uint8_t v = classes[map_cell(s, next) & 0x7F];
    if (v == 0) return 1;
    if (v >= 7 && a.sb(kType) == kCohortType) return 1;
    a.setb(kFollowX, (x >> 4) + dx);
    a.setb(kFollowY, (y >> 4) + dy);
    a.setw(kBlockedCell, next);
    a.setb(kStatus, a.b(kStatus) | kBlocked);
    a.setb(kBlockedClass, (v >= 2 && v <= 8) ? (1 << (v - 2)) : 0);  // 0x26A8F
    return 0;
}

// 0x2533E: the first army within 64 px becomes the Cohort's target.
bool find_army(CityState& s, Rec& a) {
    const int x = a.w(kX), y = a.w(kY);
    for (int j = 0; j < static_cast<int>(model::kActorCount); ++j) {
        Rec t = rec(s, j);
        if (t.b(kActive) == 0 || t.sb(kType) != kArmyType) continue;
        if (t.w(kX) < x - 64 || t.w(kX) >= x + 64 || t.w(kY) < y - 64 || t.w(kY) >= y + 64) continue;
        a.setw(kTarget, j);
        reset_search(a);
        return true;
    }
    return false;
}

// 0x26EF1: another army within 16 px, kept in +0x26.
int touching_army(CityState& s, Rec& a) {
    const int x = a.w(kX), y = a.w(kY);
    for (int j = 0; j < static_cast<int>(model::kActorCount); ++j) {
        Rec t = rec(s, j);
        if (t.b(kActive) == 0 || t.w(kIndex) == a.w(kIndex)) continue;
        if (t.w(kX) < x - 16 || t.w(kX) >= x + 16 || t.w(kY) < y - 16 || t.w(kY) >= y + 16) continue;
        if (t.sb(kType) != kArmyType) continue;
        a.setw(kContact, j);
        return j;
    }
    return -1;
}

// The dest bytes of a Cohort that stops where it stands (x / 16).
void stop_here(Rec& a) {
    a.setb(kDestX, a.w(kX) / 16);
    a.setb(kDestY, a.w(kY) / 16);
}

// 0x24B66
void march(CityState& s, const month::Random& random, int slot, Rec& a) {
    walk(s, slot, a.sb(kType) == kSeaArmyType ? kSeaClass : kLandClass);
    if (a.b(kStatus) & kStopped) {
        if ((map_cell(s, a.w(kCell)) & 0x7F) != kCityTile) {
            a.setb(kState, kLinger);
            return;
        }
        const int from = random.low7 & 7;  // DS:0x6DA7
        actors::release(s, slot);
        invade_city(s, from);
        int peace = model::global_word(s, kPeaceRating);
        peace -= model::global_word(s, 0x6C30) == 1 ? 4 : 10;
        model::set_global_word(s, kPeaceRating, peace < 0 ? 0 : peace);
        model::set_global_word(s, 0x6C84, 1);
        return;
    }
    if (!(a.b(kStatus) & kBlocked) || random.walk >= 0x14) return;
    // 0x24C62: what the obstacle's class lets the army do, gated by low7.
    const int low7 = random.low7;
    uint8_t& cell = map_cell(s, a.w(kBlockedCell));
    bool wreck = false;
    switch (a.sb(kBlockedClass)) {
        case 1:
        case 2: wreck = low7 == 0x21; break;
        case 4: wreck = low7 >= 0x0B && low7 <= 0x0D; break;
        case 8: wreck = low7 >= 0x15 && low7 <= 0x1F; break;
        case 0x10:  // 0x24D44: a town is pillaged one grade (message 0x6E7E)
            if (cell == 0x79) cell = 0x61;
            if (cell == 0x7A) cell = 0x79;
            if (cell == 0x4C) cell = 0x7A;
            return;
        case 0x20: wreck = low7 >= 0x15 && low7 <= 0x1F; break;  // message 0x6E7A at 0x15-0x18
        case 0x40: wreck = low7 >= 0x19 && low7 <= 0x1F; break;  // message 0x6E76
        default: return;
    }
    if (wreck) cell = 0x1D;
}

void run_state(CityState& s, month::Random& random, int slot, Rec& a, const Hooks* hooks) {
    switch (a.sb(kState)) {
        case 2: actors::release(s, slot); break;
        case kMarch: march(s, random, slot, a); break;
        case kHalt:
        case kGoHome:
            walk(s, slot, kLandClass);
            if (a.b(kStatus) & kStopped) reset_path(a);
            break;
        case kPatrol:
            walk(s, slot, kLandClass);
            if (a.b(kStatus) & kStopped) {
                const uint8_t x = a.b(kDestX), y = a.b(kDestY);
                a.setb(kDestX, a.b(kOtherX));
                a.setb(kOtherX, x);
                a.setb(kDestY, a.b(kOtherY));
                a.setb(kOtherY, y);
                reset_path(a);
            } else if ((a.b(kStatus) & kCentre) && find_army(s, a)) {
                a.setw(kResumeX, a.sb(kDestX));
                a.setw(kResumeY, a.sb(kDestY));
                a.setb(kState, kAttack);
            }
            break;
        case kAttack: {
            const int army = a.w(kTarget);
            const bool alive = army >= 0 && army < static_cast<int>(model::kActorCount) &&
                               s.objects[static_cast<size_t>(army)].raw[kActive] != 0 &&
                               static_cast<int8_t>(s.objects[static_cast<size_t>(army)].raw[kType]) == kArmyType;
            if (!alive) {
                if (a.b(kOtherX) == 0) {
                    a.setb(kState, kHalt);
                    stop_here(a);
                } else {
                    a.setb(kState, kPatrol);
                    a.setb(kDestX, a.b(kResumeX));
                    a.setb(kDestY, a.b(kResumeY));
                }
                break;
            }
            Rec t = rec(s, army);
            a.setb(kDestX, t.w(kX) >> 4);
            a.setb(kDestY, t.w(kY) >> 4);
            walk(s, slot, kLandClass);
            if ((a.b(kStatus) & kCentre) && touching_army(s, a) >= 0 && hooks && hooks->battle) {
                hooks->battle(s, slot, army);  // sound 0x10, then 0x22116
            }
            break;
        }
        case kDemobilized: a.setw(kFrame, a.sb(kNumber) * 4); break;
        case kLinger:
            a.setw(kTimer, a.w(kTimer) + 1);
            if (a.w(kTimer) > 16) actors::release(s, slot);
            break;
        default: break;
    }
}

}  // namespace

void walk(CityState& s, int slot, const std::array<uint8_t, 140>& classes) {
    Rec a = rec(s, slot);
    int moved = 0;
    a.setb(kStatus, a.b(kStatus) & ~kCentre);
    a.setb(kPixelsLeft, a.b(kPixelsLeft) - 1);
    if (a.sb(kPixelsLeft) > 0) {
        step_pixel(a);
        return;
    }
    a.setb(kStatus, a.b(kStatus) | kCentre);
    a.setb(kPixelsLeft, 16);
    map_cell(s, a.w(kCell)) &= 0x7F;
    a.setw(kCell, cell_of(a));
    const int cx = a.w(kX) >> 4, cy = a.w(kY) >> 4;
    const int direct = direction(cx, cy, a.sb(kDestX), a.sb(kDestY));
    if (direct >= 8) {
        settle(s, a);
        return;
    }
    int dir = direct;
    if (a.sb(kFollowing) == 1) dir = direction(cx, cy, a.sb(kFollowX), a.sb(kFollowY));
    if (dir == direct) a.setb(kFollowing, 0);
    if (a.sb(kFollowing) == 0) moved = try_step(s, a, dir, classes);
    if (moved == 0) {
        if (a.sb(kFollowing) == 0) {
            a.setb(kFollowX, a.b(kDestX));
            a.setb(kFollowY, a.b(kDestY));
            a.setb(kFollowing, 1);
        }
        for (int k = 0; k < 8; ++k) {
            dir = a.sb(kHand) == 0 ? (dir + 1) & 7 : (dir == 0 ? 7 : dir - 1);
            moved = try_step(s, a, dir, classes);
            if (moved == 1) break;
        }
    }
    if (moved != 1) {
        settle(s, a);
        return;
    }
    a.setb(kFacing, dir);
    step_pixel(a);  // before the cell is recomputed, unlike the city walker
    map_cell(s, a.w(kCell)) &= 0x7F;
    a.setw(kCell, cell_of(a));
    a.setb(kStatus, a.b(kStatus) & ~kShared);
    if (map_cell(s, a.w(kCell)) & kOccupied) a.setb(kStatus, a.b(kStatus) | kShared);
    map_cell(s, a.w(kCell)) |= kOccupied;

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

void update_actor(CityState& s, month::Random& random, int tick, int slot, const Hooks* hooks) {
    Rec a = rec(s, slot);
    switch (a.sb(kType)) {
        case kArmyType:
            run_state(s, random, slot, a, hooks);
            a.setw(kFrame, model::global_word(s, 0x6BD8));
            if (tick % 64 != 0) break;
            a.setb(kAge, a.b(kAge) + 1);
            if (a.sb(kAge) >= 120) a.setb(kState, 2);
            break;
        case kSeaArmyType: {
            run_state(s, random, slot, a, hooks);
            const int f = a.sb(kFacing) - 2;
            a.setw(kFrame, f == 0 ? 0x2F : (f >= 1 && f <= 3) ? 0x2D : f == 4 ? 0x2E : 0x2C);
            if (a.b(kStatus) & kBlocked) a.setb(kType, kArmyType);
            break;
        }
        case kCohortType:
            a.setw(kFrame, w16(a.sb(kNumber) * 4 + (((tick % 32) >> 1) & 3)));
            run_state(s, random, slot, a, hooks);
            break;
        default: break;
    }
}

int spawn_army(CityState& s, const month::Random& random, int difficulty) {
    int first = 0, every = 30;
    if (difficulty == 1) first = -6, every = 10;
    if (difficulty == 2) first = -11, every = 0;
    const int rank = model::global_word(s, 0x6C30);
    first = first - rank + 1;
    every = every - rank + 1;
    const int year = model::global_word(s, 0x6C32);
    if (year < first) return -1;
    if (year < every && !(year & 1)) return -1;

    const int r = random.low7 & 7;
    const int by_sea = 0x51 + r, by_land = 0x59 + r;
    int type = 0, col = 0, row = 0, city_col = 0, city_row = 0;
    for (int y = 0; y < kMapW; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            const int t = s.empire.cells[static_cast<size_t>(y * kMapW + x)] & 0x7F;
            if (t == by_sea || t == by_land) {
                type = t == by_sea ? kSeaArmyType : kArmyType;
                col = x;
                row = y;
            } else if (t == kCityTile) {
                city_col = x;  // DS:0x6DAF/0x6DAD keep their last value when there's no city
                city_row = y;
            }
        }
    }
    if (type == 0) return -1;
    const int slot = actors::spawn(s, type, col, row);
    if (slot < 0) return -1;
    Rec a = rec(s, slot);
    a.setb(kDestX, city_col);
    a.setb(kDestY, city_row);
    a.setb(kState, kMarch);
    a.setb(kArmySize, (random.walk & 7) + 1);
    return slot;  // message 0x27B3C at the army's cell
}

int invade_city(CityState& s, int direction_index) {
    // 0x2D90F: the first forum whose timer runs out is the target.
    int tx = 50, ty = 50;
    for (int d = 0; d < 30; ++d) {
        const size_t o = static_cast<size_t>(d) * 16;
        if (s.table_480.size() < o + 16) break;
        auto word = [&](size_t at) { return static_cast<int16_t>(s.table_480[o + at] | (s.table_480[o + at + 1] << 8)); };
        if (word(8) == 0) continue;
        int timer = word(0x0C) - 1;
        const bool ran_out = timer < 0;
        if (ran_out) {
            timer = 6;
            tx = word(0);
            ty = word(2);
        }
        s.table_480[o + 0x0C] = static_cast<uint8_t>(timer & 0xFF);
        s.table_480[o + 0x0D] = static_cast<uint8_t>((timer >> 8) & 0xFF);
        if (ran_out) break;
    }
    // 0x2D966: the first open entry cell of the direction.
    for (int k = 0; k < 5; ++k) {
        const auto [x, y] = kCityEntry[static_cast<size_t>((direction_index & 7) * 5 + k)];
        uint8_t& tile = s.city.tile[y][x];
        if (!(tile > 0x1C && tile < 0x4A)) continue;
        tile = 0x1D;
        const int slot = actors::spawn(s, 5 + model::global_word(s, 0x6BDA), x, y);
        if (slot < 0) return -1;
        Rec a = rec(s, slot);
        a.setb(kDestX, tx);
        a.setb(kDestY, ty);
        a.setb(kState, 3);
        return slot;  // sound 5, message 0x6E92 at the entry
    }
    return -1;
}

}  // namespace gaius::systems::province
