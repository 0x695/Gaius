#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draws Gaius's icon and writes every file that carries it.

The art is Gaius's own: a gold laurel wreath round a gold letter G on Roman
crimson, drawn here from a few shapes (no game assets, no fonts), so the files
it writes can live in the repository. Standard library only.

    python packaging/icon/make_icon.py

Writes, relative to the repository root:
  packaging/icon/gaius.ico                the Windows icon (16-256 px, PNG entries)
  packaging/icon/gaius.png                256 px, for Linux desktop entries and the docs
  apps/viewer/window_icon.hpp             64 px RGBA, for SDL_SetWindowIcon (every platform)
  android/app/src/main/res/mipmap-*/ic_launcher.png   the Android launcher icons
"""

import math
import os
import struct
import zlib

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

CRIMSON_TOP = (158, 38, 30)
CRIMSON_BOTTOM = (84, 16, 15)
GOLD_TOP = (255, 226, 138)
GOLD_BOTTOM = (205, 148, 50)
GOLD_EDGE = (232, 184, 84)
SHADOW = (38, 6, 6)


def lerp(a, b, t):
    t = min(1.0, max(0.0, t))
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


# --- The shapes, in unit-square coordinates (0,0 top left, 1,1 bottom right) ---

CENTRE = (0.5, 0.5)


def rounded_square(x, y, inset, radius):
    """Inside the rounded square that is the icon's body."""
    lo, hi = inset, 1.0 - inset
    if x < lo or x > hi or y < lo or y > hi:
        return False
    dx = max(lo + radius - x, 0.0, x - (hi - radius))
    dy = max(lo + radius - y, 0.0, y - (hi - radius))
    return dx * dx + dy * dy <= radius * radius


def border_ring(x, y, inset, radius, width):
    return rounded_square(x, y, inset, radius) and not rounded_square(x, y, inset + width, max(0.0, radius - width))


def g_letter(x, y, r, w):
    """A capital G: an arc open on the upper right, and a bar."""
    cx, cy = CENTRE
    dx, dy = x - cx, y - cy
    d = math.hypot(dx, dy)
    half = w / 2
    if abs(d - r) <= half:
        angle = math.degrees(math.atan2(-dy, dx)) % 360.0
        if angle >= 48.0:
            return True
        # the round end of the arc's upper terminal
        a = math.radians(48.0)
        ex, ey = cx + r * math.cos(a), cy - r * math.sin(a)
        if math.hypot(x - ex, y - ey) <= half:
            return True
    # the upper terminal's cap when the point is just off the ring
    a = math.radians(48.0)
    ex, ey = cx + r * math.cos(a), cy - r * math.sin(a)
    if math.hypot(x - ex, y - ey) <= half:
        return True
    # the bar, from a little right of the middle to the arc
    bar_h = w * 0.62
    if cx + 0.012 <= x <= cx + r + half * 0.9 and abs(y - cy) <= bar_h / 2:
        return True
    return False


def laurel_leaves(count):
    """Leaves of the wreath, as (cx, cy, rotation, semi_major, semi_minor): two branches that start at the bottom and
    climb the sides, a leaf on each side of the stem at each step."""
    leaves = []
    ring = 0.356
    for mirror in (1, -1):  # 1 the left branch, -1 the right
        for k in range(count):
            t = k / (count - 1)
            theta = math.radians(262.0 - t * 150.0)  # counter-clockwise from the right: bottom, up the side, near the top
            px = CENTRE[0] + mirror * ring * math.cos(theta)
            py = CENTRE[1] - ring * math.sin(theta)
            heading = math.atan2(ring * math.cos(theta), mirror * ring * math.sin(theta))  # the way up the branch
            nx, ny = px - CENTRE[0], py - CENTRE[1]
            norm = math.hypot(nx, ny)
            outward = math.atan2(ny, nx)
            turn = (outward - heading + math.pi) % (2 * math.pi) - math.pi  # which way is out from the heading
            for outer in (True, False):
                tilt = math.radians(34.0) * (1 if turn > 0 else -1) * (1 if outer else -1)
                shift = 0.026 * (1 if outer else -1)
                size = 0.052 - 0.012 * t  # smaller towards the tips
                leaves.append((px + nx / norm * shift, py + ny / norm * shift, heading + tilt, size, size * 0.42))
    return leaves


def in_leaf(x, y, leaf):
    lx, ly, rot, a, b = leaf
    dx, dy = x - lx, y - ly
    if dx * dx + dy * dy > a * a:
        return False
    c, s = math.cos(-rot), math.sin(-rot)
    u, v = dx * c - dy * s, dx * s + dy * c
    return (u / a) ** 2 + (v / b) ** 2 <= 1.0


