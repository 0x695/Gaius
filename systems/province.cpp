// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/province.hpp"

#include "systems/actors.hpp"
#include "systems/sounds.hpp"

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

const std::array<Exits, 12> kRoadExits = {{
    {0x00, 0}, {0x88, 0}, {0x22, 0}, {0x28, 0}, {0x0A, 0}, {0x82, 0},
    {0xA0, 0}, {0xA2, 1}, {0xA8, 1}, {0x2A, 1}, {0x8A, 1}, {0xAA, 1},
}};

const std::array<uint8_t, 128> kTownRoads = {
    0,  0,  0, 0, 0, 0, 0, 0, 0,   0,   0,  0,  0,  0,  0,  0,   // 0x00
    0,  0,  0, 0, 0, 0, 0, 0, 0,   0,   0,  0,  0,  0,  0,  0,   // 0x10
    0,  0,  0, 0, 0, 0, 0, 0, 0,   0,   0,  0,  0,  0,  0,  0,   // 0x20
    0,  0,  0, 0, 0, 0, 1, 2, 3,   4,   5,  6,  9,  7,  10, 8,   // 0x30
    11, 11, 0, 0, 1, 2, 0, 0, 0,   0,   255, 255, 11, 11, 0, 0,  // 0x40
    0,  0,  0, 0, 0, 0, 0, 0, 0,   0,   0,  0,  0,  0,  0,  0,   // 0x50
    0,  11, 0, 0, 0, 0, 0, 0, 0,   0,   0,  0,  0,  1,  2,  3,   // 0x60
    4,  5,  6, 9, 7, 10, 8, 11, 11, 11, 11, 11, 11, 0,  0,  0,   // 0x70
};

const std::array<uint8_t, 128> kHighwayRoads = {
    0, 0, 0, 0, 0, 0,  0, 0,  0,  0, 0,   0,   0, 0, 0, 0,   // 0x00
    0, 0, 0, 0, 0, 0,  0, 0,  0,  0, 0,   0,   0, 0, 0, 0,   // 0x10
    0, 0, 0, 0, 0, 0,  0, 0,  0,  0, 0,   0,   0, 0, 0, 0,   // 0x20
    0, 0, 0, 0, 0, 0,  0, 0,  0,  0, 0,   0,   0, 0, 0, 0,   // 0x30
    0, 0, 0, 0, 1, 2,  0, 0,  0,  0, 255, 255, 0, 0, 0, 0,   // 0x40
    0, 0, 0, 0, 0, 0,  0, 0,  0,  0, 0,   0,   0, 0, 0, 0,   // 0x50
    0, 0, 0, 0, 0, 0,  0, 0,  0,  0, 0,   0,   0, 1, 2, 3,   // 0x60
    4, 5, 6, 9, 7, 10, 8, 11, 11, 0, 0,   1,   2, 0, 0, 11,  // 0x70
};

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
void march(CityState& s, const month::Random& random, int slot, Rec& a, const Hooks* hooks) {
    // The army's messages name its cell (0x24CB3: x / 16, y / 16).
    const auto post = [&](messages::Id id) {
        if (hooks && hooks->message)
            hooks->message(messages::at(id, messages::Place::Province, a.w(kX) / 16, a.w(kY) / 16));
    };
    walk(s, slot, a.sb(kType) == kSeaArmyType ? kSeaClass : kLandClass);
    if (a.b(kStatus) & kStopped) {
        if ((map_cell(s, a.w(kCell)) & 0x7F) != kCityTile) {
            a.setb(kState, kLinger);
            return;
        }
        const int from = random.low7 & 7;  // DS:0x6DA7
        actors::release(s, slot);
        const int invader = invade_city(s, from);
        if (invader >= 0 && hooks && hooks->message) {
            // 0x2D908: at the entry cell, where the invader stands.
            const int cell = s.objects[static_cast<size_t>(invader)].packed_xy();
            hooks->message(messages::at(messages::Id::BarbariansEntering, messages::Place::City, cell % 100, cell / 100));
        }
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
        case 0x10:  // 0x24D44: a town is pillaged one grade, and message 0x6E7E
            if (cell == 0x79) cell = 0x61;
            if (cell == 0x7A) cell = 0x79;
            if (cell == 0x4C) cell = 0x7A;
            post(messages::Id::ApproachingTowns);
            return;
        case 0x20:  // 0x24C7C: message 0x6E7A at 0x15-0x18, then the wreck
            wreck = low7 >= 0x15 && low7 <= 0x1F;
            if (low7 >= 0x15 && low7 <= 0x18) post(messages::Id::ApproachingRoads);
            break;
        case 0x40:  // 0x24CF3: message 0x6E76, then the wreck
            wreck = low7 >= 0x19 && low7 <= 0x1F;
            if (wreck) post(messages::Id::ApproachingHighway);
            break;
        default: return;
    }
    if (wreck) cell = 0x1D;
}

