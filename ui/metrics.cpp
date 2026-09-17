// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/metrics.hpp"

namespace gaius::ui {

const char* breakpoint_name(Breakpoint b) {
    switch (b) {
        case Breakpoint::Desktop: return "desktop";
        case Breakpoint::Handheld: return "handheld";
        case Breakpoint::Phone: return "phone";
        case Breakpoint::Tv: return "tv";
    }
    return "?";
}

Breakpoint breakpoint_for(int physical_w, int physical_h, bool has_touch) {
    // No touchscreen: a pointer device, so the original's 16px icons are
    // appropriate and the window's size says nothing useful. Tv is never
    // inferred -- see the header.
    if (!has_touch) return Breakpoint::Desktop;

    // Touch present. The remaining phone-vs-handheld split IS a guess:
    // the shorter edge is used so orientation doesn't flip the answer,
    // and 600 separates phone-class panels from Steam-Deck-class ones
    // (1280x800) in the masterplan's target matrix. Both land on the same
    // 2x scale today, so a wrong answer here is currently cosmetic.
    int shorter = physical_w < physical_h ? physical_w : physical_h;
    return shorter <= 600 ? Breakpoint::Phone : Breakpoint::Handheld;
}

Metrics metrics_for(Breakpoint b) {
    switch (b) {
        case Breakpoint::Desktop: return metrics_for_scale(1);
        case Breakpoint::Handheld:
        case Breakpoint::Phone: return metrics_for_scale(2);
        case Breakpoint::Tv: return metrics_for_scale(3);
    }
    return metrics_for_scale(1);
}

Metrics metrics_for_scale(int scale) {
    Metrics m;
    m.scale = scale < 1 ? 1 : scale > 4 ? 4 : scale;
    // Everything scales off the one factor -- this is the "resizes icons,
    // text, and hit targets together" requirement expressed as code -- except
    // the label text, which stops at 2x so the label row stays one line.
    m.icon_px = kOriginalIconPx * m.scale;
    m.pad_px = 1 * m.scale;
    m.gap_px = 1 * m.scale;
    m.glyph_scale = m.scale < 2 ? m.scale : 2;
    m.label_h = 8 * m.glyph_scale;
    return m;
}

}  // namespace gaius::ui
