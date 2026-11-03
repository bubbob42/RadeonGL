#!/usr/bin/env python3
"""join_lha.py -- one LhA archive out of several, member for member.

Amiga LhA under vamos cannot make the tester's archive in one go: over both
release drawers (some 180 files) vamos runs out of locks ("no more lock
slots! max=1024", 18 Oct 2026), and adding to an archive that is there
already does nothing at all (no output, RC 0). So each drawer is packed
by a run of its own and the archives are joined here.

An LhA archive is its members one after another -- header, then packed
data -- and a single 0 byte where the next header would begin. Joined:
every archive's members, in order, and one 0 byte at the end. Each input
is walked header by header first (check_lha_bits.py's walk: levels 0 and
1, what Lha 40 writes), and must end in exactly that 0 byte -- anything
else, and nothing is written.

    join_lha.py OUT IN1 IN2 [IN3 ...]
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_lha_bits import members  # noqa: E402


def body(path):
    """The members of one archive, without its closing 0 byte."""
    with open(path, "rb") as f:
        data = f.read()
    count = sum(1 for _ in members(data))
    # members() stops at the first 0 header size: walk again for its offset
    end = 0
    while end < len(data) and data[end] != 0:
        packed = int.from_bytes(data[end + 7:end + 11], "little")
        end += data[end] + 2 + packed
    if end != len(data) - 1 or data[end] != 0:
        raise SystemExit("join_lha: %s does not end in its closing 0 byte "
                         "(walk ends at %d of %d)" % (path, end, len(data)))
    return data[:end], count


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    out, ins = sys.argv[1], sys.argv[2:]
    parts = [body(p) for p in ins]
    with open(out, "wb") as f:
        for data, _ in parts:
            f.write(data)
        f.write(b"\0")
    print("join_lha: %s -- %s members" % (
        out, " + ".join(str(n) for _, n in parts)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
