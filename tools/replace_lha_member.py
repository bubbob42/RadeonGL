#!/usr/bin/env python3
"""replace_lha_member.py OUT IN NAME NEW -- an LhA archive with one member
swapped, every other member's bytes left exactly as they were.

IN is the archive, NAME the member's file name (the header's own name field:
Lha 40 keeps the drawer in an extended header, so the name is bare -- it must
occur exactly once in IN), NEW a one-member archive holding the replacement,
packed by Amiga LhA from a root where it sits in the same drawer. OUT gets
IN's members in order with NAME's replaced by NEW's member, and the closing
0 byte. Written 2026-11-14 to correct a released archive's Aminet readme
(Requires: on one line) without rebuilding the package from a newer tree.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_lha_bits import members  # noqa: E402


def spans(data):
    """[(name, start, end)] for every member, and where the walk ended."""
    out = []
    off = 0
    names = members(data)
    while off < len(data) and data[off] != 0:
        name, _ = next(names)
        end = off + data[off] + 2 + int.from_bytes(data[off + 7:off + 11], "little")
        out.append((name, off, end))
        off = end
    return out, off


def main():
    if len(sys.argv) != 5:
        raise SystemExit(__doc__)
    out, src, name, new = sys.argv[1:5]
    data = open(src, "rb").read()
    rep = open(new, "rb").read()
    m, end = spans(data)
    if end != len(data) - 1:
        raise SystemExit("replace_lha_member: %s does not end in its closing 0 byte" % src)
    hits = [s for s in m if s[0] == name]
    if len(hits) != 1:
        raise SystemExit("replace_lha_member: %r occurs %d times in %s" % (name, len(hits), src))
    r, rend = spans(rep)
    if len(r) != 1 or r[0][0] != name or rend != len(rep) - 1:
        raise SystemExit("replace_lha_member: %s must hold exactly one member %r" % (new, name))
    _, a, b = hits[0]
    with open(out, "wb") as f:
        f.write(data[:a] + rep[r[0][1]:r[0][2]] + data[b:])
    print("replace_lha_member: %s -- %d members, %r replaced (%d -> %d bytes)"
          % (out, len(m), name, b - a, r[0][2] - r[0][1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
