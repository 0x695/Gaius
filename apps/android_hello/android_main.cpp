// Gaius — apps/android_hello/android_main.cpp
//
// Phase 1's last open item per GAIUS_ROADMAP.md: "confirm a clean build +
// 'hello sprite' run on at least one non-desktop target." This is
// deliberately NOT a port of apps/viewer -- it exists purely to answer the
// question the roadmap actually flagged as a real risk area: does
// platform/window.cpp and platform/input.cpp (the SDL2 abstraction every
// later phase builds on) actually build and run against Android's SDL2
// backend at all, having never been exercised there before.
//
// It links the real, unmodified platform/window.cpp + platform/input.cpp +
// platform/paths.cpp -- not a reimplementation -- and draws a moving
// colored square ("hello sprite") through Window::present_rgb24, the exact
// same call apps/viewer uses. Loading a real EMPIRE2/SAV asset on Android
// needs AAssetManager-based extraction first (Android can't fopen() into
// an APK) -- left for whoever picks up the Android target next; not
// required to answer this phase's question.
//
// Verify with: adb shell am start -n org.gaius.hello/org.libsdl.app.SDLActivity
//              adb exec-out screencap -p > out.png

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include "platform/input.hpp"
#include "platform/paths.hpp"
#include "platform/window.hpp"

using namespace gaius;

namespace {
constexpr int kLogicalW = 320;  // same logical resolution as apps/viewer, deliberately
constexpr int kLogicalH = 200;
}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // Same platform::paths call apps/viewer makes -- proving SDL_GetPrefPath's
    // Android backend (app-private storage) resolves correctly here too,
    // not just on desktop.
    try {
        std::string prefs = platform::data_path("android-hello-scratch");
        SDL_Log("prefs directory resolved to: %s", prefs.c_str());
    } catch (const std::exception& e) {
        SDL_Log("note: platform::data_path unavailable: %s", e.what());
    }

    try {
        platform::Window window("Gaius Android Hello", kLogicalW, kLogicalH, kLogicalW, kLogicalH);

        std::vector<uint8_t> frame(static_cast<size_t>(kLogicalW) * kLogicalH * 3);
        bool running = true;
        Uint32 start_ticks = SDL_GetTicks();

        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                auto cmd = platform::translate_event(event);
                if (cmd && cmd->type == platform::CommandType::Quit) running = false;
            }

            float t = (SDL_GetTicks() - start_ticks) / 1000.0f;

            // Background: a drifting gradient, just so a screenshot is
            // obviously "live" rather than a static test card.
            for (int y = 0; y < kLogicalH; ++y) {
                for (int x = 0; x < kLogicalW; ++x) {
                    size_t i = (static_cast<size_t>(y) * kLogicalW + x) * 3;
                    frame[i + 0] = static_cast<uint8_t>(128 + 100 * std::sin(x * 0.04f + t));
                    frame[i + 1] = static_cast<uint8_t>(128 + 100 * std::sin(y * 0.06f + t * 1.3f));
                    frame[i + 2] = 60;
                }
            }

            // "Hello sprite": a solid square bouncing around the frame --
            // something concrete to look for in a screenshot, not just a
            // gradient.
            int sw = 36, sh = 36;
            int sx = (kLogicalW - sw) / 2 + static_cast<int>((kLogicalW / 2 - sw) * std::sin(t));
            int sy = (kLogicalH - sh) / 2 + static_cast<int>((kLogicalH / 2 - sh) * std::cos(t * 1.5f));
            for (int y = sy; y < sy + sh; ++y) {
                if (y < 0 || y >= kLogicalH) continue;
                for (int x = sx; x < sx + sw; ++x) {
                    if (x < 0 || x >= kLogicalW) continue;
                    size_t i = (static_cast<size_t>(y) * kLogicalW + x) * 3;
                    frame[i + 0] = 255;
                    frame[i + 1] = 220;
                    frame[i + 2] = 40;
                }
            }

            window.present_rgb24(frame);
        }
    } catch (const std::exception& e) {
        SDL_Log("fatal: %s", e.what());
        SDL_Quit();
        return 2;
    }

    SDL_Quit();
    return 0;
}
