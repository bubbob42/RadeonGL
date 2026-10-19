#!/usr/bin/env python3
"""set_icon_pos.py -- patch position/window/type fields of an Amiga .info file.

Used by the release-packaging targets to snapshot icon positions and drawer
windows without an Amiga: everything Workbench stores for "Snapshot" lives in
the icon file itself, at fixed offsets of the classic DiskObject header, so a
byte patch is exact and format-preserving (GlowIcon IFF tails and NewIcons
tooltypes are untouched -- sizes never change).

Classic layout (big-endian):
  DiskObject:  do_Magic(0,2)=0xE310  do_Version(2,2)  Gadget(4,44)
               do_Type(48,1) pad(49,1) do_DefaultTool*(50,4) do_ToolTypes*(54,4)
               do_CurrentX(58,4) do_CurrentY(62,4) do_DrawerData*(66,4)
               do_ToolWindow*(70,4) do_StackSize(74,4)            = 78 bytes
  DrawerData (immediately after, when do_DrawerData != 0):
               struct NewWindow(48): LeftEdge(0,2) TopEdge(2,2)
               Width(4,2) Height(6,2) ...
               dd_CurrentX(48,4) dd_CurrentY(52,4)  -- the view origin

Operations (combine freely):
  --pos X Y            icon position in its parent window/drawer
  --center-x CX Y      centre the icon's WIDTH on CX, at row Y. The width is
                       read from the icon itself, so icons of different sizes
                       line up on one axis without hand-computed offsets (and
                       stay lined up if the artwork is ever resized).
  --no-pos             NO_ICON_POSITION (Workbench places it freely)
  --window L T W H     drawer/disk window box (drawer icons only)
  --type N             do_Type (1=disk 2=drawer 3=tool 4=project ...)
  --show-icons         Workbench "Show / Only Icons" + "View / By Icon"
                       (drawer icons only; see below)
  --show-all           "Show / All Files" + "View / By Name" -- for a drawer
                       whose contents have no icons of their own, where
                       --show-icons would open an EMPTY window

THE SHOW MODE IS NOT IN THE DRAWERDATA WE WRITE. dd_Flags and dd_ViewModes
are the V36 additions to struct DrawerData, but the block written inside the
file is still the OLD 56-byte one -- so those two fields live in a 6-byte
extension APPENDED AFTER the whole classic structure:

    [DiskObject 78][DrawerData 56][image1][image2][DefaultTool][ToolTypes]
    [ToolWindow][dd_Flags 4][dd_ViewModes 2][OS3.5 colour icon, if any]

Verified against stock Commodore icons rather than worked out from headers:
Workbench 3.2's own Prefs.info ends in 00 00 00 01 00 01 -- DDFLAGS_SHOWICONS
and DDVM_BYICON -- while Utilities.info and Tools.info, which are set to
inherit, end in six zero bytes.

TWO fields gate the extension, and missing the second one is silent. Besides
do_Version >= 1, icon.library reads the six bytes only when the icon's
REVISION is in 1..WB_DISKREVISION -- and the revision is the LOW BYTE of
do_Gadget.UserData (workbench.h: "I only use the lower 8 bits of
Gadget.UserData for the revision #"). With a zero there the extension is
skipped -- PDFPrinter 1.2's drawers opened showing every file -- and on a
GlowIcon worse: icon.library then looks for the colour part six bytes too
early, misses it, and the icon is classic (PrismaNG, 19 Oct 2026: Mason's
drawer art lost, MEASURED in FS-UAE). Every drawer icon on a real system
carries 1 in that byte, so --show-icons and --show-all set it too
(PDFPrinter's fix of 13 Sep 2026, brought over).

Kickstart 1.3 has no Show setting at all and stops at the classic structure,
so the six bytes are simply ignored there -- the same way it ignores a
GlowIcon tail.

Usage:
  tools/set_icon_pos.py <file.info> [ops...]
"""

import struct
import sys

NO_POS = -0x80000000

# Gadget.Width: DiskObject header (4) + ga_Next(4) + LeftEdge(2) + TopEdge(2).
ICON_W_OFF = 12

# workbench/workbench.h
DDFLAGS_SHOWICONS = 0x00000001
DDFLAGS_SHOWALL   = 0x00000002
DDVM_BYICON       = 1
DDVM_BYNAME       = 2

# do_Gadget.UserData (file offset 44): the low byte is the icon revision, and
# icon.library refuses to read the show-mode extension unless it is in
# 1..WB_DISKREVISION. See the note at the top of this file.
USERDATA_OFF        = 44
WB_DISKREVISION     = 1
WB_DISKREVISIONMASK = 255


