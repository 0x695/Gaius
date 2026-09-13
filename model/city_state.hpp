// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — model/city_state.hpp
//
// Layer 2 (normalized internal representation) per GAIUS_MASTERPLAN.md
// section 5 / CAESAR_REVERSE_ENGINEERING_COMPLETE.md section 76. Reshapes
// the flat, offset-addressed formats::save::SaveFile (Layer 1: exact
// import, no semantics) into typed structures a game engine can actually
// work with -- and back again, byte-identically, per GAIUS_ROADMAP.md
// Phase 2's round-trip requirement.
//
// This does NOT interpret every field's meaning. Most of CityMap's layers
// and every Actor field beyond the handful confirmed in
// CAESAR_CITY_STATE_v9.md are exactly as opaque here as in the save file --
// just reshaped from "offset N in a flat buffer" into "byte (row,col) of
// this named 100x100 grid" / "field X of this Actor". That reshaping is
// real progress (Layer 3 systems get typed fields instead of re-deriving
// offsets by hand), even though the *meaning* of most bytes is still
// unresolved. Thirteen of the twenty confirmed save blocks
// (global_words_128, table_480/120/720/50/8/10/60_a-d/72, final_state)
// have no proposed structure anywhere in the RE corpus -- they're carried
// through verbatim as opaque byte blobs, not modeled, so the round-trip
// stays exact without pretending to understand them.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "formats/empire2/empire2.hpp"
#include "formats/save/save.hpp"

namespace gaius::model {

constexpr int kCityW = 100;
constexpr int kCityH = 100;
constexpr int kActorCount = 70;
constexpr size_t kActorRecordSize = 50;  // 0x32, per CAESAR_CITY_STATE_v9.md

template <typename T>
using CityGrid = std::array<std::array<T, kCityW>, kCityH>;

// The five parallel 100x100 city grids -- DEFINITIVE per
// GAIUS_MASTERPLAN.md section 4 ("four parallel 10,000-byte simulation
// layers" plus the tile grid itself). Row-major: [row][col].
//
// Note: CAESAR_REVERSE_ENGINEERING_COMPLETE.md section 76's sketch struct
// also lists a sixth `coverage_limit[100][100]` field. It isn't defined,
// explained, or backed by any of the 20 confirmed save blocks anywhere
// else in the RE corpus (grepped -- appears exactly once, in that sketch),
// so it's not carried over here: an unbacked field would just be
// presenting a guess as data. Revisit if RE work ever explains it.
struct CityMap {
    CityGrid<uint8_t> tile{};               // 43A5 / city_tiles_100x100 -- tile->sprite lookup not yet recovered
    CityGrid<uint8_t> coverage{};           // A2C4 / cell_value_a2c4 -- numeric coverage
    CityGrid<uint8_t> service_flags{};      // C9D4 / cell_flags_c9d4 -- mixed persistent/derived/service/prerequisite bitfield
    CityGrid<uint8_t> operational_state{};  // 7BB4 / cell_flags_7bb4 -- operational/connection state; also cleared on actor deletion for city-space actors (CAESAR_CITY_STATE_v9.md)
    CityGrid<int8_t> land_value{};          // 54A4 / cell_value_54a4 -- signed, confirmed range -8..+50
};

// City-coordinate actors (types 0-10) use packed_xy = row*100+col;
// province/special actors (types 11-13) use packed_xy = row*40+col --
// confirmed in CAESAR_CITY_STATE_v9.md's object-allocator disassembly.
enum class ActorCoordSpace { City, Province };

// The 70x50-byte object/actor table (CAESAR_CITY_STATE_v9.md). Only a
// handful of fields have a confirmed offset; everything else in `raw` is
// still opaque, so this wraps the full record rather than picking it
// apart into a struct that would silently claim to understand bytes it
// doesn't. `type()` is HIGH CONFIDENCE (cross-referenced against the
// runtime type-dispatch far-pointer table); `active()`'s exact bit
// packing is explicitly called out in that doc as still being finalized --
// treat it as the weakest of these accessors.
struct Actor {
    std::array<uint8_t, kActorRecordSize> raw{};

