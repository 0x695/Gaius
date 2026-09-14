// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/toolbar.hpp"

#include "ui/game_font.hpp"

#include <algorithm>
#include <string>

#include "ui/font.hpp"

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

Toolbar::Toolbar(const systems::construction::CommandId* tools, int count, Metrics m, int screen_w, int screen_h)
    : tools_(tools, tools + std::max(0, count)), m_(m) {
    const int pad = std::max(1, m_.pad_px);
    const int stride = m_.button_px() + m_.gap_px;
    const int avail = std::max(m_.button_px(), screen_w - 2 * pad);

    cols_ = std::max(1, (avail + m_.gap_px) / stride);
    cols_ = std::min(cols_, std::max(1, count));
    rows_ = std::max(1, (std::max(1, count) + cols_ - 1) / cols_);

    const int grid_w = cols_ * stride - m_.gap_px;
    const int grid_h = rows_ * stride - m_.gap_px;
    const int label_block = m_.label_h + pad;
    const int panel_h = pad + label_block + grid_h + pad;

    panel_ = Rect{0, screen_h - panel_h, screen_w, panel_h};
    grid_x0_ = (screen_w - grid_w) / 2;
    grid_y0_ = panel_.y + pad + label_block;
}

Rect Toolbar::button(int i) const {
    if (i < 0 || i >= count()) return Rect{};
    const int stride = m_.button_px() + m_.gap_px;
    const int col = i % cols_;
    const int row = i / cols_;
    return Rect{grid_x0_ + col * stride, grid_y0_ + row * stride, m_.button_px(), m_.button_px()};
}

int Toolbar::hit_test(int lx, int ly) const {
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
    const char* label = (label_for >= 0 && label_for < bar.count())
                            ? systems::construction::command_name(bar.tool(label_for))
                            : "SELECT A BUILDING";
    const char* shown = (selected_label && label_for == selected) ? selected_label : label;

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

    for (int i = 0; i < bar.count(); ++i) {
        Rect b = bar.button(i);
        fill_rect(rgb, w, h, b, kButtonFill);

        Rect inner{b.x + m.pad_px, b.y + m.pad_px, b.w - 2 * m.pad_px, b.h - 2 * m.pad_px};
        const int frame = command_icon_frame(bar.tool(i));
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

int command_icon_frame(systems::construction::CommandId id) {
    using C = systems::construction::CommandId;
    switch (id) {
        // Page 0 (DS:0x1178)
        case C::ClearArea: return 33;
        case C::Housing: return 5;
        case C::BathHouses: return 9;
        // Page 1 (DS:0x11BA)
        case C::Road: return 28;
        case C::Plaza: return 7;
        case C::ReservoirPipe: return 19;
        case C::Well: return 8;
        case C::Fountain: return 11;
        case C::Wall: return 12;
        case C::Tower: return 10;
        case C::Barracks: return 16;
        case C::Prefecture: return 14;
        case C::Forum: return 17;
        // Page 2 (DS:0x11FC)
        case C::Temple: return 28;
        case C::Hospital: return 23;
        case C::School: return 30;
        case C::Oracle: return 32;
        case C::HeavyIndustry: return 31;
        case C::Market: return 20;
        case C::Workshop: return 34;
        case C::Theater: return 27;
        case C::Coliseum: return 24;
        case C::Hippodrome: return 25;
        default: return -1;
    }
}

}  // namespace gaius::ui
