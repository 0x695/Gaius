// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/panel.hpp"

#include <algorithm>

#include "ui/font.hpp"
#include "ui/game_font.hpp"

namespace gaius::ui {

namespace {

using formats::RGB;

// The same muted stone palette as the toolbar (ui/toolbar.cpp), approximating
// the original's panels without copying their art.
constexpr RGB kScreenFill{92, 86, 62};
constexpr RGB kFrameFill{74, 70, 52};
constexpr RGB kFrameLight{140, 132, 100};
constexpr RGB kFrameDark{40, 38, 28};
constexpr RGB kButtonFill{58, 55, 40};
constexpr RGB kButtonEdge{110, 104, 78};
constexpr RGB kSelectedEdge{236, 206, 116};
constexpr RGB kHoverEdge{190, 180, 130};
constexpr RGB kText{232, 226, 200};
constexpr RGB kDim{150, 144, 118};
constexpr RGB kChartFill{48, 45, 33};
constexpr RGB kBar{196, 170, 96};
constexpr RGB kBarAlt{168, 142, 78};

void put(std::vector<uint8_t>& rgb, int w, int h, int x, int y, RGB c) {
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    const size_t i = (static_cast<size_t>(y) * w + x) * 3;
    rgb[i] = c.r;
    rgb[i + 1] = c.g;
    rgb[i + 2] = c.b;
}

void fill(std::vector<uint8_t>& rgb, int w, int h, Rect r, RGB c) {
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x) put(rgb, w, h, x, y, c);
}

void bevel(std::vector<uint8_t>& rgb, int w, int h, Rect r, RGB light, RGB dark) {
    for (int x = r.x; x < r.x + r.w; ++x) {
        put(rgb, w, h, x, r.y, light);
        put(rgb, w, h, x, r.y + r.h - 1, dark);
    }
    for (int y = r.y; y < r.y + r.h; ++y) {
        put(rgb, w, h, r.x, y, light);
        put(rgb, w, h, r.x + r.w - 1, y, dark);
    }
}

int text_w(const std::string& s, const Metrics& m, const GameFont* font) {
    return font ? game_text_width(s.c_str(), m.glyph_scale, *font) : text_width(s.c_str(), m.glyph_scale);
}

void text(std::vector<uint8_t>& rgb, int w, int h, int x, int y, const std::string& s, const Metrics& m,
          const GameFont* font, RGB color = kText) {
    if (font) {
        draw_game_text(rgb, w, h, x, y, s.c_str(), m.glyph_scale, *font);
    } else {
        draw_text(rgb, w, h, x, y, s.c_str(), m.glyph_scale, color);
    }
}

int line_h(const Metrics& m) { return 10 * m.scale; }

// Buttons laid left to right from (x0, y), wrapping inside [x0, x1).
std::vector<Rect> flow(const std::vector<PanelButton>& buttons, const Metrics& m, int x0, int x1, int y) {
    std::vector<Rect> out;
    const int pad = 3 * m.scale, gap = 2 * m.scale, bh = line_h(m) + 2 * m.scale;
    int x = x0;
    for (const PanelButton& b : buttons) {
        const int bw = static_cast<int>(b.text.size()) * 8 * m.glyph_scale + 2 * pad;
        if (x > x0 && x + bw > x1) {
            x = x0;
            y += bh + gap;
        }
        out.push_back(Rect{x, y, bw, bh});
        x += bw + gap;
    }
    return out;
}

void draw_button(std::vector<uint8_t>& rgb, int w, int h, Rect r, const PanelButton& b, bool selected, bool hovered,
                 const Metrics& m, const GameFont* font) {
    fill(rgb, w, h, r, kButtonFill);
    bevel(rgb, w, h, r, selected ? kSelectedEdge : hovered ? kHoverEdge : kButtonEdge,
          selected ? kSelectedEdge : kFrameDark);
    const int tw = text_w(b.text, m, font);
    text(rgb, w, h, r.x + (r.w - tw) / 2, r.y + (r.h - 8 * m.glyph_scale) / 2, b.text, m, font,
         b.enabled ? kText : kDim);
}

void draw_arrow(std::vector<uint8_t>& rgb, int w, int h, Rect r, bool up, bool hovered) {
    if (r.w == 0) return;
    fill(rgb, w, h, r, kButtonFill);
    bevel(rgb, w, h, r, hovered ? kHoverEdge : kButtonEdge, kFrameDark);
    // A filled triangle, drawn rather than taken from the font, which has no arrows.
    const int cx = r.x + r.w / 2, size = std::max(2, r.w / 3);
    for (int i = 0; i < size; ++i) {
        const int y = up ? r.y + r.h / 2 - size / 2 + i : r.y + r.h / 2 + size / 2 - i;
        for (int x = cx - i; x <= cx + i; ++x) put(rgb, w, h, x, y, kText);
    }
}

}  // namespace

