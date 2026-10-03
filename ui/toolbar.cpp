// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/toolbar.hpp"

#include "ui/game_font.hpp"

#include <algorithm>
#include <string>

#include "ui/font.hpp"
#include "ui/strings.hpp"

namespace gaius::ui {

namespace {

using formats::RGB;

// Panel chrome. These approximate the decoded PANEL1.VPX artwork (a
// muted olive//stone bar) rather than sampling it: the real panel is
// original game content, which per the project's IP rule can't ship in
// the repo, and the toolbar has to work with no assets present at all
// (tests run without GAIUS_TEST_ASSETS). Documented as an approximation,
// not passed off as the original's palette.
constexpr RGB kPanelFill{74, 70, 52};
constexpr RGB kPanelTopEdge{122, 116, 88};
constexpr RGB kPanelBotEdge{44, 42, 30};
constexpr RGB kButtonFill{58, 55, 40};
constexpr RGB kButtonEdge{100, 95, 70};
constexpr RGB kSelectedEdge{236, 206, 116};
constexpr RGB kHoverEdge{170, 160, 120};
constexpr RGB kLabelText{232, 226, 200};
constexpr RGB kUnavailable{92, 60, 60};

void put(std::vector<uint8_t>& rgb, int w, int h, int x, int y, RGB c) {
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    size_t i = (static_cast<size_t>(y) * w + x) * 3;
    rgb[i + 0] = c.r;
    rgb[i + 1] = c.g;
    rgb[i + 2] = c.b;
}

void fill_rect(std::vector<uint8_t>& rgb, int w, int h, Rect r, RGB c) {
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x) put(rgb, w, h, x, y, c);
}

void stroke_rect(std::vector<uint8_t>& rgb, int w, int h, Rect r, RGB c) {
    for (int x = r.x; x < r.x + r.w; ++x) {
        put(rgb, w, h, x, r.y, c);
        put(rgb, w, h, x, r.y + r.h - 1, c);
    }
    for (int y = r.y; y < r.y + r.h; ++y) {
        put(rgb, w, h, r.x, y, c);
        put(rgb, w, h, r.x + r.w - 1, y, c);
    }
}

// Footprint in cells for icon purposes. SingleCell leaves width/height at
// 0 in PlacementSpec (they're documented as meaningful only for
// MultiCell), so it's normalized to 1x1 here rather than drawing nothing.
void icon_footprint(const systems::construction::PlacementSpec& spec, int* fw, int* fh) {
    using systems::construction::PlacementKind;
    switch (spec.kind) {
        case PlacementKind::SingleCell:
            *fw = 1;
            *fh = 1;
            return;
        case PlacementKind::MultiCell:
        case PlacementKind::VariantSelected:
            *fw = std::max(1, spec.width);
            *fh = std::max(1, spec.height);
            return;
        default:
            *fw = 0;
            *fh = 0;
            return;
    }
}

// Draws the building's real footprint as a miniature grid inside `box`.
void draw_footprint_icon(std::vector<uint8_t>& rgb, int w, int h, Rect box,
                         const systems::construction::PlacementSpec& spec, TileColorFn tile_color) {
    using systems::construction::PlacementKind;

    int fw = 0, fh = 0;
    icon_footprint(spec, &fw, &fh);

    if (fw == 0 || fh == 0) {
        // DragAutoTiled / NonPlacing: no footprint and no seed tile to
        // show. Draw a diagonal hatch rather than a guessed shape, so
        // "we don't know this one" is visible instead of implied.
        for (int y = 0; y < box.h; ++y)
            for (int x = 0; x < box.w; ++x)
                if (((x + y) % 4) == 0) put(rgb, w, h, box.x + x, box.y + y, kUnavailable);
        return;
    }

    // Largest whole-pixel cell size that fits the footprint in the box,
    // so a 4x4 industry and a 1x1 well both stay inside their button.
    int cell = std::max(1, std::min(box.w / fw, box.h / fh));
    int gw = cell * fw, gh = cell * fh;
    int ox = box.x + (box.w - gw) / 2;
    int oy = box.y + (box.h - gh) / 2;

    RGB body = (spec.kind == PlacementKind::VariantSelected) ? kUnavailable : tile_color(spec.seed_tile);
    RGB grid{static_cast<uint8_t>(body.r / 2), static_cast<uint8_t>(body.g / 2), static_cast<uint8_t>(body.b / 2)};

    for (int cy = 0; cy < fh; ++cy) {
        for (int cx = 0; cx < fw; ++cx) {
            for (int y = 0; y < cell; ++y) {
                for (int x = 0; x < cell; ++x) {
                    // Keep a 1px darker edge per cell when cells are big
                    // enough, so the footprint reads as N cells rather
                    // than one solid rectangle.
                    bool edge = cell >= 3 && (x == 0 || y == 0);
                    put(rgb, w, h, ox + cx * cell + x, oy + cy * cell + y, edge ? grid : body);
                }
            }
        }
    }
}

}  // namespace

