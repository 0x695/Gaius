// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — apps/viewer/overlays.hpp
//
// What the original draws over the city view, from its DOSBox captures (2026-10-02):
//   - the title plaque at the top right, with a control-bar button's name while the pointer is on it;
//   - the "Select Forum Type" and "Select Industry Type" menus, a stone panel over the map;
//   - the minimap in the top left corner: the terrain's land and water in a 25 x 25 square, and a red frame for the
//     part of the city on screen;
//   - the message box, a 16 x 3 stone panel at the top left corner with the message's two lines;
//   - the cost ghost at the pointer: a dashed frame the size of the command's footprint with its cost in it, and for a
//     building the building itself.
// Positions are in the original's 320 x 200 screen. The panels are the game's own P_BLOCKS stone (ui/interface.hpp).
// The menus' positions come from the captures (the routines at 0334:A5B9 and A66C were not located): the panel is
// 12 x 9 cells at (64, 16), the title at (68, 32), the eight records of 16 characters 12 rows apart from y = 48 (the
// Forum's at x = 96, the industries' at x = 80 with their goods' icons at x = 208, y - 2). The plaque is 9 x 2 cells at
// (176, 0) with the text at (196, 12). The minimap's colours are the captures'. The ghost's digits sit at the frame's
// top left, which is read off the captures to within a pixel or two.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "formats/common/types.hpp"
#include "model/city_state.hpp"
#include "render/city_render.hpp"
#include "systems/construction.hpp"
#include "ui/game_font.hpp"
#include "ui/interface.hpp"

