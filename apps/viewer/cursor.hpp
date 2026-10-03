// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius -- apps/viewer/cursor.hpp
//
// Gaius own mouse pointer: a plain arrow in the original game colours -- gold with a dark brown edge and a
// lighter rim on its lit side, like the stone panels -- drawn from a few lines, with no game art, so it shows
// before the game files are found. render_cursor() rasterises it at any size (with 3 x 3 supersampling) into
// straight RGBA; the viewer hands the 32 x 32 (or larger, on a scaled display) image to SDL as a system cursor, so it
// stays sharp at any window size and follows the mouse with no lag. The original pointer (POINTERS.PL8 frame 0, a
// striped orange staff) stays available in the Settings screen, drawn into the picture as before.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gaius::viewer {

struct CursorImage {
    int size = 0;           // width and height in pixels
    int hot_x = 0, hot_y = 0;  // the pixel the click lands on: the arrow tip
    std::vector<uint8_t> rgba;  // size * size * 4, straight (not premultiplied) alpha
};

namespace cursor_detail {

struct Pt {
    double x, y;
};

// The arrow, on a 32 x 32 canvas: the tip at (5, 3), the usual pointer outline (a notched tail) a little over 15 wide
// and 24 tall. Counter-clockwise from the tip: edges 0 (tip to the bottom left) and the last (back from the right
// wing) are the lit sides.
inline const std::vector<Pt>& arrow() {
    static const std::vector<Pt> pts = {{5.0, 3.0},   {5.0, 23.0},  {10.0, 18.4}, {13.6, 27.0},
                                        {18.6, 24.9}, {14.6, 16.9}, {20.5, 16.9}};
    return pts;
}

inline bool inside(const std::vector<Pt>& poly, double x, double y) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Pt& a = poly[i];
        const Pt& b = poly[j];
        if (((a.y > y) != (b.y > y)) && (x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)) in = !in;
    }
    return in;
}

// The distance from (x, y) to the nearest edge, and which edge that is.
inline double edge_distance(const std::vector<Pt>& poly, double x, double y, size_t& nearest) {
    double best = 1e9;
    for (size_t i = 0; i < poly.size(); ++i) {
        const Pt& a = poly[i];
        const Pt& b = poly[(i + 1) % poly.size()];
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double t = std::clamp(((x - a.x) * dx + (y - a.y) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
        const double d = std::hypot(x - (a.x + t * dx), y - (a.y + t * dy));
        if (d < best) {
            best = d;
            nearest = i;
        }
    }
    return best;
}

}  // namespace cursor_detail

inline CursorImage render_cursor(int size) {
    using namespace cursor_detail;
    CursorImage img;
    img.size = std::max(8, size);
    img.rgba.assign(static_cast<size_t>(img.size) * img.size * 4, 0);
    const std::vector<Pt>& poly = arrow();
    const double scale = img.size / 32.0;
    const double edge = 1.5;       // the dark edge, inside the outline, in 32-unit pixels
    const double rim = 1.2;        // the lighter rim just inside it, on the lit sides
    const double shadow_dx = 1.3, shadow_dy = 1.7;
    constexpr int kSub = 3;
    for (int py = 0; py < img.size; ++py)
        for (int px = 0; px < img.size; ++px) {
            double r = 0, g = 0, b = 0, a = 0;  // premultiplied sums
            for (int sy = 0; sy < kSub; ++sy)
                for (int sx = 0; sx < kSub; ++sx) {
                    const double x = (px + (sx + 0.5) / kSub) / scale;
                    const double y = (py + (sy + 0.5) / kSub) / scale;
                    double cr = 0, cg = 0, cb = 0, ca = 0;
                    if (inside(poly, x, y)) {
                        size_t nearest = 0;
                        const double d = edge_distance(poly, x, y, nearest);
                        if (d < edge) {
                            cr = 52, cg = 30, cb = 10, ca = 1;  // the dark brown edge
                        } else {
                            // Gold, darker towards the foot of the arrow.
                            const double t = std::clamp((y - 3.0) / 24.0, 0.0, 1.0);
                            cr = 255 - 33 * t, cg = 214 - 74 * t, cb = 102 - 74 * t;
                            const bool lit = nearest == 0 || nearest == poly.size() - 1;
                            if (lit && d < edge + rim) cr = 255, cg = 238, cb = 168;
                            ca = 1;
                        }
                    } else if (inside(poly, x - shadow_dx, y - shadow_dy)) {
                        cr = cg = cb = 0, ca = 0.30;  // a soft shadow down and to the right
                    }
                    r += cr * ca, g += cg * ca, b += cb * ca, a += ca;
                }
            const double n = kSub * kSub;
            uint8_t* o = &img.rgba[(static_cast<size_t>(py) * img.size + px) * 4];
            if (a > 0) {
                o[0] = static_cast<uint8_t>(std::lround(std::clamp(r / a, 0.0, 255.0)));
                o[1] = static_cast<uint8_t>(std::lround(std::clamp(g / a, 0.0, 255.0)));
                o[2] = static_cast<uint8_t>(std::lround(std::clamp(b / a, 0.0, 255.0)));
                o[3] = static_cast<uint8_t>(std::lround(std::clamp(a / n, 0.0, 1.0) * 255.0));
            }
        }
    img.hot_x = static_cast<int>(poly[0].x * scale);
    img.hot_y = static_cast<int>(poly[0].y * scale);
    return img;
}

}  // namespace gaius::viewer