const std::vector<BarButton>& original_bar_page(int page) {
    using C = systems::construction::CommandId;
    const auto tool = [](C c, int frame, bool available = true) { return BarButton{BarKind::Tool, c, frame, 0, available}; };
    const auto go = [](C c, int frame) { return BarButton{BarKind::Go, c, frame, 0, true}; };
    const auto turn = [](int frame, int to) { return BarButton{BarKind::Page, C::NoAction, frame, to, true}; };
    const BarButton back{BarKind::Back, C::MainToolbar, 28, 0, true};
    const auto province = [](int id, int frame, C c = C::NoAction) {
        return BarButton{BarKind::Province, c, frame, 0, true, id};
    };
    // Frames from command_icon_frame's table; see there for how the executable's records pair up.
    static const std::vector<BarButton> pages[kBarPages] = {
        // DS:0x1178 -- 0x28, Gaius's file screen, is the save and load button
        {go(C::GoToProvince, 21), go(C::GoToForum, 6), BarButton{BarKind::Files, C::NoAction, 51, 0, true},
         go(C::Maps, 33), tool(C::ClearArea, 5), tool(C::Housing, 9), tool(C::BathHouses, 22), turn(52, 1), turn(53, 2)},
        // DS:0x11BA -- the city's Tower re-tiles walls by their neighbours (findings section 18), not transcribed yet
        {back, tool(C::Road, 7), tool(C::Plaza, 19), tool(C::ReservoirPipe, 8), tool(C::Well, 11), tool(C::Fountain, 12),
         tool(C::Wall, 10), tool(C::Tower, 16, false), tool(C::Barracks, 14), tool(C::Prefecture, 17), tool(C::Forum, 18)},
        // DS:0x11FC
        {back, tool(C::Temple, 23), tool(C::Hospital, 30), tool(C::School, 32), tool(C::Oracle, 31),
         tool(C::HeavyIndustry, 20), tool(C::Market, 34), tool(C::Workshop, 27), tool(C::Theater, 24),
         tool(C::Coliseum, 25), tool(C::Hippodrome, 26)},
        // DS:0x123E -- the province view: back to the city, the Forum, then the executable's command ids 35, 36, 42, 29,
        // 37, 41, 31, 32 and 33 (the table's records, read with the same off-by-one pairing as the city's pages; the
        // Cohort's Halt, id 30, has no button)
        {BarButton{BarKind::Back, C::MainToolbar, 13, 0, true}, go(C::GoToForum, 35), province(35, 40),
         province(36, 41), province(42, 42), province(29, 15, C::Fort), province(37, 43), province(41, 39),
         province(31, 36, C::CohortPatrol), province(32, 37, C::CohortAttack), province(33, 38, C::CohortGoHome)},
    };
    return pages[std::clamp(page, 0, kBarPages - 1)];
}