namespace gaius::viewer {

// Copies the `x, y, cw, ch` rectangle of an indexed canvas into an RGB frame through a palette.
inline void blit_canvas_rect(std::vector<uint8_t>& rgb, int w, int h, const formats::IndexedImage& canvas,
                             const formats::Palette& pal, int x, int y, int cw, int ch) {
    for (int yy = std::max(0, y); yy < std::min({h, canvas.height, y + ch}); ++yy)
        for (int xx = std::max(0, x); xx < std::min({w, canvas.width, x + cw}); ++xx) {
            const formats::RGB c = pal.colors[canvas.pixels[static_cast<size_t>(yy) * canvas.width + xx]];
            const size_t i = (static_cast<size_t>(yy) * w + xx) * 3;
            rgb[i] = c.r;
            rgb[i + 1] = c.g;
            rgb[i + 2] = c.b;
        }
}

// The plaque at the top right: the name of the control-bar button under the pointer.
inline void draw_title_plaque(std::vector<uint8_t>& rgb, int w, int h, const ui::InterfaceArt& art, const std::string& text) {
    formats::IndexedImage canvas = ui::blank_canvas();
    ui::draw_panel(canvas, art, 176, 0, 9, 2);
    ui::draw_text(canvas, art, ui::Font::Font1, 196, 12, text);
    blit_canvas_rect(rgb, w, h, canvas, art.palette, 176, 0, 144, 32);
}

// --- The message box ----------------------------------------------------------------

// 0x279AC: `1F6F:1EC1` draws a 16 x 3 panel at (0, 0) -- 256 x 48 pixels, which is also the click area of 0x0F982 and
// 0x0F93A -- and `100F:142C` writes the text's two lines in the large font at x = 16, y = 14 and y = 28. A line is 28
// characters, and the second starts 30 characters into the record (the two between are blank).
inline constexpr int kMessageW = 256, kMessageH = 48, kMessageTextX = 16, kMessageLine1Y = 14, kMessageLine2Y = 28;
inline constexpr int kMessageChars = 28, kMessageLine2At = 30;

inline std::string message_line(const std::string& text, int line) {
    const size_t at = line == 0 ? 0 : static_cast<size_t>(kMessageLine2At);
    if (text.size() <= at) return std::string();
    return text.substr(at, kMessageChars);
}

inline void draw_message_box(std::vector<uint8_t>& rgb, int w, int h, const ui::InterfaceArt& art, const std::string& text) {
    formats::IndexedImage canvas = ui::blank_canvas();
    ui::draw_panel(canvas, art, 0, 0, kMessageW / 16, kMessageH / 16);
    ui::draw_text(canvas, art, ui::Font::Font1, kMessageTextX, kMessageLine1Y, message_line(text, 0));
    ui::draw_text(canvas, art, ui::Font::Font1, kMessageTextX, kMessageLine2Y, message_line(text, 1));
    blit_canvas_rect(rgb, w, h, canvas, art.palette, 0, 0, kMessageW, kMessageH);
}

// --- The Forum and Industry type menus -----------------------------------------

inline constexpr int kMenuX = 64, kMenuY = 16, kMenuCols = 12, kMenuRows = 9;
inline constexpr int kMenuItemY = 48, kMenuItemStep = 12;

inline const std::array<const char*, 8>& forum_menu_records() {
    // 16-character records, as the string at flat 0x7781F holds them: the names are centred by their spaces.
    static const std::array<const char*, 8> r = {"    Aventine    ", "    Caelian     ", "   Esquiline    ", "   Janiculan    ",
                                                 "     Regia      ", "    Pincian     ", "    Palatine    ", "    Romanum     "};
    return r;
}
inline const std::array<const char*, 8>& industry_menu_records() {
    // the string at flat 0x78F41 (the Industry report draws the same ones in the small font)
    static const std::array<const char*, 8> r = {"     Glass      ", "      Tin       ", "    Pottery     ", "    Copper      ",
                                                 "      Wine      ", "     Ivory      ", "     Wheat      ", "     Spices     "};
    return r;
}

// The item under a point (0-7), or -1. A row is the panel's width between its edges.
inline int type_menu_item(int lx, int ly) {
    if (lx < kMenuX + 8 || lx >= kMenuX + kMenuCols * 16 - 8) return -1;
    for (int i = 0; i < 8; ++i) {
        const int y = kMenuItemY + kMenuItemStep * i;
        if (ly >= y - 1 && ly < y + kMenuItemStep - 1) return i;
    }
    return -1;
}
inline bool type_menu_contains(int lx, int ly) {
    return lx >= kMenuX && lx < kMenuX + kMenuCols * 16 && ly >= kMenuY && ly < kMenuY + kMenuRows * 16;
}

// Draws the menu over a frame. `hovered` (0-7 or -1) is drawn with a thin frame: the original has no such mark, but
// without a pointer cursor in the frame there would be no telling which row a click will take.
inline void draw_type_menu(std::vector<uint8_t>& rgb, int w, int h, const ui::InterfaceArt& art, bool forum, int hovered) {
    formats::IndexedImage canvas = ui::blank_canvas();
    ui::draw_panel(canvas, art, kMenuX, kMenuY, kMenuCols, kMenuRows);
    ui::draw_text(canvas, art, ui::Font::Font1, 68, 32, forum ? "   Select Forum Type" : " Select Industry Type");
    const auto& records = forum ? forum_menu_records() : industry_menu_records();
    for (int i = 0; i < 8; ++i) {
        const int y = kMenuItemY + kMenuItemStep * i;
        ui::draw_text(canvas, art, ui::Font::Font1, forum ? 96 : 80, y, records[static_cast<size_t>(i)]);
        if (!forum) ui::draw_sprite(canvas, art.pointers, 0x38 + i, 208, y - 2);
    }
    blit_canvas_rect(rgb, w, h, canvas, art.palette, kMenuX, kMenuY, kMenuCols * 16, kMenuRows * 16);
    if (hovered >= 0 && hovered < 8) {
        const int y = kMenuItemY + kMenuItemStep * hovered - 2;
        const int x0 = kMenuX + 12, x1 = kMenuX + kMenuCols * 16 - 12;
        for (int x = x0; x < x1; ++x)
            for (int yy : {y, y + kMenuItemStep - 1}) {
                const size_t i = (static_cast<size_t>(yy) * w + x) * 3;
                rgb[i] = 236;
                rgb[i + 1] = 206;
                rgb[i + 2] = 116;
            }
    }
}

// --- The minimap -------------------------------------------------------------------

// The terrain's water: open water below 0x1D, the water pieces 0x4A-0x6F, and a reservoir (always on water).
inline bool minimap_water(uint8_t tile) { return (tile > 0 && tile < 0x1D) || (tile >= 0x4A && tile <= 0x6F) || tile == 0xA4; }

// 25 x 25 at the top left corner, 4 x 4 cells to a pixel, with a black shadow along its right and bottom; the red frame
// is the view: `col0`, `row0` the top left cell on screen, `cols` x `rows` of them (20 x 11 in the original).
inline void draw_minimap(std::vector<uint8_t>& rgb, int w, int h, const model::CityMap& city, int col0, int row0, int cols,
                         int rows) {
    constexpr int kSize = 25, kShadow = 2;
    const auto put = [&](int x, int y, formats::RGB c) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        const size_t i = (static_cast<size_t>(y) * w + x) * 3;
        rgb[i] = c.r;
        rgb[i + 1] = c.g;
        rgb[i + 2] = c.b;
    };
    const formats::RGB land{186, 113, 1}, water{0, 93, 130}, red{167, 19, 18}, black{0, 0, 0};
    for (int y = 0; y < kSize + kShadow; ++y)
        for (int x = 0; x < kSize + kShadow; ++x) {
            if (x >= kSize || y >= kSize) {
                put(x, y, black);
                continue;
            }
            int wet = 0;
            for (int dy = 0; dy < 4; ++dy)
                for (int dx = 0; dx < 4; ++dx) wet += minimap_water(city.tile[y * 4 + dy][x * 4 + dx]) ? 1 : 0;
            put(x, y, wet >= 4 ? water : land);
        }
    // The view: a frame of (cols / 4) x (rows / 4) pixels, at least 3 x 3.
    const int fx = std::clamp(col0 / 4, 0, kSize - 1), fy = std::clamp(row0 / 4, 0, kSize - 1);
    const int fw = std::max(3, (cols + 3) / 4), fh = std::max(3, (rows + 3) / 4);
    for (int x = fx; x < std::min(kSize, fx + fw); ++x) {
        put(x, fy, red);
        if (fy + fh - 1 < kSize) put(x, fy + fh - 1, red);
    }
    for (int y = fy; y < std::min(kSize, fy + fh); ++y) {
        put(fx, y, red);
        if (fx + fw - 1 < kSize) put(fx + fw - 1, y, red);
    }
}

