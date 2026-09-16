// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/options_screen.hpp"

#include <algorithm>
#include <fstream>

namespace gaius::ui {

GameOptions default_options() {
    GameOptions o;
    o.words = {0, 3, 0, 1, 0, 100, 100, 1, 1, 0, 0, 0, 7, 0x220};
    return o;
}

bool load_options(const std::string& path, GameOptions& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char bytes[28];
    if (!f.read(bytes, sizeof bytes)) return false;
    for (size_t i = 0; i < out.words.size(); ++i)
        out.words[i] = static_cast<uint16_t>(static_cast<uint8_t>(bytes[2 * i]) |
                                             static_cast<uint8_t>(bytes[2 * i + 1]) << 8);
    return true;
}

bool save_options(const std::string& path, const GameOptions& options) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    for (uint16_t w : options.words) {
        const char b[2] = {static_cast<char>(w & 0xFF), static_cast<char>(w >> 8)};
        f.write(b, 2);
    }
    return static_cast<bool>(f);
}

namespace {

Button button(int cx, int cy, int frame, int pressed, ButtonMode mode, int state = 0) {
    Button b;
    b.cx = cx;
    b.cy = cy;
    b.frame = frame;
    b.pressed = pressed;
    b.mode = mode;
    b.state = state;
    return b;
}

}  // namespace

std::vector<Button> options_buttons(OptionsDialog dialog, const GameOptions& options, bool messages_on) {
    const auto M = ButtonMode::Momentary;
    switch (dialog) {
        case OptionsDialog::None: {
            // DS:0x0564: mode 3, acting on release.
            std::vector<Button> b;
            for (int row = 2; row <= 10; ++row) b.push_back(button(15, row, 0x1D, 0x1E, ButtonMode::Release));
            return b;
        }
        case OptionsDialog::Speed:
            // DS:0x0634: the game speed's up and down (0x0F204 / 0x0F217), the
            // scroll speed's (0x0F22A / 0x0F23D), OK (0x0F250).
            return {button(12, 4, 0x12, 0x1B, M), button(13, 4, 0x13, 0x1C, M), button(12, 5, 0x12, 0x1B, M),
                    button(13, 5, 0x13, 0x1C, M), button(12, 6, 0x1D, 0x1E, M)};
        case OptionsDialog::Sound:
            // DS:0x0684: effects (0x0F415), tunes (0x0F420), city sounds
            // (0x0F432), OK (0x0F438).
            return {button(12, 4, 0x1D, 0x1E, M), button(12, 5, 0x1D, 0x1E, M), button(12, 6, 0x1D, 0x1E, M),
                    button(12, 7, 0x1D, 0x1E, M)};
        case OptionsDialog::Display:
            // DS:0x05F4: three toggles whose states the start-up (0x0F67A) and
            // the loader copy in -- cancel the position indicator, the icon
            // name and messages -- and OK.
            return {button(16, 4, 0x1D, 0x1E, ButtonMode::Toggle, options.position_indicator_off() ? 1 : 0),
                    button(16, 5, 0x1D, 0x1E, ButtonMode::Toggle, options.icon_name_off() ? 1 : 0),
                    button(16, 6, 0x1D, 0x1E, ButtonMode::Toggle, messages_on ? 0 : 1),
                    button(16, 7, 0x1D, 0x1E, M)};
        case OptionsDialog::Restart:
            // DS:0x06E4: OK (0x0F53A) and Cancel (0x0F541).
            return {button(15, 9, 0x1D, 0x1E, ButtonMode::Release), button(15, 10, 0x1D, 0x1E, ButtonMode::Release)};
        case OptionsDialog::Exit:
            // DS:0x06C4: resume (0x0EDE3) and exit to DOS (0x0EDF0).
            return {button(15, 5, 0x1D, 0x1E, ButtonMode::Release), button(15, 6, 0x1D, 0x1E, ButtonMode::Release)};
    }
    return {};
}

bool options_dialog_button(OptionsDialog dialog, int index, const std::vector<Button>& buttons,
                           GameOptions& options, bool& messages_on) {
    const auto step = [&](int word, int delta) {
        options.set(word, std::clamp(static_cast<int>(options.words[static_cast<size_t>(word)]) + delta, 0, 100));
    };
    switch (dialog) {
        case OptionsDialog::Speed:
            if (index == 0) step(kOptSpeed, 10);
            if (index == 1) step(kOptSpeed, -10);
            if (index == 2) step(kOptScroll, 10);
            if (index == 3) step(kOptScroll, -10);
            return index == 4;
        case OptionsDialog::Sound:
            // 0x0F420 also stops the music when tunes go off (31E0:039D).
            if (index == 0) options.set(kOptEffects, options.words[kOptEffects] ^ 1);
            if (index == 1) options.set(kOptTunes, options.words[kOptTunes] ^ 1);
            if (index == 2) options.set(kOptCitySoundsOff, options.words[kOptCitySoundsOff] ^ 1);
            return index == 3;
        case OptionsDialog::Display:
            if (index == 0) options.set(kOptPositionOff, buttons[0].state);
            if (index == 1) options.set(kOptIconNameOff, buttons[1].state);
            if (index == 2) messages_on = (buttons[2].state ^ 1) != 0;
            return index == 3;
        default:
            return false;
    }
}

