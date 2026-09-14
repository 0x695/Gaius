// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius Phase 1 deliverable: gaius_viewer
//
// "Look at your data" tool per GAIUS_ROADMAP.md Phase 1 -- proves the
// resolution-independence and input-abstraction architecture
// (GAIUS_MASTERPLAN.md section 5a) end-to-end with a real SDL2 window,
// not just static PNG dumps like Phase 0's tools produced.
//
// Accepts either an EMPIRE2.0xx scenario (rendered with the same
// terrain-family color classification as Phase 0's empire_view tool) or a
// CAESARxx.SAV save file (rendered as the 100x100 city tile grid or one of
// the four service-layer heatmaps -- see apps/viewer/save_view.hpp). Which
// one is picked is decided by the file's exact size, not its extension --
// same "sniff structure, don't trust extensions" lesson as
// docs/CAESAR_GOG_BUILD_FINDINGS.md's MINIFONT.PL1 finding.
//
// Controls:
//   left-drag with middle mouse / single-finger touch drag / left gamepad
//     stick  -> pan
//   scroll wheel / gamepad triggers                        -> zoom
//   right-click / two-finger tap / gamepad B                -> cycle save
//     layer (save-file mode only; no-op for an EMPIRE2 scenario)
//   Tab / gamepad X                                         -> cycle build tool
//   V / gamepad right shoulder / tap the selected button    -> next Forum grade or Workshop goods
//   Space / gamepad Y                                       -> pause / resume time (a month about every 2 s;
//                                                              save-file mode)
//   left-click / tap / gamepad A                            -> place current
//     build tool at the clicked cell (save-file mode only)
//   F11                                                     -> cycle window mode
//   Escape / window close                                   -> quit
//
// Build mode (Phase 5) places through systems::construction, which carries
// the real seed tiles, footprints and drag auto-tiling recovered from the
// executable; a mouse left-drag lays roads and walls cell by cell. Time runs
// through systems::month (walkers, fire and all), and the city view animates
// on the engine's frame counters. See systems/construction.hpp.
//
// Usage:
//   gaius_viewer <EMPIRE2.0xx | CAESARxx.SAV>
//   gaius_viewer <path> --screenshot out.png --frames N   (headless smoke test)
//   gaius_viewer <CAESARxx.SAV> --assets <game dir>       (draw the city with the game's sprites; by default
//                                                          they're looked for beside the save and one folder up)
//   gaius_viewer <CAESARxx.SAV> --test-layer 0..4         (headless: pick a data layer directly)
//   gaius_viewer <CAESARxx.SAV> --months N [--paused]     (run N months before the first frame; start paused)
//   gaius_viewer <CAESARxx.SAV> --test-build T X Y        (headless: place tool T at cell X,Y)

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "apps/viewer/save_view.hpp"
#include "formats/pl8/pl8.hpp"
#include "render/city_render.hpp"
#include "systems/month.hpp"
#include "ui/game_font.hpp"
#include "formats/empire2/empire2.hpp"
#include "formats/save/save.hpp"
#include "model/city_state.hpp"
#include "platform/input.hpp"
#include "platform/paths.hpp"
#include "platform/window.hpp"
#include "stb_image_write.h"
#include "systems/construction.hpp"
#include "systems/economy.hpp"
#include "ui/metrics.hpp"
#include "ui/toolbar.hpp"

using namespace gaius;
using gaius::formats::empire2::EmpireMap;
using gaius::formats::empire2::kMapH;
using gaius::formats::empire2::kMapW;
namespace fs = std::filesystem;

