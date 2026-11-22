/*
 * MapCheck -- does RadeonGL find the card's memory where it really is?
 * (2026-11-21: a user's A1200 with a Mediator 1200 and a Radeon 9250 --
 * every GL program crashed.)
 *
 * RadeonGL works out where a bitmap lies on the card (its GPU address)
 * from the 68k address Picasso96 hands out for it:
 *
 *     gpu = fb_start + (mem - BOARDMEMBASE) % CONFIG_APER_SIZE
 *
 * (lib/card.c ChipCheck; fb_start from MC_FB_LOCATION). That holds when
 * the board's memory is mapped from BOARDMEMBASE in one linear piece, as
 * on the A4000's Mediator and the Prometheus Firestorm. If a bridge maps
 * it differently, every frame's command packets point the Radeon at the
 * wrong memory.
 *
 * MapCheck tests that without drawing anything. The CRTC showing the
 * Workbench knows the true GPU address it scans from (its display base
 * plus CRTC_OFFSET); Picasso96 gives the 68k address of the Workbench's
 * bitmap. If RadeonGL's formula turns the one into the other, the mapping
 * is what RadeonGL assumes. A block of 4 MB (as big as RadeonGL's texture
 * heap) is placed on the board and checked against the card's memory.
 *
 * It only READS registers, inside a Picasso96 lock of a bitmap on the
 * board (as R200Info, the card report), and writes nothing to the card;
 * the screen does not change. With openpci.library present its view of
 * the card is reported beside it, the card never claimed.
 *
 *   MapCheck LOG/K       (default RAM:MapCheck.txt)
 *
 * A Shell program: from the Workbench (no console) it quits at once.
 */

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "p96board.h"
#include "pcicard.h"
#include "mmio.h"
#include "r200probe.h"
#include "plog.h"
#include "r200reg.h"

static const char ver[] = "$VER: MapCheck 0.3 (22.11.2026)";

#define TEMPLATE "LOG/K"
#define BLOCK_BYTES (4UL * 1024UL * 1024UL)    /* RadeonGL's texture heap */

/* not in r200reg.h (the library never reads them) */
#ifndef R_CRTC2_OFFSET_CNTL
#define R_CRTC2_OFFSET_CNTL     0x0328
#endif
#ifndef R_CRTC2_PITCH
#define R_CRTC2_PITCH           0x032c
#endif
#ifndef R_CRTC2_DISPLAY_BASE_ADDR
#define R_CRTC2_DISPLAY_BASE_ADDR 0x033c
#endif
#define CRTC_EN_BIT             (1UL << 25)     /* CRTC_GEN_CNTL / CRTC2_GEN_CNTL */
#define CRTC_OFFSET_MASK        0x07ffffffUL

struct Regs
{
    ULONG memsize, aper_size, aper0, aper1, fb_location, surface_cntl;
    ULONG crtc_gen, crtc_offset, crtc_offset_cntl, crtc_pitch, display_base;
    ULONG crtc2_gen, crtc2_offset, crtc2_offset_cntl, crtc2_pitch, crtc2_display_base;
};

static void ReportSystem(void)
{
    UWORD f;
    struct Library *lib;
    CONST_STRPTR cpu;
    CONST_STRPTR fpu;
    ULONG v060;
    ULONG r060;
    ULONG v040;
    ULONG r040;

    Log_Section("The machine");
    f = SysBase->AttnFlags;
    cpu = (f & AFF_68060) ? (CONST_STRPTR)"68060" : (f & AFF_68040) ? (CONST_STRPTR)"68040" :
          (f & AFF_68030) ? (CONST_STRPTR)"68030" : (f & AFF_68020) ? (CONST_STRPTR)"68020" :
          (CONST_STRPTR)"68000/68010";
    fpu = (f & AFF_FPU40) ? (CONST_STRPTR)"the CPU's own" : (f & AFF_68882) ? (CONST_STRPTR)"68882" :
          (f & AFF_68881) ? (CONST_STRPTR)"68881" : (CONST_STRPTR)"none";
    Log_Printf("exec.library %ld.%ld (Kickstart), CPU %s, FPU %s\n",
               (LONG)SysBase->LibNode.lib_Version, (LONG)SysBase->LibNode.lib_Revision, cpu, fpu);
    v060 = 0;
    r060 = 0;
    v040 = 0;
    r040 = 0;
    Forbid();
    lib = (struct Library *)FindName(&SysBase->LibList, (CONST_STRPTR)"68060.library");
    if (lib != NULL)
    {
        v060 = lib->lib_Version;
        r060 = lib->lib_Revision;
    }
    lib = (struct Library *)FindName(&SysBase->LibList, (CONST_STRPTR)"68040.library");
    if (lib != NULL)
    {
        v040 = lib->lib_Version;
        r040 = lib->lib_Revision;
    }
    Permit();
    if (v060 != 0)
        Log_Printf("68060.library %lu.%lu loaded\n", v060, r060);
    if (v040 != 0)
        Log_Printf("68040.library %lu.%lu loaded\n", v040, r040);
    if (v060 == 0 && v040 == 0)
        Log_Printf("no 68040.library or 68060.library loaded\n");
}

