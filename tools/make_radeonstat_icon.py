#!/usr/bin/env python3
"""
make_radeonstat_icon -- icons/radeonstat.info, RadeonStat's tool icon.

A raised panel with three gauges and a busy graph, in the four stock
Workbench pens (0 grey, 1 black, 2 white, 3 blue): the window in small.
Workbench highlights it by complementing. The Tool Types are the program's
settings, shipped in parentheses (switched off: the program's defaults
apply until the user removes the parentheses).

    python3 -I tools/make_radeonstat_icon.py [out.info]
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from amiga_icon import Canvas, write_info, WBTOOL

GREY, BLACK, WHITE, BLUE = 0, 1, 2, 3
W, H = 46, 24


def recessed(c, x0, y0, x1, y1):
    """A recessed frame with two-pixel sides, as the system draws them."""
    c.hline(x0, x1 - 1, y0, BLACK)
    c.vline(x0, y0, y1, BLACK)
    c.vline(x0 + 1, y0, y1 - 1, BLACK)
    c.hline(x0 + 1, x1, y1, WHITE)
    c.vline(x1, y0, y1, WHITE)
    c.vline(x1 - 1, y0 + 1, y1, WHITE)


def make():
    c = Canvas(W, H, bg=GREY)
    # the panel, raised
    c.hline(0, W - 2, 0, WHITE)
    c.vline(0, 0, H - 1, WHITE)
    c.vline(1, 0, H - 2, WHITE)
    c.hline(1, W - 1, H - 1, BLACK)
    c.vline(W - 1, 0, H - 1, BLACK)
    c.vline(W - 2, 1, H - 1, BLACK)
    # three gauges: busy, 3D, command
    x0, x1 = 4, W - 5
    inner = (x1 - 2) - (x0 + 2) + 1
    for y0, share in ((3, 0.70), (8, 0.45), (13, 0.85)):
        recessed(c, x0, y0, x1, y0 + 3)
        n = int(inner * share)
        c.fill_rect(x0 + 2, y0 + 1, x0 + 2 + n - 1, y0 + 2, BLUE)
    # the busy graph: columns two pixels wide
    gy0, gy1 = 18, H - 3
    recessed(c, x0, gy0, x1, gy1)
    heights = [1, 1, 2, 1, 2, 3, 2, 3, 3, 2, 3, 3, 2, 3, 3, 3, 2]
    x = x0 + 2
    for h in heights:
        if x + 1 > x1 - 2:
            break
        if h > 0:
            c.fill_rect(x, gy1 - h, x + 1, gy1 - 1, BLUE)
        x += 2
    return c


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "icons/radeonstat.info"
    write_info(out, make(), do_type=WBTOOL, stack=16384,
               tooltypes=["(INTERVAL=1)", "(LEFT=-1)", "(TOP=-1)"])
    print("wrote %s" % out)


if __name__ == "__main__":
    main()
