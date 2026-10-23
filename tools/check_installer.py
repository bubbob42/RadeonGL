#!/usr/bin/env python3
"""check_installer.py -- lint an Installer script for the traps that cost releases.

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
One shipped release carried four of them at once -- a version-requirement
abort that never named the version, a help hint cut off mid-sentence, and both
closing pages reduced to "The driver was You can find the Preferences Editor
in %s." That is what this exists to catch.

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


# --- the second trap: a parameter the statement does not take ---------------
# (makedir "ENV:MyApp" (optional "nofail")) parses, reads well, and stops
# the Installer dead on the Amiga with "invalid parameter for statement":
# (optional) is the failure policy of copyfiles, copylib and delete, and of
# nothing else. One project shipped that line for months and only found it
# when somebody ran the installer on a real machine.
#
# Only statements listed here are checked, and unknown ones are passed over,
# so a script using something this table has never heard of is not punished
# for it. Widen the table rather than deleting a line that trips it.
COMMON = {"prompt", "help", "confirm", "safe", "back"}

PARAMS = {
    "makedir":   {"infos"},
    "copyfiles": {"source", "dest", "newname", "choices", "all", "pattern",
                  "files", "infos", "fonts", "optional", "delopts", "nogauge",
                  "noposition", "swapcolors"},
    "copylib":   {"source", "dest", "newname", "infos", "optional", "delopts",
                  "nogauge", "noposition", "version"},
    "delete":    {"all", "pattern", "optional", "delopts", "infos"},
    "tooltype":  {"dest", "settooltype", "setdefaulttool", "setstack",
                  "setposition", "noposition"},
    "textfile":  {"dest", "append", "include", "infos"},
    "startup":   {"command"},
    "askdir":    {"default", "newpath", "disk", "assigns"},
    "askfile":   {"default", "newpath", "disk"},
    "askstring": {"default"},
    "asknumber": {"default", "range"},
    "askbool":   {"default", "choices"},
    "askchoice": {"default", "choices"},
    "askoptions":{"default", "choices"},
    "askdisk":   {"dest", "newname", "assigns"},
}


# Every parameter name the language has, so that a name NOT in here is taken
# for a function call and left alone.
KNOWN_PARAMS = set()
for _allowed in PARAMS.values():
    KNOWN_PARAMS |= _allowed
KNOWN_PARAMS |= COMMON


def param_hits(src):
    """Every (statement ... (parameter ...)) whose parameter is not allowed."""
    hits = []
    for m in re.finditer(r"\(\s*([a-z]+)", src):
        name = m.group(1)
        if name not in PARAMS:
            continue
        start = m.start()
        end = form_end(src, start)
        if end < 0:
            continue
        allowed = PARAMS[name] | COMMON
        # direct children only: a (copyfiles) nested inside an (if) inside
        # this one is somebody else's statement.
        #
        # AND ONLY KNOWN PARAMETER NAMES ARE JUDGED. The first argument of
        # these statements is an expression, and an expression is also a
        # parenthesised form: (makedir (tackon #dest "Help")) is correct and
        # ordinary, and an earlier version of this check called it an error
        # in three sibling projects at once. A name this table has never
        # seen is somebody's function, not somebody's mistake.
        i = m.end()
        depth = 0
        instr = False
        while i < end:
            c = src[i]
            if instr:
                if c == "\\":
                    i += 2
                    continue
                if c == '"':
                    instr = False
            elif c == '"':
                instr = True
            elif c == "(":
                if depth == 0:
                    child = re.match(r"\(\s*([a-z@][a-z0-9-]*)", src[i:])
                    if child is not None and child.group(1) in KNOWN_PARAMS \
                            and child.group(1) not in allowed:
                        hits.append((
                            src[:i].count("\n") + 1,
                            "(%s) does not take (%s) -- the Installer stops "
                            "with \"invalid parameter for statement\""
                            % (name, child.group(1))))
                depth += 1
            elif c == ")":
                depth -= 1
            i += 1
    return hits


# --- the pattern trap: "a|b" is no alternation --------------------------------
# An AmigaDOS pattern alternates only inside parentheses: "(68000|68010)".
# Bare, "68000|68010" matches nothing a (database) answer can be, so
#
#     (if (not (patmatch "68000|68010" (database "cpu"))) <the 68020 path>)
#
# takes the 68020 path on a 68000 -- silently. The Installer guide's own
# (database) example shows exactly that line, and a project's script was
# written from it. Write the parentheses, or compare with database's exact
# two-argument form, (database "cpu" "68000").
def pattern_hits(src):
    hits = []
    for m in re.finditer(r'\(\s*patmatch\s+"((?:[^"\\]|\\.)*)"', src, re.I):
        pat, depth = m.group(1), 0
        for c in pat:
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            elif c == "|" and depth == 0:
                hits.append((
                    src[:m.start()].count("\n") + 1,
                    '(patmatch "%s"): a | outside parentheses is no alternation '
                    'in an AmigaDOS pattern -- write "(%s)", or compare with '
                    '(database "cpu" "68000")' % (pat, pat)))
                break
    return hits


# --- the logic trap: an ask inside OR or AND ---------------------------------
# The Installer evaluates EVERY argument of (OR ...) and (AND ...): no short
# circuit. So (OR (= @user-level 0) (askchoice ...)) shows the requester to
# the novice it was meant to spare and then ignores the answer. A novice's
# default goes in a NESTED if:
#
#     (if (= @user-level 0) (set #pick 0) (set #pick (askchoice ...)))
ASKS = ("askdir", "askfile", "askstring", "asknumber", "askchoice",
        "askoptions", "askbool", "askdisk")


def blank_strings(s):
    """The same text with every string's contents blanked."""
    out, i, instr = [], 0, False
    while i < len(s):
        c = s[i]
        if instr:
            if c == "\\" and i + 1 < len(s):
                out.append("  ")
                i += 2
                continue
            if c == '"':
                instr = False
                out.append(c)
            else:
                out.append(" " if c != "\n" else c)
        else:
            if c == '"':
                instr = True
            out.append(c)
        i += 1
    return "".join(out)


