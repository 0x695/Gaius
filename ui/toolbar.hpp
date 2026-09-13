// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — ui/toolbar.hpp
//
// The build toolbar: layout, hit-testing and rendering, closing
// GAIUS_ROADMAP.md Phase 5's last checklist item ("Toolbar UI at the
// scalable-UI-factor from masterplan section 5a point 3, sized for the
// smallest target touch surface").
//
// Two design decisions worth knowing before reading the code:
//
// 1. HIT TARGETS CANNOT DRIFT FROM DRAWN BUTTONS. `hit_test()` and
//    `render()` both go through `button(i)`; neither computes geometry of
//    its own. Masterplan 5a point 3 requires icons, text and hit targets
//    to resize "together", and the cheapest way to guarantee that is to
//    make one function the only source of button geometry, rather than
//    two functions that agree by convention until someone edits one.
//
// 2. THE ICONS ARE GENERATED FROM THE RE DATA, NOT DRAWN BY HAND. Each
//    button renders a miniature of that building's *real* footprint (from
//    systems::construction::placement_spec, i.e. transcribed from the
//    executable's own handlers) filled with the same colour the map
//    renderer gives that building's seed tile. So a Hippodrome button
//    shows a 4x2 block and a Well shows 1x1, in the colours they'll
//    actually appear on the map. This is deliberate: the original's real
//    icon art wasn't recoverable when this was written (P_BLOCKS.PL8 and
//    the font sheets looked like noise; since 2026-09-13 PL8 decodes
//    correctly, but which sheet holds the toolbar icons isn't identified
//    yet -- see docs/FORMATS.md), and inventing decorative icons would put invented
//    content on screen. Footprint-and-colour is information the project
//    actually has, and it happens to be more useful than a pictogram
//    when what you need to know is how much room a building takes.
//
// KNOWN TENSION, stated rather than hidden: the logical framebuffer is
// 320x200 (masterplan section 4: a period-accurate coordinate space "for
// game logic and original sprite alignment"). At Desktop scale the
// toolbar reproduces the original's proportions almost exactly -- 16px
// icons in a bottom bar, against the measured 24px original panel. At 2x
// the buttons wrap to two rows and the panel eats about a third of the
// screen; at 3x it would take three rows and leave no usable map. A
// touch-first UI therefore cannot simply scale up inside a 320-wide
// logical space. The real fix is one of: a logical framebuffer that grows
// with the display (so UI scale and world scale are independent), or a
// paged/scrolling toolbar. Both are larger decisions than this item, and
// belong with Phase 9's platform pass -- ui::metrics_for caps at 2x for
// now so no configuration produces an unusable screen.

#pragma once

#include <vector>

#include "formats/common/types.hpp"
#include "systems/construction.hpp"
#include "ui/metrics.hpp"

namespace gaius::ui {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// Maps a tile id to the colour the map view draws it in, so a button's
// icon matches what placing it will actually look like. The viewer passes
// its own layer colouring function; keeping it a parameter is what stops
// ui/ from depending on apps/.
using TileColorFn = formats::RGB (*)(uint8_t);

class Toolbar {
public:
    Toolbar(const systems::construction::CommandId* tools, int count, Metrics m, int screen_w, int screen_h);

    int count() const { return static_cast<int>(tools_.size()); }
    systems::construction::CommandId tool(int i) const { return tools_[static_cast<size_t>(i)]; }
    const Metrics& metrics() const { return m_; }
    int columns() const { return cols_; }
    int rows() const { return rows_; }

    // The whole panel, including its label row.
    Rect panel() const { return panel_; }

    // Geometry of button `i`. The ONLY source of button geometry -- see
    // the header comment. Out-of-range indices give an empty rect rather
    // than UB, since this is called from input handling.
    Rect button(int i) const;

    // Index of the button under a logical-space point, or -1.
    int hit_test(int lx, int ly) const;

    // True if the point is anywhere on the panel. Callers use this to
    // stop a click that landed on the toolbar from also being treated as
    // a click on the map underneath.
    bool contains(int lx, int ly) const { return panel_.contains(lx, ly); }

private:
    std::vector<systems::construction::CommandId> tools_;
    Metrics m_;
    int cols_ = 1;
    int rows_ = 1;
    int grid_x0_ = 0;
    int grid_y0_ = 0;
    Rect panel_;
};

// Draws `bar` over an existing RGB24 frame. `selected` is a button index
// (or -1); `hovered` likewise, for mouse hover feedback.
void render(const Toolbar& bar, int selected, int hovered, TileColorFn tile_color, std::vector<uint8_t>& rgb, int w,
            int h);

}  // namespace gaius::ui
