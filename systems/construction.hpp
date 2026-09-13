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
    // Drag-based, auto-tiling placement (road/wall/plaza/clear-area):
    // the tile written depends on neighbouring tiles rather than being a
    // fixed seed. The mechanism is partly traced (the 0x36-0x40 family,
    // see docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 3) but not
    // to the point of being implementable here -- deliberately NOT given
    // a fake seed tile.
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

}  // namespace gaius::systems::construction
