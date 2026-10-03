// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius -- apps/viewer/original_intro_run.hpp
//
// Plays the original's opening (original_intro.hpp) in the viewer's window.

#pragma once

#include <SDL.h>

#include <functional>
#include <vector>

#include "apps/viewer/original_intro.hpp"
#include "platform/window.hpp"

namespace gaius::viewer {

// Plays the opening. Returns false if the window was closed during it.
inline bool run_original_intro(platform::Window& window, const OriginalIntroArt& art) {
    std::vector<uint8_t> rgb;
    formats::IndexedImage credits;
    bool skip_all = false;
    // One stage: `draw` fills `rgb` for the milliseconds elapsed; the stage ends at `total_ms` or on any input.
    const auto stage = [&](int total_ms, const std::function<void(int)>& draw, bool& window_closed) {
        const Uint32 start = SDL_GetTicks();
        for (;;) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                    case SDL_QUIT: window_closed = true; return;
                    case SDL_KEYDOWN:
                        if (event.key.keysym.sym == SDLK_ESCAPE) skip_all = true;
                        return;
                    case SDL_MOUSEBUTTONDOWN:
                    case SDL_FINGERDOWN:
                    case SDL_CONTROLLERBUTTONDOWN: return;
                    default: break;
                }
            }
            const int elapsed = static_cast<int>(SDL_GetTicks() - start);
            if (elapsed >= total_ms) return;
            draw(elapsed);
            window.present_rgb24(rgb);
            SDL_Delay(8);
        }
    };
    bool closed = false;
    original_intro_detail::to_rgb(art.logo, art.logo_palette, rgb);
    stage(kLogoMs, [&](int) {}, closed);
    if (closed) return false;
    if (skip_all) return true;
    // the picture stays on the window between stages; present once so it shows from the first frame
    original_intro_detail::to_rgb(art.presents, art.logo_palette, rgb);
    window.present_rgb24(rgb);
    stage(kPresentsMs, [&](int) {}, closed);
    if (closed) return false;
    if (skip_all) return true;
    original_intro_detail::to_rgb(art.title, art.title_palette, rgb);
    window.present_rgb24(rgb);
    stage(kTitleMs, [&](int) {}, closed);
    if (closed) return false;
    if (skip_all) return true;
    stage(static_cast<int>(kCreditsFrames * kCreditsMsPerPixel),
          [&](int elapsed) {
              draw_credits(credits, art.romfont, std::min(kCreditsFrames - 1, static_cast<int>(elapsed / kCreditsMsPerPixel)));
              original_intro_detail::to_rgb(credits, art.shade, rgb);
          },
          closed);
    return !closed;
}

}  // namespace gaius::viewer
