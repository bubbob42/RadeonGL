#!/usr/bin/env python3
"""
make_aminet_readme.py -- generate and validate the .readme Aminet wants beside
an upload.

Aminet has taken every AmigaOS release for thirty years, and it rejects on
formal grounds: a missing header field, an over-long Short:, CRLF line endings.
Those are all mechanically checkable, so they are checked here rather than
discovered by a bounced upload.

THE RULES ENCODED BELOW come from Aminet's own current documentation
(wiki.aminet.net, "Uploading instructions" and "The Readme file"), not from
older readmes lying around:

  * Mandatory fields: Short:, Uploader:, Type: and Architecture:. Everything
    else -- Author:, Version:, Distribution:, Requires:, Replaces:, Kurz: --
    is optional but conventional.
  * Short: is at most 40 characters. It goes in the directory listing, and it
    describes what the program DOES -- not its version and not its platform,
    both of which have fields of their own.
  * Distribution: takes exactly "Aminet" or "NoCD".
  * Lines are at most 78 characters and end with a bare linefeed. A readme
    written on a PC and uploaded with CRLF is the classic rejection.
  * The archive and its readme share a base name, at most 30 characters
    including the suffix, built only from letters, digits, dots, underscores
    and hyphens, and mainly lowercase.
  * Kurz: is the German translation of Short:, and is bounded the same way.

⚠ THERE IS NO LIMIT ON THE TOTAL LENGTH of the readme, and this was checked
rather than assumed (both pages re-read 2026-08-13, when the question came up
directly). The ONLY length rules Aminet states anywhere are the per-line 78 and
the 40 characters of Short:/Kurz:; neither page mentions a maximum file size, a
maximum number of lines, or a limit on the prose below the header. So a readme
long enough to actually describe the software is fine, and the summary printed
at the end reports the size so the question can be answered from a build log
instead of being researched again.

Architecture takes an optional version constraint, as in Aminet's own example
`m68k-amigaos >= 2.0.4`.

The prose lives in guide/aminet-readme.in, following the same @PLACEHOLDER@
convention as the rest of the project's templates, so the text can be edited
without touching this file. Only the version and date are substituted -- they
come from src/<project>_rev.h, which `gmake release` maintains, so the readme
cannot claim a version the binaries do not.

    make_aminet_readme.py [-o FILE]     write it (default dist/aminet/)
    make_aminet_readme.py --check FILE  validate an existing readme

Exit status is non-zero if anything Aminet would object to is present.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.basename(ROOT)
TEMPLATE = os.path.join(ROOT, "guide", "aminet-readme.in")


def _find_rev_h():
    """The bumprev header, wherever this project keeps it.

    The family convention is src/<project>_rev.h, and most projects follow it
    -- but bumprev names the file after the ARTEFACT, which for a library or a
    driver is not the project's own name. Rather than hardcode one spelling,
    prefer the conventional name and otherwise take the single *_rev.h in
    src/. Ambiguity is an error, not a guess: picking one of several would
    silently stamp the readme with the wrong version.
    """
    conventional = os.path.join(ROOT, "src", "%s_rev.h" % PROJECT)
    if os.path.exists(conventional):
        return conventional
    src = os.path.join(ROOT, "src")
    found = sorted(f for f in os.listdir(src) if f.endswith("_rev.h"))
    if len(found) == 1:
        return os.path.join(src, found[0])
    raise SystemExit("make_aminet_readme: cannot tell which revision header to "
                     "read in src/ (found: %s)" % (", ".join(found) or "none"))


REV_H = _find_rev_h()

MANDATORY = ("Short", "Uploader", "Type", "Architecture")
SHORT_MAX = 40
LINE_MAX = 78
NAME_MAX = 30
DISTRIBUTIONS = ("Aminet", "NoCD")

# Aminet's top-level directories. A Type: outside this set is a typo or an
# invented category, and either way the upload lands nowhere.
AMINET_DIRS = ("biz", "comm", "demo", "dev", "disk", "docs", "driver", "game",
               "gfx", "info", "mags", "misc", "mods", "mus", "pix", "text",
               "util")

ARCHITECTURES = ("m68k-amigaos", "ppc-amigaos", "ppc-morphos", "ppc-powerup",
                 "ppc-warpup", "i386-aros", "i386-amithlon", "generic")

NAME_RE = re.compile(r"^[A-Za-z0-9._-]+$")
FIELD_RE = re.compile(r"^([A-Z][A-Za-z]*):\s*(.*)$")


def revision():
    """(version, date) as the built binaries report them.

    Read from the generated _rev.h rather than accepted as an argument: the
    version in a readme that disagrees with the version in the archive is the
    kind of mistake nobody notices until a user does.
    """
    with open(REV_H, encoding="latin-1") as f:
        text = f.read()
    ver = re.search(r'#define\s+VERS\s+"[^"]*?([0-9]+\.[0-9]+)"', text)
    date = re.search(r'#define\s+DATE\s+"([^"]+)"', text)
    if not ver or not date:
        raise SystemExit("make_aminet_readme: no VERS/DATE in %s -- has "
                         "`gmake release` been run?"
                         % os.path.relpath(REV_H, ROOT))
    return ver.group(1), date.group(1)


def render():
    with open(TEMPLATE, encoding="latin-1") as f:
        body = f.read()
    ver, date = revision()
    return (body.replace("@VERSION@", ver).replace("@DATE@", date), ver)


def check_text(text, label="readme"):
    """Everything Aminet checks, plus the things it merely rejects you for."""
    problems = []

    if "\r" in text:
        problems.append("contains CR -- lines must end with a bare linefeed, "
                        "not CRLF (the classic rejection)")

    lines = text.split("\n")
    for n, line in enumerate(lines, 1):
        if len(line) > LINE_MAX:
            problems.append("line %d is %d characters, the limit is %d: %r"
                            % (n, len(line), LINE_MAX, line[:44] + "..."))
        try:
            line.encode("ascii")
        except UnicodeEncodeError:
            problems.append("line %d has non-ASCII characters, which do not "
                            "survive Aminet's index reliably: %r" % (n, line))

    # The header runs to the first blank line; a field after that is body text
    # as far as Aminet is concerned, and simply will not be seen.
    fields = {}
    for line in lines:
        if not line.strip():
            break
        m = FIELD_RE.match(line)
        if m:
            fields[m.group(1)] = m.group(2).rstrip()

    for want in MANDATORY:
        if want not in fields:
            problems.append("no %s: field -- it is mandatory" % want)

    short = fields.get("Short", "")
    if len(short) > SHORT_MAX:
        problems.append("Short: is %d characters, the limit is %d"
                        % (len(short), SHORT_MAX))
    if not short.strip():
        problems.append("Short: is empty")

    kurz = fields.get("Kurz")
    if kurz is not None and len(kurz) > SHORT_MAX:
        problems.append("Kurz: is %d characters, the limit is %d"
                        % (len(kurz), SHORT_MAX))

    typ = fields.get("Type", "")
    if typ and typ.split("/")[0] not in AMINET_DIRS:
        problems.append("Type: %r -- %r is not an Aminet top-level directory "
                        "(%s)" % (typ, typ.split("/")[0],
                                  ", ".join(AMINET_DIRS)))
    if typ and "/" not in typ:
        problems.append("Type: %r names a top-level directory only; uploads "
                        "go in a subdirectory" % typ)

    arch = fields.get("Architecture", "")
    if arch and arch.split()[0] not in ARCHITECTURES:
        problems.append("Architecture: %r -- %r is not one of %s"
                        % (arch, arch.split()[0], ", ".join(ARCHITECTURES)))

    dist = fields.get("Distribution")
    if dist is not None and dist not in DISTRIBUTIONS:
        problems.append("Distribution: %r -- the only legal values are %s"
                        % (dist, " and ".join(DISTRIBUTIONS)))

    up = fields.get("Uploader", "")
    if up and "@" not in up:
        problems.append("Uploader: %r has no email address -- Aminet needs one "
                        "to reach you when an upload goes wrong" % up)

    return problems, fields


def check_name(base):
    """The shared base name of the .lha and the .readme."""
    problems = []
    for suffix in (".lha", ".readme"):
        name = base + suffix
        if len(name) > NAME_MAX:
            problems.append("%s is %d characters; the limit is %d including "
                            "the suffix" % (name, len(name), NAME_MAX))
        if not NAME_RE.match(name):
            problems.append("%s may only contain letters, digits, dots, "
                            "underscores and hyphens" % name)
    letters = [c for c in base if c.isalpha()]
    if letters and sum(c.isupper() for c in letters) > len(letters) / 2:
        problems.append("%s is more upper-case than lower; Aminet asks for "
                        "names that are 'mainly lowercase' (mixed case is "
                        "allowed, so this is a warning worth a thought, not a "
                        "rule)" % base)
    return problems


def report(problems, what):
    if not problems:
        return 0
    print("make_aminet_readme: %d problem(s) with %s:" % (len(problems), what))
    for p in problems:
        print("    %s" % p)
    return 1


def main():
    argv = sys.argv[1:]

    if "--check" in argv:
        path = argv[argv.index("--check") + 1]
        # newline="" DISABLES universal-newline translation, and without it
        # this check is a no-op: Python would silently turn every CRLF into a
        # LF on the way in, so the CR test below could never fire and a file
        # Aminet would bounce read back as clean. The one rule most worth
        # checking was the one that could not be checked.
        with open(path, encoding="latin-1", newline="") as f:
            text = f.read()
        problems, fields = check_text(text, os.path.basename(path))
        problems += check_name(os.path.basename(path)[:-len(".readme")])
        if report(problems, os.path.basename(path)):
            return 1
        print("make_aminet_readme: %s OK -- Type %s, Architecture %s, "
              "Short %d/%d chars" % (os.path.basename(path),
                                     fields.get("Type"),
                                     fields.get("Architecture"),
                                     len(fields.get("Short", "")), SHORT_MAX))
        return 0

    out = os.path.join(ROOT, "dist", "aminet", PROJECT + ".readme")
    if "-o" in argv:
        out = argv[argv.index("-o") + 1]

    text, ver = render()
    problems, fields = check_text(text)
    problems += check_name(PROJECT)
    if report(problems, os.path.relpath(TEMPLATE, ROOT)):
        return 1

    d = os.path.dirname(out)
    if d:
        os.makedirs(d, exist_ok=True)
    # newline="\n" explicitly: the LF rule is the one this must not get wrong,
    # and on a platform that rewrote it the file would look fine here and be
    # rejected there.
    with open(out, "w", encoding="ascii", newline="\n") as f:
        f.write(text)

    print("make_aminet_readme: wrote %s" % os.path.relpath(out, ROOT))
    print("  Type %s / Architecture %s / Version %s"
          % (fields.get("Type"), fields.get("Architecture"), ver))
    lines = text.split("\n")
    print("  Short: %d/%d chars, Kurz: %d/%d, longest line %d/%d"
          % (len(fields.get("Short", "")), SHORT_MAX,
             len(fields.get("Kurz", "")), SHORT_MAX,
             max(len(l) for l in lines), LINE_MAX))
    # No limit exists for either of these -- reported so the "is it too long?"
    # question has an answer in the build log. See the module docstring.
    print("  total %d bytes in %d lines (Aminet sets no limit on either)"
          % (len(text), len(lines)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
