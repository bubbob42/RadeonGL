#!/usr/bin/env python3
"""make_install_icon_os32.py APPNAME OUT.info -- a package's Install icon from
AmigaOS 3.2's standard installer icon (artwork by Martin "Mason" Merz,
redistributed with his permission, 2026-11-07).

Two originals in icons/install/:
  Install-classic.info  3.2's classic (planar) installer icon -- its IMAGE is
                        taken; its tooltypes are 3.2's own (APPNAME=Workbench
                        3.2, SCRIPT=Install, LANGUAGE=deutsch ...) and are NOT.
  Install-glow.info     3.2's GlowIcon for installer packages -- its appended
                        IFF ICON is taken; its classic header is a 4x4
                        placeholder with default tool c:IconX and is NOT.

The result: OUR classic icon (that image, WBPROJECT, default tool Installer,
APPNAME=<app>, MINUSER=NOVICE, stack 10000 as 3.2's) with the GlowIcon
appended at its exact end (tools/merge_glowicon.py's rules), both cut to their
artwork (tools/icon_crop.py). icon.library V44+ draws the GlowIcon, older
ones the classic image; the tooltypes are ours either way. The same recipe as
WaveSmith's Install icon (its tools/make_edit_icons.py + GLOW_MERGE).

Usage:
    python3 tools/make_install_icon_os32.py "RadeonGL" install/Install.info
"""

import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import amiga_icon  # noqa: E402
import icon_crop  # noqa: E402
import merge_glowicon  # noqa: E402

ICONS = os.path.join(HERE, "..", "icons", "install")
CLASSIC = os.path.join(ICONS, "Install-classic.info")
GLOW = os.path.join(ICONS, "Install-glow.info")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    app, out = sys.argv[1], sys.argv[2]

    canvas = amiga_icon.read_icon_image(CLASSIC)
    fd, tmp = tempfile.mkstemp(suffix=".info")
    os.close(fd)
    try:
        amiga_icon.write_info(tmp, canvas, amiga_icon.WBPROJECT,
                              default_tool="Installer",
                              tooltypes=["APPNAME=" + app, "MINUSER=NOVICE"],
                              stack=10000)
        classic = merge_glowicon.read_file(tmp)
    finally:
        os.remove(tmp)

    glow = merge_glowicon.read_file(GLOW)
    merge_glowicon.check_classic(classic, "our classic icon")
    merge_glowicon.check_classic(glow, GLOW)
    if merge_glowicon.icon_type(classic) != merge_glowicon.icon_type(glow):
        sys.exit("icon types differ: ours %d, %s %d" % (merge_glowicon.icon_type(classic),
                                                     GLOW, merge_glowicon.icon_type(glow)))

    # at the EXACT end of the classic DiskObject, no alignment pad
    merged, _ = icon_crop.crop(classic + merge_glowicon.glow_tail(glow, GLOW))

    with open(out, "wb") as fh:
        fh.write(merged)
    print("install icon %s: APPNAME=%s, %d bytes (classic %d + GlowIcon)"
          % (out, app, len(merged), len(classic)))


if __name__ == "__main__":
    main()
