// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/construction.hpp"

#include <initializer_list>

#include "systems/month.hpp"

namespace gaius::systems::construction {

PlacementSpec placement_spec(CommandId id) {
    using K = PlacementKind;
    switch (id) {
        // --- Single-cell seeds (handler writes one `mov es:[bx], imm`) ---
        case CommandId::ReservoirPipe: return {K::SingleCell, 0xA4, 1, 1};
        case CommandId::Tower:         return {K::SingleCell, 0x9E, 1, 1};  // handler also writes 0x9F/0x9A/0x9B variants by orientation
        case CommandId::Well:          return {K::SingleCell, 0xB8, 1, 1};
        case CommandId::Fountain:      return {K::SingleCell, 0xBA, 1, 1};
        case CommandId::Housing:       return {K::SingleCell, 0xC8, 1, 1};
        case CommandId::Temple:        return {K::SingleCell, 0xD8, 1, 1};
        case CommandId::BathHouses:    return {K::SingleCell, 0xE8, 1, 1};
        case CommandId::Prefecture:    return {K::SingleCell, 0xEE, 1, 1};

        // --- Multi-cell (shared routine at 0x1232E; W/H from [0x6D08]/[0x6D06], seed from [0x6D04]) ---
        case CommandId::Oracle:        return {K::MultiCell, 0xEB, 2, 1};
        case CommandId::School:        return {K::MultiCell, 0xEC, 2, 2};
        case CommandId::Hospital:      return {K::MultiCell, 0xED, 2, 2};
        case CommandId::Barracks:      return {K::MultiCell, 0xEF, 3, 3};
        case CommandId::Theater:       return {K::MultiCell, 0xF0, 2, 1};
        case CommandId::Coliseum:      return {K::MultiCell, 0xF1, 3, 2};
        case CommandId::Hippodrome:    return {K::MultiCell, 0xF2, 4, 2};
        case CommandId::HeavyIndustry: return {K::MultiCell, 0xF3, 4, 4};
        case CommandId::Market:        return {K::MultiCell, 0xF4, 2, 2};

        // --- Footprint known, seed chosen per variant at runtime ---
        case CommandId::Forum:         return {K::VariantSelected, 0, 4, 4};  // 8 grades (Aventine..Romanum)
        case CommandId::Workshop:      return {K::VariantSelected, 0, 3, 3};  // 8 goods types

        // --- Drag-based auto-tiling; tile depends on neighbours ---
        case CommandId::Road:
        case CommandId::Wall:
        case CommandId::Plaza:
        case CommandId::ClearArea:     return {K::DragAutoTiled, 0, 1, 1};

        default: return {K::NonPlacing, 0, 0, 0};
    }
}

namespace {

bool footprint_ok(const model::CityMap& city, int x, int y, int w, int h) {
    if (x < 0 || y < 0) return false;
    if (x + w > model::kCityW || y + h > model::kCityH) return false;
    for (int cy = y; cy < y + h; ++cy) {
        for (int cx = x; cx < x + w; ++cx) {
            if (!is_buildable_terrain(city.tile[cy][cx])) return false;
        }
    }
    return true;
}

}  // namespace

bool can_place(const model::CityMap& city, CommandId id, int x, int y) {
    PlacementSpec spec = placement_spec(id);
    if (spec.kind != PlacementKind::SingleCell && spec.kind != PlacementKind::MultiCell) return false;
    return footprint_ok(city, x, y, spec.width, spec.height);
}

bool place(model::CityMap& city, CommandId id, int x, int y) {
    if (!can_place(city, id, x, y)) return false;
    PlacementSpec spec = placement_spec(id);
    for (int cy = y; cy < y + spec.height; ++cy) {
        for (int cx = x; cx < x + spec.width; ++cx) {
            city.tile[cy][cx] = spec.seed_tile;
            // 7BB4 records which part of the building each cell is: 4*dy + dx
            // (0 = anchor). Transcribed from the engine's shared footprint writer
            // (flat 0x1232E, `mov es:[bx+0x7bb4], cl` with cl = dy*4 + dx) and
            // confirmed on every multi-cell building in four real saves. The
            // housing pass only dispatches anchors, so this is load-bearing.
            city.operational_state[cy][cx] = static_cast<uint8_t>(4 * (cy - y) + (cx - x));
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Drag-built commands -- see the header and findings section 18.

const std::array<RoadPattern, 161> kRoadPatterns = {{
    // 3496:0A9A: neighbours N NE E SE S SW W NW; tile; modes N E S W
    {{0, 2, 0, 2, 0, 2, 0, 2}, 0x37, {0, 0, 0, 0}},
    {{1, 0, 0, 2, 0, 2, 0, 0}, 0x36, {1, 0, 0, 0}},
    {{0, 0, 1, 0, 0, 2, 0, 2}, 0x37, {0, 1, 0, 0}},
    {{0, 2, 0, 0, 1, 0, 0, 2}, 0x36, {0, 0, 1, 0}},
    {{0, 2, 0, 2, 0, 0, 1, 0}, 0x37, {0, 0, 0, 1}},
    {{1, 1, 0, 2, 0, 2, 0, 0}, 0x36, {2, 0, 0, 0}},
    {{0, 0, 1, 1, 0, 2, 0, 2}, 0x37, {0, 2, 0, 0}},
    {{0, 2, 0, 0, 1, 1, 0, 2}, 0x36, {0, 0, 2, 0}},
    {{0, 2, 0, 2, 0, 0, 1, 1}, 0x37, {0, 0, 0, 2}},
    {{1, 0, 0, 2, 0, 2, 0, 1}, 0x36, {3, 0, 0, 0}},
    {{0, 1, 1, 0, 0, 2, 0, 2}, 0x37, {0, 3, 0, 0}},
    {{0, 2, 0, 1, 1, 0, 0, 2}, 0x36, {0, 0, 3, 0}},
    {{0, 2, 0, 2, 0, 1, 1, 0}, 0x37, {0, 0, 0, 3}},
    {{1, 1, 0, 2, 0, 2, 0, 1}, 0x36, {4, 0, 0, 0}},
    {{0, 1, 1, 1, 0, 2, 0, 2}, 0x37, {0, 4, 0, 0}},
    {{0, 2, 0, 1, 1, 1, 0, 2}, 0x36, {0, 0, 4, 0}},
    {{0, 2, 0, 2, 0, 1, 1, 1}, 0x37, {0, 0, 0, 4}},
    {{1, 0, 1, 0, 0, 2, 0, 0}, 0x3B, {1, 1, 0, 0}},
    {{1, 1, 1, 0, 0, 2, 0, 0}, 0x3B, {2, 3, 0, 0}},
    {{1, 0, 1, 1, 0, 2, 0, 0}, 0x3B, {1, 2, 0, 0}},
    {{1, 0, 1, 0, 0, 2, 0, 1}, 0x3B, {3, 1, 0, 0}},
    {{1, 1, 1, 1, 0, 2, 0, 0}, 0x3B, {2, 4, 0, 0}},
    {{1, 0, 1, 1, 0, 2, 0, 1}, 0x3B, {3, 2, 0, 0}},
    {{1, 1, 1, 0, 0, 2, 0, 1}, 0x3B, {4, 3, 0, 0}},
    {{1, 1, 1, 1, 0, 2, 0, 1}, 0x3B, {4, 4, 0, 0}},
    {{0, 0, 1, 0, 1, 0, 0, 2}, 0x38, {0, 1, 1, 0}},
    {{0, 1, 1, 0, 1, 0, 0, 2}, 0x38, {0, 3, 1, 0}},
    {{0, 0, 1, 1, 1, 0, 0, 2}, 0x38, {0, 2, 3, 0}},
    {{0, 0, 1, 0, 1, 1, 0, 2}, 0x38, {0, 1, 2, 0}},
    {{0, 1, 1, 1, 1, 0, 0, 2}, 0x38, {0, 4, 3, 0}},
    {{0, 0, 1, 1, 1, 1, 0, 2}, 0x38, {0, 2, 4, 0}},
    {{0, 1, 1, 0, 1, 1, 0, 2}, 0x38, {0, 3, 2, 0}},
    {{0, 1, 1, 1, 1, 1, 0, 2}, 0x38, {0, 4, 4, 0}},
    {{0, 2, 0, 0, 1, 0, 1, 0}, 0x39, {0, 0, 1, 1}},
    {{0, 2, 0, 0, 1, 1, 1, 0}, 0x39, {0, 0, 2, 3}},
    {{0, 2, 0, 1, 1, 0, 1, 0}, 0x39, {0, 0, 3, 1}},
    {{0, 2, 0, 0, 1, 0, 1, 1}, 0x39, {0, 0, 1, 2}},
    {{0, 2, 0, 1, 1, 1, 1, 0}, 0x39, {0, 0, 4, 3}},
    {{0, 2, 0, 1, 1, 0, 1, 1}, 0x39, {0, 0, 3, 2}},
    {{0, 2, 0, 0, 1, 1, 1, 1}, 0x39, {0, 0, 2, 4}},
    {{0, 2, 0, 1, 1, 1, 1, 1}, 0x39, {0, 0, 4, 4}},
    {{1, 0, 0, 2, 0, 0, 1, 0}, 0x3A, {1, 0, 0, 1}},
    {{1, 1, 0, 2, 0, 0, 1, 0}, 0x3A, {2, 0, 0, 1}},
    {{1, 0, 0, 2, 0, 1, 1, 0}, 0x3A, {1, 0, 0, 3}},
    {{1, 0, 0, 2, 0, 0, 1, 1}, 0x3A, {3, 0, 0, 2}},
    {{1, 1, 0, 2, 0, 1, 1, 0}, 0x3A, {2, 0, 0, 3}},
    {{1, 0, 0, 2, 0, 1, 1, 1}, 0x3A, {3, 0, 0, 4}},
    {{1, 1, 0, 2, 0, 0, 1, 1}, 0x3A, {4, 0, 0, 2}},
    {{1, 1, 0, 2, 0, 1, 1, 1}, 0x3A, {4, 0, 0, 4}},
    {{1, 0, 0, 0, 1, 0, 0, 0}, 0x36, {1, 0, 1, 0}},
    {{1, 1, 0, 0, 1, 0, 0, 0}, 0x36, {2, 0, 1, 0}},
    {{1, 0, 0, 1, 1, 0, 0, 0}, 0x36, {1, 0, 3, 0}},
    {{1, 0, 0, 0, 1, 1, 0, 0}, 0x36, {1, 0, 2, 0}},
    {{1, 0, 0, 0, 1, 0, 0, 1}, 0x36, {3, 0, 1, 0}},
    {{1, 1, 0, 1, 1, 0, 0, 0}, 0x36, {2, 0, 3, 0}},
    {{1, 0, 0, 1, 1, 1, 0, 0}, 0x36, {1, 0, 4, 0}},
    {{1, 0, 0, 0, 1, 1, 0, 1}, 0x36, {3, 0, 2, 0}},
    {{1, 1, 0, 0, 1, 0, 0, 1}, 0x36, {4, 0, 1, 0}},
    {{1, 1, 0, 1, 1, 1, 0, 0}, 0x36, {2, 0, 4, 0}},
    {{1, 0, 0, 1, 1, 1, 0, 1}, 0x36, {3, 0, 4, 0}},
    {{1, 1, 0, 0, 1, 1, 0, 1}, 0x36, {4, 0, 2, 0}},
    {{1, 1, 0, 1, 1, 0, 0, 1}, 0x36, {4, 0, 3, 0}},
    {{1, 1, 0, 0, 1, 1, 0, 0}, 0x36, {2, 0, 2, 0}},
    {{1, 0, 0, 1, 1, 0, 0, 1}, 0x36, {3, 0, 3, 0}},
    {{1, 1, 0, 1, 1, 1, 0, 1}, 0x36, {4, 0, 4, 0}},
    {{0, 0, 1, 0, 0, 0, 1, 0}, 0x37, {0, 1, 0, 1}},
    {{0, 1, 1, 0, 0, 0, 1, 0}, 0x37, {0, 3, 0, 1}},
    {{0, 0, 1, 1, 0, 0, 1, 0}, 0x37, {0, 2, 0, 1}},
    {{0, 0, 1, 0, 0, 1, 1, 0}, 0x37, {0, 1, 0, 3}},
    {{0, 0, 1, 0, 0, 0, 1, 1}, 0x37, {0, 1, 0, 2}},
    {{0, 1, 1, 1, 0, 0, 1, 0}, 0x37, {0, 4, 0, 1}},
    {{0, 0, 1, 1, 0, 1, 1, 0}, 0x37, {0, 2, 0, 3}},
    {{0, 0, 1, 0, 0, 1, 1, 1}, 0x37, {0, 1, 0, 4}},
    {{0, 1, 1, 0, 0, 0, 1, 1}, 0x37, {0, 3, 0, 2}},
    {{0, 1, 1, 1, 0, 1, 1, 0}, 0x37, {0, 4, 0, 3}},
    {{0, 0, 1, 1, 0, 1, 1, 1}, 0x37, {0, 2, 0, 4}},
    {{0, 1, 1, 0, 0, 1, 1, 1}, 0x37, {0, 3, 0, 4}},
    {{0, 1, 1, 1, 0, 0, 1, 1}, 0x37, {0, 4, 0, 2}},
    {{0, 1, 1, 0, 0, 1, 1, 0}, 0x37, {0, 3, 0, 3}},
    {{0, 0, 1, 1, 0, 0, 1, 1}, 0x37, {0, 2, 0, 2}},
    {{0, 1, 1, 1, 0, 1, 1, 1}, 0x37, {0, 4, 0, 4}},
    {{1, 0, 1, 0, 1, 0, 0, 0}, 0x3F, {1, 1, 1, 0}},
    {{1, 1, 1, 0, 1, 0, 0, 0}, 0x3F, {2, 3, 1, 0}},
    {{1, 0, 1, 1, 1, 0, 0, 0}, 0x3F, {1, 2, 3, 0}},
    {{1, 0, 1, 0, 1, 1, 0, 0}, 0x3F, {1, 1, 2, 0}},
    {{1, 0, 1, 0, 1, 0, 0, 1}, 0x3F, {3, 1, 1, 0}},
    {{1, 1, 1, 1, 1, 0, 0, 0}, 0x3F, {2, 4, 3, 0}},
    {{1, 0, 1, 1, 1, 1, 0, 0}, 0x3F, {1, 2, 4, 0}},
    {{1, 0, 1, 0, 1, 1, 0, 1}, 0x3F, {3, 1, 2, 0}},
    {{1, 1, 1, 0, 1, 0, 0, 1}, 0x3F, {4, 3, 1, 0}},
    {{1, 1, 1, 1, 1, 1, 0, 0}, 0x3F, {2, 4, 4, 0}},
    {{1, 0, 1, 1, 1, 1, 0, 1}, 0x3F, {3, 2, 4, 0}},
    {{1, 1, 1, 0, 1, 1, 0, 1}, 0x3F, {4, 3, 2, 0}},
    {{1, 1, 1, 1, 1, 0, 0, 1}, 0x3F, {4, 4, 3, 0}},
    {{1, 1, 1, 0, 1, 1, 0, 0}, 0x3F, {2, 3, 2, 0}},
    {{1, 0, 1, 1, 1, 0, 0, 1}, 0x3F, {3, 2, 3, 0}},
    {{1, 1, 1, 1, 1, 1, 0, 1}, 0x3F, {4, 4, 4, 0}},
    {{0, 0, 1, 0, 1, 0, 1, 0}, 0x3C, {0, 1, 1, 1}},
    {{0, 1, 1, 0, 1, 0, 1, 0}, 0x3C, {0, 3, 1, 1}},
    {{0, 0, 1, 1, 1, 0, 1, 0}, 0x3C, {0, 2, 3, 1}},
    {{0, 0, 1, 0, 1, 1, 1, 0}, 0x3C, {0, 1, 2, 3}},
    {{0, 0, 1, 0, 1, 0, 1, 1}, 0x3C, {0, 1, 1, 2}},
    {{0, 1, 1, 1, 1, 0, 1, 0}, 0x3C, {0, 4, 3, 1}},
    {{0, 0, 1, 1, 1, 1, 1, 0}, 0x3C, {0, 2, 4, 3}},
    {{0, 0, 1, 0, 1, 1, 1, 1}, 0x3C, {0, 1, 2, 4}},
    {{0, 1, 1, 0, 1, 0, 1, 1}, 0x3C, {0, 3, 1, 2}},
    {{0, 1, 1, 1, 1, 1, 1, 0}, 0x3C, {0, 4, 4, 3}},
    {{0, 0, 1, 1, 1, 1, 1, 1}, 0x3C, {0, 2, 4, 4}},
    {{0, 1, 1, 0, 1, 1, 1, 1}, 0x3C, {0, 3, 2, 4}},
    {{0, 1, 1, 1, 1, 0, 1, 1}, 0x3C, {0, 4, 3, 2}},
    {{0, 1, 1, 0, 1, 1, 1, 0}, 0x3C, {0, 3, 2, 3}},
    {{0, 0, 1, 1, 1, 0, 1, 1}, 0x3C, {0, 2, 3, 2}},
    {{0, 1, 1, 1, 1, 1, 1, 1}, 0x3C, {0, 4, 4, 4}},
    {{1, 0, 0, 0, 1, 0, 1, 0}, 0x3E, {1, 0, 1, 1}},
    {{1, 1, 0, 0, 1, 0, 1, 0}, 0x3E, {2, 0, 1, 1}},
    {{1, 0, 0, 1, 1, 0, 1, 0}, 0x3E, {1, 0, 3, 1}},
    {{1, 0, 0, 0, 1, 1, 1, 0}, 0x3E, {1, 0, 2, 3}},
    {{1, 0, 0, 0, 1, 0, 1, 1}, 0x3E, {3, 0, 1, 2}},
    {{1, 1, 0, 1, 1, 0, 1, 0}, 0x3E, {2, 0, 3, 1}},
    {{1, 0, 0, 1, 1, 1, 1, 0}, 0x3E, {1, 0, 4, 3}},
    {{1, 0, 0, 0, 1, 1, 1, 1}, 0x3E, {3, 0, 2, 4}},
    {{1, 1, 0, 0, 1, 0, 1, 1}, 0x3E, {4, 0, 1, 2}},
    {{1, 1, 0, 1, 1, 1, 1, 0}, 0x3E, {2, 0, 4, 3}},
    {{1, 0, 0, 1, 1, 1, 1, 1}, 0x3E, {3, 0, 4, 4}},
    {{1, 1, 0, 0, 1, 1, 1, 1}, 0x3E, {4, 0, 2, 4}},
    {{1, 1, 0, 1, 1, 0, 1, 1}, 0x3E, {4, 0, 3, 2}},
    {{1, 1, 0, 0, 1, 1, 1, 0}, 0x3E, {2, 0, 2, 3}},
    {{1, 0, 0, 1, 1, 0, 1, 1}, 0x3E, {3, 0, 3, 2}},
    {{1, 1, 0, 1, 1, 1, 1, 1}, 0x3E, {4, 0, 4, 4}},
    {{1, 0, 1, 0, 0, 0, 1, 0}, 0x3D, {1, 1, 0, 1}},
    {{1, 1, 1, 0, 0, 0, 1, 0}, 0x3D, {2, 3, 0, 1}},
    {{1, 0, 1, 1, 0, 0, 1, 0}, 0x3D, {1, 2, 0, 1}},
    {{1, 0, 1, 0, 0, 1, 1, 0}, 0x3D, {1, 1, 0, 3}},
    {{1, 0, 1, 0, 0, 0, 1, 1}, 0x3D, {3, 1, 0, 2}},
    {{1, 1, 1, 1, 0, 0, 1, 0}, 0x3D, {2, 4, 0, 1}},
    {{1, 0, 1, 1, 0, 1, 1, 0}, 0x3D, {1, 2, 0, 3}},
    {{1, 0, 1, 0, 0, 1, 1, 1}, 0x3D, {3, 1, 0, 4}},
    {{1, 1, 1, 0, 0, 0, 1, 1}, 0x3D, {4, 3, 0, 2}},
    {{1, 1, 1, 1, 0, 1, 1, 0}, 0x3D, {2, 4, 0, 3}},
    {{1, 0, 1, 1, 0, 1, 1, 1}, 0x3D, {3, 2, 0, 4}},
    {{1, 1, 1, 0, 0, 1, 1, 1}, 0x3D, {4, 3, 0, 4}},
    {{1, 1, 1, 1, 0, 0, 1, 1}, 0x3D, {4, 4, 0, 2}},
    {{1, 1, 1, 0, 0, 1, 1, 0}, 0x3D, {2, 3, 0, 3}},
    {{1, 0, 1, 1, 0, 0, 1, 1}, 0x3D, {3, 2, 0, 2}},
    {{1, 1, 1, 1, 0, 1, 1, 1}, 0x3D, {4, 4, 0, 4}},
    {{1, 0, 1, 0, 1, 0, 1, 0}, 0x40, {1, 1, 1, 1}},
    {{1, 1, 1, 0, 1, 0, 1, 0}, 0x40, {2, 3, 1, 1}},
    {{1, 0, 1, 1, 1, 0, 1, 0}, 0x40, {1, 2, 3, 1}},
    {{1, 0, 1, 0, 1, 1, 1, 0}, 0x40, {1, 1, 2, 3}},
    {{1, 0, 1, 0, 1, 0, 1, 1}, 0x40, {3, 1, 1, 2}},
    {{1, 1, 1, 1, 1, 0, 1, 0}, 0x40, {2, 4, 3, 1}},
    {{1, 0, 1, 1, 1, 1, 1, 0}, 0x40, {1, 2, 4, 3}},
    {{1, 0, 1, 0, 1, 1, 1, 1}, 0x40, {3, 1, 2, 4}},
    {{1, 1, 1, 0, 1, 0, 1, 1}, 0x40, {4, 3, 1, 2}},
    {{1, 1, 1, 1, 1, 1, 1, 0}, 0x40, {2, 4, 4, 3}},
    {{1, 0, 1, 1, 1, 1, 1, 1}, 0x40, {3, 2, 4, 4}},
    {{1, 1, 1, 0, 1, 1, 1, 1}, 0x40, {4, 3, 2, 4}},
    {{1, 1, 1, 1, 1, 0, 1, 1}, 0x40, {4, 4, 3, 2}},
    {{1, 1, 1, 0, 1, 1, 1, 0}, 0x40, {2, 3, 2, 3}},
    {{1, 0, 1, 1, 1, 0, 1, 1}, 0x40, {3, 2, 3, 2}},
    {{1, 1, 1, 1, 1, 1, 1, 1}, 0x40, {4, 4, 4, 4}},
}};

namespace {

// (drow, dcol), clockwise from north: the snapshot's order (0334:4161).
constexpr std::array<std::array<int, 2>, 8> kNeighbourOffsets = {{
    {-1, 0}, {-1, 1}, {0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1},
}};

bool in_grid(int x, int y) { return x >= 0 && x < model::kCityW && y >= 0 && y < model::kCityH; }

bool one_of(uint8_t t, std::initializer_list<uint8_t> set) {
    for (uint8_t v : set)
        if (t == v) return true;
    return false;
}

// 0334:4161 with argument 0.
void snapshot_neighbours(const model::CityMap& city, DragState& drag, int x, int y) {
    for (size_t i = 0; i < kNeighbourOffsets.size(); ++i) {
        const int ny = y + kNeighbourOffsets[i][0], nx = x + kNeighbourOffsets[i][1];
        if (!in_grid(nx, ny)) {
            drag.neighbours[i] = 0;
        } else if (city.tile[ny][nx] != 0) {
            drag.neighbours[i] = city.tile[ny][nx];
        }
    }
}

using Flags = std::array<uint8_t, 8>;

// 0x17C20: flags every neighbour in [lo, hi]; returns how many matched.
int mark(const DragState& drag, Flags& flags, int lo, int hi) {
    int n = 0;
    for (size_t i = 0; i < flags.size(); ++i) {
        if (drag.neighbours[i] >= lo && drag.neighbours[i] <= hi) {
            flags[i] = 1;
            ++n;
        }
    }
    return n;
}

// 0x17CB9: the first pattern that fits; 0 (modes untouched) if none does.
uint8_t match_pattern(DragState& drag, const Flags& flags) {
    for (const RoadPattern& p : kRoadPatterns) {
        bool fits = true;
        for (size_t j = 0; j < flags.size() && fits; ++j) fits = p.neighbours[j] == 2 || p.neighbours[j] == flags[j];
        if (!fits) continue;
        for (size_t k = 0; k < 4; ++k) drag.modes[k] = p.modes[k];
        return p.tile;
    }
    return 0;
}

// How one orthogonal neighbour is re-tiled for modes 1-4. Mode 1 writes
// `straight` unless the neighbour is `straight_unless` (walls only); modes
// 2-4 write `keep` if the neighbour is already one of `keep_if`, else `other`.
struct Retile {
    std::initializer_list<uint8_t> keep_if;
    uint8_t keep;
    uint8_t other;
};
struct NeighbourRule {
    int drow, dcol;
    std::initializer_list<uint8_t> skip;  // neighbour tiles never re-tiled
    uint8_t straight;
    int straight_unless;  // -1 for none
    Retile mode2, mode3, mode4;
};

// 0x17EC5, 0x1834D, 0x1879C, 0x18C27.
const NeighbourRule kRoadRules[4] = {
    {-1, 0, {0x42, 0x94, 0x95, 0x86, 0x5E}, 0x36, -1, {{0x3B, 0x3F}, 0x3F, 0x38}, {{0x3A, 0x3E}, 0x3E, 0x39}, {{0x3D, 0x40, 0x41}, 0x40, 0x3C}},
    {0, 1, {0x43, 0x94, 0x95, 0x82}, 0x37, -1, {{0x38, 0x3C}, 0x3C, 0x39}, {{0x3B, 0x3D}, 0x3D, 0x3A}, {{0x3F, 0x40, 0x41}, 0x40, 0x3E}},
    {1, 0, {0x42, 0x94, 0x95, 0x86, 0x5E}, 0x36, -1, {{0x39, 0x3E}, 0x3E, 0x3A}, {{0x38, 0x3F}, 0x3F, 0x3B}, {{0x3C, 0x40, 0x41}, 0x40, 0x3D}},
    {0, -1, {0x43, 0x94, 0x95, 0x82}, 0x37, -1, {{0x3A, 0x3D}, 0x3D, 0x3B}, {{0x39, 0x3C}, 0x3C, 0x38}, {{0x3E, 0x40, 0x41}, 0x40, 0x3F}},
};

// 0x1C12E, 0x1C622, 0x1CB19, 0x1D010.
const NeighbourRule kWallRules[4] = {
    {-1, 0, {0xA1, 0x95, 0x9A, 0x9B}, 0x93, 0x9F, {{0x99, 0xB6, 0x9D}, 0xB6, 0x96}, {{0x98, 0xB5, 0x9C}, 0xB5, 0x97}, {{0xB4, 0xB7, 0x41}, 0xB7, 0xB3}},
    {0, 1, {0xA0, 0x94, 0x9B, 0x9C}, 0x92, 0x9E, {{0x96, 0xB3, 0x9A}, 0xB3, 0x97}, {{0x99, 0xB4, 0x9D}, 0xB4, 0x98}, {{0xB6, 0xB7, 0x41}, 0xB7, 0xB5}},
    {1, 0, {0xA1, 0x95, 0x9C, 0x9D}, 0x93, 0x9F, {{0x97, 0xB5, 0x9B}, 0xB5, 0x98}, {{0x96, 0xB6, 0x9A}, 0xB6, 0x99}, {{0xB3, 0xB7, 0x41}, 0xB7, 0xB4}},
    {0, -1, {0xA0, 0x94, 0x9A, 0x9D}, 0x92, 0x9E, {{0x98, 0xB4, 0x9C}, 0xB4, 0x99}, {{0x97, 0xB3, 0x9B}, 0xB3, 0x96}, {{0xB5, 0xB7, 0x41}, 0xB7, 0xB6}},
};

void retile_neighbours(model::CityMap& city, const DragState& drag, int x, int y, const NeighbourRule rules[4]) {
    for (int k = 0; k < 4; ++k) {
        const NeighbourRule& r = rules[k];
        const int nx = x + r.dcol, ny = y + r.drow;
        if (!in_grid(nx, ny)) continue;
        uint8_t& t = city.tile[ny][nx];
        if (one_of(t, r.skip)) continue;
        const Retile* retile = nullptr;
        switch (drag.modes[static_cast<size_t>(k)]) {
            case 1:
                if (t != r.straight_unless) t = r.straight;
                continue;
            case 2: retile = &r.mode2; break;
            case 3: retile = &r.mode3; break;
            case 4: retile = &r.mode4; break;
            default: continue;
        }
        t = one_of(t, retile->keep_if) ? retile->keep : retile->other;
    }
}

// A crossing converts the cell only if no neighbour already holds `result`.
bool cross(model::CityMap& city, const DragState& drag, Flags& flags, int x, int y, uint8_t result) {
    if (mark(drag, flags, result, result) != 0) return false;
    city.tile[y][x] = result;
    return true;
}

uint8_t wall_form(uint8_t road_tile) {
    switch (road_tile) {
        case 0x36: return 0x93;
        case 0x37: return 0x92;
        case 0x38: return 0x96;
        case 0x39: return 0x97;
        case 0x3A: return 0x98;
        case 0x3B: return 0x99;
        case 0x3C: return 0xB3;
        case 0x3D: return 0xB4;
        case 0x3E: return 0xB5;
        case 0x3F: return 0xB6;
        case 0x40: return 0xB7;
        default: return road_tile;
    }
}

}  // namespace

bool place_road(model::CityMap& city, DragState& drag, int x, int y) {
    if (!in_grid(x, y)) return false;
    uint8_t& t = city.tile[y][x];
    if (t < 0x1D) return false;
    snapshot_neighbours(city, drag, x, y);
    Flags flags{};
    auto mark_connectable = [&] {
        mark(drag, flags, 0x36, 0x43);
        mark(drag, flags, 0x82, 0x89);
        mark(drag, flags, 0x5E, 0x61);
        mark(drag, flags, 0x94, 0x95);
    };
    if (t <= 0x41) {
        mark_connectable();
        const uint8_t tile = match_pattern(drag, flags);
        if (tile == 0) return false;
        t = tile;
        city.operational_state[y][x] = 0;
        retile_neighbours(city, drag, x, y, kRoadRules);
        return true;
    }
    switch (t) {
        case 0x4A:
        case 0x4E:
        case 0x52:
            if (mark(drag, flags, 0x82, 0x89) != 0) return false;
            t = 0x82;
            return true;
        case 0x56: return cross(city, drag, flags, x, y, 0x5E);
        case 0x5A: return cross(city, drag, flags, x, y, 0x86);
        case 0x45: return cross(city, drag, flags, x, y, 0x42);
        case 0x44: return cross(city, drag, flags, x, y, 0x43);
        case 0x92:
        case 0x93:
            mark_connectable();
            match_pattern(drag, flags);  // for its modes; the gate tile is fixed
            t = t == 0x93 ? 0x95 : 0x94;
            retile_neighbours(city, drag, x, y, kRoadRules);
            return true;
        default: return false;
    }
}

bool place_wall(model::CityMap& city, DragState& drag, int x, int y) {
    if (!in_grid(x, y)) return false;
    uint8_t& t = city.tile[y][x];
    if (t < 0x1D) return false;
    snapshot_neighbours(city, drag, x, y);
    Flags flags{};
    auto mark_connectable = [&] {
        mark(drag, flags, 0x92, 0xA3);
        mark(drag, flags, 0xB3, 0xB7);
    };
    const bool wall_ground = t <= 0x35 || (t >= 0x92 && t <= 0x93) || (t >= 0x96 && t <= 0x9F) ||
                             (t >= 0xA2 && t <= 0xA3) || (t >= 0xB3 && t <= 0xB7);
    if (wall_ground) {
        mark_connectable();
        const uint8_t tile = wall_form(match_pattern(drag, flags));
        if (tile == 0) return false;
        t = tile;
        city.operational_state[y][x] = 0;
        retile_neighbours(city, drag, x, y, kWallRules);
        return true;
    }
    switch (t) {
        case 0x36:
        case 0x37:
            mark_connectable();
            match_pattern(drag, flags);
            t = t == 0x37 ? 0x95 : 0x94;
            retile_neighbours(city, drag, x, y, kWallRules);
            return true;
        case 0x45: return cross(city, drag, flags, x, y, 0xA1);
        case 0x44: return cross(city, drag, flags, x, y, 0xA0);
        default: return false;
    }
}

bool place_plaza(model::CityMap& city, int x, int y) {
    if (!in_grid(x, y)) return false;
    const uint8_t t = city.tile[y][x];
    uint8_t& flags = city.operational_state[y][x];
    if (t < 0x36 || t > 0x43 || (flags & 0x10)) return false;
    flags |= 0x10;
    return true;
}

namespace {

// 0x124F8 with direction 8 (none): walk to the anchor, then turn the
// footprint to rubble.
void demolish(model::CityMap& city, month::Random& random, int x, int y) {
    while (x > 0 && (city.operational_state[y][x] & 0x03)) --x;
    while (y > 0 && (city.operational_state[y][x] & 0x0C)) --y;
    const int index = city.tile[y][x] - 0xC8;
    if (index < 0 || index >= static_cast<int>(kBuildingMetrics.size())) return;
    const int w = kBuildingMetrics[static_cast<size_t>(index)].width >> 4;
    const int h = kBuildingMetrics[static_cast<size_t>(index)].height >> 4;
    for (int dy = 0; dy < h; ++dy) {
        for (int dx = 0; dx < w; ++dx) {
            random.advance();
            if (!in_grid(x + dx, y + dy)) continue;
            city.tile[y + dy][x + dx] = static_cast<uint8_t>(0xA7 + (random.walk & 3));
            city.operational_state[y + dy][x + dx] = 0;
        }
    }
}

}  // namespace

bool clear_area(model::CityMap& city, month::Random& random, int x, int y) {
    if (!in_grid(x, y)) return false;
    uint8_t& t = city.tile[y][x];
    uint8_t& flags = city.operational_state[y][x];
    if (t == 0x82 || t == 0x8A) {
        t = 0x4A;
    } else if (t == 0x5E || t == 0x72) {
        t = 0x56;
    } else if (t == 0x86 || t == 0x8E) {
        t = 0x5A;
    } else if (t >= 0x27 && t <= 0x49) {
        t = 0x1D;
        flags = 0;
    } else if (t == 0xA4) {
        t = flags;
        flags = 0;
    } else if (t >= 0x92 && t <= 0xC9) {
        t = 0x1D;
    } else if (t >= 0xCA) {
        demolish(city, random, x, y);
    } else {
        return false;
    }
    return true;
}

}  // namespace gaius::systems::construction
