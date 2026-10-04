// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/pad_glyphs.hpp"

#include <array>
#include <cstring>

namespace gaius::ui {

namespace {

// Palette indices of a glyph's bitmap; 0 is transparent.
enum : uint8_t { kNone = 0, kOutline, kDark, kWhite, kGreen, kRed, kBlue, kYellow, kStick, kArm };

constexpr uint8_t kColors[][3] = {{0, 0, 0},       {170, 170, 182}, {34, 34, 42},   {244, 244, 244}, {112, 204, 92},
                                  {230, 84, 72},   {84, 144, 236},  {238, 204, 72}, {84, 84, 98},    {92, 92, 106}};

struct Bitmap {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    Bitmap(int width, int height) : w(width), h(height), px(static_cast<size_t>(width) * height, kNone) {}
    void set(int x, int y, uint8_t c) {
        if (x >= 0 && y >= 0 && x < w && y < h) px[static_cast<size_t>(y) * w + x] = c;
    }
    uint8_t get(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h ? px[static_cast<size_t>(y) * w + x] : kNone; }
};

// 3 x 5 letters and digits for the discs and rectangles.
const char* const* letter_rows(char c) {
    static const char* const A[] = {".#.", "#.#", "###", "#.#", "#.#"};
    static const char* const B[] = {"##.", "#.#", "##.", "#.#", "##."};
    static const char* const X[] = {"#.#", "#.#", ".#.", "#.#", "#.#"};
    static const char* const Y[] = {"#.#", "#.#", ".#.", ".#.", ".#."};
    static const char* const L[] = {"#..", "#..", "#..", "#..", "###"};
    static const char* const R[] = {"##.", "#.#", "##.", "#.#", "#.#"};
    static const char* const D1[] = {".#.", "##.", ".#.", ".#.", "###"};
    static const char* const D2[] = {"##.", "..#", ".#.", "#..", "###"};
    static const char* const D4[] = {"#.#", "#.#", "###", "..#", "..#"};
    static const char* const D5[] = {"###", "#..", "##.", "..#", "##."};
    switch (c) {
        case 'A': return A;
        case 'B': return B;
        case 'X': return X;
        case 'Y': return Y;
        case 'L': return L;
        case 'R': return R;
        case '1': return D1;
        case '2': return D2;
        case '4': return D4;
        case '5': return D5;
        default: return nullptr;
    }
}

void letter(Bitmap& b, char c, int x0, int y0, uint8_t color) {
    const char* const* rows = letter_rows(c);
    if (!rows) return;
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 3; ++x)
            if (rows[y][x] == '#') b.set(x0 + x, y0 + y, color);
}

// A round button 9 pixels across.
Bitmap disc(uint8_t fill) {
    Bitmap b(9, 9);
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x) {
            const int d2 = (x - 4) * (x - 4) + (y - 4) * (y - 4);
            if (d2 <= 19) b.set(x, y, d2 > 12 ? kOutline : fill);
        }
    return b;
}

// A rectangle with its corners cut, for the bumpers, triggers and grips.
Bitmap pill(int w, int h) {
    Bitmap b(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const bool corner = (x == 0 || x == w - 1) && (y == 0 || y == h - 1);
            if (corner) continue;
            b.set(x, y, x == 0 || y == 0 || x == w - 1 || y == h - 1 ? kOutline : kDark);
        }
    return b;
}

Bitmap labelled(int w, int h, char first, char second) {
    Bitmap b = pill(w, h);
    const int y0 = (h - 5) / 2;
    letter(b, first, 2, y0, kWhite);
    letter(b, second, 6, y0, kWhite);
    return b;
}