def logic_hits(src):
    hits = []
    code = blank_strings(src)
    for m in re.finditer(r"\(\s*(or|and)\s", code, re.I):
        end = form_end(code, m.start())
        if end < 0:
            continue
        a = re.search(r"\(\s*(%s)\b" % "|".join(ASKS), code[m.end():end], re.I)
        if a:
            hits.append((
                code[:m.start()].count("\n") + 1,
                "(%s ...) holds (%s): the Installer evaluates every argument, "
                "so the requester always shows -- put the novice default in a "
                "nested if" % (m.group(1).upper(), a.group(1))))
    return hits


def check(path):
    raw = open(path, encoding="latin-1").read()
    src = strip_comments(raw)
    hits = []

    # A SCRIPT WITH NOTHING IN IT IS NOT A VALID SCRIPT, and every structural
    # check below passes trivially on one -- so an empty file used to be
    # reported as OK. That is not hypothetical: Python's open(path, "w")
    # truncates BEFORE it encodes, so an editing script that dies on a
    # non-Latin-1 character leaves this file at zero bytes, and the linter
    # said nothing. It also catches a `> Install` redirection that went wrong.
    if not raw.strip():
        return [(1, "the file is empty -- nothing to install")]
    if "(welcome" not in src and "(exit" not in src:
        return [(1, "no (welcome) and no (exit) -- this does not look like "
                    "an Installer script")]

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
    # THE (set) TRAP, same family, opposite symptom. (set) assigns PAIRS:
    # (set #a 1 #b 2) is two assignments. So
    #
    #     (set #hint "line one "        <- looks exactly like the (message)
    #               "line two")            form above, and joins nothing
    #
    # reads "line two" as the next SYMBOL NAME and the Installer dies with
    # "SET: Symbol name expected in line N". Where the substitution trap
    # fails SILENTLY, this one fails LOUDLY -- but only when that line is
    # reached, which for a hint built on the closing page means after the
    # whole install has been done. One shipped release had one, and an
    # unattended test walked past it, because everything WAS installed by
    # then and only the last page was wrong. Two strings that want to join
    # need (cat).
    for m in re.finditer(r"\(\s*set\s", src):
        start = m.start()
        end = form_end(src, start)
        if end < 0:
            continue
        body = src[start + 1:end]
        n = top_level_strings(body)
        if n > 1:
            hits.append((
                src[:start].count("\n") + 1,
                "(set) holds %d top-level strings -- set takes PAIRS, so the "
                "second is read as a SYMBOL NAME (\"SET: Symbol name "
                "expected\"); join them with (cat): %s..."
                % (n, re.sub(r"\s+", " ", body)[:60])))
    hits.extend(param_hits(src))
    hits.extend(pattern_hits(src))
    hits.extend(logic_hits(src))
    hits.sort()
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
