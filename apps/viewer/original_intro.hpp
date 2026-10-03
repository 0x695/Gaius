// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius -- apps/viewer/original_intro.hpp
//
// The original's opening, which follows Gaius's own title: the Impressions logo, "presents", the Caesar title
// picture, the scrolling credits, and then the start screen (findings section 49). It is the sequencer at `2700:0DA7`
// (flat 0x27DA7): `334:593D` (0x8C7D) shows IMPRLOGO.VPX with IMPRLOGO.256, `334:59FA` (0x8D3A) IMPRSEN.VPX with the
// same palette, `334:5BB2` (0x8EF2) TITLE3.VPX with TITLE3.256, each followed by `100F:0079(2)`, a wait that any key or
// mouse button ends; then the credits at 0x28055 and `334:5AD6` (0x8E16), LOGO.VPX over SHADE.256, which has no wait
// of its own and so never shows for more than the start screen's set-up. The tune CZARTIT.XMI starts before all of it
// (`31E0:0430`, 0x0F6DC) and runs on into the start screen.
//
// The credits (0x28055) draw 13 lines of 20 characters in ROMFONT.PL8 (16 x 17 capitals, `DS:1044` maps letters to
// frames and everything else to the blank frame 26; a glyph advances the text by its own width, 16 px but the I's 8
// and the M's and W's 24, and the blank 16) over black in the SHADE.256 palette, line i at
// y = Y[i] - s for the scroll position s = 0 .. 889, one pixel a frame; a frame is a page flip and two vertical
// retraces (`2EF9:0301`, `2EF9:02DB` twice), so on a 70 Hz display a pixel every 28.6 ms and the whole scroll 25.4 s.
// The wait loops are bound by the CPU (a poll count), so the three pictures' times are Gaius's reading: about two
// seconds each, three for the title. A key, a click or a gamepad button ends the picture or the scroll on show;
// Escape ends the whole opening.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "formats/common/types.hpp"
#include "formats/pal256/pal256.hpp"
#include "formats/pl8/pl8.hpp"
#include "formats/vpx/vpx.hpp"

