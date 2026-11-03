#!/usr/bin/env python3
"""source_list.py -- the files of RadeonGL's source tree that go into the
release archive's Source drawer, one path per line.

The user, 2026-11-03: "sources and makefiles should also become part of the
aminet archive". What git tracks (and what it would track: new files not
ignored), as far as it exists and builds the library, the tools, the demos
and the Installer:

    the Makefile, LICENSE, README.md, .vamosrc.in, lib/ src/ demos/
    install/ guide/ tools/ test/ glsmoke/ icons/ catalogs/ fsuae/*.in,
    sdk/minigl-29.1 (the MiniGL SDK: Hyperion licence) and
    sdk/radeon-firmware (AMD's microcode, MIT)

Left out: docs/ (design notes and measurements: in the repository), the
editor and assistant settings, sdk/openpci (Thomas Richter's kit may only
travel as his whole archive: Aminet driver/other/openpci68k.lha),
sdk/ahi and sdk/cgx (only GLQuake used them), the host-only scripts.

    source_list.py [--check]

--check: also fail on a name AmigaDOS (FFS) cannot hold (over 30
characters, or ':' in it).

Without git (the Source drawer of an unpacked archive), every file under
the same paths is listed. Run-time state never goes in: *.log, *.prefs,
*.config (release-verify refuses an archive with them).
"""

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FILES = ("Makefile", "LICENSE", "README.md", ".vamosrc.in")
DIRS = ("lib/", "src/", "demos/", "install/", "guide/", "tools/", "test/", "glsmoke/",
        "icons/", "catalogs/", "sdk/minigl-29.1/", "sdk/radeon-firmware/")


def wanted(path):
    if path in FILES:
        return True
    if path.startswith("fsuae/") and path.endswith(".in") and path.count("/") == 1:
        return True
    return path.startswith(DIRS)


PRIVATE = (".log", ".prefs", ".config")


def candidates():
    try:
        out = subprocess.run(["git", "-C", ROOT, "ls-files"], capture_output=True, text=True,
                             check=True).stdout.split("\n")
        out += subprocess.run(["git", "-C", ROOT, "ls-files", "--others", "--exclude-standard"],
                              capture_output=True, text=True, check=True).stdout.split("\n")
        return out
    except (OSError, subprocess.CalledProcessError):
        out = []
        for top, dirs, names in os.walk(ROOT):
            dirs[:] = [d for d in dirs if d not in (".git", "build", "dist")]
            for n in names:
                out.append(os.path.relpath(os.path.join(top, n), ROOT).replace(os.sep, "/"))
        return out


def main():
    out = candidates()
    files = sorted(set(p for p in out if p and wanted(p) and not p.endswith(PRIVATE)
                       and os.path.isfile(os.path.join(ROOT, p))))
    bad = []
    for p in files:
        for part in p.split("/"):
            if len(part) > 30 or ":" in part:
                bad.append(p)
                break
    if "--check" in sys.argv[1:] and bad:
        for p in bad:
            sys.stderr.write("source_list: AmigaDOS cannot hold %s\n" % p)
        return 1
    for p in files:
        print(p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
