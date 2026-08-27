#!/usr/bin/env python3
"""check_installer.py -- lint an Installer script for the substitution trap.

THE TRAP. The Installer language has two superficially identical ways of
writing a multi-line message, and only one of them concatenates:

    (message "line one "                 <- a command taking a LIST of strings;
             "line two")                    it joins them. Correct.

    ("line one %s"  #arg)                <- the SUBSTITUTION form: the first
                                            string is a format, the rest are
                                            its arguments.

Wrap a multi-line message in the second by accident -- one extra pair of
parentheses -- and it still parses, still runs, and still shows a page. It
just shows the WRONG one: line two becomes line one's first argument, and
everything after it is discarded. With no %s in line one, nothing is even
substituted and the whole rest of the message vanishes silently.

It is invisible in review because the two forms differ by one character, and
invisible in testing unless somebody reads the finished page word for word.
PDFPrinter shipped four of them at once -- a version-requirement abort that
never named the version, a help hint cut off mid-sentence, and both closing
pages reduced to "The PDF driver was You can find the Preferences Editor in
%s." That is what this exists to catch.

THE RULE. A substitution form with more than one TOP-LEVEL string literal in
it is almost certainly a concatenation that wanted to be one. Nested forms are
counted separately, so ("%s" ("%ld" n)) is fine, and a genuine multi-argument
format like ("'%s%s' is missing" #dir #file) has exactly one top-level string
and passes.

Exit status is 1 if anything is flagged, so it can gate a build.

    check_installer.py install/Install [more...]
"""

import re
import sys


def strip_comments(src):
    """Blank out ; comments, leaving strings and line numbering intact."""
    out = []
    i, n, instr = 0, len(src), False
    while i < n:
        c = src[i]
        if instr:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(src[i + 1])
                i += 2
                continue
            if c == '"':
                instr = False
            i += 1
        elif c == '"':
            instr = True
            out.append(c)
            i += 1
        elif c == ";":
            while i < n and src[i] != "\n":
                out.append(" ")
                i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def form_end(s, start):
    """Index of the ')' closing the form that opens at `start`."""
    depth, i, instr = 0, start, False
    while i < len(s):
        c = s[i]
        if instr:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                instr = False
        elif c == '"':
            instr = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def top_level_strings(body):
    depth, i, count, instr = 0, 0, 0, False
    while i < len(body):
        c = body[i]
        if instr:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                instr = False
        elif c == '"':
            instr = True
            if depth == 0:
                count += 1
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        i += 1
    return count


def check(path):
    src = strip_comments(open(path, encoding="latin-1").read())
    hits = []

    depth, i, instr, line = 0, 0, False, 1
    while i < len(src):
        c = src[i]
        if c == "\n":
            line += 1
        if instr:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                instr = False
        elif c == '"':
            instr = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth < 0:
                hits.append((line, "unbalanced ')'"))
                return hits
        i += 1
    if depth:
        hits.append((line, "%d unclosed '(' at end of file" % depth))

    for m in re.finditer(r'\(\s*"', src):
        start = m.start()
        end = form_end(src, start)
        if end < 0:
            continue
        body = src[start + 1:end]
        n = top_level_strings(body)
        if n > 1:
            hits.append((
                src[:start].count("\n") + 1,
                "substitution form holds %d top-level strings -- the ones after "
                "the first become ARGUMENTS to it and are dropped: %s..."
                % (n, re.sub(r"\s+", " ", body)[:60])))
    return hits


def strip(path):
    """The script with every comment gone, for the copy that ships.

    Uses the same string-aware scan as the lint, which is the whole reason
    this lives here rather than in a sed line in the Makefile: ';' is only a
    comment OUTSIDE a string, and a prompt that happened to contain one would
    otherwise be cut in half -- silently, since the result still parses.

    Comment-only lines go entirely; a trailing comment leaves its code behind.
    Runs of blank lines collapse to one so the result reads as a script rather
    than as a script with holes in it, and the blank lines the author put
    between sections survive.
    """
    out, blanks = [], 0
    for raw in strip_comments(open(path, encoding="latin-1").read()).split("\n"):
        line = raw.rstrip()
        if line:
            blanks = 0
            out.append(line)
        else:
            blanks += 1
            if blanks == 1:
                out.append("")
    while out and not out[0]:
        out.pop(0)
    return "\n".join(out).rstrip("\n") + "\n"


def main():
    paths = sys.argv[1:]
    if paths and paths[0] == "--strip":
        if len(paths) != 2:
            raise SystemExit("usage: check_installer.py --strip <script>")
        sys.stdout.write(strip(paths[1]))
        return 0
    if not paths:
        raise SystemExit("usage: check_installer.py <Install-script> [...]\n"
                         "       check_installer.py --strip <script>")
    bad = 0
    for p in paths:
        for line, msg in check(p):
            bad += 1
            print("%s:%d: %s" % (p, line, msg))
    if bad:
        print("check_installer: %d problem(s)" % bad)
        return 1
    print("check_installer: %s OK" % ", ".join(paths))
    return 0


if __name__ == "__main__":
    sys.exit(main())
