#!/usr/bin/env python3
"""bbb_scan.py -- loads that bebbo's bbb pass gave a wrong auto-increment.

bebbo's m68k-amigaos-gcc runs its own late pass, "bbb" (gcc/bbb-opts.c).
Its opt_autoinc() turns "op (An)" followed by "add #n,An" into "op (An)+"
and corrects the offsets after it by the size it thinks the load read: the
size of the insn's mode, halved for a SIGN_EXTEND, doubled for a
FLOAT_TRUNCATE -- and nothing else. A FLOAT_EXTEND (a float read and widened
to a double: "fdmove.s (An),fp0" on the 68040/68060, "fmove.s" on a 68881)
is counted as 8 bytes where the CPU steps 4. Everything after it in the
loop then reads 4 bytes early. GLQuake's water warp lost its textures that
way (gl_rsurf.c, 2026-11-09).

GCC's own auto-increment pass gets the size right and makes the same
instructions, so the disassembly cannot tell the two apart. The RTL can:
this tool compiles every object again into a scratch directory with RTL
dumps just before bbb (pass "bbro") and after everything ("final"), and
reports each extend or truncate of a post_inc / pre_dec memory operand
that the final RTL has and the bbro RTL had not, with the size bbb counted
and the size the CPU steps. The project's own objects are not touched.

bebbo fixed it upstream on 2026-07-16 (gcc amiga6 da7ad7f02, "refs #11: use
correct source mem size for auto inc": the size of the memory operand
inside any extend or truncate). So the tool first asks the compiler: it
builds GLQuake's warp loop as a probe and adds up how far the loop's
pointer moves. 28 bytes (the 7 floats of a vertex): the compiler has the
fix, and bbb's sizes are the memory operand's. 24: the old sizing. No
extending auto-increment in the probe: the old sizing is assumed.

Usage: tools/bbb_scan.py [TARGET...]
  TARGET   RadeonGL make targets whose compile lines to check
           (default: everything the release stages, the glsmoke tests,
           GLBench, the probes, the GL core objects, and GLQuake for the
           68060 and the 68040)
  GLQUAKE_DIR  the GLQuake source (../RadeonGL-GLQuake)
Exit 1 when bbb miscounted one, 0 when none, 2 on a failure.
"""

import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

MODES = {"QI": 1, "HI": 2, "SI": 4, "DI": 8, "SF": 4, "DF": 8, "XF": 12}
PAT = re.compile(r"(float_extend|zero_extend|sign_extend|float_truncate):(\w+) "
                 r"\(mem:(\w+) \((post_inc|pre_dec)")
SPLIT = re.compile(r"\n(?=\(insn |\(call_insn |\(jump_insn |;; Function )")
DEFAULT = ["release-stage", "glsmoke", "glbench", "probes", "r200-objs", "gcc"]
SUFFIX = {"b": 1, "w": 2, "l": 4, "s": 4, "d": 8, "x": 12, "p": 12}
FIXED = False               # set by probe(): the compiler sizes extends right

# GLQuake 1.2's DrawGLWaterPoly loop: the shape the old bbb miscompiled
PROBE = r"""
extern double rt;
extern float Q(double);
extern void V(float *);
void probe(float *v, int n)
{
    int i;
    float nv[3], t;

    for (i = 0; i < n; i++, v += 7) {
        t = Q(v[2] * 0.05 + rt);
        nv[0] = v[0] + 8 * Q(v[1] * 0.05 + rt) * t;
        nv[1] = v[1] + 8 * Q(v[0] * 0.05 + rt) * t;
        nv[2] = v[2];
        V(nv);
    }
}
"""


def compile_lines(targets, glquake):
    """Every 'm68k-amigaos-gcc ... -c' line the targets would run."""
    runs = [["gmake", "-n", "-B"] + targets]
    mk = os.path.join(glquake, "Makefile.radeongl")
    if os.path.exists(mk):
        for cpu in ("060", "040"):
            runs.append(["gmake", "-n", "-B", "-f", mk, "QSRC=" + glquake, "QCPU=" + cpu])
    else:
        sys.stderr.write("bbb_scan: no %s -- GLQuake not checked\n" % mk)
    lines = []
    for r in runs:
        out = subprocess.run(r, capture_output=True, text=True).stdout
        for line in out.splitlines():
            if "m68k-amigaos-gcc" in line and " -c " in line and " -o " in line:
                lines.append(line.strip())
    return sorted(set(lines))


def scan(path):
    """{function: [(op, outer mode, inner mode, kind), ...]} of one dump."""
    found = {}
    fn = None
    with open(path, errors="replace") as f:
        text = f.read()
    for blk in SPLIT.split(text):
        m = re.match(r";; Function (\S+)", blk)
        if m:
            fn = m.group(1)
            continue
        for g in PAT.finditer(" ".join(blk.split())):
            found.setdefault(fn, []).append(g.groups())
    return found


