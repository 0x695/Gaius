// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — systems/construction.hpp
//
// Layer 3, GAIUS_ROADMAP.md Phase 5: the construction/placement system.
//
// This is now backed by a real, complete reverse-engineering result rather
// than guesswork -- see docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md
// sections 13-14 for the full derivation. The chain, end to end:
//
//   toolbar button handler  ->  writes command id to flat [0x6D0C]
//   dispatcher at 0x11DDA   ->  lcall DS:127C[command_id]   (44-entry
//                               far-pointer table at flat 0x75ACC,
//                               segment 0x11C6)
//   per-command handler     ->  validates the target cell's existing tile
//                               is in 0x1D..0x35 (inert/buildable terrain),
//                               then writes a SEED tile across the
//                               building's footprint and clears 7BB4 there
//   DS:153A[seed_tile]      ->  the simulation handler that grows/services
//                               it from then on (systems::service)
//
// Command ids are DEFINITIVE (not merely inferred from string order any
// more): ~30 independent toolbar handler sites assign literal ids to
// [0x6D0C], and each matches this table's ordinal position exactly
// (0x0B=Housing, 0x0C=Forum, 0x0D=Temple, 0x14-0x16=Theater/Coliseum/
// Hippodrome, 0x1F-0x21=the three Cohort commands, ...).

#pragma once

#include <array>
#include <cstdint>

#include "model/city_state.hpp"

