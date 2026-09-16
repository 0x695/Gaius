// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/name_entry.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "formats/pal256/pal256.hpp"
#include "formats/pl8/pl8.hpp"

namespace gaius::ui {

namespace {

constexpr size_t kNameOffset = 12;  // DS:0x6B9E in final_state

std::string asset(const std::string& dir, std::string name) {
    namespace fs = std::filesystem;
    fs::path p = fs::path(dir) / name;
    if (fs::exists(p)) return p.string();
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    p = fs::path(dir) / name;
    if (fs::exists(p)) return p.string();
    throw formats::FormatError("name dialog: " + name + " not found in " + dir);
}

// 3496:0718.
constexpr std::array<uint8_t, 50> kPattern = {0, 2, 5, 4, 3, 1, 5, 4, 3, 4, 1, 3, 2, 5, 2, 3, 4,
                                              6, 6, 2, 3, 1, 5, 1, 3, 1, 0, 4, 3, 1, 2, 2, 0, 3,
                                              6, 5, 4, 6, 4, 2, 4, 3, 4, 5, 1, 2, 4, 0, 2, 0};

void put_frame(std::vector<uint8_t>& rgb, const NameArt& art, int index, int x, int y) {
    if (index < 0 || index >= static_cast<int>(art.blocks.frames.size())) return;
    const formats::PL8Frame& f = art.blocks.frames[static_cast<size_t>(index)];
    for (int r = 0; r < f.height; ++r) {
        for (int c = 0; c < f.width; ++c) {
            const int xx = x + c, yy = y + r;
            if (xx < 0 || yy < 0 || xx >= 320 || yy >= 200) continue;
            const formats::RGB col = art.palette.colors[f.pixels[static_cast<size_t>(r) * f.width + c]];
            const size_t i = (static_cast<size_t>(yy) * 320 + xx) * 3;
            rgb[i] = col.r;
            rgb[i + 1] = col.g;
            rgb[i + 2] = col.b;
        }
    }
}

}  // namespace

std::string governor_name(const model::CityState& state) {
    if (state.final_state.size() < kNameOffset + kNameLength) return kDefaultGovernorName;
    std::string name(state.final_state.begin() + kNameOffset, state.final_state.begin() + kNameOffset + kNameLength);
    if (std::all_of(name.begin(), name.end(), [](char c) { return c == 0; })) return kDefaultGovernorName;
    return name;
}

void set_governor_name(model::CityState& state, const std::string& name) {
    if (state.final_state.size() < kNameOffset + kNameLength) state.final_state.resize(kNameOffset + kNameLength, 0);
    for (size_t i = 0; i < kNameLength; ++i)
        state.final_state[kNameOffset + i] = static_cast<uint8_t>(i < name.size() ? name[i] : ' ');
}

NameEntry begin_name_entry(const std::string& name) {
    NameEntry e;
    for (size_t i = 0; i < kNameLength; ++i) e.name[i] = i < name.size() ? name[i] : ' ';
    return e;  // 0x0C459: the cursor starts at 0
}

std::string name_text(const NameEntry& e) { return std::string(e.name.begin(), e.name.end()); }

void name_letter_up(NameEntry& e, int i) {
    if (i < 0 || i >= kNameLength) return;
    char& c = e.name[static_cast<size_t>(i)];
    if (c == 'Z') {
        c = 'a';
    } else if (c < '@') {
        c = '@';
    } else {
        ++c;
    }
    if (c > 'z') c = 'z';
}

void name_letter_down(NameEntry& e, int i) {
    if (i < 0 || i >= kNameLength) return;
    char& c = e.name[static_cast<size_t>(i)];
    if (static_cast<int>(e.name[0]) + i == 'a') {
        c = 'Z';
    } else {
        --c;
    }
    if (c < '@') c = '@';
}

int name_key(NameEntry& e, NameKey key, char ch) {
    int& cur = e.cursor;
    const int length = kNameLength;  // both of 0x0C48C's limits are 12
    switch (key) {
        case NameKey::Escape: return 1;
        case NameKey::Enter: return 2;
        case NameKey::Backspace:
            if (cur > 0) {
                --cur;
                e.name[static_cast<size_t>(cur)] = ' ';
            }
            break;
        case NameKey::Left:
            if (--cur < 0) cur = 0;
            break;
        case NameKey::Right:
            if (++cur > length) cur = length;
            break;
        case NameKey::Delete:
            if (cur == length - 1) {
                e.name[static_cast<size_t>(cur)] = ' ';
                if (cur > 0) --cur;
            } else if (cur < length - 1) {
                e.name[static_cast<size_t>(cur)] = ' ';
            }
            break;
        case NameKey::Character: {
            const bool accepted = ch == '_' || (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') ||
                                  (ch >= 'A' && ch <= 'Z');
            if (accepted && cur < length) {
                e.name[static_cast<size_t>(cur)] = ch;
                ++cur;
            }
            break;
        }
    }
    if (cur >= length) cur = length - 1;
    return 0;
}

bool name_click(NameEntry& e, int x, int y) {
    const int col = x / 16 - 4;
    if (x >= 0 && col >= 0 && col < kNameLength) {
        if (y >= 0 && y / 16 == 5) {
            name_letter_up(e, col);
            return true;
        }
        if (y >= 0 && y / 16 == 7) {
            name_letter_down(e, col);
            return true;
        }
    }
    // 0x0C859: x from 0x40 to 0x40 + 12 x 16, y from 0x60 to 0x70; the
    // routine returns the column + 1 and the caller sets the cursor to column.
    if (x >= 0x40 && x <= 0x40 + kNameLength * 16 && y >= 0x60 && y <= 0x70) {
        e.cursor = std::min((x - 0x40) / 16, kNameLength - 1);
        return true;
    }
    return false;
}

NameArt load_name_art(const std::string& dir) {
    NameArt art;
    art.blocks = formats::pl8::load(asset(dir, "P_BLOCKS.PL8"));
    art.palette = formats::pal256::load(asset(dir, "SHADE.256"));
    art.font = GameFont{formats::pl8::load(asset(dir, "FONT1.PL8")), art.palette};
    return art;
}

void compose_name_entry(const NameEntry& e, const NameArt& art, std::vector<uint8_t>& rgb) {
    rgb.resize(static_cast<size_t>(320) * 200 * 3);
    // 1F6F:1EC1, 14 x 5 at (0x30, 0x40).
    size_t k = 0;
    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 14; ++c) {
            int frame = (r == 0 ? 0 : r == 4 ? 6 : 3) + (c == 0 ? 0 : c == 13 ? 2 : 1);
            if (frame == 4) {
                k = k + 1 > 49 ? 0 : k + 1;
                if (kPattern[k] != 0) frame = kPattern[k] + 0x13;
            }
            put_frame(rgb, art, frame, 0x30 + c * 16, 0x40 + r * 16);
        }
    }
    // DS:0x0164's arrows.
    for (int i = 0; i < kNameLength; ++i) {
        put_frame(rgb, art, 18, (4 + i) * 16, 5 * 16);
        put_frame(rgb, art, 19, (4 + i) * 16, 7 * 16);
    }
    // 0x0C2DD, then the cursor (0x0C511).
    for (int i = 0; i < kNameLength; ++i) {
        const char text[2] = {e.name[static_cast<size_t>(i)], 0};
        draw_game_text(rgb, 320, 200, 0x44 + i * 16, 0x64, text, 1, art.font);
    }
    draw_game_text(rgb, 320, 200, 0x44 + e.cursor * 16, 0x69, "-", 1, art.font);
}

}  // namespace gaius::ui