namespace {

constexpr int kLogicalW = 320;  // matches the original Caesar screen resolution -- a deliberate nod, not a constraint
constexpr int kLogicalH = 200;
constexpr int kCellPx = 16;  // world pixels per EMPIRE2 map cell at zoom == 1
constexpr int kWorldW = kMapW * kCellPx;
constexpr int kWorldH = kMapH * kCellPx;

// World pixels per city-grid cell at zoom == 1 for the data-layer heatmaps.
// With the game's sprites available the city uses its real 16 px cells
// (render::kCellPx) for every layer, so camera and clicks share one scale.
constexpr int kCityCellPx = 6;

// Build-mode tool ring: the placeable construction commands, in toolbar
// order. The drag-built ones (Road/Wall/Plaza/Clear Area) place one cell per
// click, which is what the engine's handler does for each cell of a drag.
// Forum and Workshop place the grade / goods chosen with CycleVariant (V,
// gamepad right shoulder, or tapping the selected button again).
constexpr systems::construction::CommandId kBuildTools[] = {
    systems::construction::CommandId::Road,      systems::construction::CommandId::Wall,
    systems::construction::CommandId::Plaza,     systems::construction::CommandId::ClearArea,
    systems::construction::CommandId::Forum,     systems::construction::CommandId::Workshop,
    systems::construction::CommandId::Housing,   systems::construction::CommandId::Well,
    systems::construction::CommandId::Fountain,  systems::construction::CommandId::ReservoirPipe,
    systems::construction::CommandId::Temple,    systems::construction::CommandId::BathHouses,
    systems::construction::CommandId::Hospital,  systems::construction::CommandId::School,
    systems::construction::CommandId::Oracle,    systems::construction::CommandId::Theater,
    systems::construction::CommandId::Coliseum,  systems::construction::CommandId::Hippodrome,
    systems::construction::CommandId::Barracks,  systems::construction::CommandId::Prefecture,
    systems::construction::CommandId::Market,    systems::construction::CommandId::HeavyIndustry,
};
constexpr int kBuildToolCount = sizeof(kBuildTools) / sizeof(kBuildTools[0]);

// Same classification as tools/empire_view.cpp -- kept in sync deliberately;
// this is exactly the kind of small duplication that should collapse into
// a shared formats::empire2 rendering helper once a second consumer shows
// up for real (not yet worth abstracting for one).
formats::RGB classify_color(uint8_t v) {
    if (v == 0x00) return {20, 20, 20};
    if (v == 0x41) return {255, 255, 0};
    if (v == 0x4A) return {255, 0, 0};
    if (v == 0x4B) return {255, 128, 128};
    if (v == 0x61) return {255, 165, 0};
    if (v >= 0x4E && v <= 0x50) return {40, 90, 220};
    if (v >= 0x1D && v <= 0x35) return {90, 160, 60};
    if (v >= 0x51 && v <= 0x60) return {140, 120, 90};
    if (v == 0x4C || v == 0x79 || v == 0x7A || v == 0x78) return {200, 100, 200};
    return {100, 100, 100};
}

// Generalized over world size so the same pan/zoom/input-handling code
// drives either the EMPIRE2 world or the (differently-sized) city-save
// world -- the two view modes differ only in what they sample and draw,
// never in how the camera behaves.
struct Camera {
    double world_w, world_h;
    double x, y;  // top-left world pixel visible at viewport (0,0)
    double zoom = 1.0;

    // Height of the map area actually VISIBLE to the player, in logical
    // pixels. This is less than kLogicalH whenever the toolbar panel
    // occludes the bottom of the frame. Clamping against the full buffer
    // height instead would leave the world's last panel_h/zoom pixels
    // permanently behind the panel -- reachable by neither eye nor click,
    // which for a 100x100 city at desktop scale hides its bottom ~5 rows.
    double visible_h = kLogicalH;

    explicit Camera(double ww, double wh)
        : world_w(ww), world_h(wh), x(ww / 2.0 - kLogicalW / 2.0), y(wh / 2.0 - kLogicalH / 2.0) {}