void run_state(CityState& s, month::Random& random, int slot, Rec& a, const Hooks* hooks) {
    switch (a.sb(kState)) {
        case 2: actors::release(s, slot); break;
        case kMarch: march(s, random, slot, a, hooks); break;
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
                sounds::request(sounds::kBarbarianHorn);  // 0x2513D, then the battle 0x22116
                hooks->battle(s, slot, army);
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
        sounds::request(sounds::kWarCry);  // 0x2D8ED; the message 0x6E92 is the caller's
        return slot;
    }
    return -1;
}

bool connected(const formats::empire2::EmpireMap& map, int x, int y, const std::array<uint8_t, 128>& classes) {
    std::array<uint8_t, kMapW * kMapW> visited{};  // 3496:2112, cleared by 0x2EA22
    struct Branch {
        int x, y;
        uint8_t exits, branch;
    };
    std::array<Branch, 50> stack{};
    int top = -1;
    auto cls = [&](int cx, int cy) { return classes[map.cells[static_cast<size_t>(cy * kMapW + cx)] & 0x7F]; };

    visited[static_cast<size_t>(y * kMapW + x)] = 1;
    int v = cls(x, y);
    if (v == 0 || v >= static_cast<int>(kRoadExits.size())) return false;
    uint8_t exits = kRoadExits[static_cast<size_t>(v)].exits;
    uint8_t branch = 1;

    // The four direction tests run in this order; each continues at the next,
    // and after the last the trace goes round again while exits remain.
    struct Dir {
        uint8_t bit, back;
        int dx, dy;
    };
    constexpr std::array<Dir, 4> kDirs = {{{0x80, 0x08, 0, -1}, {0x20, 0x02, 1, 0}, {0x08, 0x80, 0, 1}, {0x02, 0x20, -1, 0}}};
    auto pop = [&] {
        const Branch& b = stack[static_cast<size_t>(top--)];
        x = b.x;
        y = b.y;
        exits = b.exits;
        branch = b.branch;
    };
    size_t at = 0;
    for (;;) {
        if (at == kDirs.size()) {  // 0x2E9BC
            at = 0;
            if (exits != 0) continue;
            if (top < 0) return false;
            pop();
            continue;
        }
        const Dir& d = kDirs[at++];
        if (!(exits & d.bit)) continue;
        exits &= static_cast<uint8_t>(~d.bit);
        const bool inside = d.dy < 0 ? y > 0 : d.dy > 0 ? y < kMapW - 1 : d.dx > 0 ? x < kMapW - 1 : x > 0;
        if (!inside) continue;
        if (branch == 1) {
            if (++top >= static_cast<int>(stack.size())) return false;
            stack[static_cast<size_t>(top)] = {x, y, exits, branch};
        }
        x += d.dx;
        y += d.dy;
        uint8_t& seen = visited[static_cast<size_t>(y * kMapW + x)];
        if (seen) {
            if (top < 0) return false;
            pop();
            continue;
        }
        seen = 1;
        v = cls(x, y);
        if (v == 0xFF) return true;
        if (v != 0 && v < static_cast<int>(kRoadExits.size())) {
            exits = kRoadExits[static_cast<size_t>(v)].exits;
            branch = kRoadExits[static_cast<size_t>(v)].branch;
            if (exits & d.back) {
                exits &= static_cast<uint8_t>(~d.back);
                continue;
            }
        }
        if (top < 0) return false;
        pop();
    }
}

