// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — ui/panel.hpp
//
// A full-screen page of text rows and buttons: the Forum's advisors, the
// promotion offer and the battle screen. Like ui/toolbar, it is pure geometry
// plus pixel writes into an RGB24 buffer, and layout() is the only source of
// geometry, so what render() draws and what hit_test() finds cannot drift
// apart.
//
// These screens are Gaius's own layouts, not the original's art: they show the
// words the original's screens show (systems::forum names them) with arrow
// buttons where the original has them, drawn in the game's font when the
// user's files provide it.
//
// A page is, top to bottom:
//   a row of tabs (optional)       -- e.g. the Forum's advisors
//   a title
//   rows: label, value, and optionally a down and an up arrow
//   a row of buttons (wrapping)    -- e.g. Accept / Wait 9 years
// Every clickable element carries an action id the caller chooses.

#pragma once

#include <string>
#include <vector>

#include "formats/common/types.hpp"
#include "ui/metrics.hpp"
#include "ui/toolbar.hpp"

namespace gaius::ui {

struct GameFont;

struct PanelRow {
    std::string label;
    std::string value;
    int down_action = -1;  // -1: no arrow
    int up_action = -1;
};

struct PanelButton {
    std::string text;
    int action = -1;
    bool enabled = true;
};

struct Page {
    std::vector<PanelButton> tabs;
    int selected_tab = -1;
    std::string title;
    std::vector<PanelRow> rows;
    std::vector<PanelButton> buttons;
};

// Where each element of a page sits on a `w` x `h` screen.
struct PanelLayout {
    Rect frame;
    std::vector<Rect> tabs;
    Rect title;
    std::vector<Rect> rows;
    std::vector<Rect> down;  // empty rect where the row has no arrow
    std::vector<Rect> up;
    std::vector<Rect> buttons;
};

PanelLayout layout(const Page& page, const Metrics& m, int w, int h);

// The action under a point, or -1. Disabled buttons don't count.
int hit_test(const Page& page, const PanelLayout& lay, int x, int y);

// Draws the page over the whole buffer. `hovered` is an action id (or -1).
void render(const Page& page, const PanelLayout& lay, std::vector<uint8_t>& rgb, int w, int h, const Metrics& m,
            const GameFont* font = nullptr, int hovered = -1);

// A strip of buttons along the top of the screen (the viewer's City /
// Province / Forum switch), as a page with only tabs: its layout, drawn over
// whatever is beneath without clearing the screen.
PanelLayout strip_layout(const std::vector<PanelButton>& tabs, const Metrics& m, int w);
void render_strip(const std::vector<PanelButton>& tabs, int selected, const PanelLayout& lay, std::vector<uint8_t>& rgb,
                  int w, int h, const Metrics& m, const GameFont* font = nullptr);
// A row of buttons along the bottom of the screen (the province commands),
// wrapping to as many rows as they need.
PanelLayout bar_layout(const std::vector<PanelButton>& buttons, const Metrics& m, int w, int h);
void render_bar(const std::vector<PanelButton>& buttons, int selected, const PanelLayout& lay, std::vector<uint8_t>& rgb,
                int w, int h, const Metrics& m, const GameFont* font = nullptr, const char* label = nullptr);

}  // namespace gaius::ui
