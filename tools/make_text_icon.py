#!/usr/bin/env python3
"""Generate icons/text.info -- the Workbench icon for plain text (readmes,
COPYING, licences).

AmigaOS 3.2's own text icon, ENVARC:Sys/def_text.info (the user's choice,
2026-11-14: "use def_text.info for copying and readme"; taken from the
A4000D, dated 24-Sep-26, 2066 bytes). The original is kept byte for byte in
icons/text/def_text.info: a WBPROJECT icon with 3.2's classic image (52 x 40,
2 planes) and its GlowIcon (46 x 46), stack 16384, NO default tool (a def_
icon: Workbench's DefIcons picks the tool), no tool types, no position.

A real .info beside a file needs a default tool, or a double-click only
brings Workbench's "no default tool" requester. So the text icon is that
original cut to its artwork (tools/icon_crop.py: the GlowIcon's transparent
margin goes, every pixel stays; the cut is verified), with the default tool
SYS:Utilities/MultiView and stack 4096, as the guide icon (make_guide_icon.py)
has them. Nothing else changes.

Usage:
    python3 tools/make_text_icon.py            # what --write would make
    python3 tools/make_text_icon.py --write    # save icons/text.info
"""

import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import icon_crop  # noqa: E402
from amiga_icon import IconFile, WBPROJECT, _string_chunk  # noqa: E402

ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "icons", "text", "def_text.info")
DEST = os.path.join(ROOT, "icons", "text.info")
TOOL = "SYS:Utilities/MultiView"
STACK = 4096


def with_tool(data, tool, stack):
    """The icon with a default tool and stack: the tool's string goes where
    the classic layout keeps it (after the images, before the tool types);
    everything before and after is kept as it was."""
    icon = IconFile(data)
    if icon.do_type != WBPROJECT:
        sys.exit("make_text_icon: not a project icon")
    if icon.default_tool is not None:
        sys.exit("make_text_icon: the original already has a default tool")
    head = bytearray(data[:icon._tt_start])
    struct.pack_into(">I", head, 50, 1)         # do_DefaultTool: present
    struct.pack_into(">I", head, 74, stack)     # do_StackSize
    out = bytes(head) + _string_chunk(tool) + data[icon._tt_start:]
    check = IconFile(out)
    if check.default_tool != tool or check.tooltypes != icon.tooltypes:
        sys.exit("make_text_icon: the result does not read back")
    return out


def build():
    data = open(SRC, "rb").read()
    cut, report = icon_crop.crop(data)
    problems = icon_crop.check(data, cut)
    if problems:
        sys.exit("make_text_icon: the cut is wrong: " + "; ".join(problems))
    return with_tool(cut, TOOL, STACK), report


def main():
    out, report = build()
    if "--write" in sys.argv:
        with open(DEST, "wb") as f:
            f.write(out)
        print("wrote %s (%d bytes; 3.2's def_text.info cut to its art: %s; default tool %s, stack %d)"
              % (DEST, len(out), report, TOOL, STACK))
    else:
        print("would write %s (%d bytes): %s -- pass --write" % (DEST, len(out), report))


if __name__ == "__main__":
    main()