int develop_towns(CityState& s, int& counter) {
    int linked = 0;
    if (++counter > 5) counter = 0;
    for (int y = 0; y < kMapW; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            uint8_t& cell = s.empire.cells[static_cast<size_t>(y * kMapW + x)];
            if (cell != 0x4C && cell != 0x79 && cell != 0x7A && cell != 0x61) continue;
            if (connected(s.empire, x, y, kTownRoads)) {
                ++linked;
                if (counter != 0) continue;
                if (cell == 0x7A) cell = 0x4C;
                if (cell == 0x79) cell = 0x7A;
                if (cell == 0x61) cell = 0x79;
            } else {
                if (cell == 0x79) cell = 0x61;
                if (cell == 0x7A) cell = 0x79;
                if (cell == 0x4C) cell = 0x7A;
            }
        }
    }
    return linked;
}

void connect_highway(CityState& s) {
    const int x = model::global_word(s, 0x6C90), y = model::global_word(s, 0x6C8E);
    const bool inside = x >= 0 && x < kMapW && y >= 0 && y < kMapW;
    model::set_global_word(s, 0x6C8C, inside && connected(s.empire, x, y, kHighwayRoads) ? 1 : 0);
}

bool monthly_pass(CityState& s, int& counter) {
    int score = 0, roads = 0;
    bool worn = false;
    if (++counter > 3) counter = 0;
    const int target = model::global_word(s, 0x6C88);
    for (uint8_t& cell : s.empire.cells) {
        cell &= 0x7F;
        if ((cell >= 0x36 && cell <= 0x49) || (cell >= 0x62 && cell <= 0x77)) {
            if (++roads == target) {
                cell = 0x1D;
                worn = true;
                // With counter 0 the engine posts message 0x6E72 (sound 0x11)
                // and sets its timer to 79 frames; systems::month posts it.
            }
        }
        if ((cell >= 0x36 && cell <= 0x37) || (cell >= 0x6D && cell <= 0x6E)) ++score;
        if ((cell >= 0x38 && cell <= 0x3B) || (cell >= 0x6F && cell <= 0x72)) score -= 4;
    }
    model::set_global_word(s, 0x6C8A, roads);
    model::set_global_word(s, 0x6C86, score);
    return worn;
}

namespace {

constexpr int kHomeX = 0x2E, kHomeY = 0x2F, kMorale = 0x2B;
constexpr int kRegulars = 0x20, kIrregulars = 0x21, kAuxiliaries = 0x1D;

// 0x0F4C1's result, as the handlers then check it.
bool orderable(CityState& s, int cohort) {
    if (cohort < 0 || cohort >= static_cast<int>(model::kActorCount)) return false;
    Rec a = rec(s, cohort);
    return a.b(kActive) != 0 && a.sb(kType) == kCohortType && a.sb(kState) != kDemobilized;
}

bool has_men(Rec& a) { return a.b(kRegulars) != 0 || a.b(kIrregulars) != 0 || a.b(kAuxiliaries) != 0; }

}  // namespace

int place_fort(CityState& s, int x, int y) {
    if (x < 0 || x >= kMapW || y < 0 || y >= kMapW) return -1;
    if (x == model::global_word(s, 0x6C90) && y == model::global_word(s, 0x6C8E)) return -1;
    uint8_t& cell = s.empire.cells[static_cast<size_t>(y * kMapW + x)];
    const bool grass = cell >= 0x1D && cell <= 0x35;
    const bool border = cell >= 0x59 && cell <= 0x60;
    if (!grass && !border) return -1;
    if (model::global_word(s, 0x6C12) >= 10) return -1;
    cell = 0x4D;
    const int slot = actors::spawn(s, kCohortType, x, y);
    if (slot < 0) return -1;
    Rec a = rec(s, slot);
    a.setb(kHomeX, a.w(kX) / 16);
    a.setb(kHomeY, a.w(kY) / 16);
    a.setb(kState, kHalt);
    a.setb(kMorale, 5);
    // 0x156BD: the first number no active Cohort has.
    for (int n = 0; n < 10; ++n) {
        bool used = false;
        for (const model::Actor& other : s.objects) {
            if (other.raw[kActive] == 1 && other.raw[kType] == kCohortType && static_cast<int8_t>(other.raw[kNumber]) == n) {
                used = true;
                break;
            }
        }
        if (!used) {
            a.setb(kNumber, n);
            break;
        }
    }
    return slot;
}

