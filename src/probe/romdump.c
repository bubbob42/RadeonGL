/*
 * RomDump -- copy a PCI expansion ROM (or any other mapped range) into a
 * file. Read-only: nothing on the card or in the system is written.
 *
 *   RomDump ADDR=$4FFE0000 SIZE=131072 TO=RAM:rv280.rom
 *
 * Take ADDR and SIZE from `lspci verbose` ("ROM mapped to ... size ...").
 * The range is read twice and compared (the reads go through the PCI
 * bridge), then the file is written and the PCI ROM image is checked:
 * the 55 AA signature, the length byte, the 8-bit checksum over the image
 * and the PCIR structure (vendor and device id).
 *
 * Why (2026-11-11): THOR asked for the RV280's ROM BAR to compare the DFP
 * (TMDS) init table of this card with what Radeon.card parses.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

static const char ver[] = "$VER: RomDump 0.1 (11.11.2026)";

#define TEMPLATE "ADDR/A,SIZE/N,TO/A"
#define MAX_SIZE 0x100000UL

enum { ARG_ADDR, ARG_SIZE, ARG_TO, ARG_COUNT };

static int parse_hex(const char *s, ULONG *out)
{
    ULONG v;
    int digits;
    char c;

    if (s[0] == '$')
        s++;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    v = 0;
    digits = 0;
    while ((c = *s++) != 0)
    {
        if (c >= '0' && c <= '9')
            v = (v << 4) | (ULONG) (c - '0');
        else if (c >= 'a' && c <= 'f')
            v = (v << 4) | (ULONG) (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v = (v << 4) | (ULONG) (c - 'A' + 10);
        else
            return 0;
        if (++digits > 8)
            return 0;
    }
    if (digits == 0)
        return 0;
    *out = v;
    return 1;
}

/* byte reads: the ROM is little endian and the bridge may not like wider
   unaligned accesses; speed does not matter for 128 KB */
static void copy_range(const volatile UBYTE *src, UBYTE *dst, ULONG size)
{
    ULONG i;

    for (i = 0; i < size; i++)
        dst[i] = src[i];
}

static ULONG le16(const UBYTE *p)
{
    return (ULONG) p[0] | ((ULONG) p[1] << 8);
}

int main(void)
{
    LONG args[ARG_COUNT];
    struct RDArgs *rda;
    ULONG addr;
    ULONG size;
    ULONG i;
    ULONG diff;
    ULONG imglen;
    ULONG sum;
    ULONG pcir;
    UBYTE *a;
    UBYTE *b;
    BPTR fh;
    int rc;

    rc = RETURN_FAIL;
    a = NULL;
    b = NULL;
    fh = 0;
    for (i = 0; i < ARG_COUNT; i++)
        args[i] = 0;

    rda = ReadArgs(TEMPLATE, args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "RomDump");
        return RETURN_FAIL;
    }
    if (!parse_hex((const char *) args[ARG_ADDR], &addr))
    {
        Printf("RomDump: ADDR %s is not a hex address ($4FFE0000 or 0x4FFE0000)\n",
               (ULONG) args[ARG_ADDR]);
        goto done;
    }
    size = args[ARG_SIZE] ? (ULONG) *(LONG *) args[ARG_SIZE] : 0x10000UL;
    if (size == 0 || size > MAX_SIZE)
    {
        Printf("RomDump: SIZE must be 1 to %lu bytes\n", MAX_SIZE);
        goto done;
    }
    a = AllocVec(size, MEMF_ANY);
    b = AllocVec(size, MEMF_ANY);
    if (a == NULL || b == NULL)
    {
        Printf("RomDump: no memory for 2 x %lu bytes\n", size);
        goto done;
    }

    /* say what is read BEFORE reading: if the access hangs the machine,
       the screen still shows the address (2026-11-11, the RV280 ROM BAR) */
    Printf("reading $%08lx-$%08lx (%lu bytes) twice ...\n", addr, addr + size - 1, size);
    Flush(Output());
    copy_range((const volatile UBYTE *) addr, a, size);
    copy_range((const volatile UBYTE *) addr, b, size);
    diff = 0;
    for (i = 0; i < size; i++)
        if (a[i] != b[i])
            diff++;
    Printf("read twice: %lu bytes differ\n", diff);

    fh = Open((STRPTR) args[ARG_TO], MODE_NEWFILE);
    if (fh == 0)
    {
        PrintFault(IoErr(), (STRPTR) args[ARG_TO]);
        goto done;
    }
    if (Write(fh, a, (LONG) size) != (LONG) size)
    {
        PrintFault(IoErr(), "RomDump: write");
        goto done;
    }
    Printf("wrote %lu bytes to %s\n", size, (ULONG) args[ARG_TO]);

    if (a[0] == 0x55 && a[1] == 0xAA)
    {
        imglen = (ULONG) a[2] * 512;
        Printf("signature 55 AA, image length %lu bytes\n", imglen);
        if (imglen != 0 && imglen <= size)
        {
            sum = 0;
            for (i = 0; i < imglen; i++)
                sum += a[i];
            Printf("checksum over the image: $%02lx (%s)\n", sum & 0xFF,
                   (ULONG) ((sum & 0xFF) == 0 ? "ok" : "BAD"));
        }
        pcir = le16(a + 0x18);
        if (pcir + 8 <= size && a[pcir] == 'P' && a[pcir + 1] == 'C'
            && a[pcir + 2] == 'I' && a[pcir + 3] == 'R')
            Printf("PCIR at $%04lx: vendor $%04lx device $%04lx\n", pcir,
                   le16(a + pcir + 4), le16(a + pcir + 6));
        if (size > 0x4A)
            Printf("ATI BIOS header pointer (offset $48): $%04lx\n", le16(a + 0x48));
    }
    else
        Printf("no 55 AA signature at the start: $%02lx $%02lx\n",
               (ULONG) a[0], (ULONG) a[1]);
    rc = (diff == 0) ? RETURN_OK : RETURN_WARN;

done:
    if (fh != 0)
        Close(fh);
    if (b != NULL)
        FreeVec(b);
    if (a != NULL)
        FreeVec(a);
    FreeArgs(rda);
    return rc;
}
