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
// PAGING. The logical framebuffer is 320x200 (masterplan section 4: a
// period-accurate coordinate space "for game logic and original sprite
// alignment"). At 1x the toolbar reproduces the original's proportions; at 2x
// the buttons wrap to two rows. Past that, wrapping would leave no map, so
// the toolbar keeps at most the rows that fit in half the screen (at least
// one) and, when the tools don't fit, pages: the first and
// last column of each page hold a previous and a next arrow (hit_test
// returns kPreviousPage / kNextPage), and the tools between them show a
// page at a time. The alternative, a logical framebuffer that grows with the
// display, would change every screen drawn in the original's 320x200 art;
// paging changes only the toolbar (Phase 9, 2026-09-17).

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

struct GameFont;  // ui/game_font.hpp

// Maps a tile id to the colour the map view draws it in, so a button's
// icon matches what placing it will actually look like. The viewer passes
// its own layer colouring function; keeping it a parameter is what stops
// ui/ from depending on apps/.
using TileColorFn = formats::RGB (*)(uint8_t);

// The largest share of the screen's height the panel may take before the
// toolbar pages instead of adding a row.
inline constexpr double kMaxPanelShare = 0.5;
// hit_test's answers for the page arrows.
inline constexpr int kPreviousPage = -2, kNextPage = -3;

// --- The original's control bar -------------------------------------------
//
// The original has one bar of up to 11 buttons (x = 8 + 24 * slot) and three
// pages for it (`DS:0x6D0E`): the main bar (go to the province, go to the
// Forum, save and load, maps, clear land, housing, bath houses, and a button
// for each of the other two pages), the infrastructure page (a back arrow,
// then road, plaza, reservoir, well, fountain, wall, tower, barracks,
// prefecture and forum) and the construction page (a back arrow, then temple,
// hospital, school, oracle, heavy industry, market, workshop, theater,
// coliseum and hippodrome). Pressing a building's button keeps the page, so
// several can be placed in a row; the arrow goes back to the main bar. The
// pages and their icons are the executable's own tables (renderer findings
// section 7). The page buttons are named "Infrastructure" and
// "Construction": the plaque the original shows at the top right while the
// pointer is over them (DOSBox captures, 2026-10-02); the executable's
// string table has no text for them.
enum class BarKind {
    Tool,   // selects a placing command
    Page,   // turns to another page of the bar
    Go,     // leaves the city: the province, the Forum or the maps
    Files,  // the save and load screen
    Back,   // to the main bar
};

inline constexpr int kBarSlots = 11;
inline constexpr int kBarPages = 3;
// The bar's own geometry (0x211CB, findings section 46): the panel is the bottom 24 rows of the screen, a button's
// 16 x 16 icon is drawn at x = 8 + 24 * slot, 4 rows into it, and the funds are a five-digit number at (268, 184).
inline constexpr int kBarPanelH = 24, kBarPitch = 24, kBarX0 = 8, kBarIconPx = 16, kBarIconY = 4;
inline constexpr int kBarFundsX = 268, kBarFundsY = 184;

struct BarButton {
    BarKind kind = BarKind::Tool;
    systems::construction::CommandId command = systems::construction::CommandId::NoAction;  // Tool, Go
    int frame = -1;       // POINTERS.PL8
    int page = 0;         // Page: where it turns to
    bool available = true;  // false: drawn dimmed, and does nothing (a command Gaius has not transcribed)
};

// The buttons of page `page` (0-2) in slot order.
const std::vector<BarButton>& original_bar_page(int page);

class Toolbar {
public:
    // With `original_bar` the toolbar is the original's paged bar (above) at the desktop's 1x scale -- its 24-row panel
    // and 24-pixel pitch, as the original lays it out -- and otherwise the flat list of `tools`, which pages by arrows
    // when it overflows.
    Toolbar(const systems::construction::CommandId* tools, int count, Metrics m, int screen_w, int screen_h,
            bool original_bar = false);

    // Whether the original's bar is what shows (and then count() is the buttons of the page showing).
    bool original_bar() const { return bar_; }
    int bar_page() const { return bar_page_; }
    void set_bar_page(int page);
    // What button `i` is. In the flat list every button is a Tool.
    BarButton entry(int i) const;
    // The text for button `i`'s label row.
    const char* label(int i) const;
    // The index of the button for `command` among those showing (the flat list's own index, whatever the page), or -1.
    int index_of(systems::construction::CommandId command) const;
    // Turns to the page that has `command`'s button (the original's bar), or to the page of its tool (the flat list).
    void show_command(systems::construction::CommandId command);

