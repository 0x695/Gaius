// Gaius — apps/viewer/save_view.hpp
//
// Save-file viewer support for gaius_viewer: the "100x100 tile grid + four
// service-layer heatmaps" deliverable from GAIUS_ROADMAP.md Phase 1, which
// was left unbuilt in the original handoff because no real .SAV file had
// ever been supplied (see CLAUDE.md). formats::save (Phase 0) can already
// slice a loaded save into its confirmed blocks; this header only adds
// *visualization* on top of that -- it decodes nothing new.
//
// Deliberately NOT a semantic decode. GAIUS_MASTERPLAN.md section 4 lists
// the tile->sprite lookup table and most C9D4 bit consumers as still
// unresolved, so every layer here is rendered as a raw-byte heat gradient,
// not "what the game would actually draw." This mirrors how tools/
// empire_view.cpp existed as an uninterpreted dump before terrain-family
// classification was figured out for EMPIRE2.
//
// Pure functions, no SDL/file I/O -- unit-tested directly with synthetic
// block data in tests/test_formats.cpp (no real .SAV is required to test
// the sampling/color math, only to see real output).

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "formats/common/types.hpp"
#include "formats/save/save.hpp"
#include "model/city_state.hpp"

namespace gaius::viewer {

constexpr int kCityW = 100;
constexpr int kCityH = 100;

enum class SaveLayer {
    Tiles,          // city_tiles_100x100 (43A5) -- raw tile ID, no sprite lookup yet
    A2C4,           // cell_value_a2c4 -- numeric coverage
    C9D4,           // cell_flags_c9d4 -- mixed persistent/derived/service/prerequisite bitfield
    Flags7BB4,      // cell_flags_7bb4 -- operational/connection state
    LandValue54A4,  // cell_value_54a4 -- signed land value, confirmed range -8..+50
};

constexpr SaveLayer kSaveLayerOrder[] = {
    SaveLayer::Tiles, SaveLayer::A2C4, SaveLayer::C9D4, SaveLayer::Flags7BB4, SaveLayer::LandValue54A4,
};
constexpr int kSaveLayerCount = 5;

inline SaveLayer next_layer(SaveLayer l) {
    for (int i = 0; i < kSaveLayerCount; ++i) {
        if (kSaveLayerOrder[i] == l) return kSaveLayerOrder[(i + 1) % kSaveLayerCount];
    }
    return SaveLayer::Tiles;
}

inline const char* layer_block_name(SaveLayer l) {
    switch (l) {
        case SaveLayer::Tiles: return "city_tiles_100x100";
        case SaveLayer::A2C4: return "cell_value_a2c4";
        case SaveLayer::C9D4: return "cell_flags_c9d4";
        case SaveLayer::Flags7BB4: return "cell_flags_7bb4";
        case SaveLayer::LandValue54A4: return "cell_value_54a4";
    }
    return "";
}

inline const char* layer_label(SaveLayer l) {
    switch (l) {
        case SaveLayer::Tiles: return "city tiles (43A5, raw ID -- tile->sprite table not yet recovered)";
        case SaveLayer::A2C4: return "A2C4 coverage (raw byte)";
        case SaveLayer::C9D4: return "C9D4 flags (raw byte)";
        case SaveLayer::Flags7BB4: return "7BB4 operational/connection state (raw byte)";
        case SaveLayer::LandValue54A4: return "54A4 land value (signed, -8..+50)";
    }
    return "?";
}

// Generic 0..255 heat gradient (dark -> blue -> green -> yellow -> red).
// No semantics implied -- see file header.
inline formats::RGB heat_color(uint8_t v) {
    struct Stop { int pos; formats::RGB c; };
    static constexpr Stop stops[] = {
        {0, {10, 10, 20}}, {64, {40, 60, 200}}, {128, {60, 200, 90}}, {192, {230, 210, 40}}, {255, {220, 40, 40}},
    };
    constexpr int n = sizeof(stops) / sizeof(stops[0]);
    for (int i = 0; i + 1 < n; ++i) {
        if (v >= stops[i].pos && v <= stops[i + 1].pos) {
            double span = stops[i + 1].pos - stops[i].pos;
            double t = span == 0 ? 0.0 : (v - stops[i].pos) / span;
            auto lerp = [&](uint8_t a, uint8_t b) { return static_cast<uint8_t>(a + t * (b - a)); };
            return {lerp(stops[i].c.r, stops[i + 1].c.r), lerp(stops[i].c.g, stops[i + 1].c.g),
                    lerp(stops[i].c.b, stops[i + 1].c.b)};
        }
    }
    return stops[n - 1].c;
}

// Diverging gradient for the confirmed -8..+50 land-value range (neutral
// gray at 0), scaled to that range rather than the full int8 span so the
// gradient actually uses its contrast within real data bounds.
inline formats::RGB land_value_color(int8_t v) {
    constexpr uint8_t base = 80;
    if (v < 0) {
        double t = std::min(1.0, -static_cast<double>(v) / 8.0);
        return {static_cast<uint8_t>(base + t * 150), static_cast<uint8_t>(base - t * 60),
                static_cast<uint8_t>(base - t * 60)};
    }
    double t = std::min(1.0, static_cast<double>(v) / 50.0);
    return {static_cast<uint8_t>(base - t * 60), static_cast<uint8_t>(base + t * 150),
            static_cast<uint8_t>(base - t * 60)};
}

inline formats::RGB color_for_layer(SaveLayer layer, uint8_t raw) {
    if (layer == SaveLayer::LandValue54A4) return land_value_color(static_cast<int8_t>(raw));
    return heat_color(raw);
}

// Renders one 100x100 save layer into an RGB24 `out` buffer (view_w *
// view_h * 3 bytes), sampling world pixel (cam_x + vx/zoom, cam_y +
// vy/zoom) through a cell_px-sized grid -- same sampling shape as
// apps/viewer/main.cpp's EMPIRE2 render_frame, kept separate rather than
// templated/shared since the two now differ in exactly one place (indexed
// color lookup vs. raw-byte heat color) and forcing a shared abstraction
// for two callers isn't worth it yet.
//
// Grid is assumed row-major (row * kCityW + col), consistent with
// EMPIRE2's layout -- NOT independently confirmed against a real save
// (none has been available; see GAIUS_ROADMAP.md Phase 2/8). Flag back to
// RE if a real .SAV ever contradicts this.
// Same rendering, but reading a live model::CityMap instead of raw save
// blocks -- used by the viewer's build mode (Phase 5), where the grid is
// being mutated by systems::construction and must be re-rendered as it
// changes. Kept alongside the SaveFile version rather than replacing it:
// the SaveFile path is still the honest "look at the bytes on disk" view.
inline void render_city_map_layer(const model::CityMap& city, SaveLayer layer, int cell_px, double cam_x, double cam_y,
                                   double zoom, int view_w, int view_h, std::vector<uint8_t>& out) {
    out.resize(static_cast<size_t>(view_w) * view_h * 3);
    for (int vy = 0; vy < view_h; ++vy) {
        for (int vx = 0; vx < view_w; ++vx) {
            double wx = cam_x + vx / zoom;
            double wy = cam_y + vy / zoom;
            int cell_x = static_cast<int>(wx) / cell_px;
            int cell_y = static_cast<int>(wy) / cell_px;
            formats::RGB c{0, 0, 0};
            if (cell_x >= 0 && cell_x < kCityW && cell_y >= 0 && cell_y < kCityH) {
                uint8_t raw = 0;
                switch (layer) {
                    case SaveLayer::Tiles: raw = city.tile[cell_y][cell_x]; break;
                    case SaveLayer::A2C4: raw = city.coverage[cell_y][cell_x]; break;
                    case SaveLayer::C9D4: raw = city.service_flags[cell_y][cell_x]; break;
                    case SaveLayer::Flags7BB4: raw = city.operational_state[cell_y][cell_x]; break;
                    case SaveLayer::LandValue54A4: raw = static_cast<uint8_t>(city.land_value[cell_y][cell_x]); break;
                }
                c = color_for_layer(layer, raw);
            }
            size_t idx = (static_cast<size_t>(vy) * view_w + vx) * 3;
            out[idx + 0] = c.r;
            out[idx + 1] = c.g;
            out[idx + 2] = c.b;
        }
    }
}

inline void render_city_layer(const formats::save::SaveFile& save, SaveLayer layer, int cell_px, double cam_x,
                               double cam_y, double zoom, int view_w, int view_h, std::vector<uint8_t>& out) {
    auto block = save.block(layer_block_name(layer));
    const uint8_t* cells = block.first;

    out.resize(static_cast<size_t>(view_w) * view_h * 3);
    for (int vy = 0; vy < view_h; ++vy) {
        for (int vx = 0; vx < view_w; ++vx) {
            double wx = cam_x + vx / zoom;
            double wy = cam_y + vy / zoom;
            int cell_x = static_cast<int>(wx) / cell_px;
            int cell_y = static_cast<int>(wy) / cell_px;
            formats::RGB c{0, 0, 0};
            if (cell_x >= 0 && cell_x < kCityW && cell_y >= 0 && cell_y < kCityH) {
                uint8_t raw = cells[static_cast<size_t>(cell_y) * kCityW + cell_x];
                c = color_for_layer(layer, raw);
            }
            size_t idx = (static_cast<size_t>(vy) * view_w + vx) * 3;
            out[idx + 0] = c.r;
            out[idx + 1] = c.g;
            out[idx + 2] = c.b;
        }
    }
}

}  // namespace gaius::viewer