Toolbar::Toolbar(const systems::construction::CommandId* tools, int count, Metrics m, int screen_w, int screen_h,
                 bool original_bar)
    : tools_(tools, tools + std::max(0, count)), m_(m) {
    const int pad = std::max(1, m_.pad_px);
    const int stride = m_.button_px() + m_.gap_px;
    const int avail = std::max(m_.button_px(), screen_w - 2 * pad);
    const int n = std::max(1, count);
    const int label_block = m_.label_h + pad;

    const int fit_cols = std::max(1, (avail + m_.gap_px) / stride);
    // The rows the panel may have: as many as fit in kMaxPanelShare, at least one.
    const int budget = static_cast<int>(screen_h * kMaxPanelShare) - (pad + label_block + pad) + m_.gap_px;
    const int max_rows = std::max(1, budget / stride);
    cols_ = std::min(fit_cols, n);
    rows_ = std::max(1, (n + cols_ - 1) / cols_);
    per_page_ = n;
    const int bar_rows = (kBarSlots + fit_cols - 1) / fit_cols;
    if (original_bar && m_.scale == 1 && bar_rows == 1 && screen_w >= kBarX0 + kBarPitch * kBarSlots) {
        // The original's bar: its 11 slots, the page's buttons in them, laid out as the original lays them out.
        bar_ = true;
        cols_ = kBarSlots;
        rows_ = 1;
        per_page_ = kBarSlots;
        set_bar_page(0);
        panel_ = Rect{0, screen_h - kBarPanelH, screen_w, kBarPanelH};
        grid_x0_ = kBarX0;
        grid_y0_ = panel_.y + kBarIconY;
        return;
    } else if (rows_ > max_rows) {
        // Paged: an arrow at each end of the rows, the tools between.
        cols_ = std::max(3, fit_cols);
        rows_ = max_rows;
        per_page_ = std::max(1, (cols_ - 2) * rows_);
    }

    const int grid_w = cols_ * stride - m_.gap_px;
    const int grid_h = rows_ * stride - m_.gap_px;
    const int panel_h = pad + label_block + grid_h + pad;

    panel_ = Rect{0, screen_h - panel_h, screen_w, panel_h};
    grid_x0_ = (screen_w - grid_w) / 2;
    grid_y0_ = panel_.y + pad + label_block;
}

void Toolbar::set_bar_page(int page) {
    if (!bar_) return;
    bar_page_ = std::clamp(page, 0, kBarPages - 1);
    entries_ = original_bar_page(bar_page_);
}

BarButton Toolbar::entry(int i) const {
    if (i < 0 || i >= count()) return BarButton{};
    if (bar_) return entries_[static_cast<size_t>(i)];
    const auto command = tools_[static_cast<size_t>(i)];
    return BarButton{BarKind::Tool, command, command_icon_frame(command), 0, true};
}

// The province commands' names: the executable has strings only for the Fort and the Cohort orders (ids below 34);
// the others are the manual's, kept to 15 characters so they fit the plaque.
static const char* province_command_label(int id) {
    switch (id) {
        case 29: return "Fort";
        case 31: return "Cohort Patrol";
        case 32: return "Cohort Attack";
        case 33: return "Cohort Go Home";
        case 35: return "Clear Area";
        case 36: return "Road";
        case 37: return "Great Wall";
        case 41: return "Great Tower";
        case 42: return "Highway";
        default: return "";
    }
}

const char* Toolbar::label(int i) const {
    if (i < 0 || i >= count()) return "";
    const BarButton b = entry(i);
    if (bar_ && bar_page_ == kBarProvincePage && b.kind == BarKind::Back) return tr("Go to City");
    switch (b.kind) {
        case BarKind::Province: return province_command_label(b.province);
        case BarKind::Page: return b.page == 1 ? tr("Infrastructure") : tr("Construction");
        case BarKind::Files: return tr("Save and load");
        default: return systems::construction::command_name(b.command);
    }
}

