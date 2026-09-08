// Gaius Phase 1 deliverable: gaius_viewer
//
// "Look at your data" tool per GAIUS_ROADMAP.md Phase 1 -- proves the
// resolution-independence and input-abstraction architecture
// (GAIUS_MASTERPLAN.md section 5a) end-to-end with a real SDL2 window,
// not just static PNG dumps like Phase 0's tools produced.
//
// Renders an EMPIRE2 scenario as a pannable/zoomable world (camera larger
// than the fixed logical viewport -- deliberately, to actually exercise
// pan/zoom rather than fitting everything on screen at once) using the
// SAME terrain-family color classification as Phase 0's empire_view tool.
//
// Controls:
//   left-drag with middle mouse / single-finger touch drag / left gamepad
//     stick  -> pan
//   scroll wheel / gamepad triggers                        -> zoom
//   F11                                                     -> cycle window mode
//   Escape / window close                                   -> quit
//
// Usage:
//   gaius_viewer <EMPIRE2.0xx>
//   gaius_viewer <EMPIRE2.0xx> --screenshot out.png --frames N   (headless smoke test)

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "formats/empire2/empire2.hpp"
#include "platform/input.hpp"
#include "platform/paths.hpp"
#include "platform/window.hpp"
#include "stb_image_write.h"

using namespace gaius;
using gaius::formats::empire2::EmpireMap;
using gaius::formats::empire2::kMapH;
using gaius::formats::empire2::kMapW;

namespace {

constexpr int kLogicalW = 320;  // matches the original Caesar screen resolution -- a deliberate nod, not a constraint
constexpr int kLogicalH = 200;
constexpr int kCellPx = 16;  // world pixels per map cell at zoom == 1
constexpr int kWorldW = kMapW * kCellPx;
constexpr int kWorldH = kMapH * kCellPx;

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

struct Camera {
    double x = kWorldW / 2.0 - kLogicalW / 2.0;  // top-left world pixel visible at viewport (0,0)
    double y = kWorldH / 2.0 - kLogicalH / 2.0;
    double zoom = 1.0;

    void clamp() {
        zoom = std::clamp(zoom, 0.5, 8.0);
        double view_w = kLogicalW / zoom, view_h = kLogicalH / zoom;
        x = std::clamp(x, 0.0, std::max(0.0, kWorldW - view_w));
        y = std::clamp(y, 0.0, std::max(0.0, kWorldH - view_h));
    }
};

void render_frame(const EmpireMap& map, const Camera& cam, std::vector<uint8_t>& rgb_out) {
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
        std::fprintf(stderr, "usage: %s <EMPIRE2.0xx> [--screenshot out.png --frames N]\n", argv[0]);
        return 1;
    }
    std::string empire_path = argv[1];
    std::string screenshot_path;
    int screenshot_frames = 0;
    double test_pan_x = 0, test_pan_y = 0, test_zoom = 1.0;
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
    }

    EmpireMap map;
    try {
        map = formats::empire2::load(empire_path);
    } catch (const formats::FormatError& e) {
        std::fprintf(stderr, "failed to load %s: %s\n", empire_path.c_str(), e.what());
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

    try {
        platform::Window window("Gaius Viewer - " + empire_path, kLogicalW, kLogicalH, 960, 600);

        Camera cam;
        cam.x += test_pan_x;
        cam.y += test_pan_y;
        cam.zoom *= test_zoom;
        cam.clamp();
        std::vector<uint8_t> frame;
        bool running = true;
        int frame_count = 0;

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

            render_frame(map, cam, frame);
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