    void clamp() {
        zoom = std::clamp(zoom, 0.5, 8.0);
        double view_w = kLogicalW / zoom, view_h = visible_h / zoom;
        x = std::clamp(x, 0.0, std::max(0.0, world_w - view_w));
        y = std::clamp(y, 0.0, std::max(0.0, world_h - view_h));
    }
};

void render_empire_frame(const EmpireMap& map, const Camera& cam, std::vector<uint8_t>& rgb_out) {
    rgb_out.resize(static_cast<size_t>(kLogicalW) * kLogicalH * 3);
    for (int vy = 0; vy < kLogicalH; ++vy) {
        for (int vx = 0; vx < kLogicalW; ++vx) {
            double wx = cam.x + vx / cam.zoom;
            double wy = cam.y + vy / cam.zoom;
            int cell_x = static_cast<int>(wx) / kCellPx;
            int cell_y = static_cast<int>(wy) / kCellPx;
            formats::RGB c{0, 0, 0};
            if (cell_x >= 0 && cell_x < kMapW && cell_y >= 0 && cell_y < kMapH) {
                c = classify_color(map.at(cell_y, cell_x));
            }
            size_t idx = (static_cast<size_t>(vy) * kLogicalW + vx) * 3;
            rgb_out[idx + 0] = c.r;
            rgb_out[idx + 1] = c.g;
            rgb_out[idx + 2] = c.b;
        }
    }
}

// Samples a rendered city image (render::render_city, 16 px per cell)
// through the camera and converts it with the city palette.
void render_sprite_view(const formats::IndexedImage& img, const formats::Palette& pal, const Camera& cam,
                        std::vector<uint8_t>& rgb_out) {
    rgb_out.resize(static_cast<size_t>(kLogicalW) * kLogicalH * 3);
    for (int vy = 0; vy < kLogicalH; ++vy) {
        for (int vx = 0; vx < kLogicalW; ++vx) {
            const int ix = static_cast<int>(cam.x + vx / cam.zoom);
            const int iy = static_cast<int>(cam.y + vy / cam.zoom);
            formats::RGB c{0, 0, 0};
            if (ix >= 0 && ix < img.width && iy >= 0 && iy < img.height) {
                c = pal.colors[img.pixels[static_cast<size_t>(iy) * img.width + ix]];
            }
            size_t idx = (static_cast<size_t>(vy) * kLogicalW + vx) * 3;
            rgb_out[idx + 0] = c.r;
            rgb_out[idx + 1] = c.g;
            rgb_out[idx + 2] = c.b;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <EMPIRE2.0xx | CAESARxx.SAV> [--screenshot out.png --frames N]\n", argv[0]);
        return 1;
    }
    std::string in_path = argv[1];
    std::string screenshot_path;
    int screenshot_frames = 0;
    double test_pan_x = 0, test_pan_y = 0, test_zoom = 1.0;
    int test_layer = -1;
    struct TestBuild { int tool, x, y; };
    std::vector<TestBuild> test_builds;  // repeatable: --test-build may appear many times
    struct TestClick { int x, y; };
    std::vector<TestClick> test_clicks;  // logical-space clicks, for headless UI tests
    int ui_scale_override = -1;          // -1 = use the size heuristic
    std::string assets_dir;
    int run_months = 0;
    bool start_paused = false;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--assets") == 0 && i + 1 < argc) assets_dir = argv[++i];
        if (std::strcmp(argv[i], "--months") == 0 && i + 1 < argc) run_months = std::atoi(argv[++i]);
        if (std::strcmp(argv[i], "--paused") == 0) start_paused = true;
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshot_path = argv[++i];
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) screenshot_frames = std::atoi(argv[++i]);
        // Headless verification hooks -- there's no real mouse/touch/gamepad
        // in a CI/sandboxed environment, so these let automated smoke tests
        // exercise the same Camera math the real input path drives.
        if (std::strcmp(argv[i], "--test-pan") == 0 && i + 2 < argc) {
            test_pan_x = std::atof(argv[++i]);
            test_pan_y = std::atof(argv[++i]);
        }
        if (std::strcmp(argv[i], "--test-zoom") == 0 && i + 1 < argc) test_zoom = std::atof(argv[++i]);
        if (std::strcmp(argv[i], "--test-layer") == 0 && i + 1 < argc) test_layer = std::atoi(argv[++i]);
        // Headless build-mode hook: place tool #N at grid cell (x,y) before
        // the first frame, so the placement path can be smoke-tested without
        // a real mouse -- same spirit as --test-pan/--test-zoom. Repeatable,
        // so a whole test city can be laid down in one invocation.
        if (std::strcmp(argv[i], "--test-build") == 0 && i + 3 < argc) {
            TestBuild b;
            b.tool = std::atoi(argv[++i]);
            b.x = std::atoi(argv[++i]);
            b.y = std::atoi(argv[++i]);
            test_builds.push_back(b);
        }
        // Headless UI hook: a click in LOGICAL framebuffer coordinates,
        // fed through the same handler the real Select command uses, so
        // toolbar hit-testing is exercised rather than bypassed.
        if (std::strcmp(argv[i], "--test-click") == 0 && i + 2 < argc) {
            TestClick c;
            c.x = std::atoi(argv[++i]);
            c.y = std::atoi(argv[++i]);
            test_clicks.push_back(c);
        }
        if (std::strcmp(argv[i], "--ui-scale") == 0 && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "desktop") ui_scale_override = static_cast<int>(ui::Breakpoint::Desktop);
            else if (v == "handheld") ui_scale_override = static_cast<int>(ui::Breakpoint::Handheld);
            else if (v == "phone") ui_scale_override = static_cast<int>(ui::Breakpoint::Phone);
            else if (v == "tv") ui_scale_override = static_cast<int>(ui::Breakpoint::Tv);
            else std::fprintf(stderr, "unknown --ui-scale '%s' (desktop|handheld|phone|tv)\n", v.c_str());
        }
    }

    // Dispatch by exact file size, not extension -- EMPIRE2.0xx is always
    // exactly 1602 bytes, CAESARxx.SAV is always exactly 57126
    // (formats::save::kSaveSize); no other size is valid input here. Same
    // "sniff structure, don't trust extensions" lesson as
    // docs/CAESAR_GOG_BUILD_FINDINGS.md's MINIFONT.PL1 finding.
    std::error_code ec;
    uintmax_t file_size = fs::file_size(in_path, ec);
    if (ec) {
        std::fprintf(stderr, "failed to stat %s: %s\n", in_path.c_str(), ec.message().c_str());
        return 2;
    }
    bool save_mode = (file_size == formats::save::kSaveSize);

    EmpireMap map;
    formats::save::SaveFile save;
    model::CityState state;  // save mode works on the Layer 2 model so build mode can mutate it
    viewer::SaveLayer layer = viewer::SaveLayer::Tiles;
    if (test_layer >= 0 && test_layer < viewer::kSaveLayerCount) layer = viewer::kSaveLayerOrder[test_layer];
    int tool_index = 0;

    try {
        if (save_mode) {
            save = formats::save::load(in_path);
            state = model::load(save);
        } else {
            map = formats::empire2::load(in_path);
        }
    } catch (const formats::FormatError& e) {
        std::fprintf(stderr, "failed to load %s: %s\n", in_path.c_str(), e.what());
        return 2;
    }

    // The game's own city sprites, for save mode: from --assets, else the
    // save's folder (a game install keeps its saves beside the sheets) or the
    // folder above it. Without them the viewer shows the data layers only.
    render::CitySprites sprites;
    bool have_sprites = false;
    ui::GameFont game_font;  // FONT1.PL8, drawn with the city palette
    bool have_font = false;
    formats::PL8Sheet toolbar_icons;  // POINTERS.PL8
    bool have_icons = false;
    if (save_mode) {
        std::vector<std::string> candidates;
        if (!assets_dir.empty()) candidates.push_back(assets_dir);
        const fs::path save_dir = fs::absolute(in_path).parent_path();
        candidates.push_back(save_dir.string());
        candidates.push_back(save_dir.parent_path().string());
        for (const std::string& dir : candidates) {
            try {
                sprites = render::load_city_sprites(dir);
                have_sprites = true;
                std::printf("city sprites: %s\n", dir.c_str());
                try {
                    game_font = ui::load_game_font(dir, sprites.palette);
                    have_font = true;
                } catch (const formats::FormatError&) {
                }
                for (const char* name : {"POINTERS.PL8", "pointers.pl8"}) {
                    const fs::path p = fs::path(dir) / name;
                    if (!fs::exists(p)) continue;
                    try {
                        toolbar_icons = formats::pl8::load(p.string());
                        have_icons = true;
                    } catch (const formats::FormatError&) {
                    }
                    break;
                }
                break;
            } catch (const formats::FormatError&) {
            }
        }
        if (!have_sprites) std::printf("city sprites not found (pass --assets <game dir>); showing data layers only\n");
    }
    const int city_cell_px = have_sprites ? render::kCellPx : kCityCellPx;
    bool show_sprites = have_sprites && test_layer < 0;
    formats::IndexedImage city_image;
    bool city_image_dirty = true;  // re-rendered whenever build mode or time changes the grid
    render::RenderPhase render_phase;

    // The simulation clock (systems::month), seeded from the save.
    systems::month::SimState sim;
    if (save_mode) sim = systems::month::sim_state_from_save(state);
    bool time_running = save_mode && !start_paused;
    // One step every 19 ms: a month (106 steps) in about 2 s, and the walkers
    // move a pixel each step.
    constexpr Uint32 kStepMs = 19;
    auto report_month = [&]() {
        std::printf("month %d, year %d: population %d, funds %d Dn\n", sim.month + 1, sim.year,
                    4 * sim.population_units, model::global_word(state, systems::economy::kFunds));
        if (sim.month == 0) {
            // The year just settled (systems::economy::run_year): the Treasurer's report.
            auto g = [&](uint16_t ds) { return model::global_word(state, ds); };
            std::printf("  last year: population tax %d, industrial tax %d, construction %d, operating costs %d, "
                        "tribute %d, profit %d\n",
                        g(0x6BB2), g(0x6BB0), g(0x6BAE), g(0x6BAC), g(0x6BAA), g(0x6BB4));
        }
    };
    auto advance_month = [&]() {
        systems::month::run_month(state, sim);
        city_image_dirty = true;
        report_month();
    };
    auto advance_step = [&]() {
        systems::month::run_step(state, sim);
        city_image_dirty = true;
        if (sim.step == 0) report_month();
    };

    // One placement for the click handler and --test-build: the drag-built
    // commands keep their neighbour snapshot in `drag`, and Clear Area draws
    // rubble from the simulation's random number generator.
    systems::construction::DragState drag;
    int drag_last_x = -1, drag_last_y = -1;  // the last cell a click or drag placed on
    // The active drag's undo trail, for the original's cancel gesture (right
    // button while the left is still down -- manual, Building Roads): the
    // pre-placement tile and 7BB4 byte of every cell that might have changed
    // this drag, and the funds it cost. Cleared at the start of each new
    // gesture (handle_select_logical) and consumed by CancelDrag below.
    struct DragUndoCell {
        int x, y;
        uint8_t tile, op_state;
    };
    std::vector<DragUndoCell> drag_undo;
    int drag_refund = 0;
    // The variant choices the original makes in sub-menus: DS:0x6D89 (Forum
    // grade) and DS:0x6D87 (Workshop goods).
    int forum_grade = 0, workshop_goods = 0;
    auto tool_label = [&]() -> std::string {
        namespace construction = systems::construction;
        const auto tool = kBuildTools[tool_index];
        if (tool == construction::CommandId::Forum)
            return "Forum grade " + std::to_string(forum_grade + 1) + ", " +
                   std::to_string(construction::kForumGradeCost[static_cast<size_t>(forum_grade)]) + " Dn";
        if (tool == construction::CommandId::Workshop)
            return std::string("Workshop: ") + construction::kWorkshopGoodsNames[static_cast<size_t>(workshop_goods)] +
                   ", " + std::to_string(systems::economy::construction_cost(tool)) + " Dn";
        const int cost = systems::economy::construction_cost(tool);
        return std::string(construction::command_name(tool)) + (cost > 0 ? ", " + std::to_string(cost) + " Dn" : "");
    };
    auto cycle_variant = [&]() {
        const auto tool = kBuildTools[tool_index];
        if (tool == systems::construction::CommandId::Forum) {
            forum_grade = (forum_grade + 1) % 8;
        } else if (tool == systems::construction::CommandId::Workshop) {
            workshop_goods = (workshop_goods + 1) % 8;
        } else {
            return false;
        }
        std::printf("tool: %s\n", tool_label().c_str());
        return true;
    };
    auto place_tool = [&](systems::construction::CommandId tool, int x, int y) {
        namespace construction = systems::construction;
        namespace economy = systems::economy;
        // The engine checks the cost before calling the handler and charges it
        // only when the handler succeeds; a drag pays per cell (0x11DAC-0x120B0).
        const int cost = economy::construction_cost(tool, forum_grade);
        if (!economy::can_afford(state, cost)) {
            if (economy::grant_emergency_funds(state))
                std::printf("Rome sends 500 Dn in emergency funds\n");
            else
                std::printf("not enough funds: %s costs %d Dn\n", construction::command_name(tool), cost);
            return false;
        }
        const bool draggable = construction::placement_spec(tool).kind == construction::PlacementKind::DragAutoTiled;
        if (draggable) {
            // Snapshot a 7x7 box around the target before placing: Road, Wall
            // and Plaza only ever retile a direct neighbour (1-cell radius),
            // but Clear Area can wreck a whole building anchored up to 3 cells
            // away (construction::wreck_cells walks to the anchor first), so
            // that's the box that covers every drag command's worst case.
            // Only the first snapshot of a given cell in this drag is kept, so
            // a later overlapping placement can't clobber the pre-drag state
            // CancelDrag needs to restore.
            for (int dy = -3; dy <= 3; ++dy) {
                for (int dx = -3; dx <= 3; ++dx) {
                    const int sx = x + dx, sy = y + dy;
                    if (sx < 0 || sy < 0 || sx >= model::kCityW || sy >= model::kCityH) continue;
                    bool seen = false;
                    for (const auto& u : drag_undo) {
                        if (u.x == sx && u.y == sy) {
                            seen = true;
                            break;
                        }
                    }
                    if (!seen)
                        drag_undo.push_back({sx, sy, state.city.tile[sy][sx], state.city.operational_state[sy][sx]});
                }
            }
        }
        bool placed = false;
        switch (tool) {
            case construction::CommandId::Road: placed = construction::place_road(state.city, drag, x, y); break;
            case construction::CommandId::Wall: placed = construction::place_wall(state.city, drag, x, y); break;
            case construction::CommandId::Plaza: placed = construction::place_plaza(state.city, x, y); break;
            case construction::CommandId::ClearArea: placed = construction::clear_area(state, sim.random, x, y); break;
            case construction::CommandId::Forum: placed = construction::place_forum(state, forum_grade, x, y); break;
            case construction::CommandId::Workshop:
                placed = construction::place_workshop(state, workshop_goods, x, y);
                break;
            default: placed = construction::place(state.city, tool, x, y); break;
        }
        if (placed) {
            economy::charge(state, cost);
            if (draggable) drag_refund += cost;
        }
        return placed;
    };

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 3;
    }

    // Prove platform::paths works even though this app doesn't save
    // anything yet -- Phase 1's scope per the roadmap is wiring these up,
    // not yet using them for real persistence (that's Phase 2+).
    try {
        std::string prefs = platform::data_path("viewer-scratch");
        std::printf("prefs directory resolved to: %s\n", prefs.c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "note: platform::data_path unavailable in this environment: %s\n", e.what());
    }

    if (save_mode) {
        std::printf("save mode -- layer: %s (right-click / two-finger-tap / gamepad B to cycle)\n",
                    show_sprites ? "city view (the game's sprites)" : viewer::layer_label(layer));
        std::printf("build mode -- tool: %s (Tab / gamepad X to cycle, left-click / tap / gamepad A to place)\n",
                    systems::construction::command_name(kBuildTools[tool_index]));
        for (const auto& b : test_builds) {
            if (b.tool < 0 || b.tool >= kBuildToolCount) {
                std::printf("--test-build: tool index %d out of range (0..%d)\n", b.tool, kBuildToolCount - 1);
                continue;
            }
            auto tool = kBuildTools[b.tool];
            bool ok = place_tool(tool, b.x, b.y);
            std::printf("--test-build: place %s at (%d,%d): %s\n", systems::construction::command_name(tool), b.x, b.y,
                        ok ? "OK" : "rejected");
        }
        for (int m = 0; m < run_months; ++m) advance_month();
        std::printf("time: %s (Space / gamepad Y)\n", time_running ? "running, about 2 s a month" : "paused");
    }

    try {
        platform::Window window("Gaius Viewer - " + in_path, kLogicalW, kLogicalH, 960, 600);

        // Toolbar. Sized from the physical window (or an explicit
        // --ui-scale), but laid out in LOGICAL coordinates, which is what
        // makes one hit-test correct for mouse, touch and gamepad alike:
        // every device's position reaches it through
        // window.window_to_logical().
        int pw0 = 0, ph0 = 0;
        window.physical_size(&pw0, &ph0);
        // Touch presence is a fact SDL reports; screen size alone is not
        // enough to tell a small window from a small device.
        bool has_touch = SDL_GetNumTouchDevices() > 0;
        ui::Breakpoint bp = ui_scale_override >= 0 ? static_cast<ui::Breakpoint>(ui_scale_override)
                                                   : ui::breakpoint_for(pw0, ph0, has_touch);
        ui::Metrics metrics = ui::metrics_for(bp);
        ui::Toolbar toolbar(kBuildTools, kBuildToolCount, metrics, kLogicalW, kLogicalH);
        int hovered = -1;
        if (save_mode) {
            std::printf("toolbar: %s scale=%dx  %d buttons (%dx%d px) in %d row(s), panel %d px tall\n",
                        ui::breakpoint_name(bp), metrics.scale, kBuildToolCount, metrics.button_px(),
                        metrics.button_px(), toolbar.rows(), toolbar.panel().h);
        }

        Camera cam = save_mode ? Camera(viewer::kCityW * city_cell_px, viewer::kCityH * city_cell_px)
                               : Camera(kWorldW, kWorldH);
        if (save_mode) cam.visible_h = kLogicalH - toolbar.panel().h;
        cam.x += test_pan_x;
        cam.y += test_pan_y;
        cam.zoom *= test_zoom;
        cam.clamp();
        std::vector<uint8_t> frame;
        bool running = true;
        int frame_count = 0;

        // One handler for "the primary action happened at this logical
        // point", shared by the real input path and --test-click. The
        // toolbar gets first refusal: a click on the panel selects a tool
        // and must NOT also fall through to the map underneath it.
        auto handle_select_logical = [&](int lx, int ly) {
            if (!save_mode) return;
            drag_last_x = drag_last_y = -1;
            drag_undo.clear();
            drag_refund = 0;
            int hit = toolbar.hit_test(lx, ly);
            if (hit >= 0) {
                if (hit == tool_index && cycle_variant()) return;  // tapping the selected button again
                tool_index = hit;
                std::printf("tool: %s\n", systems::construction::command_name(kBuildTools[tool_index]));
                return;
            }
            if (toolbar.contains(lx, ly)) return;  // panel background, not a button

            int cell_x = static_cast<int>(cam.x + lx / cam.zoom) / city_cell_px;
            int cell_y = static_cast<int>(cam.y + ly / cam.zoom) / city_cell_px;
            auto tool = kBuildTools[tool_index];
            bool ok = place_tool(tool, cell_x, cell_y);
            if (ok) city_image_dirty = true;
            drag_last_x = cell_x;
            drag_last_y = cell_y;
            std::printf("place %s at (%d,%d): %s\n", systems::construction::command_name(tool), cell_x, cell_y,
                        ok ? "OK" : "rejected (terrain not buildable / off grid)");
        };

        for (const auto& c : test_clicks) {
            std::printf("--test-click (%d,%d): ", c.x, c.y);
            handle_select_logical(c.x, c.y);
        }

        Uint32 last_step_ms = SDL_GetTicks();
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                int pw, ph;
                window.physical_size(&pw, &ph);
                auto cmd = platform::translate_event(event, pw, ph);
                if (!cmd) continue;

                switch (cmd->type) {
                    case platform::CommandType::Quit:
                        running = false;
                        break;
                    case platform::CommandType::ToggleWindowMode: {
                        auto next = window.mode() == platform::WindowMode::Windowed ? platform::WindowMode::Borderless
                                    : window.mode() == platform::WindowMode::Borderless
                                        ? platform::WindowMode::Fullscreen
                                        : platform::WindowMode::Windowed;
                        window.set_mode(next);
                        break;
                    }
                    case platform::CommandType::Secondary:
                        if (save_mode) {
                            // With sprites: city view, then each data layer, then back.
                            if (show_sprites) {
                                show_sprites = false;
                                layer = viewer::kSaveLayerOrder[0];
                            } else if (have_sprites && viewer::next_layer(layer) == viewer::kSaveLayerOrder[0]) {
                                show_sprites = true;
                            } else {
                                layer = viewer::next_layer(layer);
                            }
                            std::printf("layer: %s\n",
                                        show_sprites ? "city view (the game's sprites)" : viewer::layer_label(layer));
                        }
                        break;
                    case platform::CommandType::ToggleTime:
                        if (save_mode) {
                            time_running = !time_running;
                            std::printf("time: %s\n", time_running ? "running" : "paused");
                        }
                        break;
                    case platform::CommandType::CycleVariant:
                        if (save_mode) cycle_variant();
                        break;
                    case platform::CommandType::CycleTool:
                        if (save_mode) {
                            tool_index = (tool_index + 1) % kBuildToolCount;
                            std::printf("build tool: %s\n",
                                        systems::construction::command_name(kBuildTools[tool_index]));
                        }
                        break;
                    case platform::CommandType::Select: {
                        if (!save_mode) break;
                        // Physical click -> logical framebuffer -> toolbar or
                        // world -> grid cell, reusing the same camera math the
                        // renderer samples through, so mouse, touch tap and
                        // gamepad A all land on the same button or cell.
                        int lx = 0, ly = 0;
                        if (!window.window_to_logical(cmd->x, cmd->y, &lx, &ly)) break;
                        handle_select_logical(lx, ly);
                        break;
                    }
                    case platform::CommandType::SelectMove: {
                        // Dragging a drag-built command: the engine calls its
                        // handler for each cell the cursor passes over, so step
                        // one cell at a time from the last placed cell.
                        if (!save_mode || drag_last_x < 0) break;
                        const auto tool = kBuildTools[tool_index];
                        if (systems::construction::placement_spec(tool).kind !=
                            systems::construction::PlacementKind::DragAutoTiled) {
                            break;
                        }
                        int lx = 0, ly = 0;
                        if (!window.window_to_logical(cmd->x, cmd->y, &lx, &ly) || toolbar.contains(lx, ly)) break;
                        const int tx = static_cast<int>(cam.x + lx / cam.zoom) / city_cell_px;
                        const int ty = static_cast<int>(cam.y + ly / cam.zoom) / city_cell_px;
                        while (drag_last_x != tx || drag_last_y != ty) {
                            const int ddx = tx - drag_last_x, ddy = ty - drag_last_y;
                            if (std::abs(ddx) >= std::abs(ddy)) {
                                drag_last_x += ddx > 0 ? 1 : -1;
                            } else {
                                drag_last_y += ddy > 0 ? 1 : -1;
                            }
                            if (place_tool(tool, drag_last_x, drag_last_y)) city_image_dirty = true;
                        }
                        break;
                    }
                    case platform::CommandType::CancelDrag: {
                        // The original's drag-cancel gesture (see DragUndoCell's
                        // comment): put back every cell this drag touched and
                        // refund what it cost. Does nothing outside an active
                        // drag-built placement.
                        if (!save_mode || drag_undo.empty()) break;
                        for (const auto& u : drag_undo) {
                            state.city.tile[u.y][u.x] = u.tile;
                            state.city.operational_state[u.y][u.x] = u.op_state;
                        }
                        systems::economy::refund(state, drag_refund);
                        std::printf("drag cancelled: refunded %d Dn\n", drag_refund);
                        drag_undo.clear();
                        drag_refund = 0;
                        drag_last_x = drag_last_y = -1;
                        city_image_dirty = true;
                        break;
                    }
                    case platform::CommandType::Hover: {
                        if (!save_mode) break;
                        int lx = 0, ly = 0;
                        hovered = window.window_to_logical(cmd->x, cmd->y, &lx, &ly) ? toolbar.hit_test(lx, ly) : -1;
                        break;
                    }
                    case platform::CommandType::PanBegin:
                        break;
                    case platform::CommandType::PanEnd:
                        break;
                    case platform::CommandType::PanMove:
                        cam.x -= cmd->dx / cam.zoom;
                        cam.y -= cmd->dy / cam.zoom;
                        cam.clamp();
                        break;
                    case platform::CommandType::Zoom:
                        cam.zoom *= std::pow(1.1, cmd->zoom_delta);
                        cam.clamp();
                        break;
                    default:
                        break;
                }
            }

            if (time_running) {
                const Uint32 now = SDL_GetTicks();
                for (int budget = 8; budget > 0 && now - last_step_ms >= kStepMs; --budget) {
                    advance_step();
                    last_step_ms += kStepMs;
                }
                if (now - last_step_ms >= kStepMs) last_step_ms = now;  // behind: drop the backlog
            }

            const std::string tool_text = save_mode ? tool_label() : std::string();
            const std::string funds_text =
                save_mode ? "Funds " + std::to_string(model::global_word(state, systems::economy::kFunds)) + " Dn"
                          : std::string();
            if (save_mode && show_sprites) {
                // The draw loop's animation (renderer findings section 6): the
                // water phase advances on each drawn frame while the 32-step
                // counter DS:0x6D3E is odd, and burning tiles blink on bit 2
                // of the 128-step counter DS:0x6D3A.
                if ((sim.ticks % 32) & 1) {
                    render_phase.water = (render_phase.water + 1) % 3;
                    city_image_dirty = true;
                }
                const int blink = ((sim.ticks % 128) >> 2) & 1;
                if (blink != render_phase.blink) {
                    render_phase.blink = blink;
                    city_image_dirty = true;
                }
                if (render_phase.ticks != sim.ticks || !render_phase.workshop_records) {
                    render_phase.ticks = sim.ticks;
                    render_phase.population_units = model::global_word(state, 0x6C10);
                    render_phase.coverage_base = model::global_word(state, 0x6BF8);
                    render_phase.workshop_records = &state.table_720;
                    render_phase.barracks_records = &state.table_120;
                    city_image_dirty = true;
                }
                if (city_image_dirty) {
                    render::render_city(state.city, sprites, 0, 0, viewer::kCityW, viewer::kCityH, city_image,
                                        render_phase, &state.objects);
                    city_image_dirty = false;
                }
                render_sprite_view(city_image, sprites.palette, cam, frame);
                ui::render(toolbar, tool_index, hovered, viewer::heat_color, frame, kLogicalW, kLogicalH,
                           have_font ? &game_font : nullptr, have_icons ? &toolbar_icons : nullptr,
                           have_sprites ? &sprites.palette : nullptr, tool_text.c_str(), funds_text.c_str());
            } else if (save_mode) {
                viewer::render_city_map_layer(state.city, layer, city_cell_px, cam.x, cam.y, cam.zoom, kLogicalW,
                                               kLogicalH, frame);
                // Toolbar draws over the map, as the original's panel does.
                // The map is still rendered full-frame so the click ->
                // world math stays a single uniform mapping; the panel
                // simply occludes the bottom, and handle_select_logical
                // keeps clicks there from reaching the occluded cells.
                ui::render(toolbar, tool_index, hovered, viewer::heat_color, frame, kLogicalW, kLogicalH,
                           have_font ? &game_font : nullptr, have_icons ? &toolbar_icons : nullptr,
                           have_sprites ? &sprites.palette : nullptr, tool_text.c_str(), funds_text.c_str());
            } else {
                render_empire_frame(map, cam, frame);
            }
            window.present_rgb24(frame);
            ++frame_count;

            if (screenshot_frames > 0 && frame_count >= screenshot_frames) {
                if (!screenshot_path.empty()) {
                    stbi_write_png(screenshot_path.c_str(), kLogicalW, kLogicalH, 3, frame.data(), kLogicalW * 3);
                    std::printf("wrote screenshot to %s after %d frame(s)\n", screenshot_path.c_str(), frame_count);
                }
                running = false;
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        SDL_Quit();
        return 4;
    }

    SDL_Quit();
    return 0;
}
