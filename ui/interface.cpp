// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/interface.hpp"

#include <array>
#include <cctype>
#include <filesystem>

#include "formats/pal256/pal256.hpp"
#include "formats/pl8/pl8.hpp"
#include "ui/game_font.hpp"

namespace gaius::ui {

namespace {

std::string asset(const std::string& dir, std::string name) {
    namespace fs = std::filesystem;
    fs::path p = fs::path(dir) / name;
    if (fs::exists(p)) return p.string();
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    p = fs::path(dir) / name;
    if (fs::exists(p)) return p.string();
    throw formats::FormatError("interface: " + name + " not found in " + dir);
}

const formats::PL8Frame* frame_at(const formats::PL8Sheet& sheet, int index) {
    if (index < 0 || index >= static_cast<int>(sheet.frames.size())) return nullptr;
    const formats::PL8Frame& f = sheet.frames[static_cast<size_t>(index)];
    return f.pixels.empty() ? nullptr : &f;
}

void put(formats::IndexedImage& img, int x, int y, uint8_t c) {
    if (x < 0 || y < 0 || x >= img.width || y >= img.height) return;
    img.pixels[static_cast<size_t>(y) * img.width + x] = c;
}

// 3496:0718: the stone pattern the panels scatter.
constexpr std::array<uint8_t, 50> kPattern = {0, 2, 5, 4, 3, 1, 5, 4, 3, 4, 1, 3, 2, 5, 2, 3, 4,
                                              6, 6, 2, 3, 1, 5, 1, 3, 1, 0, 4, 3, 1, 2, 2, 0, 3,
                                              6, 5, 4, 6, 4, 2, 4, 3, 4, 5, 1, 2, 4, 0, 2, 0};

int stone(size_t& k) {
    k = k + 1 > 49 ? 0 : k + 1;
    return kPattern[k] != 0 ? kPattern[k] + 0x13 : 4;
}

}  // namespace

InterfaceArt load_interface_art(const std::string& dir) {
    InterfaceArt art;
    art.blocks = formats::pl8::load(asset(dir, "P_BLOCKS.PL8"));
    art.pointers = formats::pl8::load(asset(dir, "POINTERS.PL8"));
    art.font = formats::pl8::load(asset(dir, "FONT1.PL8"));
    art.mini = formats::pl8::load_pl1(asset(dir, "MINIFONT.PL1"));
    art.palette = formats::pal256::load(asset(dir, "SHADE.256"));
    return art;
}

formats::IndexedImage blank_canvas() {
    formats::IndexedImage img;
    img.width = 320;
    img.height = 200;
    img.pixels.assign(64000, 0);
    return img;
}

void draw_block(formats::IndexedImage& img, const formats::PL8Sheet& sheet, int index, int x, int y) {
    const formats::PL8Frame* f = frame_at(sheet, index);
    if (!f) return;
    for (int r = 0; r < f->height; ++r)
        for (int c = 0; c < f->width; ++c) put(img, x + c, y + r, f->pixels[static_cast<size_t>(r) * f->width + c]);
}

void draw_sprite(formats::IndexedImage& img, const formats::PL8Sheet& sheet, int index, int x, int y) {
    const formats::PL8Frame* f = frame_at(sheet, index);
    if (!f) return;
    for (int r = 0; r < f->height; ++r)
        for (int c = 0; c < f->width; ++c) {
            const uint8_t p = f->pixels[static_cast<size_t>(r) * f->width + c];
            if (p != 0) put(img, x + c, y + r, p);
        }
}

void draw_sprite_rows(formats::IndexedImage& img, const formats::PL8Sheet& sheet, int index, int x, int bottom,
                      int rows) {
    const formats::PL8Frame* f = frame_at(sheet, index);
    if (!f || rows <= 0) return;
    rows = std::min(rows, f->height);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < f->width; ++c) {
            const uint8_t p = f->pixels[static_cast<size_t>(r) * f->width + c];
            if (p != 0) put(img, x + c, bottom - rows + 1 + r, p);
        }
}

void fill_rect(formats::IndexedImage& img, int x, int y, int w, int h, uint8_t colour) {
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) put(img, xx, yy, colour);
}

void draw_panel(formats::IndexedImage& img, const InterfaceArt& art, int x, int y, int cols, int rows) {
    size_t k = 0;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            int frame = (r == 0 ? 0 : r == rows - 1 ? 6 : 3) + (c == 0 ? 0 : c == cols - 1 ? 2 : 1);
            if (frame == 4) frame = stone(k);
            draw_block(img, art.blocks, frame, x + c * 16, y + r * 16);
        }
}

void draw_inset(formats::IndexedImage& img, const InterfaceArt& art, int x, int y, int cols, int rows) {
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            draw_block(img, art.blocks, (r == 0 ? 9 : r == rows - 1 ? 15 : 12) + (c == 0 ? 0 : c == cols - 1 ? 2 : 1),
                       x + c * 16, y + r * 16);
}

void draw_stone(formats::IndexedImage& img, const InterfaceArt& art, int x, int y, int cols, int rows) {
    size_t k = 0;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) draw_block(img, art.blocks, stone(k), x + c * 16, y + r * 16);
}

void draw_text(formats::IndexedImage& img, const InterfaceArt& art, Font font, int x, int y, const std::string& text) {
    const bool mini = font == Font::Mini;
    const formats::PL8Sheet& sheet = mini ? art.mini : art.font;
    for (char ch : text) {
        const int frame = game_glyph_frame(ch);
        if (const formats::PL8Frame* g = frame >= 0 ? frame_at(sheet, frame) : nullptr) {
            for (int r = 0; r < g->height; ++r)
                for (int c = 0; c < g->width; ++c) {
                    const uint8_t p = g->pixels[static_cast<size_t>(r) * g->width + c];
                    if (p != 0) put(img, x + c, y + r, mini ? 0 : p);
                }
        }
        x += mini ? 6 : 8;
    }
}

std::string number_text(long value, int digits, int mode) {
    std::string out(static_cast<size_t>(std::max(digits, 0)), ' ');
    for (int k = digits - 1; k >= 0; --k) {
        const long d = value % 10;
        char ch = static_cast<char>('0' + d);
        if (mode == 0) {
            ch = static_cast<char>(std::clamp<long>('0' + d, '0', '9'));
        } else if (value <= 0 && k != digits - 1) {
            ch = ' ';
        }
        out[static_cast<size_t>(k)] = ch;
        value /= 10;
    }
    return out;
}

void draw_number(formats::IndexedImage& img, const InterfaceArt& art, Font font, int x, int y, long value,
                 int digits, int mode) {
    // 0x11AAF: characters from x 0x139 on aren't drawn.
    const std::string text = number_text(value, digits, mode);
    const int advance = font == Font::Mini ? 6 : 8;
    for (size_t i = 0; i < text.size(); ++i) {
        const int cx = x + static_cast<int>(i) * advance;
        if (cx < 0x139) draw_text(img, art, font, cx, y, std::string(1, text[i]));
    }
}

void canvas_to_rgb(const formats::IndexedImage& img, const formats::Palette& palette, std::vector<uint8_t>& rgb) {
    rgb.resize(img.pixels.size() * 3);
    for (size_t i = 0; i < img.pixels.size(); ++i) {
        const formats::RGB c = palette.colors[img.pixels[i]];
        rgb[i * 3] = c.r;
        rgb[i * 3 + 1] = c.g;
        rgb[i * 3 + 2] = c.b;
    }
}

}  // namespace gaius::ui