def classic_end(d):
    """Offset just past the last classic section -- where the V36 drawer
    extension belongs, and where any OS3.5 colour icon starts."""
    u32 = lambda o: struct.unpack(">I", d[o:o + 4])[0]
    off = 78
    if u32(66):                                  # do_DrawerData
        off += 56
    for ptr in (22, 26):                         # ga_Render, ga_SelectRender
        if not u32(ptr):
            continue
        w, h, depth = struct.unpack(">hhh", d[off + 4:off + 10])
        has_data = u32(off + 10)
        off += 20
        if has_data:
            off += depth * h * (((w + 15) // 16) * 2)
    if u32(50):                                  # do_DefaultTool
        off += 4 + u32(off)
    if u32(54):                                  # do_ToolTypes
        n = u32(off)
        off += 4
        for _ in range(n // 4 - 1):
            off += 4 + u32(off)
    if u32(70):                                  # do_ToolWindow
        off += 4 + u32(off)
    return off


def set_show_mode(data, flags, viewmode):
    """Put dd_Flags / dd_ViewModes in place, keeping any tail.

    Three cases, told apart by what is actually sitting at the classic end:
    nothing (append), an IFF "FORM" (a colour icon -- insert in front of it,
    because icon.library reads the extension first), or an existing extension
    (overwrite). Guessing between the last two by file length would put the
    six bytes inside a GlowIcon and destroy it."""
    if struct.unpack(">I", data[66:70])[0] == 0:
        return False                             # not a drawer/disk icon
    ext = struct.pack(">IH", flags, viewmode)
    end = classic_end(data)
    tail = bytes(data[end:])
    if len(tail) >= 6 and tail[:4] != b"FORM":
        data[end:end + 6] = ext                  # already there: overwrite
    else:
        data[end:end] = ext                      # append, or insert before FORM
    if struct.unpack(">H", data[2:4])[0] < 1:    # icon.library ignores it below 1
        struct.pack_into(">H", data, 2, 1)
    # ...and the revision, or the six bytes above are read by nobody (and a
    # GlowIcon's colour part is looked for in the wrong place)
    ud = struct.unpack_from(">I", data, USERDATA_OFF)[0]
    if (ud & WB_DISKREVISIONMASK) != WB_DISKREVISION:
        struct.pack_into(">I", data, USERDATA_OFF,
                         (ud & ~WB_DISKREVISIONMASK) | WB_DISKREVISION)
    return True


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    path = args.pop(0)

    with open(path, "rb") as fh:
        data = bytearray(fh.read())

    if len(data) < 78 or struct.unpack(">H", data[0:2])[0] != 0xE310:
        sys.exit("%s: not an Amiga .info file" % path)

    done = []
    while args:
        op = args.pop(0)
        if op == "--pos":
            x, y = int(args.pop(0)), int(args.pop(0))
            struct.pack_into(">ll", data, 58, x, y)
            done.append("pos %d,%d" % (x, y))
        elif op == "--center-x":
            cx, y = int(args.pop(0)), int(args.pop(0))
            w = struct.unpack_from(">H", data, ICON_W_OFF)[0]
            x = cx - w // 2
            struct.pack_into(">ll", data, 58, x, y)
            done.append("center-x %d (w=%d -> x=%d), y=%d" % (cx, w, x, y))
        elif op == "--no-pos":
            struct.pack_into(">ll", data, 58, NO_POS, NO_POS)
            done.append("pos free")
        elif op == "--window":
            l, t = int(args.pop(0)), int(args.pop(0))
            w, h = int(args.pop(0)), int(args.pop(0))
            if struct.unpack(">L", data[66:70])[0] == 0:
                sys.exit("%s: no DrawerData (not a drawer/disk icon)" % path)
            struct.pack_into(">hhhh", data, 78, l, t, w, h)
            # View origin top-left, so the snapshotted icons are in sight.
            struct.pack_into(">ll", data, 78 + 48, 0, 0)
            done.append("window %d,%d %dx%d" % (l, t, w, h))
        elif op == "--show-icons":
            if set_show_mode(data, DDFLAGS_SHOWICONS, DDVM_BYICON):
                done.append("show only icons, view by icon")
            else:
                sys.exit("%s: no DrawerData (not a drawer/disk icon)" % path)
        elif op == "--show-all":
            if set_show_mode(data, DDFLAGS_SHOWALL, DDVM_BYNAME):
                done.append("show all files, view by name")
            else:
                sys.exit("%s: no DrawerData (not a drawer/disk icon)" % path)
        elif op == "--type":
            data[48] = int(args.pop(0))
            done.append("type %d" % data[48])
        else:
            sys.exit("unknown operation: %s" % op)

    with open(path, "wb") as fh:
        fh.write(data)
    print("%s: %s" % (path, ", ".join(done)))


if __name__ == "__main__":
    main()