/* The $VER string of LIBS:minigl.library, read from the file (the library
 * is NOT opened: that would start RadeonGL). */
static void ReportMiniGL(void)
{
    static UBYTE buf[4096];
    BPTR fh;
    LONG n;
    LONG i;
    LONG k;
    LONG keep;
    BOOL found;

    Log_Section("The installed minigl.library");
    fh = Open((CONST_STRPTR)"LIBS:minigl.library", MODE_OLDFILE);
    if (fh == 0)
    {
        Log_Printf("LIBS:minigl.library: not found\n");
        return;
    }
    found = FALSE;
    keep = 0;
    while (!found && (n = Read(fh, buf + keep, (LONG)sizeof(buf) - keep)) > 0)
    {
        n += keep;
        for (i = 0; i + 5 < n && !found; i++)
            if (buf[i] == '$' && buf[i + 1] == 'V' && buf[i + 2] == 'E' && buf[i + 3] == 'R'
                && buf[i + 4] == ':')
            {
                for (k = i; k < n && buf[k] >= 32 && buf[k] < 127; k++)
                    ;
                if (k < n)
                {
                    buf[k] = 0;
                    Log_Printf("%s\n", (CONST_STRPTR)(buf + i + 5));
                    found = TRUE;
                }
            }
        /* a string cut at the buffer's end: keep the last 200 bytes */
        keep = (n > 200) ? 200 : n;
        if (!found)
            for (i = 0; i < keep; i++)
                buf[i] = buf[n - keep + i];
    }
    Close(fh);
    if (!found)
        Log_Printf("LIBS:minigl.library: no version string found\n");
}

static void ReadRegs(APTR io, struct Regs *r)
{
    r->memsize = Mmio_Read(io, R_CONFIG_MEMSIZE);
    r->aper_size = Mmio_Read(io, R_CONFIG_APER_SIZE);
    r->aper0 = Mmio_Read(io, R_CONFIG_APER_0_BASE);
    r->aper1 = Mmio_Read(io, R_CONFIG_APER_1_BASE);
    r->fb_location = Mmio_Read(io, R_MC_FB_LOCATION);
    r->surface_cntl = Mmio_Read(io, R_SURFACE_CNTL);
    r->crtc_gen = Mmio_Read(io, R_CRTC_GEN_CNTL);
    r->crtc_offset = Mmio_Read(io, R_CRTC_OFFSET);
    r->crtc_offset_cntl = Mmio_Read(io, R_CRTC_OFFSET_CNTL);
    r->crtc_pitch = Mmio_Read(io, R_CRTC_PITCH);
    r->display_base = Mmio_Read(io, R_DISPLAY_BASE_ADDR);
    r->crtc2_gen = Mmio_Read(io, R_CRTC2_GEN_CNTL);
    r->crtc2_offset = Mmio_Read(io, R_CRTC2_OFFSET);
    r->crtc2_offset_cntl = Mmio_Read(io, R_CRTC2_OFFSET_CNTL);
    r->crtc2_pitch = Mmio_Read(io, R_CRTC2_PITCH);
    r->crtc2_display_base = Mmio_Read(io, R_CRTC2_DISPLAY_BASE_ADDR);
}

/* RadeonGL's formula: the GPU address of a 68k address on the board */
static ULONG GpuOf(const struct Regs *r, APTR membase, APTR mem)
{
    ULONG fb_start;

    fb_start = (r->fb_location & 0xffffUL) << 16;
    return fb_start + ((ULONG)mem - (ULONG)membase) % r->aper_size;
}

/* One CRTC against the Workbench bitmap. TRUE when it shows it as
 * RadeonGL would place it. */