void disband_fort(CityState& s, int x, int y) {
    for (int j = 0; j < static_cast<int>(model::kActorCount); ++j) {
        Rec a = rec(s, j);
        if (a.b(kActive) != 1 || a.sb(kType) != kCohortType) continue;
        if (a.sb(kHomeX) != x || a.sb(kHomeY) != y) continue;
        actors::release(s, j);
        return;
    }
}

bool order_halt(CityState& s, int cohort) {
    if (!orderable(s, cohort)) return false;
    Rec a = rec(s, cohort);
    a.setb(kState, kHalt);
    stop_here(a);
    return true;
}

bool order_patrol(CityState& s, int cohort, int x1, int y1, int x2, int y2) {
    if (cohort < 0 || cohort >= static_cast<int>(model::kActorCount)) return false;
    Rec a = rec(s, cohort);
    if (!has_men(a) || !orderable(s, cohort)) return false;
    a.setb(kDestX, x1);
    a.setb(kDestY, y1);
    a.setb(kState, kPatrol);
    a.setb(kOtherX, x2);
    a.setb(kOtherY, y2);
    return true;
}

bool order_attack(CityState& s, int cohort, int army) {
    if (cohort < 0 || cohort >= static_cast<int>(model::kActorCount)) return false;
    if (army < 0 || army >= static_cast<int>(model::kActorCount)) return false;
    Rec a = rec(s, cohort);
    if (!has_men(a) || !orderable(s, cohort)) return false;
    a.setb(kState, kAttack);
    a.setb(kOtherX, 0);
    a.setw(kTarget, rec(s, army).w(kIndex));
    reset_search(a);  // 0x25438
    return true;
}

bool order_go_home(CityState& s, int cohort) {
    if (!orderable(s, cohort)) return false;
    Rec a = rec(s, cohort);
    a.setb(kState, kGoHome);
    a.setb(kDestX, a.b(kHomeX));
    a.setb(kDestY, a.b(kHomeY));
    return true;
}

