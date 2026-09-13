// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/game_font.hpp"

#include <cctype>
#include <cstring>
#include <filesystem>

#include "formats/pl8/pl8.hpp"

namespace gaius::ui {

GameFont load_game_font(const std::string& asset_dir, const formats::Palette& palette) {
    namespace fs = std::filesystem;
    for (const char* name : {"FONT1.PL8", "font1.pl8"}) {
        fs::path p = fs::path(asset_dir) / name;
        if (fs::exists(p)) return GameFont{formats::pl8::load(p.string()), palette};
    }
    throw formats::FormatError("ui: FONT1.PL8 not found in " + asset_dir);
}

GameFont load_mini_font(const std::string& asset_dir, formats::RGB ink) {
    namespace fs = std::filesystem;
    for (const char* name : {"MINIFONT.PL1", "minifont.pl1"}) {
        fs::path p = fs::path(asset_dir) / name;
        if (fs::exists(p)) {
            GameFont font{formats::pl8::load_pl1(p.string()), {}};
            font.advance = 6;
            font.use_ink = true;
            font.ink = ink;
            return font;
        }
    }
    throw formats::FormatError("ui: MINIFONT.PL1 not found in " + asset_dir);
}

int game_text_width(const char* text, int scale) {
    return static_cast<int>(std::strlen(text)) * kGameGlyphPx * scale;
}

int game_text_width(const char* text, int scale, const GameFont& font) {
    return static_cast<int>(std::strlen(text)) * font.advance * scale;
}

void draw_game_text(std::vector<uint8_t>& rgb, int w, int h, int x, int y, const char* text, int scale,
                    const GameFont& font) {
    if (scale < 1 || w <= 0 || h <= 0 || rgb.size() < static_cast<size_t>(w) * h * 3) return;
    for (int n = 0; text[n] != '\0'; ++n, x += font.advance * scale) {
        const int frame = game_glyph_frame(text[n]);
        if (frame < 0 || frame >= static_cast<int>(font.sheet.frames.size())) continue;
        const formats::PL8Frame& g = font.sheet.frames[static_cast<size_t>(frame)];
        if (g.pixels.empty()) continue;
        for (int gy = 0; gy < g.height; ++gy) {
            for (int gx = 0; gx < g.width; ++gx) {
                const uint8_t idx = g.pixels[static_cast<size_t>(gy) * g.width + gx];
                if (idx == 0) continue;
                const formats::RGB& c = font.use_ink ? font.ink : font.palette.colors[idx];
                for (int sy = 0; sy < scale; ++sy) {
                    const int py = y + gy * scale + sy;
                    if (py < 0 || py >= h) continue;
                    for (int sx = 0; sx < scale; ++sx) {
                        const int px = x + gx * scale + sx;
                        if (px < 0 || px >= w) continue;
                        const size_t o = (static_cast<size_t>(py) * w + px) * 3;
                        rgb[o] = c.r;
                        rgb[o + 1] = c.g;
                        rgb[o + 2] = c.b;
                    }
                }
            }
        }
    }
}

}  // namespace gaius::ui