// --- The cost ghost ------------------------------------------------------------------

// What the original draws at the pointer while a command is chosen (the pointer routine 1F6F:159F, findings section
// 49). Each toolbar handler sets DS:0x6D12, the command's preview tile, and DS:0x6D10, how far the picture rises above
// the cell; the Forum's handler takes its tile from the type menu (0xDF + the choice) and the Workshop's from the
// goods (0x18, or 0x28 past the fourth). A command with no preview tile (Clear Area, Road, Plaza, Reservoir, Well,
// Fountain, Wall, Tower) shows POINTERS frame 1, the dashed frame; any other shows that tile's one picture -- a frame
// of HOUSES.PL8 (tile - 0xC8) from 0xC8 up, of HOUSES2.PL8 below -- with its top left `rise` pixels above the cell.
// The cost is written at the cell's left, two rows down, in the large font.
struct GhostSeed {
    int tile = 0;  // 0: the dashed frame
    int rise = 0;
};

inline GhostSeed ghost_seed(systems::construction::CommandId command, int forum_grade, int workshop_goods) {
    using C = systems::construction::CommandId;
    switch (command) {
        case C::Housing: return {0xC8, 0};
        case C::BathHouses: return {0xE8, 12};
        case C::Barracks: return {0xEF, 4};
        case C::Prefecture: return {0xEE, 3};
        case C::Temple: return {0xD8, 2};
        case C::Hospital: return {0xED, 0};
        case C::School: return {0xEC, 0};
        case C::Oracle: return {0xEB, 16};
        case C::HeavyIndustry: return {4, 0};
        case C::Market: return {5, 0};
        case C::Theater: return {0xF0, 16};
        case C::Coliseum: return {0xF1, 16};
        case C::Hippodrome: return {0xF2, 6};
        case C::Forum: return {0xE0 + std::clamp(forum_grade, 0, 7), 0};
        case C::Workshop: return {workshop_goods > 3 ? 0x28 : 0x18, 2};
        default: return {0, 0};
    }
}

// Draws a sprite frame with its top left at screen (x, y), each pixel `zoom` times as large, index 0 transparent.
inline void draw_ghost_frame(std::vector<uint8_t>& rgb, int w, int h, const formats::PL8Frame& frame,
                             const formats::Palette& palette, int x, int y, double zoom) {
    if (frame.pixels.empty() || frame.width <= 0 || frame.height <= 0) return;
    const int dw = std::max(1, static_cast<int>(frame.width * zoom)), dh = std::max(1, static_cast<int>(frame.height * zoom));
    for (int dy = 0; dy < dh; ++dy) {
        const int py = y + dy;
        if (py < 0 || py >= h) continue;
        for (int dx = 0; dx < dw; ++dx) {
            const int px = x + dx;
            if (px < 0 || px >= w) continue;
            const int sx = std::min(frame.width - 1, static_cast<int>(dx / zoom));
            const int sy = std::min(frame.height - 1, static_cast<int>(dy / zoom));
            const uint8_t index = frame.pixels[static_cast<size_t>(sy) * frame.width + sx];
            if (index == 0) continue;
            const formats::RGB c = palette.colors[index];
            const size_t i = (static_cast<size_t>(py) * w + px) * 3;
            rgb[i] = c.r;
            rgb[i + 1] = c.g;
            rgb[i + 2] = c.b;
        }
    }
}

// A dashed frame (red and yellow dashes, as the captures show) around `w` x `h` cells with their top left at screen
// (sx, sy), `cell` pixels to a cell.
inline void draw_dashed_frame(std::vector<uint8_t>& rgb, int fw, int fh, int sx, int sy, int pw, int ph) {
    const auto put = [&](int x, int y, int k) {
        if (x < 0 || y < 0 || x >= fw || y >= fh) return;
        const size_t i = (static_cast<size_t>(y) * fw + x) * 3;
        const bool red = (k / 2) % 2 == 0;
        rgb[i] = red ? 167 : 232;
        rgb[i + 1] = red ? 19 : 188;
        rgb[i + 2] = red ? 18 : 64;
    };
    for (int k = 0; k < pw; ++k) {
        put(sx + k, sy, k);
        put(sx + k, sy + ph - 1, k);
    }
    for (int k = 0; k < ph; ++k) {
        put(sx, sy + k, k);
        put(sx + pw - 1, sy + k, k);
    }
}

}  // namespace gaius::viewer