Bitmap build(char code) {
    switch (code) {
        case 'a': { Bitmap b = disc(kDark); letter(b, 'A', 3, 2, kGreen); return b; }
        case 'b': { Bitmap b = disc(kDark); letter(b, 'B', 3, 2, kRed); return b; }
        case 'x': { Bitmap b = disc(kDark); letter(b, 'X', 3, 2, kBlue); return b; }
        case 'y': { Bitmap b = disc(kDark); letter(b, 'Y', 3, 2, kYellow); return b; }
        case 'l': return labelled(11, 7, 'L', '1');
        case 'r': return labelled(11, 7, 'R', '1');
        case 'L': return labelled(11, 9, 'L', '2');
        case 'R': return labelled(11, 9, 'R', '2');
        case 'e': return labelled(11, 7, 'L', '4');
        case 'f': return labelled(11, 7, 'L', '5');
        case 'h': return labelled(11, 7, 'R', '4');
        case 'i': return labelled(11, 7, 'R', '5');
        case 's': { Bitmap b = disc(kStick); letter(b, 'L', 3, 2, kWhite); return b; }
        case 't': { Bitmap b = disc(kStick); letter(b, 'R', 3, 2, kWhite); return b; }
        case 'u':
        case 'd':
        case '<':
        case '>': {
            Bitmap b(9, 9);
            for (int y = 0; y < 9; ++y)
                for (int x = 0; x < 9; ++x) {
                    const bool vertical = x >= 3 && x <= 5, horizontal = y >= 3 && y <= 5;
                    if (!vertical && !horizontal) continue;
                    const bool lit = (code == 'u' && vertical && y <= 3) || (code == 'd' && vertical && y >= 5) ||
                                     (code == '<' && horizontal && x <= 3) || (code == '>' && horizontal && x >= 5);
                    b.set(x, y, lit ? kWhite : kArm);
                }
            return b;
        }
        case 'v': {  // View: two overlapping squares
            Bitmap b = disc(kDark);
            for (int i = 2; i <= 5; ++i) {
                b.set(i, 2, kWhite);
                b.set(2, i, kWhite);
                b.set(5, i, kWhite);
                b.set(i, 5, kWhite);
            }
            for (int y = 3; y <= 6; ++y)
                for (int x = 3; x <= 6; ++x) b.set(x, y, x == 3 || x == 6 || y == 3 || y == 6 ? kWhite : kDark);
            return b;
        }
        case 'm': {  // Menu: three lines
            Bitmap b = disc(kDark);
            for (int x = 2; x <= 6; ++x)
                for (int y : {2, 4, 6}) b.set(x, y, kWhite);
            return b;
        }
        case 'g': {  // Steam: a ring round a dot
            Bitmap b = disc(kDark);
            for (int y = 0; y < 9; ++y)
                for (int x = 0; x < 9; ++x) {
                    const int d2 = (x - 4) * (x - 4) + (y - 4) * (y - 4);
                    if (d2 >= 5 && d2 <= 9) b.set(x, y, kWhite);
                }
            b.set(4, 4, kWhite);
            return b;
        }
        default: return Bitmap(0, 0);
    }
}

}  // namespace

bool is_pad_code(char code) { return build(code).w > 0; }

int pad_glyph_w(char code, int scale) { return build(code).w * (scale < 1 ? 1 : scale); }
int pad_glyph_h(char code, int scale) { return build(code).h * (scale < 1 ? 1 : scale); }

void draw_pad_glyph(std::vector<uint8_t>& rgb, int w, int h, int x, int y, char code, int scale) {
    const Bitmap b = build(code);
    if (b.w == 0 || scale < 1 || rgb.size() < static_cast<size_t>(w) * h * 3) return;
    for (int by = 0; by < b.h; ++by)
        for (int bx = 0; bx < b.w; ++bx) {
            const uint8_t c = b.get(bx, by);
            if (c == kNone) continue;
            for (int sy = 0; sy < scale; ++sy)
                for (int sx = 0; sx < scale; ++sx) {
                    const int px = x + bx * scale + sx, py = y + by * scale + sy;
                    if (px < 0 || py < 0 || px >= w || py >= h) continue;
                    uint8_t* p = &rgb[(static_cast<size_t>(py) * w + px) * 3];
                    p[0] = kColors[c][0];
                    p[1] = kColors[c][1];
                    p[2] = kColors[c][2];
                }
        }
}

}  // namespace gaius::ui