    // +0x00: the sprite frame drawn for this actor -- MOREMEN.PL8 in the city
    // view, SPRITE2.PL8 in the province view (draw routine at flat 0x6946).
    uint16_t frame() const { return static_cast<uint16_t>(raw[0x00] | (raw[0x01] << 8)); }
    // +0x02/+0x04: world pixel position; the sprite's bottom edge is drawn at
    // screen_y + 8 (0x6946).
    uint16_t screen_x() const { return static_cast<uint16_t>(raw[0x02] | (raw[0x03] << 8)); }  // +0x02
    uint16_t screen_y() const { return static_cast<uint16_t>(raw[0x04] | (raw[0x05] << 8)); }  // +0x04
    uint8_t active() const { return raw[0x06]; }                                                // +0x06 -- exact bit packing unconfirmed
    uint8_t type() const { return raw[0x07]; }                                                  // +0x07 -- actor class, NOT a building tile id
    // +0x08: the record's own index in the table, which the draw list stores
    // (0x6DA6); it matches the slot in every active record of four real saves.
    uint16_t index() const { return static_cast<uint16_t>(raw[0x08] | (raw[0x09] << 8)); }
    // raw_x/raw_y (+0x12/+0x13) are the current cell for PROVINCE actors, but
    // NOT for city actors. In four real saves the province actor's raw (21,23)
    // agrees with both packed_xy (941 = 23*40+21) and screen/16 (336,368). City
    // actors instead hold values like (99,99) and (50,0) here, while packed_xy
    // and screen_x/16 agree with each other (col 81 <-> screen_x 1296). So for
    // a city actor's position use packed_xy (or screen_x/y / 16); what
    // raw_x/raw_y mean for city actors -- an origin or destination? -- is
    // unresolved.
    uint8_t raw_x() const { return raw[0x12]; }                                                 // +0x12
    uint8_t raw_y() const { return raw[0x13]; }                                                 // +0x13
    uint16_t packed_xy() const { return static_cast<uint16_t>(raw[0x18] | (raw[0x19] << 8)); }  // +0x18
    uint8_t state() const { return raw[0x31]; }                                                 // +0x31

    ActorCoordSpace coord_space() const { return type() < 11 ? ActorCoordSpace::City : ActorCoordSpace::Province; }
};

// Normalized in-memory representation of one CAESARxx.SAV. Loading is
// read-only reshaping (no semantic decoding beyond what
// CAESAR_CITY_STATE_v9.md already confirms); serialize() must reproduce
// the exact input bytes for the round trip to hold -- see
// tests/test_formats.cpp's test_model_city_state_round_trip (synthetic
// data only; no real .SAV exists yet to check against real data, see
// CLAUDE.md).
struct CityState {
    CityMap city;
    formats::empire2::EmpireMap empire;  // reused as-is -- already a normalized Layer 1 type, nothing left to reshape
    std::array<Actor, kActorCount> objects{};

    // The remaining 13 of 20 confirmed save blocks (see
    // formats::save::block_table()) are carried through byte-for-byte,
    // unmodeled, so the round trip stays exact without guessing at their
    // content. Their runtime addresses are known from the save writer
    // (docs/FORMATS.md): table_480 = DS:0x5BA4 forum records (30x16),
    // table_120 = DS:0x5B2C barracks records (10x12), table_720 =
    // DS:0x585C workshop records (30x24), table_8 = workshops per goods, table_50/8/10 =
    // DS:0x581E/5816/580C, table_60_a-d = 3496:01F0/022C/0268/02A4,
    // table_72 = 3496:02E0, final_state = 34 globals.
    std::vector<uint8_t> global_words_128;
    std::vector<uint8_t> table_480;
    std::vector<uint8_t> table_120;
    std::vector<uint8_t> table_720;
    std::vector<uint8_t> table_50;
    std::vector<uint8_t> table_8;
    std::vector<uint8_t> table_10;
    std::vector<uint8_t> table_60_a;
    std::vector<uint8_t> table_60_b;
    std::vector<uint8_t> table_60_c;
    std::vector<uint8_t> table_60_d;
    std::vector<uint8_t> table_72;
    std::vector<uint8_t> final_state;
};

// Reshapes an already-loaded save file into a CityState.
CityState load(const formats::save::SaveFile& sf);

// A global word by its DS address: the 128 words of global_words_128 (see
// formats::save::kGlobalWordDsAddress) and the 34 words of final_state (see
// docs/FORMATS.md). Signed, as the engine uses them. Returns `fallback` for an
// address the save doesn't store.
int global_word(const CityState& state, uint16_t ds, int fallback = 0);

// Sets a stored global word; returns false (and changes nothing) if the save
// doesn't store `ds`.
bool set_global_word(CityState& state, uint16_t ds, int value);

// Inverse of load(): reproduces a formats::save::SaveFile's raw bytes
// exactly, per the block table, from a CityState. NOTE this trusts the
// serializer's block table is symmetric with the (not yet reverse
// engineered) loader the original engine actually uses -- see
// GAIUS_ROADMAP.md Phase 2 "RE blockers." Flag any real-save byte-diff
// mismatch straight back into RE work, don't silently adjust this to
// match.
formats::save::SaveFile serialize(const CityState& state);

}  // namespace gaius::model
