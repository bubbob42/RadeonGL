#!/usr/bin/env python3
"""check_fpu060.py -- no FPU instruction the 68060 does not have.

The 68060's FPU leaves FScc, FDBcc and FTRAPcc out: each one traps into
68060.library, which emulates it at hundreds of cycles. They are legal on
the 68881/68882 and the 68040, so a compiler targeting "68020 to 68060"
emits them freely, and nothing on the 030 test machines notices.

MEASURED (Reflector, 2026-10-23, the A4000T's 68060): a comparison stored as
a value -- `x = (BOOL) (a > b)`, `return a <= b` on floats -- is one FScc.
The drag sketch's polygon fill and depth sort did that per row and per
comparison: 275 ms and 90 ms a frame for a 1000-triangle mesh. The tracer's
shadow test did it for every sphere a shadow ray passes, the 1987 look for
every sphere hit.

How each compiler is kept off them -- in the source, by both alike:
  GCC     a stored float comparison becomes FScc, and so may an if/else
          or a ternary that only picks a value; an if whose branch does
          real work (a store) or feeds another branch stays a branch.
          (-mtune=68060 would avoid them, but it makes (int) casts round to
          nearest: it failed the robot gate, docs/compilers-and-cpus.md --
          do not add it.)
  SAS/C   a stored float comparison, a negated one or a nested ternary
          becomes FScc; a plain if does not. Write the if.

Usage: check_fpu060.py OBJ...   (GCC_OBJDUMP: the objdump to use)
Exit 1 when one is found, naming the function.
"""

import os
import re
import subprocess
import sys

OBJDUMP = os.environ.get("GCC_OBJDUMP", "m68k-amigaos-objdump")

CC = r"(?:f|eq|ogt|oge|olt|ole|ogl|or|un|ueq|ugt|uge|ult|ule|ne|t|sf|seq|gt|ge|lt|le|gl|gle|ngle|ngl|nle|nlt|nge|ngt|sne|st)"
BAD = re.compile(r"\t(f(?:s%s|db%s|trap%s))(?:[.\s]|$)" % (CC, CC, CC))
FUNC = re.compile(r"^[0-9a-f]+ <(.*)>:$")


def scan(path):
    out = subprocess.run([OBJDUMP, "-d", "-m", "m68k:68060", path],
                         capture_output=True, text=True, errors="replace").stdout
    found = []
    fn = "?"
    for line in out.splitlines():
        m = FUNC.match(line)
        if m:
            fn = m.group(1)
            continue
        m = BAD.search(line)
        if m:
            found.append((fn, m.group(1), line.split(":")[0].strip()))
    return found


def main(argv):
    bad = 0
    for path in argv[1:]:
        for fn, insn, at in scan(path):
            print("check_fpu060: %s: %s at %s in %s -- the 68060 traps it" % (path, insn, at, fn))
            bad += 1
    if bad:
        print("check_fpu060: %d instruction(s) the 68060 has not (see the head of this file)" % bad)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