static BOOL CheckCrtc(CONST_STRPTR name, ULONG gen, ULONG base, ULONG offset, ULONG pitch,
                      ULONG wb_gpu, ULONG wb_bytes, ULONG fb_start)
{
    ULONG scan;
    LONG delta;

    if (!(gen & CRTC_EN_BIT))
    {
        Log_Printf("%s: off (GEN_CNTL 0x%08lx)\n", name, gen);
        return FALSE;
    }
    /* the display base is a GPU address; a driver that leaves it at 0
     * scans from the start of the frame buffer */
    scan = ((base != 0) ? base : fb_start) + (offset & CRTC_OFFSET_MASK);
    delta = (LONG)(scan - wb_gpu);
    Log_Printf("%s: on; display base 0x%08lx, offset 0x%08lx, pitch 0x%08lx: scans from GPU 0x%08lx\n",
               name, base, offset, pitch, scan);
    if (delta == 0)
    {
        Log_Printf("  = RadeonGL's address of the Workbench bitmap: MATCH\n");
        return TRUE;
    }
    if (delta > 0 && (ULONG)delta < wb_bytes)
    {
        Log_Printf("  %ld bytes into RadeonGL's Workbench bitmap (a panned screen): MATCH\n", delta);
        return TRUE;
    }
    Log_Printf("  %ld bytes away from RadeonGL's address of the Workbench bitmap (0x%08lx): "
               "MISMATCH\n", delta, wb_gpu);
    return FALSE;
}