namespace gaius::viewer {

struct OriginalIntroArt {
    formats::IndexedImage logo, presents, title;  // IMPRLOGO.VPX, IMPRSEN.VPX, TITLE3.VPX
    formats::Palette logo_palette, title_palette, shade;  // IMPRLOGO.256 (both Impressions pictures), TITLE3.256, SHADE.256
    formats::PL8Sheet romfont;                    // ROMFONT.PL8
};

// The credits: the line, its x offset and its y at scroll 0 (DS:0x489E + 21 * i, the pushes at 0x28055).
struct CreditLine {
    const char* text;
    int x, y;
};
inline const std::array<CreditLine, 13>& credit_lines() {
    static const std::array<CreditLine, 13> lines = {{
        {"    Programming     ", 4, 220}, {"   Simon Bradbury   ", 0, 240},
        {"       Design       ", 0, 340}, {"    David Lester    ", 0, 360},
        {"  Additional Design ", 0, 460}, {"   Simon Bradbury   ", 0, 480},
        {"     Production     ", 0, 580}, {"   Chris Bamford    ", 8, 600},
        {"      Graphics      ", 0, 700}, {"     Jon Baker      ", 4, 720},
        {"     Erik Casey     ", 0, 740}, {"  Music and Sound   ", 8, 820},
        {"     Chris Denman   ", 0, 840},
    }};
    return lines;
}
inline constexpr int kCreditsFrames = 890;           // the loop runs while the scroll < 0x37A
inline constexpr double kCreditsMsPerPixel = 2000.0 / 70.0;  // two vertical retraces at 70 Hz
inline constexpr int kLogoMs = 2000, kPresentsMs = 2000, kTitleMs = 3200;

// ROMFONT's glyph for a character (`DS:1044`: letters, either case, are frames 0-25; the rest frame 26, blank).
inline int romfont_frame(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';
    return 26;
}

// Draws the credits at scroll position `scroll` into an indexed 320 x 200 image (black ground, index 0).
inline void draw_credits(formats::IndexedImage& image, const formats::PL8Sheet& font, int scroll) {
    image.width = 320;
    image.height = 200;
    image.pixels.assign(320 * 200, 0);
    for (const CreditLine& line : credit_lines()) {
        const int y = line.y - scroll;
        if (y <= -17 || y >= 200) continue;
        int x = line.x;
        for (const char* c = line.text; *c; ++c) {
            const int frame = romfont_frame(*c);
            if (frame >= static_cast<int>(font.frames.size())) continue;
            const formats::PL8Frame& g = font.frames[static_cast<size_t>(frame)];
            if (frame < 26) {
                for (int iy = 0; iy < g.height; ++iy)
                    for (int ix = 0; ix < g.width; ++ix) {
                        const uint8_t index = g.pixels[static_cast<size_t>(iy) * g.width + ix];
                        const int px = x + ix, py = y + iy;
                        if (index == 0 || px < 0 || px >= 320 || py < 0 || py >= 200) continue;
                        image.pixels[static_cast<size_t>(py) * 320 + px] = index;
                    }
            }
            x += g.width;  // 0x118B1: the advance is the glyph's width
        }
    }
}

// Loads the pictures from the game's folder; false when any is missing (the opening is then left out).
inline bool load_original_intro(const std::string& dir, OriginalIntroArt& art) {
    namespace fs = std::filesystem;
    const auto path = [&](const char* name) {
        const fs::path p = fs::path(dir) / name;
        if (fs::exists(p)) return p.string();
        std::string lower = name;
        for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return (fs::path(dir) / lower).string();
    };
    try {
        art.logo = formats::vpx::decode(path("IMPRLOGO.VPX")).image;
        art.presents = formats::vpx::decode(path("IMPRSEN.VPX")).image;
        art.title = formats::vpx::decode(path("TITLE3.VPX")).image;
        art.logo_palette = formats::pal256::load(path("IMPRLOGO.256"));
        art.title_palette = formats::pal256::load(path("TITLE3.256"));
        art.shade = formats::pal256::load(path("SHADE.256"));
        art.romfont = formats::pl8::load(path("ROMFONT.PL8"));
    } catch (const formats::FormatError&) {
        return false;
    }
    return art.logo.width == 320 && art.logo.height == 200 && art.presents.width == 320 && art.title.width == 320 &&
           art.romfont.frames.size() >= 27;
}

namespace original_intro_detail {

inline void to_rgb(const formats::IndexedImage& image, const formats::Palette& palette, std::vector<uint8_t>& rgb) {
    rgb.resize(static_cast<size_t>(image.width) * image.height * 3);
    for (size_t i = 0; i < image.pixels.size(); ++i) {
        const formats::RGB c = palette.colors[image.pixels[i]];
        rgb[i * 3] = c.r;
        rgb[i * 3 + 1] = c.g;
        rgb[i * 3 + 2] = c.b;
    }
}

}  // namespace original_intro_detail

// The picture on show at `stage` (0 logo, 1 presents, 2 title, 3 and up the credits at scroll 50 * (stage - 3)), as
// RGB, for the headless --original-intro-screenshot.
inline std::vector<uint8_t> original_intro_frame(const OriginalIntroArt& art, int stage) {
    std::vector<uint8_t> rgb;
    if (stage <= 0) {
        original_intro_detail::to_rgb(art.logo, art.logo_palette, rgb);
    } else if (stage == 1) {
        original_intro_detail::to_rgb(art.presents, art.logo_palette, rgb);
    } else if (stage == 2) {
        original_intro_detail::to_rgb(art.title, art.title_palette, rgb);
    } else {
        formats::IndexedImage credits;
        draw_credits(credits, art.romfont, 50 * (stage - 3));
        original_intro_detail::to_rgb(credits, art.shade, rgb);
    }
    return rgb;
}

}  // namespace gaius::viewer
