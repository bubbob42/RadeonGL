#!/usr/bin/env python3
"""check_lha_bits.py -- what the Amiga's LhA put into a release archive.

Amiga LhA (under vamos) records each member's protection bits from the HOST
file's mode: a read-only host file ships as ----r---, and after unpacking
the user cannot delete it (PDFPrinter 1.0 shipped one). The bits sit in
byte 19 of every member header; bits 0..3 are d e w r and ACTIVE LOW, so a
set bit 0 forbids deleting.

Checked here, from the archive itself:
  * no member that cannot be deleted
  * as many members as files were staged (when a count is given)

    check_lha_bits.py ARCHIVE [EXPECTED_MEMBERS]

Level-0 and level-1 headers (what Lha 40 writes): [0] header size,
[7:11] packed size -- for level 1 including the extended headers --,
[19] attribute, [20] level, [21] name length, [22:] name. The next header
follows at offset + header size + 2 + packed size.
"""

import struct
import sys


def members(data):
    off = 0
    while off < len(data) and data[off] != 0:
        size = data[off]
        level = data[off + 20]
        if level not in (0, 1):
            raise SystemExit("check_lha_bits: header level %d at %d -- only "
                             "0 and 1 are understood" % (level, off))
        packed = struct.unpack("<I", data[off + 7:off + 11])[0]
        attr = data[off + 19]
        nlen = data[off + 21]
        name = data[off + 22:off + 22 + nlen].decode("latin-1")
        yield name, attr
        off += size + 2 + packed


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    found = list(members(data))
    bad = [(n, a) for n, a in found if a & 0x01]
    rc = 0
    for name, attr in bad:
        print("check_lha_bits: %s cannot be deleted after unpacking "
              "(attribute 0x%02x) -- chmod u+w the staged file" % (name, attr))
        rc = 1
    if len(sys.argv) > 2 and len(found) != int(sys.argv[2]):
        print("check_lha_bits: %d members, but %s files were staged"
              % (len(found), sys.argv[2]))
        rc = 1
    if not rc:
        print("check_lha_bits: %d members, every one deletable" % len(found))
    return rc


if __name__ == "__main__":
    sys.exit(main())