int Toolbar::index_of(systems::construction::CommandId command) const {
    for (int i = 0; i < count(); ++i)
        if (tool(i) == command && entry(i).kind == BarKind::Tool) return i;
    return -1;
}

void Toolbar::show_command(systems::construction::CommandId command) {
    if (!bar_) {
        for (int i = 0; i < count(); ++i)
            if (tools_[static_cast<size_t>(i)] == command) {
                show_tool(i);
                return;
            }
        return;
    }
    for (int p = 0; p < kBarPages; ++p)
        for (const BarButton& b : original_bar_page(p))
            if (b.kind == BarKind::Tool && b.command == command) {
                set_bar_page(p);
                return;
            }
}

void Toolbar::turn_page(int delta) {
    const int n = pages();
    page_ = ((page_ + delta) % n + n) % n;
}

void Toolbar::show_tool(int i) {
    if (i >= 0 && i < count() && paged()) page_ = i / per_page_;
}

Rect Toolbar::previous_arrow() const {
    if (!paged()) return Rect{};
    return Rect{grid_x0_, grid_y0_, m_.button_px(), rows_ * (m_.button_px() + m_.gap_px) - m_.gap_px};
}

Rect Toolbar::next_arrow() const {
    if (!paged()) return Rect{};
    const int stride = m_.button_px() + m_.gap_px;
    return Rect{grid_x0_ + (cols_ - 1) * stride, grid_y0_, m_.button_px(), rows_ * stride - m_.gap_px};
}

Rect Toolbar::button(int i) const {
    if (i < 0 || i >= count()) return Rect{};
    if (bar_) return Rect{grid_x0_ + kBarPitch * i, grid_y0_, kBarIconPx, kBarIconPx};
    const int stride = m_.button_px() + m_.gap_px;
    if (!paged()) {
        const int col = i % cols_;
        const int row = i / cols_;
        return Rect{grid_x0_ + col * stride, grid_y0_ + row * stride, m_.button_px(), m_.button_px()};
    }
    if (i / per_page_ != page_) return Rect{};
    const int k = i % per_page_;
    const int inner = cols_ - 2;
    return Rect{grid_x0_ + (1 + k % inner) * stride, grid_y0_ + (k / inner) * stride, m_.button_px(), m_.button_px()};
}

int Toolbar::hit_test(int lx, int ly) const {
    if (previous_arrow().contains(lx, ly)) return kPreviousPage;
    if (next_arrow().contains(lx, ly)) return kNextPage;
    for (int i = 0; i < count(); ++i) {
        if (button(i).contains(lx, ly)) return i;
    }
    return -1;
}