namespace {

using construction::DragState;
using Flags = std::array<uint8_t, 8>;

bool on_map(int x, int y) { return x >= 0 && x < kMapW && y >= 0 && y < kMapW; }
uint8_t& at(CityState& s, int x, int y) { return s.empire.cells[static_cast<size_t>(y * kMapW + x)]; }
bool at_entry(const CityState& s, int x, int y) {
    return x == model::global_word(s, 0x6C90) && y == model::global_word(s, 0x6C8E);
}
bool in(uint8_t t, int lo, int hi) { return t >= lo && t <= hi; }
bool one_of(uint8_t t, std::initializer_list<uint8_t> set) {
    for (uint8_t v : set)
        if (t == v) return true;
    return false;
}

// 0334:450D: the eight neighbours, clockwise from north; off the map 0, and a
// 0 byte leaves the slot as it was.
void snapshot(CityState& s, DragState& d, int x, int y) {
    constexpr std::array<std::array<int, 2>, 8> kOffsets = {
        {{-1, 0}, {-1, 1}, {0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}}};
    for (size_t i = 0; i < kOffsets.size(); ++i) {
        const int ny = y + kOffsets[i][0], nx = x + kOffsets[i][1];
        if (!on_map(nx, ny)) {
            d.neighbours[i] = 0;
        } else if (at(s, nx, ny) != 0) {
            d.neighbours[i] = at(s, nx, ny);
        }
    }
}

// 0x17C20
int mark(const DragState& d, Flags& f, int lo, int hi) {
    int n = 0;
    for (size_t i = 0; i < f.size(); ++i) {
        if (d.neighbours[i] >= lo && d.neighbours[i] <= hi) {
            f[i] = 1;
            ++n;
        }
    }
    return n;
}

// 0x17CB9
uint8_t match(DragState& d, const Flags& f) {
    for (const construction::RoadPattern& p : construction::kRoadPatterns) {
        bool fits = true;
        for (size_t j = 0; j < f.size() && fits; ++j) fits = p.neighbours[j] == 2 || p.neighbours[j] == f[j];
        if (!fits) continue;
        for (size_t k = 0; k < 4; ++k) d.modes[k] = p.modes[k];
        return p.tile;
    }
    return 0;
}

struct Retile {
    std::initializer_list<uint8_t> keep_if;
    uint8_t keep;
    uint8_t other;
};
struct Rule {
    int drow, dcol;
    std::initializer_list<uint8_t> skip;
    uint8_t straight;
    int straight_unless;  // -1 for none
    Retile mode2, mode3, mode4;
};

// 0x19073 (north, 0x19087 east, 0x19677... in the order N, E, S, W).
const Rule kRoadRules[4] = {
    {-1, 0, {0x44, 0x45, 0x4A, 0x4B, 0x4C, 0x4D, 0x61, 0x79, 0x7A, 0x7B, 0x7C}, 0x36, -1,
     {{0x3B, 0x3F}, 0x3F, 0x38}, {{0x3A, 0x3E}, 0x3E, 0x39}, {{0x3D, 0x40, 0x41}, 0x40, 0x3C}},
    {0, 1, {0x44, 0x45, 0x4A, 0x4B, 0x4C, 0x4D, 0x61, 0x79, 0x7A, 0x7B, 0x7C}, 0x37, -1,
     {{0x38, 0x3C}, 0x3C, 0x39}, {{0x3B, 0x3D}, 0x3D, 0x3A}, {{0x3F, 0x40, 0x41}, 0x40, 0x3E}},
    {1, 0, {0x44, 0x45, 0x4A, 0x4B, 0x4C, 0x4D, 0x61, 0x79, 0x7A, 0x7B, 0x7C}, 0x36, -1,
     {{0x39, 0x3E}, 0x3E, 0x3A}, {{0x38, 0x3F}, 0x3F, 0x3B}, {{0x3C, 0x40, 0x41}, 0x40, 0x3D}},
    {0, -1, {0x44, 0x45, 0x4A, 0x4B, 0x4C, 0x4D, 0x61, 0x79, 0x7A, 0x7B, 0x7C}, 0x37, -1,
     {{0x3A, 0x3D}, 0x3D, 0x3B}, {{0x39, 0x3C}, 0x3C, 0x38}, {{0x3E, 0x40, 0x41}, 0x40, 0x3F}},
};

// 0x1A84E: the road's rules shifted to the highway's pieces, 0x78 skipped too.
const Rule kHighwayRules[4] = {
    {-1, 0, {0x44, 0x45, 0x4A, 0x4B, 0x4C, 0x4D, 0x61, 0x78, 0x79, 0x7A, 0x7B, 0x7C}, 0x6D, -1,
     {{0x72, 0x76}, 0x76, 0x6F}, {{0x71, 0x75}, 0x75, 0x70}, {{0x74, 0x77, 0x78}, 0x77, 0x73}},
    {0, 1, {0x44, 0x45, 0x4A, 0x4B, 0x4C, 0x4D, 0x61, 0x78, 0x79, 0x7A, 0x7B, 0x7C}, 0x6E, -1,
     {{0x6F, 0x73}, 0x73, 0x70}, {{0x72, 0x74}, 0x74, 0x71}, {{0x76, 0x77, 0x78}, 0x77, 0x75}},
    {1, 0, {0x44, 0x45, 0x4A, 0x4B, 0x4C, 0x4D, 0x61, 0x78, 0x79, 0x7A, 0x7B, 0x7C}, 0x6D, -1,
     {{0x70, 0x75}, 0x75, 0x71}, {{0x6F, 0x76}, 0x76, 0x72}, {{0x73, 0x77, 0x78}, 0x77, 0x74}},
    {0, -1, {0x44, 0x45, 0x4A, 0x4B, 0x4C, 0x4D, 0x61, 0x78, 0x79, 0x7A, 0x7B, 0x7C}, 0x6E, -1,
     {{0x71, 0x74}, 0x74, 0x72}, {{0x70, 0x73}, 0x73, 0x6F}, {{0x75, 0x77, 0x78}, 0x77, 0x76}},
};

// 0x1D504. East's mode 4 keeps only 0x6B and 0x6C, as transcribed.
const Rule kWallRules[4] = {
    {-1, 0, {0x44, 0x45, 0x62, 0x63}, 0x43, 0x67, {{0x49, 0x6B, 0x65}, 0x6B, 0x46}, {{0x48, 0x6A, 0x64}, 0x6A, 0x47},
     {{0x69, 0x6C, 0x41}, 0x6C, 0x68}},
    {0, 1, {0x44, 0x45, 0x63, 0x64}, 0x42, 0x66, {{0x46, 0x68, 0x62}, 0x68, 0x47}, {{0x49, 0x69, 0x65}, 0x69, 0x48},
     {{0x6B, 0x6C}, 0x6C, 0x6A}},
    {1, 0, {0x44, 0x45, 0x64, 0x65}, 0x43, 0x67, {{0x47, 0x6A, 0x63}, 0x6A, 0x48}, {{0x46, 0x6B, 0x62}, 0x6B, 0x49},
     {{0x68, 0x6C, 0x41}, 0x6C, 0x69}},
    {0, -1, {0x44, 0x45, 0x62, 0x65}, 0x42, 0x66, {{0x48, 0x69, 0x64}, 0x69, 0x49}, {{0x47, 0x68, 0x63}, 0x68, 0x46},
     {{0x6A, 0x6C, 0x41}, 0x6C, 0x6B}},
};

void retile(CityState& s, const DragState& d, int x, int y, const Rule rules[4]) {
    for (int k = 0; k < 4; ++k) {
        const Rule& r = rules[k];
        const int nx = x + r.dcol, ny = y + r.drow;
        if (!on_map(nx, ny)) continue;
        uint8_t& t = at(s, nx, ny);
        if (one_of(t, r.skip)) continue;
        const Retile* rt = nullptr;
        switch (d.modes[static_cast<size_t>(k)]) {
            case 1:
                if (t != r.straight_unless) t = r.straight;
                continue;
            case 2: rt = &r.mode2; break;
            case 3: rt = &r.mode3; break;
            case 4: rt = &r.mode4; break;
            default: continue;
        }
        t = one_of(t, rt->keep_if) ? rt->keep : rt->other;
    }
}

// 0x1669E: a road piece as the highway's (0x41 has no highway form).
uint8_t highway_form(uint8_t road) { return in(road, 0x36, 0x40) ? static_cast<uint8_t>(road + 0x37) : road; }

// 0x16CAD: a road piece as the wall's.
uint8_t wall_form(uint8_t road) {
    constexpr std::array<uint8_t, 11> kWall = {0x43, 0x42, 0x46, 0x47, 0x48, 0x49, 0x68, 0x69, 0x6A, 0x6B, 0x6C};
    return in(road, 0x36, 0x40) ? kWall[static_cast<size_t>(road - 0x36)] : road;
}

// The common start of the construction handlers; nullptr when refused.
uint8_t* open_cell(CityState& s, int x, int y) {
    if (!on_map(x, y)) return nullptr;
    uint8_t& t = at(s, x, y);
    t &= 0x7F;
    if (at_entry(s, x, y)) return nullptr;
    return &t;
}

}  // namespace