PanelLayout layout(const Page& page, const Metrics& m, int w, int h) {
    PanelLayout lay;
    const int margin = 4 * m.scale, lh = line_h(m);
    lay.frame = Rect{margin, margin, w - 2 * margin, h - 2 * margin};
    const int x0 = lay.frame.x + 4 * m.scale, x1 = lay.frame.x + lay.frame.w - 4 * m.scale;
    int y = lay.frame.y + 3 * m.scale;
    if (!page.tabs.empty()) {
        lay.tabs = flow(page.tabs, m, x0, x1, y);
        y = lay.tabs.back().y + lay.tabs.back().h + 4 * m.scale;
    }
    lay.title = Rect{x0, y, x1 - x0, lh};
    y += lh + 3 * m.scale;
    const int arrow = lh;
    for (const PanelRow& row : page.rows) {
        lay.rows.push_back(Rect{x0, y, x1 - x0, lh});
        const bool arrows = row.down_action >= 0 || row.up_action >= 0;
        lay.up.push_back(arrows ? Rect{x1 - arrow, y, arrow, arrow} : Rect{});
        lay.down.push_back(arrows ? Rect{x1 - 2 * arrow - m.scale, y, arrow, arrow} : Rect{});
        y += lh + m.scale;
    }
    if (!page.charts.empty()) {
        const int gap = 6 * m.scale, cw = (x1 - x0 - gap) / 2, ch = 5 * lh;
        y += 2 * m.scale;
        for (size_t i = 0; i < page.charts.size(); ++i) {
            const int col = static_cast<int>(i % 2);
            lay.charts.push_back(Rect{x0 + col * (cw + gap), y, cw, ch});
            if (col == 1 || i + 1 == page.charts.size()) y += ch + 3 * m.scale;
        }
    }
    if (!page.buttons.empty()) {
        std::vector<Rect> probe = flow(page.buttons, m, x0, x1, 0);
        const int height = probe.back().y + probe.back().h;
        const int by = std::max(y + 2 * m.scale, lay.frame.y + lay.frame.h - 3 * m.scale - height);
        lay.buttons = flow(page.buttons, m, x0, x1, by);
    }
    return lay;
}

int hit_test(const Page& page, const PanelLayout& lay, int x, int y) {
    for (size_t i = 0; i < lay.tabs.size() && i < page.tabs.size(); ++i)
        if (lay.tabs[i].contains(x, y) && page.tabs[i].enabled) return page.tabs[i].action;
    for (size_t i = 0; i < lay.rows.size() && i < page.rows.size(); ++i) {
        if (lay.down[i].contains(x, y) && page.rows[i].down_action >= 0) return page.rows[i].down_action;
        if (lay.up[i].contains(x, y) && page.rows[i].up_action >= 0) return page.rows[i].up_action;
    }
    for (size_t i = 0; i < lay.buttons.size() && i < page.buttons.size(); ++i)
        if (lay.buttons[i].contains(x, y) && page.buttons[i].enabled) return page.buttons[i].action;
    return -1;
}

