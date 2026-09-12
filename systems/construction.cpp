// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/construction.hpp"

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
            city.operational_state[cy][cx] = 0;  // matches the handlers' `mov 3496:[bx+0x7bb4], 0`
        }
    }
    return true;
}

}  // namespace gaius::systems::construction