Built clear_province(CityState& s, int x, int y) {
    uint8_t* cell = open_cell(s, x, y);
    if (!cell) return Built::Refused;
    uint8_t& t = *cell;
    if (t == 0x4D) {
        t = 0x1D;
        disband_fort(s, x, y);
        return Built::Charged;
    }
    if (t <= 0x24 || (t > 0x49 && t < 0x62) || t == 0x79 || t == 0x7A) return Built::Refused;
    t = 0x1D;
    return Built::Charged;
}

Built place_province_road(CityState& s, DragState& d, int x, int y) {
    uint8_t* cell = open_cell(s, x, y);
    if (!cell || *cell < 0x1D) return Built::Refused;
    uint8_t& t = *cell;
    snapshot(s, d, x, y);
    Flags f{};
    auto links = [&] {
        mark(d, f, 0x36, 0x41);
        mark(d, f, 0x4A, 0x4D);
        mark(d, f, 0x61, 0x61);
        mark(d, f, 0x44, 0x45);
        mark(d, f, 0x79, 0x7C);
    };
    if (t <= 0x35 || in(t, 0x36, 0x41) || in(t, 0x59, 0x60)) {
        links();
        const uint8_t piece = match(d, f);
        if (piece == 0) return Built::Refused;
        const Built built = t > 0x35 ? Built::Free : Built::Charged;
        t = piece;
        retile(s, d, x, y, kRoadRules);
        return built;
    }
    if (t == 0x43 || t == 0x42) {
        links();
        match(d, f);  // for its modes; the gate is fixed
        t = t == 0x43 ? 0x45 : 0x44;
        retile(s, d, x, y, kRoadRules);
        return Built::Charged;
    }
    if (t == 0x6D) {
        t = 0x7B;
        return Built::Charged;
    }
    if (t == 0x6E) {
        t = 0x7C;
        return Built::Charged;
    }
    return Built::Refused;
}

