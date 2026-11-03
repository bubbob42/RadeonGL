#!/usr/bin/env python3
"""check_fpu881.py -- no FPU instruction a 68881/68882 does not have.

The 68040 and the 68060 added FPU instructions that round to single or
double precision (fsmove, fsadd, fsmul, fsdiv, fssqrt, fsabs, fsneg and the
same with fd...). GCC -m68040/-m68060 emits them freely: the 68040/68060
build of minigl.library holds some 450 (audit, 2026-11-03). A 68881 or 68882
does not know them: the first one is an F-line exception (Guru 8000000B).
So the 68020/68030 builds (-m68030 -m68881) are checked here for none.

Usage: check_fpu881.py OBJ...   (GCC_OBJDUMP: the objdump to use)
Exit 1 when one is found, naming the function.
"""

import os
import re
import subprocess
import sys

OBJDUMP = os.environ.get("GCC_OBJDUMP", "m68k-amigaos-objdump")

BAD = re.compile(r"\t(f[sd](?:move|add|sub|mul|div|sqrt|abs|neg))[bwlsdxp]?(?:\s|$)")
FUNC = re.compile(r"^[0-9a-f]+ <(.*)>:$")


def main():
    if len(sys.argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    bad = 0
    for path in sys.argv[1:]:
        out = subprocess.run([OBJDUMP, "-d", "-m68040", path], capture_output=True,
                             text=True, check=True).stdout
        func = "?"
        found = {}
        for line in out.split("\n"):
            m = FUNC.match(line)
            if m:
                func = m.group(1)
                continue
            m = BAD.search(line)
            if m:
                found.setdefault(func, []).append(m.group(1))
        for f in sorted(found):
            sys.stderr.write("%s: %s: %d 68040+ FPU instruction(s), e.g. %s\n"
                             % (path, f, len(found[f]), found[f][0]))
            bad += len(found[f])
    if bad:
        sys.stderr.write("check_fpu881: %d instruction(s) a 68881/68882 does not have\n" % bad)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
