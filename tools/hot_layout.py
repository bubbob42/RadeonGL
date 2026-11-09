#!/usr/bin/env python3
"""hot_layout.py MAP REF [--update] -- did this build move the hot code?

On the 68060 (8 KB instruction and data caches, 4-way, 16-byte lines: the
sets repeat every 2 KB) where GLQuake's and the library's hot code sit
decides how much they evict each other. A change that moves the hot
functions can cost or win about 1% with the work unchanged (MEASURED
2026-11-09: an identical Hook_GLVertex3fv took twice the samples at another
address). This reads the link map and each object's symbols, finds the hot
functions' offsets in the code hunk and compares them with REF:

  unchanged  the hot code sits where it sat: no layout A/B needed
  moved      a GLQuake A/B is due (below), the layout may have
             changed its speed whatever the change itself does

--update writes REF from this build (after an A/B accepted the layout).
Always exit 0 (a report, not a failure); exit 2 on a missing map or REF.

The GLQuake A/B (A4000D, docs/hw/a4000d/glquake_run.py): the old and the
new library, vertical sync off (glquake_run.py passes +gl_videosync 0),
RadeonGLCheck 0, the millisecond build GLQuake-ms, demo1 at 320 x 240, at
six RAM pads (QPAD: the load address alone moves a run by ~0.1 s), each
pair back to back with the order alternating; the means decide. A single
run, or runs from different sessions, decide nothing.

The hot list comes from the PC-sample profiles of GLQuake's demo1
(docs/hw/a4000d/split.py, 2026-11-09). When other games' profiles name
other hot functions, add them here and --update.
"""

import os
import re
import subprocess
import sys

HOT = ["ToVram", "ValidateOnce", "Core_DrawElements", "Upload", "PackFast", "Mat_Mul",
       "Hook_GLVertex4f", "Hook_GLVertex3fv", "CardFlush", "Core_Enable", "Core_Texture",
       "R2E_Reg", "R2E_LoadVbpntr", "R2E_DrawIndexed", "RegsPkt", "SetArray", "ULook",
       "CopyUCache", "Core_Begin", "Core_End"]

NM = os.environ.get("GCC_NM", os.path.join(os.environ.get("PREFIX", os.path.expanduser("~/amiga-arm")),
                                           "bin", "m68k-amigaos-nm"))
TEXT = re.compile(r"^ \.text\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$")


def offsets(mapfile):
    """{function: offset in the code hunk} for every part of every hot function."""
    found = {}
    seen = set()
    for line in open(mapfile):
        m = TEXT.match(line.rstrip("\n"))
        if not m or int(m.group(2), 16) == 0:
            continue
        base, obj = int(m.group(1), 16), m.group(3)
        if obj in seen or not os.path.exists(obj):
            continue
        seen.add(obj)
        out = subprocess.run([NM, "-n", obj], capture_output=True, text=True).stdout
        for sym in out.splitlines():
            p = sym.split()
            if len(p) == 3 and p[1] in "tT":
                name = p[2].lstrip("_")
                if name.split(".")[0] in HOT:
                    found[name] = base + int(p[0], 16)
    return found


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 2:
        sys.stderr.write(__doc__)
        return 2
    mapfile, ref = args
    if not os.path.exists(mapfile):
        sys.stderr.write("hot_layout: no %s\n" % mapfile)
        return 2
    now = offsets(mapfile)
    if "--update" in sys.argv:
        with open(ref, "w") as f:
            f.write("# hot functions' offsets in minigl.library's code hunk (tools/hot_layout.py)\n")
            for name, off in sorted(now.items(), key=lambda kv: kv[1]):
                f.write("%-32s %6d\n" % (name, off))
        print("hot_layout: %s written, %d functions" % (ref, len(now)))
        return 0
    if not os.path.exists(ref):
        sys.stderr.write("hot_layout: no %s -- gmake hot-layout-ref\n" % ref)
        return 2
    was = {}
    for line in open(ref):
        p = line.split()
        if len(p) == 2 and not line.startswith("#"):
            was[p[0]] = int(p[1])
    moved = [(n, was.get(n), now.get(n)) for n in sorted(set(was) | set(now)) if was.get(n) != now.get(n)]
    if not moved:
        print("hot_layout: the hot code sits where it sat (%d functions): no layout A/B needed" % len(now))
        return 0
    print("hot_layout: THE HOT CODE MOVED (%d of %d functions) -- a GLQuake A/B is due (tools/hot_layout.py says how);"
          " after it, gmake hot-layout-ref" % (len(moved), len(now)))
    for n, a, b in moved[:8]:
        print("  %-32s %6s -> %6s" % (n, a if a is not None else "-", b if b is not None else "-"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
