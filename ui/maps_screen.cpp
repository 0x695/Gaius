// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/maps_screen.hpp"

#include <array>
#include <cctype>
#include <filesystem>

#include "formats/pal256/pal256.hpp"
#include "formats/pl8/pl8.hpp"

namespace gaius::ui {

namespace {

std::string asset(const std::string& dir, std::string name) {
    namespace fs = std::filesystem;
    fs::path p = fs::path(dir) / name;
    if (fs::exists(p)) return p.string();
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    p = fs::path(dir) / name;
    if (fs::exists(p)) return p.string();
    throw formats::FormatError("maps screen: " + name + " not found in " + dir);
}

// 303E:0BF0 draws a block whole, colour 0 included (the sprite routine
// 303E:13D6 is the one that skips it).
void blit(formats::IndexedImage& img, const formats::PL8Sheet& sheet, int index, int x, int y) {
    if (index < 0 || index >= static_cast<int>(sheet.frames.size())) return;
    const formats::PL8Frame& f = sheet.frames[static_cast<size_t>(index)];
    for (int r = 0; r < f.height; ++r) {
        const int yy = y + r;
        if (yy < 0 || yy >= img.height) continue;
        for (int c = 0; c < f.width; ++c) {
            const int xx = x + c;
            if (xx < 0 || xx >= img.width) continue;
            const uint8_t p = f.pixels[static_cast<size_t>(r) * f.width + c];
            img.pixels[static_cast<size_t>(yy) * img.width + xx] = p;
        }
    }
}

void fill(formats::IndexedImage& img, int x, int y, int w, int h, uint8_t colour) {
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            if (xx >= 0 && yy >= 0 && xx < img.width && yy < img.height)
                img.pixels[static_cast<size_t>(yy) * img.width + xx] = colour;
}

// 3496:0718: the stone pattern the panels scatter.
constexpr std::array<uint8_t, 50> kPattern = {0, 2, 5, 4, 3, 1, 5, 4, 3, 4, 1, 3, 2, 5, 2, 3, 4,
                                              6, 6, 2, 3, 1, 5, 1, 3, 1, 0, 4, 3, 1, 2, 2, 0, 3,
                                              6, 5, 4, 6, 4, 2, 4, 3, 4, 5, 1, 2, 4, 0, 2, 0};

int stone(size_t& k) {
    k = k + 1 > 49 ? 0 : k + 1;
    return kPattern[k] != 0 ? kPattern[k] + 0x13 : 4;
}

// 1F6F:1EC1: a framed panel (P_BLOCKS.PL8 frames 0-8).
void panel(formats::IndexedImage& img, const formats::PL8Sheet& blocks, int x, int y, int cols, int rows) {
    size_t k = 0;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int frame = (r == 0 ? 0 : r == rows - 1 ? 6 : 3) + (c == 0 ? 0 : c == cols - 1 ? 2 : 1);
            if (frame == 4) frame = stone(k);
            blit(img, blocks, frame, x + c * 16, y + r * 16);
        }
    }
}

// 1F6F:2008: an inset (frames 9-17).
void inset(formats::IndexedImage& img, const formats::PL8Sheet& blocks, int x, int y, int cols, int rows) {
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            blit(img, blocks, (r == 0 ? 9 : r == rows - 1 ? 15 : 12) + (c == 0 ? 0 : c == cols - 1 ? 2 : 1),
                 x + c * 16, y + r * 16);
}

// 1F6F:2115: plain stone, no frame.
void stone_area(formats::IndexedImage& img, const formats::PL8Sheet& blocks, int x, int y, int cols, int rows) {
    size_t k = 0;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) blit(img, blocks, stone(k), x + c * 16, y + r * 16);
}

bool is_city(uint8_t tile) { return (tile > 0x91 && tile <= 0xA1) || tile >= 0xC8; }

}  // namespace

MapsArt load_maps_art(const std::string& dir) {
    MapsArt art;
    art.blocks = formats::pl8::load(asset(dir, "P_BLOCKS.PL8"));
    art.palette = formats::pal256::load(asset(dir, "SHADE.256"));
    art.font = GameFont{formats::pl8::load(asset(dir, "FONT1.PL8")), art.palette};
    art.mini = load_mini_font(dir, art.palette.colors[0]);
    return art;
}

