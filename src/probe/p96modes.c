/*
 * P96Modes -- every Picasso96 screen mode: its id, size, depth and RGB
 * format, and whether RadeonGL can draw in it (R5G6B5PC or B8G8R8A8). Reads
 * only. (2026-11-07: GLQuake's Video Modes page lists RadeonGL's 16-bit
 * modes; a size defined only for another depth is not among them.)
 *
 * Three lists: Picasso96's (p96AllocModeListTagList, which stops at 256
 * modes), the ids past it found by probing, and graphics.library's display
 * database in its own order with ModeNotAvailable (docs/p96-mode-list-256.md).
 *
 *   P96Modes [WIDTH/N] [HEIGHT/N] [MINWIDTH/N]
 */

#include <exec/types.h>
#include <exec/lists.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <utility/tagitem.h>
#include <libraries/Picasso96.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/Picasso96.h>
#include <graphics/gfxbase.h>
#include <proto/graphics.h>

static const char ver[] = "$VER: P96Modes 0.1 (07.11.2026)";

struct Library *P96Base;
struct GfxBase *GfxBase;

static CONST_STRPTR FormatName(ULONG f)
{
    switch (f)
    {
        case RGBFB_CLUT: return "CLUT (8 bit)";
        case RGBFB_R8G8B8: return "R8G8B8 (24)";
        case RGBFB_B8G8R8: return "B8G8R8 (24)";
        case RGBFB_R5G6B5PC: return "R5G6B5PC (16)";
        case RGBFB_R5G5B5PC: return "R5G5B5PC (15)";
        case RGBFB_A8R8G8B8: return "A8R8G8B8 (32)";
        case RGBFB_A8B8G8R8: return "A8B8G8R8 (32)";
        case RGBFB_R8G8B8A8: return "R8G8B8A8 (32)";
        case RGBFB_B8G8R8A8: return "B8G8R8A8 (32)";
        case RGBFB_R5G6B5: return "R5G6B5 (16)";
        case RGBFB_R5G5B5: return "R5G5B5 (15)";
        case RGBFB_B5G6R5PC: return "B5G6R5PC (16)";
        case RGBFB_B5G5R5PC: return "B5G5R5PC (15)";
        default: return "other";
    }
}

