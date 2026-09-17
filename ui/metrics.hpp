// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — ui/metrics.hpp
//
// The single source of UI sizing, implementing GAIUS_MASTERPLAN.md
// section 5a point 3:
//
//   "A single scale factor (or a small number of breakpoints: phone /
//    handheld / desktop / tv-distance) that resizes toolbar icons, text,
//    and hit targets together -- this is what makes the same UI usable on
//    a 6" phone and a 34" monitor. Original toolbar icon sizes are a
//    floor, not a ceiling, on touch targets."
//
// The "together" is the load-bearing word, and it's why this is a struct
// rather than a pile of constants: every size the toolbar uses -- icon
// edge, button box, gap, padding, text scale, panel height -- is derived
// from one `scale`, and BOTH the renderer and the hit-tester read the
// same computed values (see ui::Toolbar, whose hit_test() literally
// iterates the same button_rect() the renderer draws). It is therefore
// not possible for the drawn button and its touch target to drift apart,
// which is the bug this design exists to prevent.
//
// The original's numbers below are MEASURED, not assumed -- see the
// constants' comments for exactly what was measured and from which file.

#pragma once

namespace gaius::ui {

// --- Measured from the real game, not invented -------------------------
//
// kOriginalPanelTop/kOriginalPanelH: PANEL1.VPX decodes (via
// formats::vpx + PANEL1.P32, both of which agree with PANEL1.256) to a
// 320x200 image whose control panel occupies rows 176..199 inclusive --
// full width, 24 rows tall, anchored to the bottom. Measured by scanning
// for non-index-0 rows in the decoded output.
//
// kOriginalIconPx: P_BLOCKS.PL8 ("panel blocks") is 40 frames of exactly
// 16x16. 16px icons inside a 24px bar leaves 4px above and below, which
// is consistent with the decoded panel artwork.
//
// Confidence: HIGH for the panel geometry (directly measured from a
// correctly-decoded image). STRONG INFERENCE for "16x16 is the toolbar
// *icon* size" -- P_BLOCKS' frames are certainly 16x16 and certainly
// panel content, but the frames themselves decode as texture/pattern
// blocks rather than recognisable per-building icons, so the link from
// "16x16 panel block" to "16x16 toolbar button icon" is inferred from
// the size fitting the measured bar, not proven. Nothing here depends on
// that inference being exactly right -- it only sets the floor below.
constexpr int kOriginalScreenW = 320;
constexpr int kOriginalScreenH = 200;
constexpr int kOriginalPanelTop = 176;
constexpr int kOriginalPanelH = 24;
constexpr int kOriginalIconPx = 16;

// Breakpoints, exactly the four the masterplan names.
enum class Breakpoint {
    Desktop,   // mouse: precise, so the original 16px icon size is fine
    Handheld,  // Steam Deck: touch + gamepad, small screen
    Phone,     // smallest safe-area budget of any target
    Tv,        // gamepad at distance: legibility, not touch, drives size
};

const char* breakpoint_name(Breakpoint b);

// Everything the toolbar draws or hit-tests, derived from one scale.
struct Metrics {
    int scale = 1;
    int icon_px = kOriginalIconPx;  // the building-footprint miniature
    int pad_px = 1;                 // inside a button, around the icon
    int gap_px = 1;                 // between buttons
    int glyph_scale = 1;            // text pixel size (see ui/font.hpp)
    int label_h = 8;                // reserved height for the label row

    // The actual touch/click target: the icon plus its padding. This is
    // the value the masterplan's "floor" clause constrains.
    int button_px() const { return icon_px + 2 * pad_px; }
};

// Picks a breakpoint for the current device.
//
// `has_touch` is the load-bearing input, not the window size. Resolution
// alone cannot distinguish a 960x600 window on a desktop from a 960x600
// handheld -- an earlier version of this function tried, and classified
// the viewer's own default desktop window as a handheld, handing a mouse
// user 36px buttons and a panel filling half the screen. Whether a
// touchscreen exists is a fact SDL can actually report
// (SDL_GetNumTouchDevices), so the touch/no-touch split is a real signal
// and only the phone-vs-handheld size cut below it remains a guess.
//
// Tv is deliberately never returned: a 4K monitor and a television are
// indistinguishable from pixels, and guessing wrong means a desktop user
// gets television-sized controls. It is opt-in via --ui-scale only.
//
// Keeping this SDL-free (the caller passes the flag in) is what lets ui/
// stay headlessly testable.
Breakpoint breakpoint_for(int physical_w, int physical_h, bool has_touch);

// The scale table. Desktop is 1x -- i.e. exactly the original's 16px
// icons, which is the masterplan's stated floor and is comfortable for a
// mouse. Touch targets go above that floor, never below it.
//
// Phone and Handheld land on 2x: the logical framebuffer is 320 x 200 and
// already scaled up to fill the screen, so 2x buttons are comfortable touch
// targets there. Tv gets 3x, paged (ui/toolbar.hpp), for reading at a
// distance; the Settings screen's UI scale can pick any of 1-4.
Metrics metrics_for(Breakpoint b);

// Metrics at an explicit scale, 1-4 (the Settings screen's UI scale).
// Past 2x the toolbar pages rather than wrapping (ui/toolbar.hpp). The
// toolbar's label text stops growing at 2x, so it stays one line.
Metrics metrics_for_scale(int scale);

}  // namespace gaius::ui