int main(void)
{
    static struct P96Board board;   /* zero: P96Board_Close is right before Open */
    struct RDArgs *rda;
    LONG argv[1];
    struct PciCard card;
    struct PciCard pci;
    struct RenderInfo ri;
    struct Regs r;
    ULONG memsize;
    ULONG how;
    ULONG fb_start;
    ULONG fb_end;
    ULONG wb_off;
    ULONG wb_gpu;
    ULONG wb_bytes;
    ULONG blk_off;
    ULONG blk_gpu;
    ULONG vram;
    ULONG p96mem;
    LONG lock;
    APTR wb_mem;
    APTR blk_mem;
    BOOL havePci;
    BOOL m1;
    BOOL m2;
    BOOL problems;
    BOOL doubts;
    int rc;

    if (((struct Process *)FindTask(NULL))->pr_CLI == 0)
        return RETURN_FAIL;
    argv[0] = 0;
    rda = ReadArgs((CONST_STRPTR)TEMPLATE, argv, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), (CONST_STRPTR)"MapCheck");
        return RETURN_ERROR;
    }
    if (!Log_Open(argv[0] ? (CONST_STRPTR)argv[0] : (CONST_STRPTR)"RAM:MapCheck.txt"))
        Log_Printf("MapCheck: cannot write the log file, console only\n");
    Log_Printf("%s -- RadeonGL's card addresses against the card's own (reads only)\n", ver + 6);
    ReportSystem();
    ReportMiniGL();

    rc = RETURN_FAIL;
    havePci = FALSE;
    problems = FALSE;
    doubts = FALSE;
    if (!P96Board_Open(&board, BLOCK_BYTES))
    {
        Log_Printf("MapCheck: Picasso96 gave no 4 MB block on the Workbench's board\n");
        goto out;
    }
    P96Board_Report(&board);
    if (!P96Board_IsRadeon(&board) || board.memIoBase == NULL)
    {
        Log_Printf("MapCheck: the Workbench's board is not a Radeon: nothing to check\n");
        goto out;
    }
    R200_CardFromP96(&card, &board);
    havePci = PciCard_Open(&pci);
    if (havePci)
    {
        PciCard_Report(&pci);
        Log_Printf("openpci: BAR0 (the memory aperture) 0x%08lx, %lu KB; P96's memory base 0x%08lx\n",
                   (ULONG)pci.aper, pci.aperSize >> 10, (ULONG)board.memBase);
    }

    /* the registers and the block, inside the block's lock */
    if (!P96Board_Lock(&board))
    {
        Log_Printf("MapCheck: the 4 MB block could not be locked on the board\n");
        goto out;
    }
    how = Mmio_DetectSwap(card.mmio, havePci ? (pci.cfgBar[0] & 0xfffffff0UL) : 0, &memsize);
    ReadRegs(card.mmio, &r);
    blk_mem = board.mem;
    P96Board_Unlock(&board);

    Log_Section("The card's own view (registers, read only)");
    Log_Printf("register byte order: %s (decided by method %lu)\n",
               Mmio_Swap ? (CONST_STRPTR)"swapped" : (CONST_STRPTR)"native", how);
    Log_Printf("CONFIG_MEMSIZE     0x%08lx (%lu MB)\n", r.memsize, r.memsize >> 20);
    Log_Printf("CONFIG_APER_SIZE   0x%08lx (%lu MB)\n", r.aper_size, r.aper_size >> 20);
    Log_Printf("CONFIG_APER_0_BASE 0x%08lx, CONFIG_APER_1_BASE 0x%08lx\n", r.aper0, r.aper1);
    Log_Printf("MC_FB_LOCATION     0x%08lx, SURFACE_CNTL 0x%08lx\n", r.fb_location, r.surface_cntl);
    Log_Printf("CRTC_GEN_CNTL      0x%08lx, CRTC_OFFSET_CNTL 0x%08lx\n", r.crtc_gen, r.crtc_offset_cntl);
    Log_Printf("CRTC2_GEN_CNTL     0x%08lx, CRTC2_OFFSET_CNTL 0x%08lx\n", r.crtc2_gen, r.crtc2_offset_cntl);
    if (r.aper_size == 0 || (r.aper_size & (r.aper_size - 1)) != 0)
    {
        Log_Printf("CONFIG_APER_SIZE is not a power of two: RadeonGL refuses this card "
                   "(\"CONFIG_APER_SIZE is not a power of two\")\n");
        problems = TRUE;
        goto verdict;
    }
    fb_start = (r.fb_location & 0xffffUL) << 16;
    fb_end = (((r.fb_location >> 16) & 0xffffUL) << 16) | 0xffffUL;
    Log_Printf("the frame buffer in the GPU's address space: 0x%08lx - 0x%08lx\n", fb_start, fb_end);
    /* the card's memory size: CONFIG_MEMSIZE, which reads 0 where no PC
     * BIOS ever set it (MEASURED on the A4000D's Radeon 9200, 2026-11-21:
     * MapCheck's first run took that 0 for the size and called a working
     * card a mismatch); then Picasso96's figure */
    vram = r.memsize;
    if (vram == 0)
    {
        vram = board.totalMem;
        Log_Printf("CONFIG_MEMSIZE reads 0 (no PC BIOS set it): Picasso96's %lu KB of video RAM used\n",
                   vram >> 10);
    }
    else if (board.totalMem != 0 && board.totalMem != r.memsize)
        Log_Printf("note: Picasso96 reports %lu KB of video RAM, the card %lu KB\n",
                   board.totalMem >> 10, r.memsize >> 10);

    /* the Workbench's bitmap */
    Log_Section("The Workbench screen's bitmap");
    lock = P96Board_LockBitMap(board.scrbm, &ri);
    if (lock == 0)
    {
        Log_Printf("MapCheck: the Workbench bitmap could not be locked\n");
        problems = TRUE;
        goto verdict;
    }
    wb_mem = ri.Memory;
    wb_bytes = (ULONG)ri.BytesPerRow * (ULONG)board.scr->Height;
    P96Board_UnlockBitMap(board.scrbm, lock);
    wb_off = (ULONG)wb_mem - (ULONG)board.memBase;
    wb_gpu = GpuOf(&r, board.memBase, wb_mem);
    Log_Printf("68k address 0x%08lx, %lu bytes a row, %lu rows: %lu bytes after P96's memory base\n",
               (ULONG)wb_mem, (ULONG)ri.BytesPerRow, (ULONG)board.scr->Height, wb_off);
    Log_Printf("RadeonGL's GPU address: 0x%08lx (aperture %lu)\n", wb_gpu, wb_off / r.aper_size);
    if ((ULONG)wb_mem < (ULONG)board.memBase)
    {
        Log_Printf("the bitmap lies BELOW P96's memory base: RadeonGL's formula cannot hold\n");
        problems = TRUE;
    }
    else if (vram != 0 && wb_off % r.aper_size >= vram)
    {
        Log_Printf("its offset is beyond the card's %lu MB: RadeonGL's formula cannot hold\n",
                   vram >> 20);
        problems = TRUE;
    }

    Log_Section("Who shows it");
    m1 = CheckCrtc((CONST_STRPTR)"CRTC (the first head)", r.crtc_gen, r.display_base, r.crtc_offset,
                   r.crtc_pitch, wb_gpu, wb_bytes, fb_start);
    m2 = CheckCrtc((CONST_STRPTR)"CRTC2 (the second head)", r.crtc2_gen, r.crtc2_display_base,
                   r.crtc2_offset, r.crtc2_pitch, wb_gpu, wb_bytes, fb_start);
    if (!m1 && !m2)
        problems = TRUE;

    /* the 4 MB block, as RadeonGL's texture heap */
    Log_Section("A 4 MB block, as big as RadeonGL's texture heap");
    blk_off = (ULONG)blk_mem - (ULONG)board.memBase;
    blk_gpu = GpuOf(&r, board.memBase, blk_mem);
    Log_Printf("68k address 0x%08lx: %lu bytes after P96's memory base, RadeonGL's GPU address 0x%08lx\n",
               (ULONG)blk_mem, blk_off, blk_gpu);
    if ((ULONG)blk_mem < (ULONG)board.memBase
        || (vram != 0 && blk_off % r.aper_size + BLOCK_BYTES > vram))
    {
        Log_Printf("it does not fit in the card's memory by RadeonGL's formula: MISMATCH\n");
        problems = TRUE;
    }
    else if (vram == 0)
        Log_Printf("the card's memory size is unknown (CONFIG_MEMSIZE and Picasso96 both 0): not checked\n");
    else
        Log_Printf("inside the card's %lu MB: OK\n", vram >> 20);

    /* the bridge's mapping as openpci sees it: the Workbench bitmap sits
     * at offset 0 on many boards, where the CRTC check above cannot tell a
     * linear mapping from a window over the card's first megabytes */
    Log_Section("The bridge's mapping (openpci)");
    if (!havePci)
        Log_Printf("openpci.library not present: not checked\n");
    else
    {
        if ((ULONG)pci.aper != (ULONG)board.memBase)
        {
            Log_Printf("P96's memory base 0x%08lx is NOT openpci's BAR0 0x%08lx: the card's memory may be\n"
                       "reached through a window, which RadeonGL's formula does not allow for\n",
                       (ULONG)board.memBase, (ULONG)pci.aper);
            doubts = TRUE;
        }
        else
            Log_Printf("P96's memory base = openpci's BAR0 (0x%08lx): OK\n", (ULONG)pci.aper);
        /* what counts is Picasso96's video RAM: every address RadeonGL
         * uses comes from a Picasso96 bitmap. A BAR0 of one aperture holds
         * it whole (2026-11-21, the A1200 tester's Mediator 1200: BAR0
         * 128 MB, Radeon.card manages 64 MB of it; MapCheck 0.2 called
         * that a window and doubted a sound mapping) */
        p96mem = (board.totalMem != 0) ? board.totalMem : vram;
        if (pci.aperSize != 0 && pci.aperSize < 2 * r.aper_size
            && p96mem != 0 && p96mem <= pci.aperSize)
            Log_Printf("BAR0 maps %lu KB, not both of the card's apertures of %lu KB each;\n"
                       "Picasso96's %lu KB of video RAM lie inside it: OK\n",
                       pci.aperSize >> 10, r.aper_size >> 10, p96mem >> 10);
        else if (pci.aperSize != 0 && pci.aperSize < 2 * r.aper_size)
        {
            Log_Printf("BAR0 maps %lu KB, less than Picasso96's %lu KB of video RAM:\n"
                       "a window, not the whole card\n", pci.aperSize >> 10, p96mem >> 10);
            doubts = TRUE;
        }
        else if (pci.aperSize != 0)
            Log_Printf("BAR0 maps %lu KB: both apertures of %lu KB: OK\n", pci.aperSize >> 10,
                       r.aper_size >> 10);
        if (pci.aperSize != 0 && (wb_off + wb_bytes > pci.aperSize || blk_off + BLOCK_BYTES > pci.aperSize))
        {
            Log_Printf("a bitmap of the board lies beyond what BAR0 maps: MISMATCH\n");
            problems = TRUE;
        }
    }

verdict:
    Log_Section("Verdict");
    if (problems)
        Log_Printf("RadeonGL's idea of where the card's memory is does NOT hold on this machine.\n"
                   "Its command packets would point the Radeon at the wrong memory: that would\n"
                   "explain GL programs crashing. Please send this report.\n");
    else if (doubts)
        Log_Printf("The checks that could be made hold, but the bridge reaches the card's memory\n"
                   "differently from the machines RadeonGL was tested on (see \"The bridge's\n"
                   "mapping\"). That may explain GL programs crashing. Please send this report.\n");
    else
        Log_Printf("RadeonGL's card addresses hold on this machine: the Workbench bitmap is where\n"
                   "the Radeon scans it from, and a 4 MB block fits. The crashes have another\n"
                   "cause; please send this report with the RadeonGL log of a crashing run.\n");
    rc = (problems || doubts) ? RETURN_WARN : RETURN_OK;

out:
    if (havePci)
        PciCard_Close(&pci);
    P96Board_Close(&board);
    Log_Printf("\nThe report is in %s\n", argv[0] ? (CONST_STRPTR)argv[0] : (CONST_STRPTR)"RAM:MapCheck.txt");
    Log_Close();
    FreeArgs(rda);
    return rc;
}