void render(const Toolbar& bar, int selected, int hovered, TileColorFn tile_color, std::vector<uint8_t>& rgb, int w,
            int h, const GameFont* font, const formats::PL8Sheet* icons, const formats::Palette* icon_palette,
            const char* selected_label, const char* funds_text) {
    if (w <= 0 || h <= 0 || rgb.size() < static_cast<size_t>(w) * h * 3) return;

    const Rect p = bar.panel();
    fill_rect(rgb, w, h, p, kPanelFill);
    for (int x = p.x; x < p.x + p.w; ++x) {
        put(rgb, w, h, x, p.y, kPanelTopEdge);
        put(rgb, w, h, x, p.y + p.h - 1, kPanelBotEdge);
    }

    const Metrics& m = bar.metrics();

    // Label row: the selected tool's name, or a hovered one in preview.
    // Falls back to a hint when nothing is selected, so the panel is
    // never a blank bar with no explanation of what it does.
    int label_for = (hovered >= 0) ? hovered : selected;
    const char* label = (label_for >= 0 && label_for < bar.count()) ? bar.label(label_for) : tr("SELECT A BUILDING");
    const char* shown = (selected_label && selected >= 0 && label_for == selected) ? selected_label : label;

    // Try the label with the funds figure appended; fall back to the label
    // alone if that would overflow the panel (see funds_text's doc comment).
    std::string with_funds;
    if (funds_text && *funds_text) {
        with_funds = std::string(shown) + " - " + funds_text;
    }
    const char* candidate = with_funds.empty() ? shown : with_funds.c_str();
    const int available = std::max(0, p.w - 2 * m.pad_px);
    if (font) {
        int tw = game_text_width(candidate, m.glyph_scale, *font);
        if (!with_funds.empty() && tw > available) {
            candidate = shown;
            tw = game_text_width(shown, m.glyph_scale, *font);
        }
        draw_game_text(rgb, w, h, p.x + (p.w - tw) / 2, p.y + std::max(1, m.pad_px), candidate, m.glyph_scale, *font);
    } else {
        int tw = text_width(candidate, m.glyph_scale);
        if (!with_funds.empty() && tw > available) {
            candidate = shown;
            tw = text_width(shown, m.glyph_scale);
        }
        draw_text(rgb, w, h, p.x + (p.w - tw) / 2, p.y + std::max(1, m.pad_px), candidate, m.glyph_scale, kLabelText);
    }

    // The page arrows: a triangle in each end column, and the page as dots.
    if (bar.paged()) {
        for (const bool next : {false, true}) {
            const Rect a = next ? bar.next_arrow() : bar.previous_arrow();
            fill_rect(rgb, w, h, a, kButtonFill);
            stroke_rect(rgb, w, h, a, hovered == (next ? kNextPage : kPreviousPage) ? kHoverEdge : kButtonEdge);
            const int half = std::max(2, std::min(a.w, a.h) / 4);
            const int cx = a.x + a.w / 2, cy = a.y + a.h / 2;
            for (int dy = -half; dy <= half; ++dy) {
                const int len = half - (dy < 0 ? -dy : dy);
                for (int dx = 0; dx <= len; ++dx) put(rgb, w, h, next ? cx - half / 2 + dx : cx + half / 2 - dx, cy + dy, kLabelText);
            }
        }
        const Rect a = bar.previous_arrow();
        const int dot = std::max(1, m.scale);
        const int dots_w = bar.pages() * dot * 2 - dot;
        const int dy = a.y + a.h + std::max(1, m.pad_px / 2);
        for (int k = 0; k < bar.pages(); ++k) {
            const Rect d{(w - dots_w) / 2 + k * dot * 2, std::min(dy, p.y + p.h - dot - 1), dot, dot};
            fill_rect(rgb, w, h, d, k == bar.page() ? kSelectedEdge : kButtonEdge);
        }
    }

    for (int i = 0; i < bar.count(); ++i) {
        Rect b = bar.button(i);
        if (b.w == 0) continue;  // on another page
        fill_rect(rgb, w, h, b, kButtonFill);

        Rect inner{b.x + m.pad_px, b.y + m.pad_px, b.w - 2 * m.pad_px, b.h - 2 * m.pad_px};
        const BarButton info = bar.entry(i);
        const int frame = info.frame;
        const formats::PL8Frame* icon =
            (icons && icon_palette && frame >= 0 && frame < static_cast<int>(icons->frames.size()) &&
             !icons->frames[static_cast<size_t>(frame)].pixels.empty())
                ? &icons->frames[static_cast<size_t>(frame)]
                : nullptr;
        if (icon) {
            // The icon, scaled by whole pixels to fit the inner box, centred;
            // index 0 is transparent.
            const int scale = std::max(1, std::min(inner.w / icon->width, inner.h / icon->height));
            const int ox = inner.x + (inner.w - icon->width * scale) / 2;
            const int oy = inner.y + (inner.h - icon->height * scale) / 2;
            for (int iy = 0; iy < icon->height; ++iy) {
                for (int ix = 0; ix < icon->width; ++ix) {
                    const uint8_t idx = icon->pixels[static_cast<size_t>(iy) * icon->width + ix];
                    if (idx == 0) continue;
                    const RGB c = icon_palette->colors[idx];
                    for (int sy = 0; sy < scale; ++sy)
                        for (int sx = 0; sx < scale; ++sx) put(rgb, w, h, ox + ix * scale + sx, oy + iy * scale + sy, c);
                }
            }
        } else {
            draw_footprint_icon(rgb, w, h, inner, systems::construction::placement_spec(bar.tool(i)), tile_color);
        }

        if (!info.available) {
            // A command Gaius has not transcribed: dimmed, and it does nothing.
            for (int y = b.y; y < b.y + b.h; ++y)
                for (int x = b.x; x < b.x + b.w; ++x) {
                    if (x < 0 || x >= w || y < 0 || y >= h) continue;
                    const size_t k = (static_cast<size_t>(y) * w + x) * 3;
                    rgb[k] = static_cast<uint8_t>(rgb[k] / 3);
                    rgb[k + 1] = static_cast<uint8_t>(rgb[k + 1] / 3);
                    rgb[k + 2] = static_cast<uint8_t>(rgb[k + 2] / 3);
                }
        }
        RGB edge = (i == selected) ? kSelectedEdge : (i == hovered ? kHoverEdge : kButtonEdge);
        stroke_rect(rgb, w, h, b, edge);
        if (i == selected) {
            // Double border for the selected tool, so selection survives
            // being viewed on a small screen or in grayscale.
            Rect o{b.x - 1, b.y - 1, b.w + 2, b.h + 2};
            stroke_rect(rgb, w, h, o, kSelectedEdge);
        }
    }
}

}  // namespace gaius::ui