formats::IndexedImage compose_options_screen(const InterfaceArt& art, OptionsDialog dialog,
                                             const GameOptions& options, bool messages_on) {
    formats::IndexedImage img = blank_canvas();
    ButtonTracker tracker;
    // The speed, sound, display and leaving dialogs start with 0x0F5AC /
    // 0x0F5EA, whose 100F:00EF clears both pages to colour 0 (2EF9:0C50);
    // only the restart question is drawn over the menu.
    const bool over_menu = dialog == OptionsDialog::None || dialog == OptionsDialog::Restart;
    // 0x0B47D: a 20 x 12 panel over the whole screen, the title and the nine
    // items (DS:0x708E-0x706A) at x 0x20.
    if (over_menu) {
        draw_panel(img, art, 0, 0, 20, 12);
        struct Line {
            int y;
            const char* text;
        };
        static constexpr Line kItems[] = {
            {0x0C, " Caesar - Options screen"}, {0x22, "   Resume game "},   {0x32, "   Game speed"},
            {0x42, "   Sound effects"},         {0x54, "   Load a game"},    {0x64, "   Save a game"},
            {0x74, "   Display options"},       {0x84, "   Pause the game"}, {0x94, "   Restart the game"},
            {0xA4, "   Exit to DOS"}};
        for (const Line& l : kItems) draw_text(img, art, Font::Font1, 0x20, l.y, l.text);
        // The menu's buttons (drawn by 0x0ECDF each frame while it's open).
        std::vector<Button> menu = options_buttons(OptionsDialog::None, options, messages_on);
        draw_buttons(img, art.blocks, menu, tracker, Pointer{});
    }

    switch (dialog) {
        case OptionsDialog::None:
            return img;
        case OptionsDialog::Speed:
            // 0x0F0D3: the panel (0x0F5AC, 16 x 5 at (0x20, 0x30)), and each
            // frame the labels, the stone and the figures into "    %"
            // (DS:0x0D5F / 0x0D65, 3 digits, mode 2).
            draw_panel(img, art, 0x20, 0x30, 16, 5);
            draw_text(img, art, Font::Font1, 0x40, 0x44, "Game speed");
            draw_text(img, art, Font::Font1, 0x40, 0x54, "Scroll speed");
            draw_text(img, art, Font::Font1, 0x40, 0x64, "     OK");
            draw_stone(img, art, 0xE0, 0x40, 2, 2);
            draw_text(img, art, Font::Font1, 0xE8, 0x44, number_text(options.speed(), 3, 2) + " %");
            draw_text(img, art, Font::Font1, 0xE8, 0x54, number_text(options.scroll_speed(), 3, 2) + " %");
            break;
        case OptionsDialog::Sound:
            // 0x0F257: the panel (0x0F5EA, 16 x 6), and Yes / No
            // (DS:0x7196 / 0x7192) at x 0xE0; city sounds shows "No" when its
            // word is set.
            draw_panel(img, art, 0x20, 0x30, 16, 6);
            draw_text(img, art, Font::Font1, 0x40, 0x44, "Allow effects");
            draw_text(img, art, Font::Font1, 0x40, 0x54, "Allow tunes");
            draw_text(img, art, Font::Font1, 0x40, 0x64, "City sounds");
            draw_text(img, art, Font::Font1, 0x40, 0x74, "     OK");
            draw_stone(img, art, 0xE0, 0x40, 2, 3);
            draw_text(img, art, Font::Font1, 0xE0, 0x44, options.effects() ? "Yes" : "No");
            draw_text(img, art, Font::Font1, 0xE0, 0x54, options.tunes() ? "Yes" : "No");
            draw_text(img, art, Font::Font1, 0xE0, 0x64, options.city_sounds_off() ? "No" : "Yes");
            break;
        case OptionsDialog::Display:
            // 0x0EF28: the panel (0x0F5EA); the first label starts at x 0x30.
            draw_panel(img, art, 0x20, 0x30, 16, 6);
            draw_text(img, art, Font::Font1, 0x30, 0x44, "Cancel Position indicator");
            draw_text(img, art, Font::Font1, 0x40, 0x54, "Cancel icon name");
            draw_text(img, art, Font::Font1, 0x40, 0x64, "Cancel messages");
            draw_text(img, art, Font::Font1, 0x40, 0x74, "     OK");
            break;
        case OptionsDialog::Restart:
            // 0x0F43F with DS:0x7032: a 14 x 5 panel at (0x30, 0x70) over the menu.
            draw_panel(img, art, 0x30, 0x70, 14, 5);
            draw_text(img, art, Font::Font1, 0x50, 0x80, "Restart the game?");
            draw_text(img, art, Font::Font1, 0x40, 0x94, "     OK");
            draw_text(img, art, Font::Font1, 0x40, 0xA4, "   Cancel");
            break;
        case OptionsDialog::Exit:
            // 0x0ED2C: the panel (0x0F5AC).
            draw_panel(img, art, 0x20, 0x30, 16, 5);
            draw_text(img, art, Font::Font1, 0x40, 0x40, "   Are you sure ?!");
            draw_text(img, art, Font::Font1, 0x40, 0x54, "   Resume game ");
            draw_text(img, art, Font::Font1, 0x40, 0x64, "   Exit to DOS");
            break;
    }
    std::vector<Button> buttons = options_buttons(dialog, options, messages_on);
    draw_buttons(img, art.blocks, buttons, tracker, Pointer{});
    return img;
}

}  // namespace gaius::ui