namespace gaius::systems::construction {

// Ordinal position in the string table == numeric command id, now
// confirmed against the executable's own dispatch code (see file header).
enum class CommandId {
    NoAction = 0,
    MainToolbar,
    ClearArea,
    GoToForum,
    Road,
    ReservoirPipe,
    Maps,
    Wall,
    Tower,
    Well,
    Fountain,
    Housing,
    Forum,
    Temple,
    BathHouses,
    Hospital,
    School,
    Oracle,
    Career,
    GoToProvince,
    Theater,
    Coliseum,
    Hippodrome,
    Plaza,
    Barracks,
    Prefecture,
    HeavyIndustry,
    Market,
    Workshop,
    Fort,
    Halt,
    CohortPatrol,
    CohortAttack,
    CohortGoHome,
};

constexpr int kCommandCount = 34;

// The executable's dispatch table has 44 entries (ids 0-43); ids 34-43
// have handlers but no display string, so they aren't named here. They're
// out of scope until something identifies them.
constexpr int kDispatchTableEntryCount = 44;

// Exact text, including the original's own typo ("Resevoir\pipe", not
// "Reservoir/Pipe" -- single backslash, missing second "r") -- transcribed
// verbatim from the decompressed image.
inline const std::array<const char*, kCommandCount> kCommandNames = {{
    "No action", "Main Toolbar", "Clear Area", "Go to Forum", "Road", "Resevoir\\pipe", "Maps", "Wall", "Tower",
    "Well", "Fountain", "Housing", "Forum", "Temple", "Bath Houses", "Hospital", "School", "Oracle", "Career",
    "Go to Province", "Theater", "Coliseum", "Hippodrome", "Plaza", "Barracks", "Prefecture", "Heavy Industry",
    "Market", "Workshop", "Fort", "Halt", "Cohort Patrol", "Cohort Attack", "Cohort Go Home",
}};

inline const char* command_name(CommandId id) { return kCommandNames[static_cast<int>(id)]; }

// How a command places (if it places at all).
enum class PlacementKind {
    // Not a placement at all -- a UI/mode action (open a panel, change
    // level, select a cohort order). The handler writes no tile.
    NonPlacing,
    // Writes a single seed tile at the clicked cell.
    SingleCell,
    // Writes a seed tile across a width x height footprint via the
    // executable's shared multi-cell routine at 0x1232E.
    MultiCell,
    // Drag-built, one cell per call (road/wall/plaza/clear-area): the tile
    // written depends on the cell's neighbours rather than a fixed seed.
    // place() refuses these; use place_road / place_wall / place_plaza /
    // clear_area below (transcribed 2026-09-13).
    DragAutoTiled,
    // Footprint is known but the seed tile is selected per-variant at
    // runtime (Forum has 8 grades, Workshop 8 goods types), from a table
    // this pass didn't decode. Placement is therefore not implementable
    // without inventing a tile id.
    VariantSelected,
};

struct PlacementSpec {
    PlacementKind kind = PlacementKind::NonPlacing;
    uint8_t seed_tile = 0;  // meaningful only for SingleCell / MultiCell
    int width = 0;          // meaningful only for MultiCell / VariantSelected
    int height = 0;
};

// Every value below is transcribed from the real per-command handlers
// reached through DS:127C -- no estimates. Commands absent from the
// switch are NonPlacing.
PlacementSpec placement_spec(CommandId id);

// The buildable-terrain window every traced placement handler checks the
// target cell against before writing anything: the existing tile must
// satisfy 0x1D <= tile <= 0x35. That range is exactly the span the
// DS:153A simulation table fills with no-op handlers (inert terrain), so
// "you may only build on ground that does nothing" is enforced by the
// same numbers in both directions.
constexpr uint8_t kBuildableTerrainMin = 0x1D;
constexpr uint8_t kBuildableTerrainMax = 0x35;

inline bool is_buildable_terrain(uint8_t tile) {
    return tile >= kBuildableTerrainMin && tile <= kBuildableTerrainMax;
}

// True if `id` can be placed with its whole footprint on buildable
// terrain, fully inside the grid. Non-placeable kinds always return false.
bool can_place(const model::CityMap& city, CommandId id, int x, int y);

// Places `id` at (x,y): writes the seed tile across the footprint and
// writes 7BB4 (operational_state) at each of those cells as its part index
// within the building, 4*dy + dx -- 0 for the anchor and for every single-cell
// building. (Corrected 2026-09-13: this previously wrote 0 everywhere, a
// generalisation from the single-cell handlers; the engine's shared multi-cell
// writer at 0x1232E writes the part index, and real saves confirm it.) Returns false and changes
// nothing if can_place() would have rejected it -- the executable does the
// same, setting its failure flag at [0x6D0A] instead of writing.
bool place(model::CityMap& city, CommandId id, int x, int y);

// Per-building-tile metrics, the table at 3496:14B2 (3 bytes per tile from
// 0xC8): footprint width and height in pixels (16 per cell), and how many
// rows the building's sprite rises above its footprint. Used by demolition
// (below) and by render::render_city; HOUSES.PL8 frame i is exactly
// width x (height + extra) of tile 0xC8 + i for all 43 frames it holds.
struct BuildingMetrics {
    uint8_t width;
    uint8_t height;
    uint8_t extra;
};
inline constexpr std::array<BuildingMetrics, 50> kBuildingMetrics = {{
    {16, 16, 0},  {16, 16, 0},  {16, 16, 0},  {16, 16, 0},  {32, 16, 0},  {32, 16, 8},  {32, 16, 15}, {16, 16, 12},  // C8-CF
    {16, 16, 15}, {32, 16, 5},  {32, 16, 5},  {32, 16, 7},  {32, 16, 13}, {32, 32, 0},  {32, 32, 0},  {48, 48, 0},   // D0-D7
    {16, 16, 3},  {16, 16, 7},  {16, 32, 4},  {16, 32, 5},  {16, 32, 5},  {32, 32, 5},  {32, 32, 5},  {48, 32, 16},  // D8-DF
    {32, 32, 0},  {32, 32, 0},  {32, 32, 0},  {48, 48, 0},  {48, 48, 0},  {48, 48, 0},  {48, 48, 0},  {64, 64, 0},   // E0-E7
    {16, 16, 12}, {16, 16, 16}, {32, 32, 16}, {32, 16, 16}, {32, 32, 0},  {32, 32, 0},  {16, 16, 3},  {48, 48, 4},   // E8-EF
    {32, 16, 16}, {48, 32, 16}, {64, 32, 6},  {64, 64, 0},  {32, 32, 0},  {48, 48, 2},  {48, 48, 2},  {16, 16, 0},   // F0-F7
    {16, 16, 0},  {16, 16, 0},                                                                                         // F8-F9
}};

}  // namespace gaius::systems::construction

