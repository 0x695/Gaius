// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — apps/viewer/intro.hpp
//
// The opening: the title on a dark ground for about two and a half seconds,
// fading in and out, before the start screen. A key, a click, a tap or a
// gamepad button skips it. Drawn with the game's own font when its files are
// there (the title is its FONT1 letters at six times their size), and with
// Gaius's placeholder font when they are not.

#pragma once

#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "formats/common/types.hpp"
#include "platform/window.hpp"
#include "ui/font.hpp"
#include "ui/game_font.hpp"

namespace gaius::viewer {

// Returns false if the window was closed during the intro.
// With `dump`, the full-brightness frame is stored there and nothing is shown (the headless --intro-screenshot).
inline bool run_intro(platform::Window& window, int logical_w, int logical_h, const ui::GameFont* font,
                      int total_ms = 2600, std::vector<uint8_t>* dump = nullptr) {
    const formats::RGB ground{22, 18, 14};
    const formats::RGB gold{214, 168, 72};
    const formats::RGB dim_gold{120, 92, 40};
    const char* title = "GAIUS";
    const char* tagline = "a Caesar engine";

    std::vector<uint8_t> base(static_cast<size_t>(logical_w) * logical_h * 3);
    for (size_t i = 0; i < base.size(); i += 3) {
        base[i] = ground.r;
        base[i + 1] = ground.g;
        base[i + 2] = ground.b;
    }
    const auto fill = [&](int x, int y, int w, int h, formats::RGB c) {
        for (int yy = std::max(0, y); yy < std::min(logical_h, y + h); ++yy)
            for (int xx = std::max(0, x); xx < std::min(logical_w, x + w); ++xx) {
                const size_t i = (static_cast<size_t>(yy) * logical_w + xx) * 3;
                base[i] = c.r;
                base[i + 1] = c.g;
                base[i + 2] = c.b;
            }
    };

    // The title, centred a little above the middle, between two rules. The game's letters are drawn as silhouettes in
    // gold with a shadow: FONT1's own colours are made for a light ground.
    constexpr int kScale = 6;
    const int title_w = font ? ui::game_text_width(title, kScale) : ui::text_width(title, 8);
    const int title_h = font ? ui::kGameGlyphPx * kScale : ui::kGlyphH * 8;
    const int title_x = (logical_w - title_w) / 2;
    const int title_y = logical_h / 2 - title_h / 2 - 10;
    if (font) {
        // FONT1's letters are dark with an orange rim: the dark strokes become the bright gold, the rim a deep one.
        const formats::RGB rim{110, 78, 30};
        int x = title_x;
        for (const char* c = title; *c; ++c, x += ui::kGameGlyphPx * kScale) {
            const int frame = ui::game_glyph_frame(*c);
            if (frame < 0 || frame >= static_cast<int>(font->sheet.frames.size())) continue;
            const formats::PL8Frame& glyph = font->sheet.frames[static_cast<size_t>(frame)];
            for (int iy = 0; iy < glyph.height; ++iy) {
                for (int ix = 0; ix < glyph.width; ++ix) {
                    const uint8_t index = glyph.pixels[static_cast<size_t>(iy) * glyph.width + ix];
                    if (index == 0) continue;
                    const formats::RGB& src = font->palette.colors[index];
                    const int luma = (src.r * 3 + src.g * 6 + src.b) / 10;
                    // Light at the top of a letter, darker at the foot.
                    const int t = iy * 100 / std::max(1, glyph.height - 1);
                    const formats::RGB face{static_cast<uint8_t>(244 - t * 50 / 100), static_cast<uint8_t>(206 - t * 54 / 100),
                                            static_cast<uint8_t>(110 - t * 30 / 100)};
                    fill(x + ix * kScale, title_y + iy * kScale, kScale, kScale, luma < 70 ? face : rim);
                }
            }
        }
    } else {
        ui::draw_text(base, logical_w, logical_h, title_x, title_y, title, 8, gold);
    }
    const int rule_w = title_w + 24;
    fill((logical_w - rule_w) / 2, title_y - 10, rule_w, 2, dim_gold);
    fill((logical_w - rule_w) / 2, title_y + title_h + 8, rule_w, 2, dim_gold);
    fill(logical_w / 2 - 3, title_y - 13, 6, 6, gold);  // a small stud on the top rule
    // The tagline under the lower rule, in the plain placeholder font for legibility at this size.
    const int tag_w = ui::text_width(tagline, 1);
    ui::draw_text(base, logical_w, logical_h, (logical_w - tag_w) / 2, title_y + title_h + 22, tagline, 1, dim_gold);

    if (dump) {
        *dump = base;
        return true;
    }
    std::vector<uint8_t> frame(base.size());
    const Uint32 start = SDL_GetTicks();
    constexpr int kFadeInMs = 600, kFadeOutMs = 500;
    for (;;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT: return false;
                case SDL_KEYDOWN:
                case SDL_MOUSEBUTTONDOWN:
                case SDL_FINGERDOWN:
                case SDL_CONTROLLERBUTTONDOWN: return true;
                default: break;
            }
        }
        const int elapsed = static_cast<int>(SDL_GetTicks() - start);
        if (elapsed >= total_ms) return true;
        // 0..256, in and out.
        int alpha = 256;
        if (elapsed < kFadeInMs) alpha = elapsed * 256 / kFadeInMs;
        if (elapsed > total_ms - kFadeOutMs) alpha = std::min(alpha, (total_ms - elapsed) * 256 / kFadeOutMs);
        for (size_t i = 0; i < base.size(); ++i) frame[i] = static_cast<uint8_t>(base[i] * alpha >> 8);
        window.present_rgb24(frame);
        SDL_Delay(16);
    }
}

}  // namespace gaius::viewer
