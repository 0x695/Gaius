// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/font.hpp"

#include <cstring>

namespace gaius::ui {

namespace {

// 5x7 glyphs, one uint8 per row, low 5 bits used, MSB-of-those = leftmost
// pixel. Written as binary literals so the shapes are readable in source
// -- if you need to fix a glyph, you can see it here rather than decoding
// hex by hand.
struct Glyph {
    uint8_t rows[kGlyphH];
};

// Order matters: index 0 is space, 1..26 are A..Z, 27..36 are 0..9, then
// the punctuation block. glyph_index() below is the only thing that
// depends on this ordering.
constexpr Glyph kFont[] = {
    {{0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000}},  // space
    {{0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}},  // A
    {{0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110}},  // B
    {{0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110}},  // C
    {{0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110}},  // D
    {{0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111}},  // E
    {{0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000}},  // F
    {{0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111}},  // G
    {{0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}},  // H
    {{0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}},  // I
    {{0b00111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100}},  // J
    {{0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001}},  // K
    {{0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111}},  // L
    {{0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001}},  // M
    {{0b10001, 0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001}},  // N
    {{0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}},  // O
    {{0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}},  // P
    {{0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101}},  // Q
    {{0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001}},  // R
    {{0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}},  // S
    {{0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}},  // T
    {{0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}},  // U
    {{0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100}},  // V
    {{0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001}},  // W
    {{0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001}},  // X
    {{0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100}},  // Y
    {{0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111}},  // Z
    {{0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}},  // 0
    {{0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}},  // 1
    {{0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}},  // 2
    {{0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110}},  // 3
    {{0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}},  // 4
    {{0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110}},  // 5
    {{0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}},  // 6
    {{0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}},  // 7
    {{0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}},  // 8
    {{0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}},  // 9
    {{0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000}},  // -
    {{0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b01100, 0b01100}},  // .
    {{0b00001, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b10000}},  // /
    {{0b00000, 0b01100, 0b01100, 0b00000, 0b01100, 0b01100, 0b00000}},  // :
};

// Returns -1 for characters with no glyph, which draw_text renders as a
// gap rather than a missing-glyph box -- a toolbar label is not the place
// to shout about an unmapped character.
int glyph_index(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c == ' ') return 0;
    if (c >= 'A' && c <= 'Z') return 1 + (c - 'A');
    if (c >= '0' && c <= '9') return 27 + (c - '0');
    switch (c) {
        case '-': return 37;
        case '.': return 38;
        case '/': return 39;
        case ':': return 40;
        default: return -1;
    }
}

}  // namespace

int text_width(const char* text, int scale) {
    if (!text || scale <= 0) return 0;
    int n = static_cast<int>(std::strlen(text));
    if (n == 0) return 0;
    // Last glyph contributes its width but not the trailing letterspace.
    return (n * kGlyphAdvance - (kGlyphAdvance - kGlyphW)) * scale;
}

void draw_text(std::vector<uint8_t>& rgb, int w, int h, int x, int y, const char* text, int scale,
               formats::RGB color) {
    if (!text || scale <= 0 || w <= 0 || h <= 0) return;
    if (rgb.size() < static_cast<size_t>(w) * h * 3) return;

    int pen = x;
    for (const char* p = text; *p; ++p, pen += kGlyphAdvance * scale) {
        int gi = glyph_index(*p);
        if (gi < 0) continue;
        const Glyph& g = kFont[gi];
        for (int row = 0; row < kGlyphH; ++row) {
            uint8_t bits = g.rows[row];
            for (int col = 0; col < kGlyphW; ++col) {
                if (!(bits & (1 << (kGlyphW - 1 - col)))) continue;
                // One font pixel becomes a scale x scale block, so text
                // grows in lockstep with icons and hit targets.
                for (int dy = 0; dy < scale; ++dy) {
                    int py = y + row * scale + dy;
                    if (py < 0 || py >= h) continue;
                    for (int dx = 0; dx < scale; ++dx) {
                        int px = pen + col * scale + dx;
                        if (px < 0 || px >= w) continue;
                        size_t idx = (static_cast<size_t>(py) * w + px) * 3;
                        rgb[idx + 0] = color.r;
                        rgb[idx + 1] = color.g;
                        rgb[idx + 2] = color.b;
                    }
                }
            }
        }
    }
}

}  // namespace gaius::ui