namespace gaius::ui {

void render_original_bar(const Toolbar& bar, int selected, const BarArt* art, const formats::PL8Sheet* icons,
                         const formats::Palette* palette, const GameFont* font, int funds, std::vector<uint8_t>& rgb, int w,
                         int h) {
    if (!bar.original_bar() || w <= 0 || h <= 0 || rgb.size() < static_cast<size_t>(w) * h * 3) return;
    const Rect p = bar.panel();
    // The panel: the game's own picture of it (PANEL1A for the main bar and the province view, PANEL1D for the building
    // pages -- 0x0FDED), else a flat one.
    const formats::IndexedImage* image = nullptr;
    if (art && palette) image = (bar.bar_page() == 0 || bar.bar_page() == kBarProvincePage) ? &art->main : &art->build;
    if (image && image->width == w && image->height == h && !image->pixels.empty()) {
        for (int y = p.y; y < p.y + p.h; ++y)
            for (int x = 0; x < w; ++x) put(rgb, w, h, x, y, palette->colors[image->pixels[static_cast<size_t>(y) * w + x]]);
    } else {
        fill_rect(rgb, w, h, p, kPanelFill);
        for (int x = p.x; x < p.x + p.w; ++x) put(rgb, w, h, x, p.y, kPanelTopEdge);
    }
    for (int i = 0; i < bar.count(); ++i) {
        const Rect b = bar.button(i);
        const BarButton info = bar.entry(i);
        const formats::PL8Frame* icon =
            (icons && palette && info.frame >= 0 && info.frame < static_cast<int>(icons->frames.size()) &&
             !icons->frames[static_cast<size_t>(info.frame)].pixels.empty())
                ? &icons->frames[static_cast<size_t>(info.frame)]
                : nullptr;
        if (icon) {
            for (int iy = 0; iy < icon->height; ++iy)
                for (int ix = 0; ix < icon->width; ++ix) {
                    const uint8_t idx = icon->pixels[static_cast<size_t>(iy) * icon->width + ix];
                    if (idx != 0) put(rgb, w, h, b.x + ix, b.y + iy, palette->colors[idx]);
                }
        } else {
            fill_rect(rgb, w, h, b, kButtonFill);
            stroke_rect(rgb, w, h, b, kButtonEdge);
        }
        if (!info.available) {
            for (int y = b.y; y < b.y + b.h; ++y)
                for (int x = b.x; x < b.x + b.w; ++x) {
                    const size_t k = (static_cast<size_t>(y) * w + x) * 3;
                    rgb[k] = static_cast<uint8_t>(rgb[k] / 3);
                    rgb[k + 1] = static_cast<uint8_t>(rgb[k + 1] / 3);
                    rgb[k + 2] = static_cast<uint8_t>(rgb[k + 2] / 3);
                }
        }
        // The original shows the chosen command only by the pointer; a thin frame keeps it findable.
        if (i == selected) stroke_rect(rgb, w, h, Rect{b.x - 1, b.y - 1, b.w + 2, b.h + 2}, kSelectedEdge);
    }
    // The funds (0x212A1): five digits in the game's font at (268, 184), padded with spaces.
    const std::string digits = [&]() {
        std::string s = std::to_string(std::max(0, funds));
        if (s.size() > 5) s = s.substr(s.size() - 5);
        return std::string(5 - s.size(), ' ') + s;
    }();
    if (font)
        draw_game_text(rgb, w, h, kBarFundsX, kBarFundsY, digits.c_str(), 1, *font);
    else
        draw_text(rgb, w, h, kBarFundsX, kBarFundsY, digits.c_str(), 1, kLabelText);
}

// The panel's button tables (DS:0x1178, 0x11BA, 0x11FC, 0x123E) hold 6-byte records, a POINTERS.PL8 frame word and then
// a far pointer to a click handler -- but the click code (0x12140-0x122F9) reads `lcall [slot * 6 + 0x116E]`, with
// slots numbered from 1, which is the *previous* record's pointer. So the handler in record j belongs to the button
// drawn in slot j + 1: the first button of pages 1 and 2 is the "back" arrow (frame 28, the handler before the table is
// Main Toolbar), and every command's icon is the frame of the record after its own. Until 2026-10-02 this read each
// record's own pair, which put a Temple on the Road's icon and every building after it one icon off.
// Checked against the main-game capture: slots 1-10 of page 1 are frames 7, 19, 8, 11, 12, 10, 16, 14, 17, 18.
int command_icon_frame(systems::construction::CommandId id) {
    using C = systems::construction::CommandId;
    switch (id) {
        // Page 0 (DS:0x1178): the main bar
        case C::GoToProvince: return 21;
        case C::GoToForum: return 6;
        case C::Maps: return 33;
        case C::ClearArea: return 5;
        case C::Housing: return 9;
        case C::BathHouses: return 22;
        // Page 1 (DS:0x11BA): the infrastructure page
        case C::MainToolbar: return 28;
        case C::Road: return 7;
        case C::Plaza: return 19;
        case C::ReservoirPipe: return 8;
        case C::Well: return 11;
        case C::Fountain: return 12;
        case C::Wall: return 10;
        case C::Tower: return 16;
        case C::Barracks: return 14;
        case C::Prefecture: return 17;
        case C::Forum: return 18;
        // Page 2 (DS:0x11FC): the culture page
        case C::Temple: return 23;
        case C::Hospital: return 30;
        case C::School: return 32;
        case C::Oracle: return 31;
        case C::HeavyIndustry: return 20;
        case C::Market: return 34;
        case C::Workshop: return 27;
        case C::Theater: return 24;
        case C::Coliseum: return 25;
        case C::Hippodrome: return 26;
        // The province page (DS:0x123E)
        case C::Fort: return 15;
        case C::CohortPatrol: return 36;
        case C::CohortAttack: return 37;
        case C::CohortGoHome: return 38;
        default: return -1;
    }
}

}  // namespace gaius::ui
