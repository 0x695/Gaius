#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draws Gaius's README banner, docs/images/banner.svg.

Like the icon it is Gaius's own art -- the same gold laurel wreath and G on Roman crimson, drawn from shapes (the
geometry comes from make_icon.py), with the name set in whatever serif the viewer has. No game assets, no font files.

    python packaging/icon/make_banner.py
"""

import math
import os

import make_icon as icon

ROOT = icon.ROOT
W, H = 1280, 300


def hexc(c):
    return "#%02X%02X%02X" % tuple(int(round(v)) for v in c)


def emblem():
    """The wreath and the G, in the unit square (0,0)-(1,1) of the icon."""
    out = []
    # leaves
    for lx, ly, rot, a, b in icon.laurel_leaves(8):
        out.append('<ellipse cx="%.4f" cy="%.4f" rx="%.4f" ry="%.4f" transform="rotate(%.2f %.4f %.4f)"/>' %
                   (lx, ly, a, b, math.degrees(rot), lx, ly))
    leaves = "\n      ".join(out)

    cx, cy = icon.CENTRE
    r, w = 0.205, 0.125
    a = math.radians(48.0)
    sx, sy = cx + r * math.cos(a), cy - r * math.sin(a)
    ex, ey = cx + r, cy
    arc = 'M %.4f %.4f A %.4f %.4f 0 1 0 %.4f %.4f' % (sx, sy, r, r, ex, ey)
    bar_h = w * 0.62
    bar = '<rect x="%.4f" y="%.4f" width="%.4f" height="%.4f" fill="@C@" stroke="none"/>' % (
        cx + 0.012, cy - bar_h / 2, r + w / 2 * 0.9 - 0.012, bar_h)
    letter = ('<path d="%s" fill="none" stroke="@C@" stroke-width="%.4f" stroke-linecap="round"/>\n      %s' %
              (arc, w, bar))
    return leaves, letter


def build():
    leaves, letter = emblem()
    gold_top, gold_bottom, edge = hexc(icon.GOLD_TOP), hexc(icon.GOLD_BOTTOM), hexc(icon.GOLD_EDGE)
    crimson_top, crimson_bottom = hexc(icon.CRIMSON_TOP), hexc(icon.CRIMSON_BOTTOM)
    size = 244
    ex, ey = 56, (H - size) / 2
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" role="img" aria-label="Gaius, an open-source engine for Caesar (1992)">
  <title>Gaius</title>
  <defs>
    <linearGradient id="ground" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="{crimson_top}"/>
      <stop offset="1" stop-color="{crimson_bottom}"/>
    </linearGradient>
    <linearGradient id="goldUnit" gradientUnits="userSpaceOnUse" x1="0" y1="0.1" x2="0" y2="0.9">
      <stop offset="0" stop-color="{gold_top}"/>
      <stop offset="1" stop-color="{gold_bottom}"/>
    </linearGradient>
    <linearGradient id="goldText" gradientUnits="userSpaceOnUse" x1="0" y1="70" x2="0" y2="190">
      <stop offset="0" stop-color="{gold_top}"/>
      <stop offset="1" stop-color="{gold_bottom}"/>
    </linearGradient>
  </defs>
  <rect width="{W}" height="{H}" rx="26" fill="url(#ground)"/>
  <rect x="10" y="10" width="{W - 20}" height="{H - 20}" rx="18" fill="none" stroke="{edge}" stroke-width="2" opacity="0.85"/>
  <g transform="translate({ex} {ey:.0f}) scale({size})">
    <g fill="url(#goldUnit)">
      {leaves}
    </g>
    <g transform="translate(0.012 0.012)" opacity="0.55">
      {letter.replace("@C@", "#260606")}
    </g>
    <g>
      {letter.replace("@C@", "url(#goldUnit)")}
    </g>
  </g>
  <g font-family="'Trajan Pro','Cinzel','Palatino Linotype','Book Antiqua',Palatino,Georgia,'Times New Roman',serif" font-weight="700">
    <text x="403" y="193" font-size="136" textLength="580" lengthAdjust="spacing" fill="#260606" opacity="0.55">GAIUS</text>
    <text x="400" y="190" font-size="136" textLength="580" lengthAdjust="spacing" fill="url(#goldText)">GAIUS</text>
  </g>
  <line x1="404" y1="212" x2="1224" y2="212" stroke="{edge}" stroke-width="1.5" opacity="0.6"/>
  <text x="404" y="252" font-family="'Palatino Linotype','Book Antiqua',Palatino,Georgia,'Times New Roman',serif" font-size="23" textLength="820" lengthAdjust="spacing" fill="#EBD9A9" opacity="0.92">AN OPEN-SOURCE ENGINE FOR CAESAR (1992)</text>
</svg>
"""


def main():
    path = os.path.join(ROOT, "docs", "images", "banner.svg")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(build())
    print("wrote docs/images/banner.svg")


if __name__ == "__main__":
    main()
