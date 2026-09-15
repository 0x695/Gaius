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
//   M / gamepad Back / the City, Province, Forum buttons    -> switch screen
//   on the province: the command bar, then click the map    -> build, place a fort, or order a Cohort
//                                                              (click the Cohort, then a point or an army)
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
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "apps/viewer/save_view.hpp"
#include "apps/viewer/screens.hpp"
#include "formats/pal256/pal256.hpp"
#include "formats/pl8/pl8.hpp"
#include "formats/screen_data/screen_data.hpp"
#include "formats/vpx/vpx.hpp"
#include "render/city_render.hpp"
#include "render/province_render.hpp"
#include "systems/campaign.hpp"
#include "systems/month.hpp"
#include "systems/province.hpp"
#include "ui/panel.hpp"
#include "ui/font.hpp"
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
    std::string start_screen;       // --screen city|province|forum
    int start_forum_tab = 0;        // --forum-tab 0..4
    std::vector<int> test_actions;  // --test-action N: a page action applied before the first frame
    bool test_battle = false;       // --test-battle: the first Cohort meets a new army
    bool test_promotion = false;    // --test-promotion: a promotion is offered now
    std::string save_dir_option;    // --save-dir: where the save slots live (default: the per-user data folder)
    int start_speed = 100;          // --speed 0-100: DS:0x5292
    int test_message = -1;          // --test-message N: post systems::messages::Id N before the first frame
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--assets") == 0 && i + 1 < argc) assets_dir = argv[++i];
        if (std::strcmp(argv[i], "--months") == 0 && i + 1 < argc) run_months = std::atoi(argv[++i]);
        if (std::strcmp(argv[i], "--paused") == 0) start_paused = true;
        if (std::strcmp(argv[i], "--screen") == 0 && i + 1 < argc) start_screen = argv[++i];
        if (std::strcmp(argv[i], "--forum-tab") == 0 && i + 1 < argc) start_forum_tab = std::atoi(argv[++i]);
        if (std::strcmp(argv[i], "--test-action") == 0 && i + 1 < argc) test_actions.push_back(std::atoi(argv[++i]));
        if (std::strcmp(argv[i], "--test-battle") == 0) test_battle = true;
        if (std::strcmp(argv[i], "--test-promotion") == 0) test_promotion = true;
        if (std::strcmp(argv[i], "--save-dir") == 0 && i + 1 < argc) save_dir_option = argv[++i];
        if (std::strcmp(argv[i], "--speed") == 0 && i + 1 < argc) start_speed = std::atoi(argv[++i]);
        if (std::strcmp(argv[i], "--test-message") == 0 && i + 1 < argc) test_message = std::atoi(argv[++i]);
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
    // A folder is the game's own: a new career, from the start screen.
    const bool new_career = fs::is_directory(in_path);
    std::error_code ec;
    uintmax_t file_size = new_career ? 0 : fs::file_size(in_path, ec);
    if (ec) {
        std::fprintf(stderr, "failed to stat %s: %s\n", in_path.c_str(), ec.message().c_str());
        return 2;
    }
    bool save_mode = new_career || (file_size == formats::save::kSaveSize);
    if (new_career && assets_dir.empty()) assets_dir = in_path;

    EmpireMap map;
    formats::save::SaveFile save;
    model::CityState state;  // save mode works on the Layer 2 model so build mode can mutate it
    viewer::SaveLayer layer = viewer::SaveLayer::Tiles;
    if (test_layer >= 0 && test_layer < viewer::kSaveLayerCount) layer = viewer::kSaveLayerOrder[test_layer];
    int tool_index = 0;

    try {
        if (new_career) {
            state = model::blank_state();
        } else if (save_mode) {
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
    render::ProvinceSprites province_sprites;  // FIXT3.PL8, SPRITE2.PL8
    bool have_province_sprites = false;
    // The Forum's hall: NEWFORUM.VPX in NEWFORUM.256, and CONTFRM.GD8, the map
    // of which figure is under a click (formats/screen_data).
    formats::IndexedImage forum_picture;
    formats::Palette forum_palette;
    formats::screen_data::ClickMap forum_clicks;
    bool have_forum_picture = false;
    std::string game_dir;  // where the game's files are: a new province's EMPIRE2.0NN is read from here
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
                game_dir = dir;
                try {
                    province_sprites = render::load_province_sprites(dir);
                    have_province_sprites = true;
                } catch (const formats::FormatError&) {
                }
                const auto asset = [&](std::string name) {
                    fs::path p = fs::path(dir) / name;
                    if (fs::exists(p)) return p.string();
                    for (char& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    return (fs::path(dir) / name).string();
                };
                try {
                    forum_picture = formats::vpx::decode(asset("NEWFORUM.VPX")).image;
                    forum_palette = formats::pal256::load(asset("NEWFORUM.256"));
                    forum_clicks = formats::screen_data::load_click_map(asset("CONTFRM.GD8"));
                    have_forum_picture = true;
                } catch (const formats::FormatError&) {
                }
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
        if (game_dir.empty()) game_dir = fs::absolute(in_path).parent_path().string();
    }
    const int city_cell_px = have_sprites ? render::kCellPx : kCityCellPx;
    bool show_sprites = have_sprites && test_layer < 0;
    formats::IndexedImage city_image;
    bool city_image_dirty = true;  // re-rendered whenever build mode or time changes the grid
    formats::IndexedImage province_image;
    bool province_image_dirty = true;
    render::RenderPhase render_phase;

    // The simulation clock (systems::month), seeded from the save.
    systems::month::SimState sim;
    if (save_mode) sim = systems::month::sim_state_from_save(state);
    sim.speed = std::clamp(start_speed / 10 * 10, 0, 100);
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
        province_image_dirty = true;
        report_month();
    };
    // One frame of the main loop (systems::month::run_frame): at the game speed
    // not every frame runs a step, but every frame draws and counts the message.
    auto advance_step = [&]() {
        const int month_before = sim.month;
        if (systems::month::run_frame(state, sim)) {
            city_image_dirty = true;
            province_image_dirty = true;
            if (sim.month != month_before) report_month();
        }
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
        namespace msg = systems::messages;
        // 0x11C72: fewer than 50 pleb groups refuses every construction command.
        if (!economy::enough_plebs(state, static_cast<int>(tool))) {
            sim.messages.post(msg::plain(msg::Id::ConstructionPlebs));
            return false;
        }
        // 0x14E1D / 0x15252: at 30 forums or 30 workshops.
        if (tool == construction::CommandId::Forum && model::global_word(state, 0x6CA0) >= 30) {
            sim.messages.post(msg::plain(msg::Id::NoForum));
            return false;
        }
        if (tool == construction::CommandId::Workshop && model::global_word(state, 0x6C9E) >= 30) {
            sim.messages.post(msg::plain(msg::Id::NoFactory));
            return false;
        }
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

    // ---- The screens: the city, the province, the Forum, a promotion offer and
    // a battle. The last two open themselves when the simulation asks and stop
    // time until they're answered.
    enum class Screen { City, Province, Maps, ForumHall, Forum, Promotion, Battle, Ending, Start, Files, Notice };
    Screen notice_return = Screen::City;  // where the funds warning's Continue goes back to
    int hall_hover = 0;  // the CONTFRM.GD8 region under the pointer
    Screen screen = Screen::City;
    Screen files_return = Screen::Forum;  // where the save / load page goes back to
    bool files_saving = false;
    int funding_level = 0, start_difficulty = 0;  // the start screen's DS:0x6CBA and DS:0x6CB8
    std::vector<ui::PanelButton> slot_buttons;
    if (new_career) {
        screen = Screen::Start;
        time_running = false;
    }
    int overlay = 0;             // viewer::Overlay on the maps panel
    bool ending_caesar = false;  // the ending page: Caesar, or dismissed
    bool quit_requested = false;
    if (start_screen == "maps") screen = Screen::Maps;
    if (start_screen == "province") screen = Screen::Province;
    if (start_screen == "forum") screen = have_forum_picture ? Screen::ForumHall : Screen::Forum;
    if (start_screen == "forum-page") screen = Screen::Forum;
    Screen battle_return = Screen::Province;
    viewer::ForumTab forum_tab =
        static_cast<viewer::ForumTab>(std::clamp(start_forum_tab, 0, static_cast<int>(viewer::kForumTabCount) - 1));
    bool promotion_to_caesar = false;
    viewer::BattleView battle_view;
    int shore_variant = 0;  // DS:0x079C, kept from one generated city to the next

    auto install_hooks = [&]() {
        // The yearly routine offers a promotion. The player answers on the
        // promotion screen, so the hook leaves it unanswered and stops time;
        // the answer is applied from there (administration::accept_promotion,
        // defer_promotion), which is all 0x29023 would have done with it.
        sim.on_promotion = [&](model::CityState&, bool to_caesar) {
            promotion_to_caesar = to_caesar;
            screen = Screen::Promotion;
            time_running = false;
            return 0;
        };
        // A Cohort reaches the army it attacks: the battle screen, with time stopped.
        sim.on_battle = [&](model::CityState&, int cohort, int army) {
            if (screen == Screen::Battle) return;
            battle_return = screen == Screen::City ? Screen::City : Screen::Province;
            battle_view = viewer::BattleView{};
            battle_view.cohort = cohort;
            battle_view.army = army;
            screen = Screen::Battle;
            time_running = false;
            std::printf("battle: the Cohort in slot %d meets the army in slot %d\n", cohort, army);
        };
    };
    if (save_mode) install_hooks();

    // 0x0F81B: an accepted promotion (DS:0x6C26 = 1) moves the governor to the
    // new province: its map EMPIRE2.0NN (0x0FF1C writes the province number in
    // three digits) and a new city (systems::campaign::start_province).
    auto start_new_province = [&]() {
        const int province = model::global_word(state, 0x6CA6);
        char name[16];
        std::snprintf(name, sizeof name, "EMPIRE2.%03d", province);
        fs::path path = fs::path(game_dir) / name;
        if (!fs::exists(path)) {
            std::string lower = name;
            for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            path = fs::path(game_dir) / lower;
        }
        formats::empire2::EmpireMap province_map;
        try {
            province_map = formats::empire2::load(path.string());
        } catch (const std::exception& e) {
            std::printf("can't start the new province, %s: %s\n", name, e.what());
            return false;
        }
        systems::month::Random random = sim.random;
        const int difficulty = sim.difficulty;
        systems::campaign::start_province(state, province_map, random, difficulty, shore_variant);
        model::set_global_word(state, 0x6CB8, difficulty);  // a promotion may have raised it from Easy
        const int speed = sim.speed;
        sim = systems::month::sim_state_from_save(state);
        sim.speed = speed;
        sim.random = random;
        install_hooks();
        city_image_dirty = province_image_dirty = true;
        std::printf("new province: %s (%s), funds %d Dn\n", viewer::province_name(province), name,
                    model::global_word(state, systems::economy::kFunds));
        return true;
    };

    // The save slots: CAESAR01.SAV-CAESAR08.SAV in --save-dir, or the per-user
    // data folder. The files are the original's format (formats::save::write).
    auto slot_path = [&](int slot) {
        std::string dir = save_dir_option;
        if (dir.empty()) {
            try {
                dir = platform::data_path("saves");
            } catch (const std::exception&) {
                dir = game_dir;
            }
        }
        std::error_code mkdir_error;
        fs::create_directories(dir, mkdir_error);
        char name[16];
        std::snprintf(name, sizeof name, "CAESAR%02d.SAV", slot + 1);
        return (fs::path(dir) / name).string();
    };
    auto refresh_slots = [&]() {
        slot_buttons.clear();
        for (int i = 0; i < viewer::kSaveSlots; ++i) {
            const std::string path = slot_path(i);
            std::string label = std::to_string(i + 1) + "  empty";
            bool exists = false;
            if (fs::exists(path)) {
                try {
                    const auto saved = std::make_unique<model::CityState>(model::load(formats::save::load(path)));
                    label = std::to_string(i + 1) + "  " + viewer::province_name(model::global_word(*saved, 0x6CA6)) +
                            ", " + viewer::year_text(model::global_word(*saved, 0x6C32));
                    exists = true;
                } catch (const formats::FormatError&) {
                    label = std::to_string(i + 1) + "  unreadable";
                }
            }
            slot_buttons.push_back({label, viewer::kActionSlot + i, files_saving || exists});
        }
        slot_buttons.push_back({"Back", viewer::kActionBack});
    };
    // After a load: the simulation restarts from the state's words, as the
    // engine's loader leaves them (DS:0x6BFE = rate x month, 0x0568F).
    auto adopt_state = [&]() {
        const systems::month::Random random = sim.random;
        const int speed = sim.speed;
        sim = systems::month::sim_state_from_save(state);
        sim.speed = speed;
        sim.random = random;
        install_hooks();
        city_image_dirty = province_image_dirty = true;
    };

    auto current_page = [&]() -> ui::Page {
        switch (screen) {
            case Screen::Forum: return viewer::forum_page(state, forum_tab, sim.speed);
            case Screen::Notice:
                return viewer::notice_page(systems::messages::kFundsWarning.data(),
                                           systems::messages::kFundsWarning.size());
            case Screen::Promotion: return viewer::promotion_page(state, promotion_to_caesar);
            case Screen::Battle: return viewer::battle_page(state, battle_view);
            case Screen::Ending: return viewer::ending_page(state, ending_caesar);
            case Screen::Start: return viewer::start_page(funding_level, start_difficulty);
            case Screen::Files: return viewer::files_page(files_saving, slot_buttons);
            default: return ui::Page{};
        }
    };
    const auto page_screen = [&]() {
        return screen == Screen::Forum || screen == Screen::Promotion || screen == Screen::Battle ||
               screen == Screen::Ending || screen == Screen::Start || screen == Screen::Files ||
               screen == Screen::Notice;
    };
    auto apply_page_action = [&](int action) {
        namespace admin = systems::administration;
        namespace battle = systems::battle;
        if (action == viewer::kActionSpeedDown || action == viewer::kActionSpeedUp) {
            // 0x0F204 / 0x0F217: tens, 0-100.
            sim.speed = std::clamp(sim.speed + (action == viewer::kActionSpeedUp ? 10 : -10), 0, 100);
            return;
        }
        if (screen == Screen::Notice) {
            if (action == viewer::kActionContinue) {
                screen = notice_return;
                time_running = true;
            }
            return;
        }
        if (action == viewer::kActionOpenSave || action == viewer::kActionOpenLoad) {
            files_return = screen;
            files_saving = action == viewer::kActionOpenSave;
            refresh_slots();
            screen = Screen::Files;
            return;
        }
        if (screen == Screen::Files) {
            if (action == viewer::kActionBack) {
                screen = files_return;
                return;
            }
            const int slot = action - viewer::kActionSlot;
            if (slot < 0 || slot >= viewer::kSaveSlots) return;
            const std::string path = slot_path(slot);
            try {
                if (files_saving) {
                    formats::save::write(model::serialize(state), path);
                    std::printf("saved %s\n", path.c_str());
                    screen = files_return;
                } else {
                    state = model::load(formats::save::load(path));
                    adopt_state();
                    std::printf("loaded %s\n", path.c_str());
                    screen = Screen::City;
                    time_running = true;
                }
            } catch (const std::exception& e) {
                std::printf("%s: %s\n", path.c_str(), e.what());
            }
            return;
        }
        if (screen == Screen::Start) {
            // 0x27F54-0x27F83: funding 0-9, difficulty 0-2.
            if (action == viewer::kActionFundingDown && funding_level > 0) --funding_level;
            if (action == viewer::kActionFundingUp && funding_level < 9) ++funding_level;
            if (action == viewer::kActionDifficultyDown && start_difficulty > 0) --start_difficulty;
            if (action == viewer::kActionDifficultyUp && start_difficulty < 2) ++start_difficulty;
            if (action == viewer::kActionBegin) {
                state = model::blank_state();
                sim.difficulty = start_difficulty;
                const int province =
                    systems::campaign::begin_new_game(state, sim.random, funding_level, start_difficulty);
                if (province >= 0 && start_new_province()) {
                    // 0x0F7D3 / 0x0F7DA: the first message, shown 78 frames.
                    sim.messages.post(systems::messages::plain(systems::messages::Id::NoCity));
                    sim.messages.timer = 0x4E;
                    screen = Screen::City;
                    time_running = true;
                }
            }
            return;
        }
        if (screen == Screen::Forum) {
            if (!viewer::apply_forum_action(state, action, forum_tab))
                screen = have_forum_picture ? Screen::ForumHall : Screen::City;
        } else if (screen == Screen::Promotion) {
            if (action == viewer::kActionAccept) {
                if (promotion_to_caesar) {
                    admin::become_caesar(state);
                    std::printf("you are Caesar\n");
                    ending_caesar = true;
                    screen = Screen::Ending;
                    return;
                } else {
                    admin::accept_promotion(state, sim.difficulty);
                    std::printf("promotion accepted: %s\n", viewer::rank_name(model::global_word(state, admin::kRank)));
                    if (model::global_word(state, 0x6C26) == 1) start_new_province();
                }
            } else if (action == viewer::kActionWait9 || action == viewer::kActionWait24) {
                admin::defer_promotion(state, action == viewer::kActionWait9 ? 9 : 24);
            } else {
                return;
            }
            screen = Screen::City;
            time_running = true;
        } else if (screen == Screen::Battle) {
            if (action >= viewer::kActionTactic && action < viewer::kActionTactic + 4) {
                // The screen draws the generator once a frame while it waits (systems/battle.hpp).
                sim.random.advance();
                battle_view.last = battle::fight_round(state, battle_view.cohort, battle_view.army,
                                                       static_cast<battle::Tactic>(action - viewer::kActionTactic),
                                                       sim.random);
                battle_view.has_round = true;
            } else if (action == viewer::kActionRetreat) {
                battle::retreat(state, battle_view.cohort);
                battle_view.retreated = true;
            } else if (action == viewer::kActionContinue) {
                screen = battle_return;
                time_running = true;
            }
        } else if (screen == Screen::Ending) {
            if (action == viewer::kActionQuit) quit_requested = true;
            if (action == viewer::kActionContinue) screen = Screen::City;  // governing on, time still stopped
        }
        city_image_dirty = province_image_dirty = true;
    };

    // The province toolbar (DS:0x123E): construction ids with the manual's names.
    struct ProvinceCommand {
        const char* label;
        int id;
    };
    static constexpr ProvinceCommand kProvinceCommands[] = {
        {"Clear", 35}, {"Road", 36}, {"Wall", 37},   {"Tower", 41},  {"Highway", 42},
        {"Fort", 29},  {"Halt", 30}, {"Patrol", 31}, {"Attack", 32}, {"Home", 33}};
    constexpr int kProvinceCommandCount = sizeof(kProvinceCommands) / sizeof(kProvinceCommands[0]);
    int province_command = 1;
    int order_cohort = -1;             // the Cohort a Patrol or Attack order is being given to
    int patrol_x = -1, patrol_y = -1;  // the patrol's first point
    systems::construction::DragState province_drag;
    int province_drag_x = -1, province_drag_y = -1;

    // The province actor of a type range standing on a cell (+0x12/+0x13), or -1.
    auto province_actor_at = [&](int x, int y, int type_lo, int type_hi) {
        for (int i = 0; i < model::kActorCount; ++i) {
            const model::Actor& a = state.objects[static_cast<size_t>(i)];
            if (a.active() != 0 && a.type() >= type_lo && a.type() <= type_hi && a.raw_x() == x && a.raw_y() == y)
                return i;
        }
        return -1;
    };
    // One province construction command on a cell, charged the way the build
    // routine charges it: the terrain's cost shift, the pleb gate and the funds.
    auto province_place = [&](int id, int x, int y) {
        namespace province = systems::province;
        namespace economy = systems::economy;
        if (x < 0 || y < 0 || x >= province::kMapW || y >= province::kMapW) return false;
        if (!economy::enough_plebs(state, id)) {
            sim.messages.post(systems::messages::plain(systems::messages::Id::ConstructionPlebs));  // 0x11C8F
            return false;
        }
        if (id == 29 && model::global_word(state, 0x6C12) >= 10) {
            sim.messages.post(systems::messages::plain(systems::messages::Id::NoFort));  // 0x15482
            return false;
        }
        const uint8_t tile = state.empire.cells[static_cast<size_t>(y) * province::kMapW + x] & 0x7F;
        const int cost = economy::kConstructionCost[static_cast<size_t>(id)] << economy::province_cost_shift(id, tile);
        if (!economy::can_afford(state, cost)) {
            if (economy::grant_emergency_funds(state))
                std::printf("Rome sends 500 Dn in emergency funds\n");
            else
                std::printf("not enough funds: %d Dn\n", cost);
            return false;
        }
        province::Built built = province::Built::Refused;
        switch (id) {
            case 35: built = province::clear_province(state, x, y); break;
            case 36: built = province::place_province_road(state, province_drag, x, y); break;
            case 37: built = province::place_great_wall(state, province_drag, x, y); break;
            case 41: built = province::place_great_tower(state, x, y); break;
            case 42: built = province::place_highway(state, province_drag, x, y); break;
            case 29:
                built = province::place_fort(state, x, y) >= 0 ? province::Built::Charged : province::Built::Refused;
                break;
            default: break;
        }
        if (built == province::Built::Charged) economy::charge(state, cost);
        if (built != province::Built::Refused) province_image_dirty = true;
        return built != province::Built::Refused;
    };
    // A click on the province map with the selected command.
    auto province_click = [&](int x, int y) {
        namespace province = systems::province;
        const ProvinceCommand& command = kProvinceCommands[province_command];
        if (command.id < 30 || command.id > 33) {
            const bool ok = province_place(command.id, x, y);
            province_drag_x = x;
            province_drag_y = y;
            std::printf("%s at (%d,%d): %s\n", command.label, x, y, ok ? "OK" : "refused");
            return;
        }
        if (order_cohort < 0) {
            const int cohort = province_actor_at(x, y, province::kCohortType, province::kCohortType);
            if (cohort < 0) {
                std::printf("%s: click a Cohort\n", command.label);
                return;
            }
            if (command.id == 30 || command.id == 33) {
                const bool ok = command.id == 30 ? province::order_halt(state, cohort)
                                                 : province::order_go_home(state, cohort);
                std::printf("%s: %s\n", command.label, ok ? "OK" : "refused");
                return;
            }
            order_cohort = cohort;
            patrol_x = patrol_y = -1;
            std::printf(command.id == 31 ? "Patrol: click the first point\n" : "Attack: click an army\n");
            return;
        }
        bool ok = false;
        if (command.id == 31) {
            if (patrol_x < 0) {
                patrol_x = x;
                patrol_y = y;
                std::printf("Patrol: click the second point\n");
                return;
            }
            ok = province::order_patrol(state, order_cohort, patrol_x, patrol_y, x, y);
        } else {
            const int army = province_actor_at(x, y, province::kArmyType, province::kSeaArmyType);
            if (army < 0) {
                std::printf("Attack: click an army\n");
                return;
            }
            ok = province::order_attack(state, order_cohort, army);
        }
        std::printf("%s: %s\n", command.label, ok ? "OK" : "refused");
        order_cohort = -1;
        patrol_x = patrol_y = -1;
    };
    auto province_label = [&]() -> std::string {
        const ProvinceCommand& command = kProvinceCommands[province_command];
        std::string s = command.label;
        const int cost = systems::economy::kConstructionCost[static_cast<size_t>(command.id)];
        if (cost > 0) s += ", " + std::to_string(cost) + " Dn";
        if (command.id >= 30 && command.id <= 33) {
            // FONT1 has no colon (DS:0F64 draws it as '0').
            if (order_cohort < 0)
                s += ", pick a Cohort";
            else if (command.id == 31)
                s += patrol_x < 0 ? ", first point" : ", second point";
            else
                s += ", pick an army";
        }
        return s + " - Funds " + std::to_string(model::global_word(state, systems::economy::kFunds)) + " Dn";
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

        // Pages, the screen strip and the province bar are laid out at 1x: the
        // same 320 x 200 constraint ui/toolbar.hpp describes -- at 2x a Forum
        // page's dozen rows would not fit the logical screen.
        const ui::Metrics page_metrics = ui::metrics_for(ui::Breakpoint::Desktop);
        const std::vector<ui::PanelButton> screen_tabs = {
            {"City", 900}, {"Province", 901}, {"Maps", 902}, {"Forum", 903}};
        std::vector<ui::PanelButton> overlay_buttons;
        for (int i = 0; i < viewer::kOverlayCount; ++i) overlay_buttons.push_back({viewer::kOverlayNames[i], 1100 + i});
        const ui::PanelLayout overlay_bar = ui::bar_layout(overlay_buttons, page_metrics, kLogicalW, kLogicalH);
        const ui::PanelLayout hall_bar = ui::bar_layout({}, page_metrics, kLogicalW, kLogicalH);
        const ui::PanelLayout strip = ui::strip_layout(screen_tabs, page_metrics, kLogicalW);
        std::vector<ui::PanelButton> province_buttons;
        for (int i = 0; i < kProvinceCommandCount; ++i) province_buttons.push_back({kProvinceCommands[i].label, 1000 + i});
        const ui::PanelLayout province_bar = ui::bar_layout(province_buttons, page_metrics, kLogicalW, kLogicalH);
        Camera pcam(40.0 * render::kProvincePx, 40.0 * render::kProvincePx);
        pcam.visible_h = kLogicalH - province_bar.frame.h;
        pcam.clamp();
        int page_hovered = -1;
        const auto strip_hit = [&](int lx, int ly) {
            for (size_t i = 0; i < strip.tabs.size(); ++i)
                if (strip.tabs[i].contains(lx, ly)) return static_cast<int>(i);
            return -1;
        };
        // The message box, and whether it shows on this screen (the city,
        // province and maps views; the messages option DS:0x6C78 on).
        static constexpr ui::Rect kMessageBox{12, 16, 240, 28};
        const auto message_visible = [&]() {
            return (screen == Screen::City || screen == Screen::Province || screen == Screen::Maps) &&
                   sim.messages.showing() && model::global_word(state, 0x6C78) != 0;
        };
        const auto switch_to = [&](int i) {
            screen = i == 0   ? Screen::City
                     : i == 1 ? Screen::Province
                     : i == 2 ? Screen::Maps
                              : (have_forum_picture ? Screen::ForumHall : Screen::Forum);
            order_cohort = patrol_x = patrol_y = -1;
            province_image_dirty = true;
        };

        // One handler for "the primary action happened at this logical
        // point", shared by the real input path and --test-click. The
        // toolbar gets first refusal: a click on the panel selects a tool
        // and must NOT also fall through to the map underneath it.
        auto handle_select_logical = [&](int lx, int ly) {
            if (!save_mode) return;
            drag_last_x = drag_last_y = -1;
            province_drag_x = province_drag_y = -1;
            drag_undo.clear();
            drag_refund = 0;
            if (page_screen()) {
                const ui::Page page = current_page();
                const int action = ui::hit_test(page, ui::layout(page, page_metrics, kLogicalW, kLogicalH), lx, ly);
                if (action >= 0) apply_page_action(action);
                return;
            }
            if (const int tab = strip_hit(lx, ly); tab >= 0) {
                switch_to(tab);
                return;
            }
            if (message_visible() && kMessageBox.contains(lx, ly)) {
                // 0x0F982: a message with a place takes the view there (its
                // cell less 10 and 5), and the message goes.
                const systems::messages::Message m = sim.messages.current;
                if (m.place != systems::messages::Place::None) {
                    sim.messages.timer = 0;
                    if (m.place == systems::messages::Place::City) {
                        screen = Screen::City;
                        cam.x = (m.x - 10) * city_cell_px;
                        cam.y = (m.y - 5) * city_cell_px;
                        cam.clamp();
                    } else {
                        screen = Screen::Province;
                        pcam.x = (m.x - 10) * render::kProvincePx;
                        pcam.y = (m.y - 5) * render::kProvincePx;
                        pcam.clamp();
                        province_image_dirty = true;
                    }
                }
                return;
            }
            if (screen == Screen::ForumHall) {
                // 0x0DF57: the figure under the click opens its advisor
                // (findings section 34.2). Those Gaius has no page for say so.
                const int region = forum_clicks.region_at(lx, ly);
                const int tab = region == 2   ? viewer::kGovernor
                                : region == 3 ? viewer::kLegion
                                : region == 5 ? viewer::kTreasurer
                                : region == 6 ? viewer::kRatings
                                : region == 7 ? viewer::kTribune
                                              : -1;
                if (tab >= 0) {
                    forum_tab = static_cast<viewer::ForumTab>(tab);
                    screen = Screen::Forum;
                } else if (region != 0) {
                    std::printf("that advisor isn't modeled yet\n");
                }
                return;
            }
            if (screen == Screen::Maps) {
                for (size_t i = 0; i < overlay_bar.buttons.size(); ++i) {
                    if (overlay_bar.buttons[i].contains(lx, ly)) overlay = static_cast<int>(i);
                }
                return;
            }
            if (screen == Screen::Province) {
                for (size_t i = 0; i < province_bar.buttons.size(); ++i) {
                    if (province_bar.buttons[i].contains(lx, ly)) {
                        province_command = static_cast<int>(i);
                        order_cohort = patrol_x = patrol_y = -1;
                        std::printf("province command: %s\n", kProvinceCommands[i].label);
                        return;
                    }
                }
                if (province_bar.frame.contains(lx, ly)) return;
                province_click(static_cast<int>(pcam.x + lx / pcam.zoom) / render::kProvincePx,
                               static_cast<int>(pcam.y + ly / pcam.zoom) / render::kProvincePx);
                return;
            }
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

        if (save_mode && test_message > 0) {
            const auto id = static_cast<systems::messages::Id>(test_message);
            sim.messages.post(systems::messages::at(id, systems::messages::Place::City, 50, 50));
            if (model::global_word(state, 0x6C78) == 0) model::set_global_word(state, 0x6C78, 1);
        }
        if (save_mode && test_promotion) {
            model::set_global_word(state, 0x6CA4, (model::global_word(state, 0x6CA6) + 1) % 50);
            sim.on_promotion(state, false);
        }
        if (save_mode && test_battle) {
            // A five-strong army on the first Cohort's cell, in the first free slot.
            int cohort = -1, army = -1;
            for (int i = 0; i < model::kActorCount; ++i) {
                const model::Actor& a = state.objects[static_cast<size_t>(i)];
                if (a.active() != 0 && a.type() == systems::province::kCohortType && cohort < 0) cohort = i;
                if (a.active() == 0 && army < 0) army = i;
            }
            if (cohort >= 0 && army >= 0) {
                model::Actor& a = state.objects[static_cast<size_t>(army)];
                a.raw = state.objects[static_cast<size_t>(cohort)].raw;
                a.raw[0x07] = systems::province::kArmyType;
                a.raw[0x08] = static_cast<uint8_t>(army);
                a.raw[0x09] = 0;
                a.raw[systems::battle::kArmySize] = 5;
                a.raw[0x31] = systems::province::kLinger;
                sim.on_battle(state, cohort, army);
            } else {
                std::printf("--test-battle: no Cohort or no free slot\n");
            }
        }
        for (const auto& c : test_clicks) {
            std::printf("--test-click (%d,%d): ", c.x, c.y);
            handle_select_logical(c.x, c.y);
        }
        for (int action : test_actions) {
            std::printf("--test-action %d\n", action);
            if (save_mode && page_screen()) apply_page_action(action);
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
                    case platform::CommandType::CycleScreen:
                        if (save_mode && (!page_screen() || screen == Screen::Forum))
                            switch_to(screen == Screen::City ? 1 : screen == Screen::Province ? 2
                                                              : screen == Screen::Maps    ? 3
                                                                                          : 0);
                        break;
                    case platform::CommandType::Secondary:
                        if (save_mode && message_visible()) {
                            // 0x0F93A: a right-click on the message dismisses it.
                            int mx = 0, my = 0;
                            if (window.window_to_logical(cmd->x, cmd->y, &mx, &my) && kMessageBox.contains(mx, my)) {
                                sim.messages.timer = 1;
                                sim.messages.current.place = systems::messages::Place::None;
                                break;
                            }
                        }
                        if (save_mode && screen == Screen::ForumHall) {
                            screen = Screen::City;  // right-click leaves the Forum
                            break;
                        }
                        if (save_mode && screen == Screen::Province) {
                            // Right-click forgets an order being given.
                            order_cohort = patrol_x = patrol_y = -1;
                            break;
                        }
                        if (save_mode && screen == Screen::City) {
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
                        if (save_mode && screen == Screen::Province) {
                            province_command = (province_command + 1) % kProvinceCommandCount;
                            order_cohort = patrol_x = patrol_y = -1;
                            std::printf("province command: %s\n", kProvinceCommands[province_command].label);
                        } else if (save_mode && screen == Screen::City) {
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
                        if (save_mode && screen == Screen::Province) {
                            // Clear, Road, Wall and Highway follow the pointer cell by cell.
                            const int id = kProvinceCommands[province_command].id;
                            if (province_drag_x < 0 || !(id == 35 || id == 36 || id == 37 || id == 42)) break;
                            int lx = 0, ly = 0;
                            if (!window.window_to_logical(cmd->x, cmd->y, &lx, &ly) || province_bar.frame.contains(lx, ly))
                                break;
                            const int tx = static_cast<int>(pcam.x + lx / pcam.zoom) / render::kProvincePx;
                            const int ty = static_cast<int>(pcam.y + ly / pcam.zoom) / render::kProvincePx;
                            while (province_drag_x != tx || province_drag_y != ty) {
                                const int ddx = tx - province_drag_x, ddy = ty - province_drag_y;
                                if (std::abs(ddx) >= std::abs(ddy))
                                    province_drag_x += ddx > 0 ? 1 : -1;
                                else
                                    province_drag_y += ddy > 0 ? 1 : -1;
                                province_place(id, province_drag_x, province_drag_y);
                            }
                            break;
                        }
                        if (!save_mode || screen != Screen::City || drag_last_x < 0) break;
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
                        const bool inside = window.window_to_logical(cmd->x, cmd->y, &lx, &ly);
                        hovered = inside && screen == Screen::City ? toolbar.hit_test(lx, ly) : -1;
                        page_hovered = -1;
                        hall_hover = inside && screen == Screen::ForumHall ? forum_clicks.region_at(lx, ly) : 0;
                        if (inside && page_screen()) {
                            const ui::Page page = current_page();
                            page_hovered =
                                ui::hit_test(page, ui::layout(page, page_metrics, kLogicalW, kLogicalH), lx, ly);
                        }
                        break;
                    }
                    case platform::CommandType::PanBegin:
                        break;
                    case platform::CommandType::PanEnd:
                        break;
                    case platform::CommandType::PanMove: {
                        Camera& c = screen == Screen::Province ? pcam : cam;
                        c.x -= cmd->dx / c.zoom;
                        c.y -= cmd->dy / c.zoom;
                        c.clamp();
                        break;
                    }
                    case platform::CommandType::Zoom: {
                        Camera& c = screen == Screen::Province ? pcam : cam;
                        c.zoom *= std::pow(1.1, cmd->zoom_delta);
                        c.clamp();
                        break;
                    }
                    default:
                        break;
                }
            }

            if (time_running) {
                const Uint32 now = SDL_GetTicks();
                // A battle or a promotion offer stops time mid-step.
                for (int budget = 8; budget > 0 && time_running && now - last_step_ms >= kStepMs; --budget) {
                    advance_step();
                    last_step_ms += kStepMs;
                    if (sim.funds_warning) {
                        // 0x0FAD3: the one full-screen warning, which waits for a click.
                        sim.funds_warning = false;
                        notice_return = screen;
                        screen = Screen::Notice;
                        time_running = false;
                    }
                    if (sim.dismissed) {
                        sim.dismissed = false;
                        ending_caesar = false;
                        screen = Screen::Ending;
                        time_running = false;
                        std::printf("dismissed: three tributes missed\n");
                    }
                }
                if (now - last_step_ms >= kStepMs) last_step_ms = now;  // behind: drop the backlog
            }

            const std::string tool_text = save_mode ? tool_label() : std::string();
            const std::string funds_text =
                save_mode ? "Funds " + std::to_string(model::global_word(state, systems::economy::kFunds)) + " Dn"
                          : std::string();
            const ui::GameFont* font = have_font ? &game_font : nullptr;
            if (save_mode && page_screen()) {
                const ui::Page page = current_page();
                frame.assign(static_cast<size_t>(kLogicalW) * kLogicalH * 3, 0);
                ui::render(page, ui::layout(page, page_metrics, kLogicalW, kLogicalH), frame, kLogicalW, kLogicalH,
                           page_metrics, font, page_hovered);
            } else if (save_mode && screen == Screen::ForumHall) {
                // The original's Forum picture, clicked through its own click map.
                frame.resize(static_cast<size_t>(kLogicalW) * kLogicalH * 3);
                for (size_t i = 0; i < forum_picture.pixels.size() && i * 3 + 2 < frame.size(); ++i) {
                    const formats::RGB c = forum_palette.colors[forum_picture.pixels[i]];
                    frame[i * 3] = c.r;
                    frame[i * 3 + 1] = c.g;
                    frame[i * 3 + 2] = c.b;
                }
                static constexpr const char* kFigures[] = {"Choose an advisor",       "The statue",
                                                            "The governor's affairs",  "The Military Advisor",
                                                            "An advisor",              "The Treasurer",
                                                            "The ratings",             "The Tribune of the Plebs",
                                                            "Industry"};
                ui::render_bar({}, -1, hall_bar, frame, kLogicalW, kLogicalH, page_metrics, font,
                               kFigures[std::clamp(hall_hover, 0, 8)]);
                ui::render_strip(screen_tabs, 3, strip, frame, kLogicalW, kLogicalH, page_metrics, font);
            } else if (save_mode && screen == Screen::Province) {
                if (have_province_sprites) {
                    if (province_image_dirty) {
                        render::render_province(state.empire, province_sprites, state.objects, province_image);
                        province_image_dirty = false;
                    }
                    render_sprite_view(province_image, province_sprites.palette, pcam, frame);
                } else {
                    render_empire_frame(state.empire, pcam, frame);
                }
                const std::string label = province_label();
                ui::render_bar(province_buttons, province_command, province_bar, frame, kLogicalW, kLogicalH,
                               page_metrics, font, label.c_str());
                ui::render_strip(screen_tabs, 1, strip, frame, kLogicalW, kLogicalH, page_metrics, font);
            } else if (save_mode && screen == Screen::Maps) {
                // The city under the overlay: cells it marks take its colour
                // over a third of the city's, the rest are dimmed.
                if (have_sprites) {
                    if (city_image_dirty) {
                        render::render_city(state.city, sprites, 0, 0, viewer::kCityW, viewer::kCityH, city_image,
                                            render_phase, &state.objects);
                        city_image_dirty = false;
                    }
                    render_sprite_view(city_image, sprites.palette, cam, frame);
                } else {
                    viewer::render_city_map_layer(state.city, viewer::SaveLayer::Tiles, city_cell_px, cam.x, cam.y,
                                                  cam.zoom, kLogicalW, kLogicalH, frame);
                }
                for (int vy = 0; vy < kLogicalH; ++vy) {
                    const int cy = static_cast<int>(cam.y + vy / cam.zoom) / city_cell_px;
                    for (int vx = 0; vx < kLogicalW; ++vx) {
                        const int cx = static_cast<int>(cam.x + vx / cam.zoom) / city_cell_px;
                        if (cx < 0 || cy < 0 || cx >= viewer::kCityW || cy >= viewer::kCityH) continue;
                        const size_t i = (static_cast<size_t>(vy) * kLogicalW + vx) * 3;
                        formats::RGB c;
                        if (viewer::overlay_color(state.city, static_cast<viewer::Overlay>(overlay), cx, cy, c)) {
                            frame[i] = static_cast<uint8_t>((frame[i] + 2 * c.r) / 3);
                            frame[i + 1] = static_cast<uint8_t>((frame[i + 1] + 2 * c.g) / 3);
                            frame[i + 2] = static_cast<uint8_t>((frame[i + 2] + 2 * c.b) / 3);
                        } else {
                            frame[i] = static_cast<uint8_t>(frame[i] / 3);
                            frame[i + 1] = static_cast<uint8_t>(frame[i + 1] / 3);
                            frame[i + 2] = static_cast<uint8_t>(frame[i + 2] / 3);
                        }
                    }
                }
                const std::string label = std::string("Maps - ") + viewer::kOverlayNames[overlay];
                ui::render_bar(overlay_buttons, overlay, overlay_bar, frame, kLogicalW, kLogicalH, page_metrics, font,
                               label.c_str());
                ui::render_strip(screen_tabs, 2, strip, frame, kLogicalW, kLogicalH, page_metrics, font);
            } else if (save_mode && show_sprites) {
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
            if (save_mode && screen == Screen::City)
                ui::render_strip(screen_tabs, 0, strip, frame, kLogicalW, kLogicalH, page_metrics, font);
            if (save_mode && message_visible()) {
                // 0x279AC: the message's two 28-character lines. The original
                // draws them at (16, 14) and (16, 30); here they sit below the
                // screen strip.
                const std::string& t = sim.messages.current.text;
                const std::string line1 = t.substr(0, std::min<size_t>(28, t.size()));
                const std::string line2 = t.size() > 28 ? t.substr(28) : std::string();
                for (int y = kMessageBox.y; y < kMessageBox.y + kMessageBox.h; ++y) {
                    for (int x = kMessageBox.x; x < kMessageBox.x + kMessageBox.w; ++x) {
                        const size_t i = (static_cast<size_t>(y) * kLogicalW + x) * 3;
                        const bool edge = y == kMessageBox.y || x == kMessageBox.x ||
                                          y == kMessageBox.y + kMessageBox.h - 1 || x == kMessageBox.x + kMessageBox.w - 1;
                        frame[i] = edge ? 140 : 58;
                        frame[i + 1] = edge ? 132 : 55;
                        frame[i + 2] = edge ? 100 : 40;
                    }
                }
                for (const auto& [line, y] : {std::pair<const std::string&, int>{line1, kMessageBox.y + 3},
                                              std::pair<const std::string&, int>{line2, kMessageBox.y + 15}}) {
                    if (font)
                        ui::draw_game_text(frame, kLogicalW, kLogicalH, kMessageBox.x + 4, y, line.c_str(), 1, *font);
                    else
                        ui::draw_text(frame, kLogicalW, kLogicalH, kMessageBox.x + 4, y, line.c_str(), 1,
                                      formats::RGB{232, 226, 200});
                }
            }
            window.present_rgb24(frame);
            if (quit_requested) running = false;
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
