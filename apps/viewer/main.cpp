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
//   left-click / tap / gamepad A                            -> place current
//     build tool at the clicked cell (save-file mode only)
//   F11                                                     -> cycle window mode
//   Escape / window close                                   -> quit
//
// Build mode (Phase 5) places through systems::construction, which carries
// the real seed tiles and footprints recovered from the executable. The
// drag-based commands (Road/Wall/Plaza/Clear Area) are deliberately absent
// from the tool ring -- their auto-tiling rules aren't reverse engineered
// to implementable precision yet, and faking them would be worse than
// leaving them out. See systems/construction.hpp.
//
// Usage:
//   gaius_viewer <EMPIRE2.0xx | CAESARxx.SAV>
//   gaius_viewer <path> --screenshot out.png --frames N   (headless smoke test)
//   gaius_viewer <CAESARxx.SAV> --test-layer 0..4         (headless: pick a layer directly)
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
#include "formats/empire2/empire2.hpp"
#include "formats/save/save.hpp"
#include "model/city_state.hpp"
#include "platform/input.hpp"
#include "platform/paths.hpp"
#include "platform/window.hpp"
#include "stb_image_write.h"
#include "systems/construction.hpp"
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

constexpr int kCityCellPx = 6;  // world pixels per city-grid cell at zoom == 1
constexpr int kCityWorldW = viewer::kCityW * kCityCellPx;
constexpr int kCityWorldH = viewer::kCityH * kCityCellPx;

// Build-mode tool ring: the placeable construction commands, in toolbar
// order. Deliberately excludes the drag-auto-tiled ones (Road/Wall/Plaza/
// Clear Area) and the variant-selected ones (Forum/Workshop) -- see
// systems/construction.hpp for why those can't be placed yet.
constexpr systems::construction::CommandId kBuildTools[] = {
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
    for (int i = 2; i < argc; ++i) {
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
                    viewer::layer_label(layer));
        std::printf("build mode -- tool: %s (Tab / gamepad X to cycle, left-click / tap / gamepad A to place)\n",
                    systems::construction::command_name(kBuildTools[tool_index]));
        for (const auto& b : test_builds) {
            if (b.tool < 0 || b.tool >= kBuildToolCount) {
                std::printf("--test-build: tool index %d out of range (0..%d)\n", b.tool, kBuildToolCount - 1);
                continue;
            }
            auto tool = kBuildTools[b.tool];
            bool ok = systems::construction::place(state.city, tool, b.x, b.y);
            std::printf("--test-build: place %s at (%d,%d): %s\n", systems::construction::command_name(tool), b.x, b.y,
                        ok ? "OK" : "rejected");
        }
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

        Camera cam = save_mode ? Camera(kCityWorldW, kCityWorldH) : Camera(kWorldW, kWorldH);
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
            int hit = toolbar.hit_test(lx, ly);
            if (hit >= 0) {
                tool_index = hit;
                std::printf("tool: %s\n", systems::construction::command_name(kBuildTools[tool_index]));
                return;
            }
            if (toolbar.contains(lx, ly)) return;  // panel background, not a button

            int cell_x = static_cast<int>(cam.x + lx / cam.zoom) / kCityCellPx;
            int cell_y = static_cast<int>(cam.y + ly / cam.zoom) / kCityCellPx;
            auto tool = kBuildTools[tool_index];
            bool ok = systems::construction::place(state.city, tool, cell_x, cell_y);
            std::printf("place %s at (%d,%d): %s\n", systems::construction::command_name(tool), cell_x, cell_y,
                        ok ? "OK" : "rejected (terrain not buildable / off grid)");
        };

        for (const auto& c : test_clicks) {
            std::printf("--test-click (%d,%d): ", c.x, c.y);
            handle_select_logical(c.x, c.y);
        }

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
                            layer = viewer::next_layer(layer);
                            std::printf("layer: %s\n", viewer::layer_label(layer));
                        }
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

            if (save_mode) {
                viewer::render_city_map_layer(state.city, layer, kCityCellPx, cam.x, cam.y, cam.zoom, kLogicalW,
                                               kLogicalH, frame);
                // Toolbar draws over the map, as the original's panel does.
                // The map is still rendered full-frame so the click ->
                // world math stays a single uniform mapping; the panel
                // simply occludes the bottom, and handle_select_logical
                // keeps clicks there from reaching the occluded cells.
                ui::render(toolbar, tool_index, hovered, viewer::heat_color, frame, kLogicalW, kLogicalH);
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
