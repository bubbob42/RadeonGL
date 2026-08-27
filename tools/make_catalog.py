#!/usr/bin/env python3
"""
make_catalog.py -- build a locale.library message catalog on the HOST.

Until now a `.catalog` could only be produced inside FS-UAE, because the
Amiga's catalog compiler needs `iffparse.library` and will not run under vamos.
That round trip is slow, easy to forget, and -- worst of all -- SILENT when
skipped: a wording change in a `.ct` does not move the catalog version
fingerprint, so a stale `.catalog` still loads and still passes every guard,
showing the previous translation with nothing to warn anybody. This closes that
hole: `gmake catalogs` can now build the real thing directly.

    tools/make_catalog.py <descriptor.cd> <translation.ct> <out.catalog>
    tools/make_catalog.py --compare <a.catalog> <b.catalog>

THE FILE FORMAT is an IFF `FORM CTLG` holding, in order:

    FVER   the version string, NUL-terminated -- the text after `## version`
    LANG   the language name, NUL-terminated -- after `## language`
    CSET   32 bytes: the code-set number as a big-endian LONG, then 28 zero
           bytes reserved
    STRS   one record per translated string:
               ULONG  id
               ULONG  len
               len bytes of string
               1..3 zero bytes of padding

           `len` and the padding follow one rule: the record's string part is
           padded to a multiple of four, and there is ALWAYS at least one zero
           byte after the text, so the string is NUL-terminated in place. When
           the text length is already a multiple of four, `len` counts the
           terminator as well (text+1) and three pad bytes follow; otherwise
           `len` is the plain text length and 4 - (len % 4) zeros follow.
           An empty translation is written as len = 1 and four zero bytes.

Two behaviours are less obvious and matter for byte-compatibility:

  * A translation IDENTICAL to the descriptor's own English line is OMITTED
    from the catalog entirely -- the built-in string is already right, so the
    catalog carries nothing for that id. (This is why a translator's "no change
    needed" lines never appear in the file.)
  * Escape sequences are resolved before the string is measured or written:
    \\n \\t \\r \\a \\b \\f \\v, octal \\0..\\377, hex \\xNN, and any other
    escaped character stands for itself (so \\\\ is a backslash). Two are
    Amiga-specific and NOT what a C programmer expects: **\\e is 0x1E**, not
    0x1B, and **\\c is 0x9B**, the CSI byte.

Strings are written as ISO-8859-1, the Amiga's code set, from the UTF-8 source
files -- the same conversion the FS-UAE route did with iconv on the way in.

Verified by REPRODUCING a catalog built by the Amiga tool itself, byte for
byte, from the same descriptor and translation (`--compare`). That comparison
is the only thing that makes "maximum compatibility" a claim rather than a
hope, so keep it: after changing anything here, rebuild a known-good pair and
compare again.
"""

import re
import struct
import sys

# \e and \c are the two that surprise people; the rest are the C set.
SIMPLE_ESCAPES = {
    "a": 0x07, "b": 0x08, "t": 0x09, "n": 0x0A, "v": 0x0B,
    "f": 0x0C, "r": 0x0D, "e": 0x1E, "c": 0x9B,
}


def unescape(text):
    """Resolve escapes to the exact bytes the catalog will carry."""
    out = bytearray()
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        i += 1
        if ch != "\\" or i >= n:
            out += ch.encode("latin-1", "replace")
            continue

        ch = text[i]
        i += 1
        if ch == "x":                                   # \xNN, up to 2 digits
            value, count = 0, 0
            while i < n and count < 2 and text[i] in "0123456789abcdefABCDEF":
                value = value * 16 + int(text[i], 16)
                i += 1
                count += 1
            out.append(value & 0xFF)
        elif ch in "01234567":                          # \NNN, up to 3 digits
            value, count = int(ch, 8), 1
            while i < n and count < 3 and text[i] in "01234567":
                value = value * 8 + int(text[i], 8)
                i += 1
                count += 1
            out.append(value & 0xFF)
        elif ch in SIMPLE_ESCAPES:
            out.append(SIMPLE_ESCAPES[ch])
        else:
            out += ch.encode("latin-1", "replace")      # \\ , \" , anything
    return bytes(out)


def read_lines(path):
    """Raw lines with ONLY the newline removed.

    Not .strip() and not .rstrip() -- trailing spaces are part of a catalog
    string, and a stray CR would be too (a CRLF file really does put CRs in the
    strings, which is worth a warning rather than a silent divergence).
    """
    with open(path, encoding="utf-8") as fh:
        lines = [ln[:-1] if ln.endswith("\n") else ln for ln in fh]
    if any(ln.endswith("\r") for ln in lines):
        sys.stderr.write("%s: WARNING - CRLF line endings; the carriage "
                         "returns become part of the strings\n" % path)
    return lines


def take_string_line(lines, i, path, what):
    """The string that follows a header line, and the index after it.

    Whatever is on the line IS the string -- there are no comments in this
    position, so a string may perfectly well begin with ';' or '#' (AmiMatters
    has "#%s - %ld post(s)"). A line ending in a backslash continues onto the
    next one, the backslash dropped.
    """
    if i >= len(lines):
        raise SystemExit("%s: %s has no string line" % (path, what))
    out = ""
    while True:
        line = lines[i]
        i += 1
        if line.endswith("\\"):
            out += line[:-1]
            if i >= len(lines):
                raise SystemExit("%s: %s: continuation runs off the end"
                                 % (path, what))
            continue
        return out + line, i


