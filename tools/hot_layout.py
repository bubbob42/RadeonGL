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
import struct
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


def code_hunk_symbols(path):
    """[[(offset, name), ...] per HUNK_CODE] of an Amiga object, read from its
    own HUNK_SYMBOL / HUNK_EXT tables. GCC can split one object into two code
    hunks (.text and .text.unlikely, both shown as ".text": core_array.o's
    upload-cache functions and PackFast's clone sit in the second), and nm
    gives no hunk, so each symbol must be taken from the hunk it is in."""
    d = open(path, "rb").read()
    o = 0
    out = []
    cur = None

    def L():
        return struct.unpack(">I", d[o:o + 4])[0]

    def name(n):
        return d[o:o + 4 * n].split(b"\0", 1)[0].decode("latin-1")

    while o < len(d):
        t = L() & 0x3fffffff
        o += 4
        if t in (0x3E7, 0x3E8):                     # UNIT, NAME
            n = L()
            o += 4 + 4 * n
        elif t in (0x3E9, 0x3EA):                   # CODE, DATA
            n = L() & 0x3fffffff
            o += 4 + 4 * n
            cur = [] if t == 0x3E9 else None
            if t == 0x3E9:
                out.append(cur)
        elif t == 0x3EB:                            # BSS
            o += 4
            cur = None
        elif t in (0x3EC, 0x3ED, 0x3EE, 0x3F7, 0x3F8, 0x3F9):   # RELOC32/16/8, DREL32/16/8
            while True:
                n = L()
                o += 4
                if n == 0:
                    break
                o += 4 + 4 * n
        elif t == 0x3FC:                            # RELOC32SHORT
            while True:
                n = struct.unpack(">H", d[o:o + 2])[0]
                o += 2
                if n == 0:
                    break
                o += 2 + 2 * n
            if o & 2:
                o += 2
        elif t == 0x3EF:                            # EXT
            while True:
                w = L()
                o += 4
                if w == 0:
                    break
                typ, n = w >> 24, w & 0xffffff
                nm = name(n)
                o += 4 * n
                if typ < 128:                       # definitions (GCC writes locals as $21)
                    v = L()
                    o += 4
                    if (typ & 0x1f) == 1 and cur is not None:
                        cur.append((v, nm))
                elif typ == 130:                    # COMMON
                    o += 4
                    c = L()
                    o += 4 + 4 * c
                else:                               # references
                    c = L()
                    o += 4 + 4 * c
        elif t == 0x3F0:                            # SYMBOL
            while True:
                n = L()
                o += 4
                if n == 0:
                    break
                nm = name(n)
                o += 4 * n
                v = L()
                o += 4
                if cur is not None:
                    cur.append((v, nm))
        elif t == 0x3F1:                            # DEBUG
            n = L()
            o += 4 + 4 * n
        elif t == 0x3F2:                            # END
            pass
        else:
            raise SystemExit("%s: hunk type $%x at %d not known" % (path, t, o - 4))
    return out


def offsets(mapfile):
    """{function: offset in the code hunk} for every part of every hot function.
    2026-11-15: the k-th code hunk of an object is the k-th ".text" line of
    that object in the map. Until then only the first line counted and a
    symbol of the second hunk was placed against the first's base: core_array.o's
    ULook and PackFast clone looked unchanged while that hunk moved."""
    bases = {}
    order = []
    for line in open(mapfile):
        m = TEXT.match(line.rstrip("\n"))
        if not m:
            continue
        obj = m.group(3)
        if obj not in bases:
            bases[obj] = []
            order.append(obj)
        bases[obj].append(int(m.group(1), 16))
    found = {}
    for obj in order:
        if not os.path.exists(obj):
            continue
        for k, syms in enumerate(code_hunk_symbols(obj)):
            if k >= len(bases[obj]):
                break
            for v, nm in syms:
                name = nm.lstrip("_")
                if name.split(".")[0] in HOT:
                    found[name] = bases[obj][k] + v
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