void render(const Page& page, const PanelLayout& lay, std::vector<uint8_t>& rgb, int w, int h, const Metrics& m,
            const GameFont* font, int hovered) {
    if (rgb.size() < static_cast<size_t>(w) * h * 3) return;
    fill(rgb, w, h, Rect{0, 0, w, h}, kScreenFill);
    fill(rgb, w, h, lay.frame, kFrameFill);
    bevel(rgb, w, h, lay.frame, kFrameLight, kFrameDark);
    for (size_t i = 0; i < lay.tabs.size(); ++i)
        draw_button(rgb, w, h, lay.tabs[i], page.tabs[i], static_cast<int>(i) == page.selected_tab,
                    page.tabs[i].action == hovered, m, font);
    text(rgb, w, h, lay.title.x + (lay.title.w - text_w(page.title, m, font)) / 2, lay.title.y, page.title, m, font);
    for (size_t i = 0; i < lay.rows.size(); ++i) {
        const PanelRow& row = page.rows[i];
        const Rect r = lay.rows[i];
        text(rgb, w, h, r.x, r.y + m.scale, row.label, m, font);
        const int right = lay.down[i].w ? lay.down[i].x - 3 * m.scale : r.x + r.w;
        text(rgb, w, h, right - text_w(row.value, m, font), r.y + m.scale, row.value, m, font);
        draw_arrow(rgb, w, h, lay.down[i], false, row.down_action >= 0 && row.down_action == hovered);
        draw_arrow(rgb, w, h, lay.up[i], true, row.up_action >= 0 && row.up_action == hovered);
    }
    for (size_t i = 0; i < lay.charts.size() && i < page.charts.size(); ++i) {
        const PanelChart& c = page.charts[i];
        const Rect r = lay.charts[i];
        const int lh = line_h(m);
        text(rgb, w, h, r.x, r.y + m.scale, c.label, m, font);
        const Rect area{r.x, r.y + lh + m.scale, r.w, r.h - 2 * lh - 2 * m.scale};
        fill(rgb, w, h, area, kChartFill);
        bevel(rgb, w, h, area, kFrameDark, kFrameLight);
        const int n = static_cast<int>(c.bars.size());
        if (n > 0 && c.max > 0) {
            const int inner_w = area.w - 4 * m.scale, inner_h = area.h - 4 * m.scale;
            const int slot = inner_w / n, bw = std::max(1, slot - m.scale);
            for (int k = 0; k < n; ++k) {
                const int v = std::min(c.bars[static_cast<size_t>(k)], c.max);
                if (v <= 0) continue;
                const int bh = std::max(1, v * inner_h / c.max);
                fill(rgb, w, h, Rect{area.x + 2 * m.scale + k * slot, area.y + 2 * m.scale + inner_h - bh, bw, bh},
                     k % 2 ? kBarAlt : kBar);
            }
        }
        text(rgb, w, h, r.x + r.w - text_w(c.caption, m, font), r.y + r.h - lh, c.caption, m, font);
    }
    for (size_t i = 0; i < lay.buttons.size(); ++i)
        draw_button(rgb, w, h, lay.buttons[i], page.buttons[i], false, page.buttons[i].action == hovered, m, font);
}

PanelLayout strip_layout(const std::vector<PanelButton>& tabs, const Metrics& m, int w) {
    PanelLayout lay;
    lay.tabs = flow(tabs, m, m.scale, w - m.scale, m.scale);
    if (!lay.tabs.empty()) lay.frame = Rect{0, 0, w, lay.tabs.back().y + lay.tabs.back().h + m.scale};
    return lay;
}

void render_strip(const std::vector<PanelButton>& tabs, int selected, const PanelLayout& lay, std::vector<uint8_t>& rgb,
                  int w, int h, const Metrics& m, const GameFont* font) {
    if (rgb.size() < static_cast<size_t>(w) * h * 3) return;
    for (size_t i = 0; i < lay.tabs.size() && i < tabs.size(); ++i)
        draw_button(rgb, w, h, lay.tabs[i], tabs[i], static_cast<int>(i) == selected, false, m, font);
}

PanelLayout bar_layout(const std::vector<PanelButton>& buttons, const Metrics& m, int w, int h) {
    PanelLayout lay;
    std::vector<Rect> probe = flow(buttons, m, 2 * m.scale, w - 2 * m.scale, 0);
    const int height = probe.empty() ? 0 : probe.back().y + probe.back().h;
    const int label = line_h(m);
    const int top = h - height - label - 3 * m.scale;
    lay.frame = Rect{0, top, w, h - top};
    lay.title = Rect{2 * m.scale, top + m.scale, w - 4 * m.scale, label};
    lay.buttons = flow(buttons, m, 2 * m.scale, w - 2 * m.scale, top + label + m.scale);
    return lay;
}

void render_bar(const std::vector<PanelButton>& buttons, int selected, const PanelLayout& lay, std::vector<uint8_t>& rgb,
                int w, int h, const Metrics& m, const GameFont* font, const char* label) {
    if (rgb.size() < static_cast<size_t>(w) * h * 3) return;
    fill(rgb, w, h, lay.frame, kFrameFill);
    bevel(rgb, w, h, lay.frame, kFrameLight, kFrameDark);
    if (label) {
        const std::string s = label;
        text(rgb, w, h, lay.title.x + (lay.title.w - text_w(s, m, font)) / 2, lay.title.y + m.scale, s, m, font);
    }
    for (size_t i = 0; i < lay.buttons.size() && i < buttons.size(); ++i)
        draw_button(rgb, w, h, lay.buttons[i], buttons[i], static_cast<int>(i) == selected, false, m, font);
}

}  // namespace gaius::ui