def render(size, detail):
    """RGBA bytes of the icon at `size` px. `detail` is 2 (everything), 1 (no wreath) or 0 (letter only)."""
    ss = 3 if size >= 128 else 5 if size >= 48 else 7
    leaves = laurel_leaves(8) if detail >= 2 else []
    r_g = 0.205 if detail >= 2 else 0.26
    w_g = 0.125 if detail >= 2 else 0.17
    shadow = 0.012
    out = bytearray()
    for py in range(size):
        for px in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]  # premultiplied r, g, b, a
            for sy in range(ss):
                for sx in range(ss):
                    x = (px + (sx + 0.5) / ss) / size
                    y = (py + (sy + 0.5) / ss) / size
                    if not rounded_square(x, y, 0.0, 0.2):
                        continue
                    colour = lerp(CRIMSON_TOP, CRIMSON_BOTTOM, y)
                    d = math.hypot(x - 0.5, y - 0.5)
                    if detail >= 1 and border_ring(x, y, 0.035, 0.17, 0.016):
                        colour = GOLD_EDGE
                    elif detail >= 2 and 0.27 < d < 0.47 and any(in_leaf(x, y, lf) for lf in leaves):
                        colour = lerp(GOLD_TOP, GOLD_BOTTOM, (y - 0.1) / 0.8)
                    # the letter, with a shadow underneath
                    if d < 0.36:
                        if g_letter(x, y, r_g, w_g):
                            colour = lerp(GOLD_TOP, GOLD_BOTTOM, (y - 0.25) / 0.5)
                        elif g_letter(x - shadow, y - shadow, r_g, w_g):
                            colour = lerp(colour, SHADOW, 0.55)
                    acc[0] += colour[0]
                    acc[1] += colour[1]
                    acc[2] += colour[2]
                    acc[3] += 1.0
            n = ss * ss
            if acc[3] == 0:
                out += bytes((0, 0, 0, 0))
            else:
                out += bytes((round(acc[0] / acc[3]), round(acc[1] / acc[3]), round(acc[2] / acc[3]),
                              round(255 * acc[3] / n)))
    return bytes(out)


def detail_for(size):
    return 2 if size >= 48 else 1 if size >= 24 else 0


# --- File formats ------------------------------------------------------------

def png_bytes(size, rgba):
    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + rgba[y * size * 4:(y + 1) * size * 4] for y in range(size))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def ico_bytes(images):
    """images: {size: png bytes}."""
    sizes = sorted(images)
    head = struct.pack("<HHH", 0, 1, len(sizes))
    entries = b""
    offset = 6 + 16 * len(sizes)
    body = b""
    for s in sizes:
        data = images[s]
        entries += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
        body += data
    return head + entries + body


def write(path, data, mode="wb"):
    full = os.path.join(ROOT, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, mode) as f:
        f.write(data)
    print("wrote", path)


def main():
    cache = {}

    def at(size):
        if size not in cache:
            cache[size] = render(size, detail_for(size))
        return cache[size]

    ico_sizes = (256, 128, 64, 48, 32, 24, 16)
    write("packaging/icon/gaius.ico", ico_bytes({s: png_bytes(s, at(s)) for s in ico_sizes}))
    write("packaging/icon/gaius.png", png_bytes(256, at(256)))

    # SDL_SetWindowIcon: 64 x 64, one little-endian word a pixel (bytes R, G, B, A)
    words = struct.unpack("<%dI" % (64 * 64), at(64))
    lines = []
    for i in range(0, len(words), 8):
        lines.append("    " + ", ".join("0x%08X" % w for w in words[i:i + 8]) + ",")
    header = ("// SPDX-License-Identifier: GPL-3.0-or-later\n"
              "// Gaius -- apps/viewer/window_icon.hpp: the window icon, 64 x 64, one word a pixel (bytes R, G, B, A).\n"
              "// Generated by packaging/icon/make_icon.py -- change the script, not this file.\n\n"
              "#pragma once\n\n#include <cstdint>\n\nnamespace gaius::viewer {\n\n"
              "inline constexpr int kWindowIconSize = 64;\n"
              "inline constexpr uint32_t kWindowIcon[kWindowIconSize * kWindowIconSize] = {\n" +
              "\n".join(lines) + "\n};\n\n}  // namespace gaius::viewer\n")
    write("apps/viewer/window_icon.hpp", header, "w")

    for folder, size in (("mdpi", 48), ("hdpi", 72), ("xhdpi", 96), ("xxhdpi", 144), ("xxxhdpi", 192)):
        write("android/app/src/main/res/mipmap-%s/ic_launcher.png" % folder, png_bytes(size, at(size)))


if __name__ == "__main__":
    main()