HEADER_RE = re.compile(r"^([A-Za-z_]\w*)[ \t]*\(\s*([+-]?\d*)\s*/")


def parse_cd(path):
    """The descriptor: [(name, id, english_raw_line)] in file order.

    The file ALTERNATES: a header line `NAME (id/min/max)`, then that entry's
    English string on the next line. Only in the header position does a line
    get classified -- '#' starts a directive, ';' a comment; in the string
    position the line is taken as-is. Getting that wrong eats every string that
    happens to start with one of those two characters.

    An empty id means "one more than the previous"; +n / -n are relative to it.
    """
    entries = []
    nxt = 0
    lines = read_lines(path)
    i = 0
    while i < len(lines):
        line = lines[i]
        i += 1
        if line.startswith("#") or line.startswith(";") or not line.strip():
            continue                       # directive or comment or blank
        m = HEADER_RE.match(line)
        if not m:
            raise SystemExit("%s: cannot parse entry line: %r" % (path, line))
        name, num = m.group(1), m.group(2)
        if num == "":
            ident = nxt
        elif num.startswith(("+", "-")):
            ident = nxt + int(num)
        else:
            ident = int(num)
        nxt = ident + 1
        text, i = take_string_line(lines, i, path, name)
        entries.append((name, ident, text))
    return entries


def parse_ct(path):
    """The translation: the ## directives, and {name: raw_string} for what it
    carries. Same alternation and the same positional rule as the descriptor.
    """
    version = language = None
    codeset = 0
    trans = {}
    lines = read_lines(path)
    i = 0
    while i < len(lines):
        line = lines[i]
        i += 1
        if line.startswith("#"):
            body = line.lstrip("#").strip()
            key, _, rest = body.partition(" ")
            key = key.lower()
            if key == "version":
                version = rest.strip()
            elif key == "language":
                language = rest.strip()
            elif key == "codeset":
                codeset = int(rest.strip() or "0")
            continue
        if line.startswith(";") or not line.strip():
            continue
        name = line.strip()
        trans[name], i = take_string_line(lines, i, path, name)
    return version, language, codeset, trans


def chunk(cid, data):
    """One IFF chunk: id, big-endian size, data, and a pad byte if odd.

    The pad is NOT counted in the size -- that is the IFF rule, and getting it
    wrong produces a file that reads correctly right up until the chunk after
    the odd one.
    """
    out = cid + struct.pack(">I", len(data)) + data
    if len(data) & 1:
        out += b"\0"
    return out


def build_strs(entries, trans):
    """The STRS chunk: every id the translation actually changes."""
    out = bytearray()
    written = skipped_same = skipped_absent = 0
    for name, ident, english in entries:
        if name not in trans:
            skipped_absent += 1
            continue
        if trans[name] == english:      # identical to the built-in: omitted
            skipped_same += 1
            continue

        text = unescape(trans[name])
        out += struct.pack(">I", ident)
        if len(text) == 0:
            out += struct.pack(">I", 1) + b"\0\0\0\0"
        else:
            if len(text) % 4 == 0:
                length = len(text) + 1          # the terminator is counted
                pad = 3
            else:
                length = len(text)
                pad = 4 - (length % 4)
            out += struct.pack(">I", length)
            out += text
            if length > len(text):
                out += b"\0"                    # the counted terminator
            out += b"\0" * pad
        written += 1
    return bytes(out), written, skipped_same, skipped_absent


def build_catalog(cd_path, ct_path):
    entries = parse_cd(cd_path)
    version, language, codeset, trans = parse_ct(ct_path)

    unknown = [n for n in trans if n not in {e[0] for e in entries}]
    if unknown:
        raise SystemExit("%s: names not in the descriptor: %s"
                         % (ct_path, ", ".join(sorted(unknown))))

    body = b""
    if version:
        body += chunk(b"FVER", version.encode("latin-1") + b"\0")
    if language:
        body += chunk(b"LANG", language.encode("latin-1") + b"\0")
    body += chunk(b"CSET", struct.pack(">i", codeset) + b"\0" * 28)

    strs, written, same, absent = build_strs(entries, trans)
    body += chunk(b"STRS", strs)

    form = b"FORM" + struct.pack(">I", len(body) + 4) + b"CTLG" + body
    return form, (len(entries), written, same, absent, version, language)


def main():
    argv = sys.argv[1:]

    if argv and argv[0] == "--compare":
        a, b = argv[1], argv[2]
        da, db = open(a, "rb").read(), open(b, "rb").read()
        if da == db:
            print("identical: %d bytes" % len(da))
            return 0
        print("DIFFER: %d vs %d bytes" % (len(da), len(db)))
        for i in range(min(len(da), len(db))):
            if da[i] != db[i]:
                print("  first difference at offset %d (0x%X): %02X vs %02X"
                      % (i, i, da[i], db[i]))
                lo = max(0, i - 16)
                print("  %s: %s" % (a, da[lo:i + 16].hex()))
                print("  %s: %s" % (b, db[lo:i + 16].hex()))
                break
        return 1

    if len(argv) != 3:
        sys.exit(__doc__)

    cd_path, ct_path, out_path = argv
    data, (total, written, same, absent, version, language) = \
        build_catalog(cd_path, ct_path)
    with open(out_path, "wb") as fh:
        fh.write(data)

    print("wrote %s (%d bytes)" % (out_path, len(data)))
    print("  language %s, version %s" % (language, version))
    print("  %d of %d strings; %d identical to English (omitted), %d absent"
          % (written, total, same, absent))
    return 0


if __name__ == "__main__":
    sys.exit(main())