Built place_highway(CityState& s, DragState& d, int x, int y) {
    uint8_t* cell = open_cell(s, x, y);
    if (!cell || *cell < 0x1D) return Built::Refused;
    uint8_t& t = *cell;
    snapshot(s, d, x, y);
    Flags f{};
    if (t <= 0x35 || in(t, 0x6D, 0x78) || in(t, 0x59, 0x60)) {
        mark(d, f, 0x6D, 0x7C);
        mark(d, f, 0x4A, 0x4D);
        mark(d, f, 0x61, 0x61);
        mark(d, f, 0x44, 0x45);
        const uint8_t piece = highway_form(match(d, f));
        if (piece == 0) return Built::Refused;
        const Built built = t > 0x35 ? Built::Free : Built::Charged;
        t = piece;
        retile(s, d, x, y, kHighwayRules);
        return built;
    }
    if (t == 0x43 || t == 0x42) {
        const uint8_t gate = t == 0x43 ? 0x45 : 0x44;
        if (mark(d, f, gate, gate) != 0) return Built::Refused;
        t = gate;
        return Built::Charged;
    }
    if (t == 0x37) {
        t = 0x7B;
        return Built::Charged;
    }
    if (t == 0x36) {
        t = 0x7C;
        return Built::Charged;
    }
    return Built::Refused;
}

Built place_great_wall(CityState& s, DragState& d, int x, int y) {
    uint8_t* cell = open_cell(s, x, y);
    if (!cell || *cell < 0x1D) return Built::Refused;
    uint8_t& t = *cell;
    snapshot(s, d, x, y);
    Flags f{};
    auto links = [&] {
        mark(d, f, 0x42, 0x49);
        mark(d, f, 0x62, 0x6C);
    };
    if (t <= 0x35 || in(t, 0x42, 0x43) || in(t, 0x62, 0x6C) || in(t, 0x46, 0x49) || in(t, 0x59, 0x60)) {
        links();
        const uint8_t piece = wall_form(match(d, f));
        if (piece == 0) return Built::Refused;
        const Built built = (in(t, 0x42, 0x49) || in(t, 0x62, 0x6C)) ? Built::Free : Built::Charged;
        t = piece;
        retile(s, d, x, y, kWallRules);
        return built;
    }
    if (t == 0x37 || t == 0x6E || t == 0x36 || t == 0x6D) {
        links();
        match(d, f);
        t = (t == 0x37 || t == 0x6E) ? 0x45 : 0x44;
        retile(s, d, x, y, kWallRules);
        return Built::Charged;
    }
    return Built::Refused;
}

Built place_great_tower(CityState& s, int x, int y) {
    if (!on_map(x, y)) return Built::Refused;
    uint8_t& t = at(s, x, y);
    switch (t) {
        case 0x42: t = 0x66; break;
        case 0x43: t = 0x67; break;
        case 0x46: t = 0x62; break;
        case 0x47: t = 0x63; break;
        case 0x48: t = 0x64; break;
        case 0x49: t = 0x65; break;
        default: return Built::Refused;
    }
    return Built::Charged;
}

}  // namespace gaius::systems::province