namespace gaius::systems::month {
struct Random;
}

namespace gaius::systems::construction {

// ---------------------------------------------------------------------------
// Drag-built commands: Road (id 4), Wall (7), Plaza (23), Clear Area (2).
// Transcribed 2026-09-13 from their DS:127C handlers (flat 0x131E7, 0x1415E,
// 0x15098, 0x12B79) and helpers -- docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md
// section 18. Dragging calls the handler once for each cell the cursor
// passes over; each function here is one such call at (x, y) and returns
// false where the engine refuses (setting its failure flag DS:0x6D0A).
//
// Road and Wall choose their tile from the cell's eight neighbours:
//   1. a snapshot of the neighbour tiles, clockwise from north (routine
//      0334:4161). A neighbour off the grid reads 0; a neighbour whose tile
//      is 0 leaves the previous snapshot byte in place -- an engine quirk,
//      kept, which is why the snapshot lives in DragState;
//   2. a flag for each neighbour in the command's connectable tile ranges
//      (0x17C20): roads 0x36-0x43, 0x82-0x89, 0x5E-0x61, 0x94-0x95; walls
//      0x92-0xA3, 0xB3-0xB7;
//   3. the first entry of the 161-entry pattern table at 3496:0A9A whose
//      neighbour pattern fits the flags (0x17CB9): it gives the tile, and a
//      mode for each orthogonal neighbour. No match leaves the modes as they
//      were and refuses the cell;
//   4. each orthogonal neighbour is re-tiled by its mode (road: 0x17EB0 ->
//      0x17EC5 / 0x1834D / 0x1879C / 0x18C27; wall: 0x1C119 -> 0x1C12E /
//      0x1C622 / 0x1CB19 / 0x1D010).
// Walls use the same table and translate its road tile to a wall tile.
//
// Validated against four real saves: clearing every road piece (0x36-0x40)
// and rebuilding the network cell by cell in row order reproduces 40/40,
// 90/90, 137/141 and 149/153 road tiles. The four misses are the same cells
// in both later saves, and the earlier CAESARWX rebuilds that area exactly:
// in between, road cells beside those junctions were built over (one became
// a well), and Clear Area doesn't re-tile a cleared road's neighbours -- so
// the junction shapes are history a rebuild from the final grid can't see.

// State the engine keeps between drag calls.
struct DragState {
    std::array<uint8_t, 8> neighbours{};  // 3496:0328, clockwise from north
    std::array<int, 4> modes{};           // DS:0x57E2 / 57E0 / 57DE / 57DC: north, east, south, west
};

// One entry of the table at 3496:0A9A.
struct RoadPattern {
    std::array<uint8_t, 8> neighbours;  // per neighbour: 0 must not connect, 1 must connect, 2 either
    uint8_t tile;                       // road tile to place (0x36-0x40)
    std::array<uint8_t, 4> modes;       // re-tiling mode for north, east, south, west (0 = leave)
};
extern const std::array<RoadPattern, 161> kRoadPatterns;

// Road (0x131E7). On open ground or an existing road piece (0x1D-0x41) it
// places the pattern's tile, clears 7BB4 and re-tiles the neighbours. It also
// builds crossings: water 0x4A/0x4E/0x52 -> 0x82, 0x56 -> 0x5E, 0x5A -> 0x86,
// 0x45 -> 0x42, 0x44 -> 0x43 (each only if no neighbour already holds the
// result), and a road across a wall makes a gate: 0x93 -> 0x95, 0x92 -> 0x94.
bool place_road(model::CityMap& city, DragState& drag, int x, int y);

// Wall (0x1415E). On open ground or an existing wall piece it places the
// wall form of the pattern's tile (0x36->0x93, 0x37->0x92, 0x38->0x96,
// 0x39->0x97, 0x3A->0x98, 0x3B->0x99, 0x3C->0xB3 ... 0x40->0xB7); across a
// road it makes a gate (0x37 -> 0x95, 0x36 -> 0x94); 0x45 -> 0xA1 and
// 0x44 -> 0xA0 when no neighbour holds the result.
bool place_wall(model::CityMap& city, DragState& drag, int x, int y);

// Plaza (0x15098): paves an existing road piece (0x36-0x43) by setting its
// 7BB4 bit 0x10. The renderer and the coverage handlers both read that bit.
bool place_plaza(model::CityMap& city, int x, int y);

// Clear Area (0x12B79). Crossings revert to water (0x82/0x8A -> 0x4A,
// 0x5E/0x72 -> 0x56, 0x86/0x8E -> 0x5A); 0x27-0x49 and 0x92-0xC9 become open
// ground 0x1D; a reservoir 0xA4 restores the tile it stored in 7BB4; and a
// building (>= 0xCA) is demolished whole (0x124F8): from its anchor, every
// footprint cell becomes rubble 0xA7 + (random & 3), one generator draw per
// cell. This grid-only version leaves the building record tables alone; the
// model::CityState overload below also removes the record.
bool clear_area(model::CityMap& city, month::Random& random, int x, int y);

// ---------------------------------------------------------------------------
// Buildings with a record: Forum (command 12, 0x14D2F) and Workshop (command
// 28, 0x15377), transcribed 2026-09-13. Each takes its tile from a player
// choice and is tracked in a record table the save keeps (docs/FORMATS.md).
//
// Forum: grade 0-7 (DS:0x6D89) -> tile 0xE0 + grade, 2x2 for grades 0-2, 3x3
// for 3-6, 4x4 for 7 -- the sizes kBuildingMetrics gives tiles 0xE0-0xE7.
// Refused once DS:0x6CA0 counts 30. Record in table_480 (DS:0x5BA4, 30 x 16
// bytes): +0 column, +2 row, +4 grade, +6 timer, +8 active, +A frame counter.
// (Tiles 0xE0-0xE7 were called temples in the inherited corpus; the command
// that places them is the Forum.)
//
// Workshop: goods type 0-7 (DS:0x6D87) -> tile 0xF5 for goods 0-3, 0xF6 for
// 4-7, 3x3. Refused once DS:0x6C9E counts 30. Record in table_720 (DS:0x585C,
// 30 x 24 bytes): +0 column, +2 row, +4 goods, +6 timer, +8 active, +0x10
// production level; table_8 (DS:0x5816) counts workshops per goods type.
//
// Both place through the shared footprint writer (0x1232E) -- the same
// terrain rule and part indices as place() -- and return false where it
// refuses. The record goes into the first free slot; with none free, the
// building is placed without one, as in the engine.
bool place_forum(model::CityState& state, int grade, int x, int y);
bool place_workshop(model::CityState& state, int goods, int x, int y);

// Clear Area on a whole save. As the grid version, and demolishing a forum
// (0xE0-0xE7), workshop (0xF5/0xF6) or barracks (0xEF) first removes its
// record and decrements its count (0x1287C / 0x12963 / 0x12A94). Removing a
// forum's record also calls the flag routine in clear mode for C9D4 bits
// 0x02 and 0x10 over radius 10 -- see service::apply_flags_clear_mode for
// what that actually does.
bool clear_area(model::CityState& state, month::Random& random, int x, int y);

}  // namespace gaius::systems::construction