    int count() const { return bar_ ? static_cast<int>(entries_.size()) : static_cast<int>(tools_.size()); }
    // Paging: whether the tools need more than one page, how many pages, the
    // one showing, and turning it (wrapping around). show_tool turns to the
    // page holding tool i.
    bool paged() const { return !bar_ && per_page_ < count(); }
    int pages() const { return paged() ? (count() + per_page_ - 1) / per_page_ : 1; }
    int page() const { return page_; }
    void turn_page(int delta);
    void show_tool(int i);
    // The arrows' boxes; empty when not paged.
    Rect previous_arrow() const;
    Rect next_arrow() const;
    systems::construction::CommandId tool(int i) const {
        return bar_ ? entries_[static_cast<size_t>(i)].command : tools_[static_cast<size_t>(i)];
    }
    const Metrics& metrics() const { return m_; }
    int columns() const { return cols_; }
    int rows() const { return rows_; }

    // The whole panel, including its label row.
    Rect panel() const { return panel_; }

    // Geometry of button `i`. The ONLY source of button geometry -- see
    // the header comment. Out-of-range indices, and tools on another page,
    // give an empty rect rather than UB, since this is called from input
    // handling.
    Rect button(int i) const;

    // Index of the button under a logical-space point, kPreviousPage /
    // kNextPage for an arrow, or -1.
    int hit_test(int lx, int ly) const;

    // True if the point is anywhere on the panel. Callers use this to
    // stop a click that landed on the toolbar from also being treated as
    // a click on the map underneath.
    bool contains(int lx, int ly) const { return panel_.contains(lx, ly); }

private:
    std::vector<systems::construction::CommandId> tools_;
    bool bar_ = false;
    int bar_page_ = 0;
    std::vector<BarButton> entries_;  // the original's bar: the page showing
    Metrics m_;
    int cols_ = 1;
    int rows_ = 1;
    int grid_x0_ = 0;
    int grid_y0_ = 0;
    int per_page_ = 1;  // tools a page shows
    int page_ = 0;
    Rect panel_;
};

// Draws `bar` over an existing RGB24 frame. `selected` is a button index
// (or -1); `hovered` likewise, for mouse hover feedback. With `font` (the
// game's FONT1.PL8, when the user's copy is available) the label is drawn in
// the original's typeface; without it, in ui/font.hpp's placeholder.
//
// With `icons` (the game's POINTERS.PL8) and `icon_palette` (the city palette,
// SHADE.256), each button shows its command's real toolbar icon, scaled to the
// button; otherwise it shows the building's footprint.
void render(const Toolbar& bar, int selected, int hovered, TileColorFn tile_color, std::vector<uint8_t>& rgb, int w,
            int h, const GameFont* font = nullptr, const formats::PL8Sheet* icons = nullptr,
            const formats::Palette* icon_palette = nullptr, const char* selected_label = nullptr,
            const char* funds_text = nullptr);
// `selected_label`, when given, replaces the selected tool's name in the label
// row (e.g. "Forum grade 3, 140 Dn"); a hovered tool still shows its own name.
// `funds_text` (e.g. "Funds 1234 Dn"), when given, is appended to the label
// row -- but only when the combined text still fits the panel's width, since
// the original never lets a label overflow a fixed-width bar. Dropped
// silently otherwise, so a very long tool name never doubles up with a
// clipped, unreadable funds figure.

// The game's own pictures of the bar's panel, drawn under the buttons: PANEL1A.VPX (the main bar) and PANEL1B.VPX
// (the infrastructure and construction pages), both 320 x 200 in the city palette, of which the bottom 24 rows show.
struct BarArt {
    formats::IndexedImage main;
    formats::IndexedImage build;
};

// Draws the original's paged bar -- only when `bar.original_bar()` -- over a frame: the panel, each button's POINTERS
// icon at its place, the chosen command's thin frame, and the funds at (268, 184). `selected` is a button index or -1.
void render_original_bar(const Toolbar& bar, int selected, const BarArt* art, const formats::PL8Sheet* icons,
                         const formats::Palette* palette, const GameFont* font, int funds, std::vector<uint8_t>& rgb, int w,
                         int h);

// The POINTERS.PL8 frame the original's control panel shows for a command, or
// -1 if it has no button. Read from the panel's button tables (DS:0x1178,
// DS:0x11BA, DS:0x11FC: 11 records per page of POINTERS frame + far pointer to
// the click handler, which sets the command id; drawn by 0x211CB at y = 180,
// x = 8 + 24 * slot). A button's handler is the previous record's, so a command's
// icon is the frame of the record after its own (see toolbar.cpp). Two captures
// show pages 0 and 1 in exactly this order.
int command_icon_frame(systems::construction::CommandId id);

}  // namespace gaius::ui
