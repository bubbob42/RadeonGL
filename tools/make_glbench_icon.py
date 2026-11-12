#!/usr/bin/env python3
"""
make_glbench_icon -- icons/glbench.info and icons/glbench-burn.info, the
GLBench launchers' icons (IconX projects).

A dial gauge in the four stock Workbench pens (0 grey, 1 black, 2 white,
3 blue): the needle in the middle for GLBench, hard over in the blue zone
for GLBench Burn. Workbench highlights them by complementing. The default
tool is C:IconX; its window stays open on the result (WAIT).

    python3 -I tools/make_glbench_icon.py
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from amiga_icon import Canvas, write_info, WBPROJECT

GREY, BLACK, WHITE, BLUE = 0, 1, 2, 3
W, H = 44, 24
CX, CY = 21.5, 19.0     # the dial's centre (pixels are twice as tall as wide)
RX, RY = 19.0, 15.5


def inside(x, y, rx, ry):
    dx = (x + 0.5 - CX) / rx
    dy = (y + 0.5 - CY) / ry
    return dx * dx + dy * dy <= 1.0 and y + 0.5 <= CY + 0.5


def make(angle):
    c = Canvas(W, H, bg=GREY)
    # the dial: a white half disc, a black rim, the blue zone at the top end
    for y in range(H):
        for x in range(W):
            if inside(x, y, RX, RY):
                c.set(x, y, WHITE)
                a = math.degrees(math.atan2(CY - (y + 0.5), (x + 0.5) - CX))
                r = ((x + 0.5 - CX) / RX) ** 2 + ((y + 0.5 - CY) / RY) ** 2
                if a < 42 and 0.42 < r < 0.78:
                    c.set(x, y, BLUE)
                if not inside(x, y, RX - 2.4, RY - 1.7):
                    c.set(x, y, BLACK)
    c.hline(int(CX - RX), int(CX + RX), int(CY), BLACK)
    # ticks
    for k in range(7):
        a = math.radians(180 - k * 30)
        for t in (0.62, 0.70):
            x = int(round(CX + math.cos(a) * RX * t - 0.5))
            y = int(round(CY - math.sin(a) * RY * t - 0.5))
            c.set(x, y, BLACK)
    # the needle, two pixels wide, from the hub
    a = math.radians(angle)
    for i in range(40):
        t = i / 39.0 * 0.78
        x = CX + math.cos(a) * RX * t
        y = CY - math.sin(a) * RY * t
        c.set(int(x - 0.5), int(y - 0.5), BLACK)
        c.set(int(x + 0.5), int(y - 0.5), BLACK)
    c.fill_rect(int(CX) - 2, int(CY) - 1, int(CX) + 1, int(CY), BLACK)
    # a base under the dial
    c.fill_rect(int(CX - RX) - 1, int(CY) + 2, int(CX + RX) + 1, int(CY) + 3, BLACK)
    c.hline(int(CX - RX) - 1, int(CX + RX) + 1, int(CY) + 1, WHITE)
    return c


def main():
    tt = ["WINDOW=CON:0/20/640/200/GLBench/CLOSE/WAIT"]
    write_info("icons/glbench.info", make(100), do_type=WBPROJECT, default_tool="C:IconX",
               tooltypes=tt, stack=8192)
    write_info("icons/glbench-burn.info", make(18), do_type=WBPROJECT, default_tool="C:IconX",
               tooltypes=tt, stack=8192)
    print("wrote icons/glbench.info, icons/glbench-burn.info")


if __name__ == "__main__":
    main()