def probe(cc, work):
    """The bytes the probe loop's pointer moves a turn, or None."""
    src = os.path.join(work, "probe.c")
    obj = os.path.join(work, "probe.o")
    with open(src, "w") as f:
        f.write(PROBE)
    r = subprocess.run([cc, "-m68060", "-m68881", "-O2", "-c", src, "-o", obj],
                       capture_output=True, text=True)
    if r.returncode:
        return None
    dis = subprocess.run([os.environ.get("GCC_OBJDUMP", "m68k-amigaos-objdump"),
                          "-d", "-m", "m68k:68060", obj], capture_output=True, text=True).stdout
    ins = []                # (address, mnemonic, operands)
    for line in dis.splitlines():
        m = re.match(r"\s*([0-9a-f]+):\t[0-9a-f ]+\t(\S+)\s*(.*)$", line)
        if m:
            ins.append((int(m.group(1), 16), m.group(2), m.group(3)))
    reg = None
    for _, mn, ops in ins:
        m = re.match(r"(a[0-7])@\+,%fp", ops)
        if mn == "fdmoves" and m:
            reg = m.group(1)
    if reg is None:
        return None
    loop = None             # the backward branch closing the loop
    for addr, mn, ops in ins:
        m = re.match(r"([0-9a-f]+) <", ops)
        if mn.startswith("b") and m and int(m.group(1), 16) < addr:
            loop = (int(m.group(1), 16), addr)
    if loop is None:
        return None
    step = 0
    for addr, mn, ops in ins:
        if not loop[0] <= addr <= loop[1]:
            continue
        size = SUFFIX.get(mn[-1], 0)
        step += size * ops.count(reg + "@+") - size * ops.count(reg + "@-")
        m = re.match(r"%s@\((-?\d+)\),%s$" % (reg, reg), ops)
        if mn == "lea" and m:
            step += int(m.group(1))
        m = re.match(r"#(\d+),%s$" % reg, ops)
        if m and mn.startswith("add"):
            step += int(m.group(1))
        if m and mn.startswith("sub"):
            step -= int(m.group(1))
    return step


def counted(op, outer, inner):
    """The size opt_autoinc() steps for such a load."""
    if FIXED:
        return MODES.get(inner, 0)
    size = MODES.get(outer, 0)
    if op == "sign_extend":
        size //= 2
    elif op == "float_truncate":
        size *= 2
    return size


def check(work, item):
    i, line = item
    args = shlex.split(line)
    o = args.index("-o")
    obj = args[o + 1]
    tag = "%04d" % i
    args[o + 1] = os.path.join(work, tag + ".o")
    args += ["-fdump-rtl-bbro", "-fdump-rtl-final", "-dumpbase", tag, "-dumpdir", work + "/"]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode:
        return obj, None, r.stderr.strip().splitlines()[-1:] or ["failed"]
    names = [n for n in os.listdir(work) if n.startswith(tag + ".")]
    final = [n for n in names if n.endswith("r.final")]
    before = [n for n in names if n.endswith("r.bbro")]
    if not final:
        return obj, [], None        # no function in it (a table, a string)
    after = scan(os.path.join(work, final[0]))
    prior = scan(os.path.join(work, before[0])) if before else {}
    hits = []
    for fn, loads in after.items():
        old = list(prior.get(fn, []))
        for g in loads:
            if g in old:            # GCC's own auto-increment: sized right
                old.remove(g)
                continue
            op, outer, inner, kind = g
            hits.append((fn, op, outer, inner, kind, counted(op, outer, inner), MODES.get(inner, 0)))
    return obj, hits, None


def main():
    targets = [a for a in sys.argv[1:] if not a.startswith("-")] or DEFAULT
    glquake = os.path.abspath(os.environ.get("GLQUAKE_DIR", "../RadeonGL-GLQuake"))
    lines = compile_lines(targets, glquake)
    if not lines:
        sys.stderr.write("bbb_scan: no compile lines found (run it in the RadeonGL directory)\n")
        return 2
    global FIXED
    work = tempfile.mkdtemp(prefix="bbb_scan.")
    step = probe(shlex.split(lines[0])[0], work)
    if step == 28:
        FIXED = True
        print("bbb_scan: the compiler sizes extending loads right (probe loop steps 28 bytes)")
    elif step is None:
        print("bbb_scan: no extending auto-increment in the probe -- the old sizing assumed")
    else:
        print("bbb_scan: THIS COMPILER HAS THE BUG (probe loop steps %d bytes, not 28) --"
              " see bebbo's gcc da7ad7f02" % step)
    try:
        with ThreadPoolExecutor(os.cpu_count() or 4) as ex:
            results = list(ex.map(lambda it: check(work, it), enumerate(lines)))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    wrong = failed = 0
    for obj, hits, err in results:
        if err:
            failed += 1
            print("FAILED %s: %s" % (obj, err[0]))
            continue
        for fn, op, outer, inner, kind, size, step in hits:
            if size != step:
                wrong += 1
                print("WRONG %s %s: %s:%s (mem:%s %s) counted %d bytes, the CPU steps %d"
                      % (obj, fn, op, outer, inner, kind, size, step))
    print("bbb_scan: %d objects, %d miscounted, %d not compiled" % (len(results), wrong, failed))
    if failed:
        return 2
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main())