uint8_t map_colour(const model::CityMap& city, MapMode mode, bool show_city, int x, int y) {
    const size_t sx = static_cast<size_t>(x), sy = static_cast<size_t>(y);
    const uint8_t tile = city.tile[sy][sx];
    // 0x1FADA: the ground -- black for the city when it shows, blue for
    // water, 0xC for the low tiles, 6 for the rest.
    const auto ground = [&]() -> uint8_t {
        if (show_city && is_city(tile)) return 0;
        if (tile == 0 || (tile >= 0xA4 && tile <= 0xA6) || (tile >= 0x4A && tile <= 0x91)) return 0xF;
        if (tile <= 0x26) return 0xC;
        return 6;
    };
    const auto scale = [](int v) -> uint8_t { return v < 8 ? 0x16 : v < 16 ? 0x17 : v < 24 ? 0x18 : 0x19; };
    switch (mode) {
        case MapMode::Water:
            if (city.service_flags[sy][sx] & 0x01) return show_city && is_city(tile) ? 0xE : 0xF;
            return ground();
        case MapMode::Administration:
            if (city.service_flags[sy][sx] & 0x20) return show_city && is_city(tile) ? 8 : 9;
            return ground();
        case MapMode::Roads:
            if (tile == 0x5E || tile == 0x82 || tile == 0x86 || (tile >= 0x36 && tile <= 0x43) ||
                (tile >= 0x94 && tile <= 0x95))
                return 0xA;
            return ground();
        case MapMode::LandValue: {
            // A2C4, signed.
            const int v = static_cast<int8_t>(city.coverage[sy][sx]);
            if (v <= -4) return 0x1F;
            if (v == -3) return 0x1D;
            if (v == -2) return 0x1B;
            if (v == -1) return 0x1A;
            if (v == 0) return ground();
            return v < 4 ? 0x16 : v < 8 ? 0x17 : v < 16 ? 0x18 : 0x19;
        }
        case MapMode::Trouble: {
            // 54A4 on housing.
            const int v = city.land_value[sy][sx];
            if (tile <= 0xC8 || v <= 0) return ground();
            return scale(v);
        }
        case MapMode::None: break;
    }
    return ground();
}

bool maps_button_at(int x, int y, bool& toggle_city, MapMode& mode) {
    if (x / 16 != 18 || x < 0 || y < 0) return false;
    toggle_city = false;
    switch (y / 16) {
        case 2: toggle_city = true; return true;
        case 3: mode = MapMode::Water; return true;
        case 4: mode = MapMode::Administration; return true;
        case 5: mode = MapMode::Roads; return true;
        case 6: mode = MapMode::LandValue; return true;
        case 7: mode = MapMode::Trouble; return true;
        default: return false;
    }
}

bool maps_cell_at(int x, int y, int& cell_x, int& cell_y) {
    if (x < 0x16 || x >= 0x7A || y < 0x26 || y >= 0x8A) return false;
    cell_x = x - kMapX;
    cell_y = y - kMapY;
    return true;
}

void compose_maps_screen(const model::CityMap& city, MapMode mode, bool show_city, const MapsArt& art,
                         std::vector<uint8_t>& rgb) {
    formats::IndexedImage img;
    img.width = 320;
    img.height = 200;
    img.pixels.assign(64000, 0);
    // 0x0B717: the panel, the map's inset, the labels.
    panel(img, art.blocks, 0, 0x10, 20, 11);
    inset(img, art.blocks, 0x10, 0x20, 7, 7);
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 100; ++x)
            img.pixels[static_cast<size_t>(kMapY + y) * 320 + kMapX + x] = map_colour(city, mode, show_city, x, y);
    // The buttons (0x0D521, DS:0x0704): the pressed frame on the city toggle
    // while it's on and on the chosen mode.
    static constexpr MapMode kRows[] = {MapMode::None, MapMode::Water, MapMode::Administration,
                                        MapMode::Roads, MapMode::LandValue, MapMode::Trouble};
    for (int i = 0; i < 6; ++i) {
        const bool pressed = i == 0 ? show_city : kRows[i] == mode;
        blit(img, art.blocks, pressed ? 30 : 29, 18 * 16, (2 + i) * 16);
    }
    // The legend (0x0B8BC, 0x0B964, 0x0BA0C, 0x0BAB4).
    stone_area(img, art.blocks, 0x90, 0x88, 10, 3);
    struct Label { int x, y; const char* text; };
    std::vector<Label> legend;
    const auto box = [&](int x, int y, uint8_t colour) { fill(img, x, y, 12, 12, colour); };
    switch (mode) {
        case MapMode::Water: box(0x90, 0x88, 0xF); legend.push_back({0x90, 0x98, "water"}); break;
        case MapMode::Administration: box(0x90, 0x88, 0x11); legend.push_back({0x90, 0x98, "admin"}); break;
        case MapMode::Roads: box(0x90, 0x88, 0xA); legend.push_back({0x90, 0x98, "roads"}); break;
        default:
            box(0x90, 0x88, 0x1F);
            for (int i = 0; i < 6; ++i) box(0xD0 + i * 16, 0x88, static_cast<uint8_t>(0x14 + i));
            legend.push_back({0x90, 0x98, "negative"});
            legend.push_back({0xD0, 0x98, "low  -  high"});
            break;
    }
    if (show_city) {
        box(0x90, 0xA0, 0xB);
        legend.push_back({0x90, 0xB0, "city"});
    }

    rgb.resize(64000 * 3);
    for (size_t i = 0; i < 64000; ++i) {
        const formats::RGB c = art.palette.colors[img.pixels[i]];
        rgb[i * 3] = c.r;
        rgb[i * 3 + 1] = c.g;
        rgb[i * 3 + 2] = c.b;
    }
    static constexpr const char* kLabels[] = {"   urbanization", "water distribution", "  administration",
                                              "   road layout",    "    land value",      "  trouble areas"};
    for (int i = 0; i < 6; ++i) draw_game_text(rgb, 320, 200, 0x86, 0x24 + i * 16, kLabels[i], 1, art.font);
    for (const Label& l : legend) draw_game_text(rgb, 320, 200, l.x, l.y, l.text, 1, art.mini);
}

}  // namespace gaius::ui