int main(void)
{
    struct RDArgs *rda;
    LONG args[3];
    struct TagItem t[3];
    struct List *l;
    struct P96Mode *pm;
    ULONG f;
    ULONG n;
    LONG w;
    LONG h;

    args[0] = 0;
    args[1] = 0;
    args[2] = 0;
    rda = ReadArgs("WIDTH/N,HEIGHT/N,MINWIDTH/N", args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "P96Modes");
        return RETURN_ERROR;
    }
    w = args[0] ? *(LONG *) args[0] : 0;
    h = args[1] ? *(LONG *) args[1] : 0;
    P96Base = OpenLibrary("Picasso96API.library", 2);
    if (P96Base == NULL)
    {
        Printf("P96Modes: no Picasso96API.library V2\n");
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    GfxBase = (struct GfxBase *) OpenLibrary("graphics.library", 39);
    if (GfxBase == NULL)
    {
        Printf("P96Modes: no graphics.library V39\n");
        CloseLibrary(P96Base);
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    Printf("%s\n", (ULONG) (ver + 6));
    t[0].ti_Tag = P96MA_FormatsAllowed;
    t[0].ti_Data = 0xffffffffUL;
    t[1].ti_Tag = args[2] ? P96MA_MinWidth : TAG_DONE;
    t[1].ti_Data = args[2] ? (ULONG) *(LONG *) args[2] : 0;
    t[2].ti_Tag = TAG_DONE;
    t[2].ti_Data = 0;
    n = 0;
    l = p96AllocModeListTagList(t);
    if (l != NULL)
    {
        for (pm = (struct P96Mode *) l->lh_Head; pm->Node.ln_Succ != NULL;
             pm = (struct P96Mode *) pm->Node.ln_Succ)
        {
            if ((w != 0 && pm->Width != (UWORD) w) || (h != 0 && pm->Height != (UWORD) h))
                continue;
            f = p96GetModeIDAttr(pm->DisplayID, P96IDA_RGBFORMAT);
            Printf("0x%08lx %4ld x %4ld depth %2ld  %-16s %s  %s\n", pm->DisplayID,
                   (LONG) pm->Width, (LONG) pm->Height, (LONG) pm->Depth, (ULONG) FormatName(f),
                   (ULONG) ((f == RGBFB_R5G6B5PC || f == RGBFB_B8G8R8A8) ? "RadeonGL" : "-       "),
                   (ULONG) pm->Description);
            n++;
        }
        p96FreeModeList(l);
    }
    Printf("%lu modes in p96AllocModeListTagList's list\n", n);
    /* the ids themselves, past the list (2026-11-07: the list stopped at
     * 256 with the A4000D's 1920x1080 Workbench mode not in it): the
     * resolution in bits 16-23, each format's code in bits 0-15 */
    {
        static const UWORD codes[] = {0x1000, 0x1300, 0x1102, 0x1103, 0x1201, 0x1100, 0x1101, 0x1303};
        ULONG r, c, id, m;

        m = 0;
        for (r = 0; r < 0x40; r++)
            for (c = 0; c < sizeof(codes) / sizeof(codes[0]); c++)
            {
                id = 0x51000000UL | (r << 16) | codes[c];
                if (!p96GetModeIDAttr(id, P96IDA_ISP96))
                    continue;
                f = p96GetModeIDAttr(id, P96IDA_RGBFORMAT);
                if ((w != 0 && p96GetModeIDAttr(id, P96IDA_WIDTH) != (ULONG) w)
                    || (h != 0 && p96GetModeIDAttr(id, P96IDA_HEIGHT) != (ULONG) h))
                    continue;
                if (r < 0x20 && w == 0)
                    continue;       /* the list has those */
                Printf("by id 0x%08lx %4ld x %4ld %-16s %s\n", id,
                       (LONG) p96GetModeIDAttr(id, P96IDA_WIDTH), (LONG) p96GetModeIDAttr(id, P96IDA_HEIGHT),
                       (ULONG) FormatName(f),
                       (ULONG) ((f == RGBFB_R5G6B5PC || f == RGBFB_B8G8R8A8) ? "RadeonGL" : "-"));
                m++;
            }
        Printf("%lu modes by id (resolutions 0x20 and up)\n", m);
    }
    /* graphics.library's display database, in its own order (2026-11-07:
     * RadeonGL's walk of it met a 320 x 200 R5G6B5PC id $FFF01100 that
     * p96OpenScreenTagList refuses): every P96 id, in R5G6B5PC or with a
     * prefix other than the board's */
    {
        ULONG id, k, pos;

        k = 0;
        pos = 0;
        for (id = NextDisplayInfo((ULONG) INVALID_ID); id != (ULONG) INVALID_ID; id = NextDisplayInfo(id))
        {
            pos++;
            if (!p96GetModeIDAttr(id, P96IDA_ISP96))
                continue;
            k++;
            f = p96GetModeIDAttr(id, P96IDA_RGBFORMAT);
            if (f != RGBFB_R5G6B5PC && (id >> 24) == 0x51)
                continue;
            if ((w != 0 && p96GetModeIDAttr(id, P96IDA_WIDTH) != (ULONG) w)
                || (h != 0 && p96GetModeIDAttr(id, P96IDA_HEIGHT) != (ULONG) h))
                continue;
            Printf("db #%3lu 0x%08lx %4ld x %4ld %-16s board %ld  avail %ld\n", pos, id,
                   (LONG) p96GetModeIDAttr(id, P96IDA_WIDTH), (LONG) p96GetModeIDAttr(id, P96IDA_HEIGHT),
                   (ULONG) FormatName(f), (LONG) p96GetModeIDAttr(id, P96IDA_BOARDNUMBER),
                   (LONG) ModeNotAvailable(id));
        }
        Printf("display database: %lu ids, %lu of them P96\n", pos, k);
    }
    CloseLibrary((struct Library *) GfxBase);
    CloseLibrary(P96Base);
    FreeArgs(rda);
    return RETURN_OK;
}
