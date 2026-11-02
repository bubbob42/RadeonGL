/*
 * Tri -- the first picture from the R200 (concept section 11, probe 4).
 *
 * The GL core (src/core) draws the scene of triscene.c -- glClear and three
 * triangles through hardware TCL -- into a 256 x 256 ARGB8888 buffer in
 * VRAM; the command processor runs it as CPRing showed it can. Then the
 * buffer is shown in a window on Workbench.
 *
 * One block of VRAM (p96board.h: a friend bitmap of the Workbench screen,
 * 256 pixels of 32 bits a row) holds the ring (16 KB), the colour buffer
 * (rows of the bitmap, so the window can show them), a Z16 buffer and the
 * core's 64 KB heap, each 4 KB aligned. The card is found through
 * Picasso96, and everything that touches the chip -- the register checks,
 * the CP, the draw, the read-back -- happens inside ONE lock of the block,
 * which keeps Radeon.card off the chip (well under a second). Tri needs a
 * 32-bit Workbench: the block takes the screen's format, and the window
 * shows the colour buffer's rows as they are.
 *
 * Safety, in order:
 *  1. the core writes the stream into FAST RAM first; the packet checker
 *     (r2check.c) must pass all of it against the block's ranges, or the
 *     CP is never started;
 *  2. the CP starts and runs CPRing's canary (harmless in both byte orders)
 *     before the stream is copied into the ring;
 *  3. one fence, bounded to 100 ms. If it does not pass, the CP is stopped
 *     (and soft-reset if the engine stays busy) and then, only if the
 *     engine is still busy, the 3D blocks SE, RE and PP are reset -- never
 *     the 2D engine or the render backend, which are Radeon.card's too;
 *  4. the CP registers, PP_TFACTOR_0 and SCRATCH_REG2 are put back as
 *     found (a CP found running, left by a run that crashed, is stopped
 *     first and left stopped). The 3D engine's state stays as the scene left it: Radeon.card
 *     does not use it.
 * Eight pixels are read back through the aperture and compared with what
 * the host's reference rasteriser draws for the same stream (test_sim).
 *
 *   Tri LOG/K,WAIT/N,NOSHOW/S,PPM/K,SCENE/K
 *
 * SCENE=TRI (default) is triscene.c, SCENE=TEX the textured texscene.c.
 *
 * PPM= writes the drawn buffer, read back through the aperture, as a PPM
 * file (remote screenshots of the RTG Workbench do not work).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <libraries/Picasso96.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/Picasso96.h>

#include "p96board.h"
#include "pcicard.h"
#include "mmio.h"
#include "r200regs.h"
#include "r200probe.h"
#include "eclock.h"
#include "plog.h"
#include "r2cs.h"
#include "r2pm4.h"
#include "r2ring.h"
#include "r2cp.h"
#include "r2check.h"
#include "core.h"
#include "triscene.h"
#include "texscene.h"

static const char ver[] = "$VER: Tri 0.2 (02.11.2026)";

#define TEMPLATE "LOG/K,WAIT/N,NOSHOW/S,PPM/K,SCENE/K"

struct Args
{
    STRPTR log;
    LONG *wait;
    LONG noshow;
    STRPTR ppm;
    STRPTR scene;
};

#define RING_DW       4096UL
#define RING_BYTES    (RING_DW * 4)
#define COLOR_BYTES   (TRI_W * TRI_H * 4UL)
#define DEPTH_BYTES   (TRI_W * TRI_H * 2UL)
#define HEAP_BYTES    0x10000UL
#define BLOCK_BYTES   (RING_BYTES + COLOR_BYTES + DEPTH_BYTES + HEAP_BYTES + 4096UL)
#define STREAM_DW     8192UL
#define FENCE_REG     R_SCRATCH_REG2
#define TIMEOUT_US    100000UL

#define MAXPIX        32
#define PAL_FILL      0x80000080UL
#define CANARY_VALUE  0xA5C3C3A5UL
#define MMIO_TEST     0x5A3C3C5AUL

/* RBBM_SOFT_RESET: the 3D blocks only (Linux r100 names) */
#define SOFT_RESET_SE (1UL << 2)
#define SOFT_RESET_RE (1UL << 3)
#define SOFT_RESET_PP (1UL << 4)

struct Ctx
{
    struct PciCard *card;   /* the bases P96 gave */
    struct P96Board *board; /* the block */
    struct R2Hw hw;
    struct R2CpSaved saved;
    ULONG fb_start;         /* GPU address of video RAM's first byte */
    ULONG apSize;           /* CONFIG_APER_SIZE: one aperture */
    ULONG tf_orig;          /* PP_TFACTOR_0 and SCRATCH_REG2 as found */
    ULONG scr_orig;
    ULONG clock_base;
    ULONG bpr;              /* bytes per row of the block (pitch of the colour buffer) */
    ULONG color_row;        /* the colour buffer's first row in the bitmap */
    const char *scene;      /* "TRI" or "TEX" */
    void (*draw)(struct Core *c);
    struct TriPixel pix[MAXPIX]; /* the scene's check pixels */
    ULONG npix;
    ULONG got[MAXPIX];      /* the pixels read back, 0xRRGGBB */
    UBYTE *rgb;             /* the whole buffer read back (PPM=), or NULL */
    BOOL drawn;             /* the fence passed */
};

static struct Core core;
static struct R2ChkState chk;

/* --- struct R2Hw over the probes' MMIO helpers (as CPRing) -------------------- */

static ULONG HwRead(APTR hw, ULONG reg)
{
    return Mmio_Read(((struct Ctx *)hw)->card->mmio, reg);
}

static void HwWrite(APTR hw, ULONG reg, ULONG val)
{
    Mmio_Write(((struct Ctx *)hw)->card->mmio, reg, val);
}

static ULONG HwReadBack(APTR hw, volatile ULONG *p)
{
    (void)hw;
    return *p;
}

static ULONG HwClock(APTR hw)
{
    return Clock_Us(Clock_Since(((struct Ctx *)hw)->clock_base));
}

/* --- the core's environment: fast RAM, nothing to wait for ----------------- */

static APTR EnvAlloc(APTR env, ULONG size)
{
    (void)env;
    return AllocVec(size, MEMF_ANY | MEMF_CLEAR);
}

static void EnvFree(APTR env, APTR mem, ULONG size)
{
    (void)env;
    (void)size;
    FreeVec(mem);
}

static LONG EnvIdle(APTR env)
{
    (void)env;
    return 0;               /* no texture is rewritten in this scene */
}

/* --- helpers ----------------------------------------------------------------- */

static ULONG Align4k(ULONG a)
{
    return (a + 4095UL) & ~4095UL;
}

/* The GPU address of a 68k address in the block: the CPU sees video RAM
 * twice (apertures AP0 and AP1), the GPU once. */
static ULONG Gpu(struct Ctx *c, ULONG cpu)
{
    return c->fb_start + (cpu - (ULONG)c->card->aper) % c->apSize;
}

static BOOL InSurface(struct Ctx *c, ULONG off, ULONG bytes)
{
    ULONG n;
    ULONG lo;
    ULONG hi;
    ULONG inf;

    for (n = 0; n < R_SURFACE_COUNT; n++)
    {
        inf = HwRead(c, R_SURFACE0_INFO + n * R_SURFACE_STRIDE);
        if ((inf & 0x00f70000UL) == 0)
            continue;
        lo = HwRead(c, R_SURFACE0_LOWER_BOUND + n * R_SURFACE_STRIDE);
        hi = HwRead(c, R_SURFACE0_UPPER_BOUND + n * R_SURFACE_STRIDE);
        if (off <= hi && off + bytes - 1 >= lo)
            return TRUE;
    }
    return FALSE;
}

/* The engine busy after the CP stopped: reset the 3D blocks, never the 2D
 * engine or the render backend. TRUE when it is idle afterwards. */
static BOOL Reset3D(struct Ctx *c)
{
    HwWrite(c, R_RBBM_SOFT_RESET, SOFT_RESET_SE | SOFT_RESET_RE | SOFT_RESET_PP);
    (void)HwRead(c, R_RBBM_SOFT_RESET);
    HwWrite(c, R_RBBM_SOFT_RESET, 0);
    (void)HwRead(c, R_RBBM_SOFT_RESET);
    return (BOOL)(R2Cp_WaitIdle(&c->hw, TIMEOUT_US) == R2_OK);
}

/* --- the GPU part, inside one lock of the block -------------------------------- */

/* Inside the lock, before anything is written: the register byte order, a
 * CP left running, where video RAM sits, PP_TFACTOR_0 for the canary.
 * TRUE when the scene may go ahead; tf_orig and scr_orig are then set. */
static BOOL Prepare(struct Ctx *c)
{
    ULONG v;
    ULONG reset;
    LONG e;

    if (R200_ReportEndian(c->card) == MMIO_UNDECIDED)
    {
        Log_Printf("Tri: register byte order undecided: the card is not touched\n");
        return FALSE;
    }
    R2Cp_Save(&c->hw, &c->saved);
    if (R2Cp_InUse(&c->saved))
    {
        /* Radeon.card never runs the CP: a run that crashed left it on */
        Log_Printf("the CP is RUNNING (CSQ_CNTL 0x%08lx): left over from a run that crashed\n",
                   c->saved.csq_cntl);
        e = R2Cp_Reclaim(&c->hw, &c->saved, TIMEOUT_US, &reset);
        Log_Printf("  stopped: %ld%s\n", e,
                   reset ? (CONST_STRPTR)" (the CP was soft-reset)" : (CONST_STRPTR)"");
        if (e != R2_OK)
            return FALSE;
    }
    v = HwRead(c, R_MC_FB_LOCATION);
    c->fb_start = (v & 0xffffUL) << 16;
    c->apSize = HwRead(c, R_CONFIG_APER_SIZE);
    if (c->apSize == 0 || (c->apSize & (c->apSize - 1)) != 0)
    {
        Log_Printf("Tri: CONFIG_APER_SIZE 0x%08lx is not a power of two: nothing drawn\n",
                   c->apSize);
        return FALSE;
    }
    c->tf_orig = HwRead(c, R_PP_TFACTOR_0);
    c->scr_orig = HwRead(c, FENCE_REG);
    HwWrite(c, R_PP_TFACTOR_0, MMIO_TEST);
    v = HwRead(c, R_PP_TFACTOR_0);
    HwWrite(c, R_PP_TFACTOR_0, c->tf_orig);
    if (v != MMIO_TEST)
    {
        Log_Printf("Tri: PP_TFACTOR_0 does not read back (0x%08lx): no canary, nothing drawn\n", v);
        return FALSE;
    }
    return TRUE;
}

static BOOL RenderScene(struct Ctx *c, ULONG *buf)
{
    struct R2Cs bcs;
    struct R2Ring ring;
    struct CoreTarget t;
    struct CoreEnv env;
    struct R2ChkCfg cfg;
    struct R2ChkError err;
    volatile ULONG *mem;
    volatile ULONG *col;
    ULONG *ring_cpu;
    ULONG ring_gpu;
    ULONG color_cpu;
    ULONG depth_cpu;
    ULONG heap_cpu;
    ULONG n;
    ULONG *p;
    ULONG i;
    ULONG t0;
    ULONG dt;
    ULONG rptr;
    ULONG v;
    ULONG seq;
    ULONG reset;
    LONG e;

    if (!P96Board_Lock(c->board))
    {
        Log_Printf("the block is not on the board now: nothing drawn\n");
        return FALSE;
    }
    mem = (volatile ULONG *)c->board->mem;
    c->bpr = c->board->bpr;
    if (!Prepare(c))
    {
        P96Board_Unlock(c->board);
        return FALSE;
    }

    Log_Section("Layout");
    ring_cpu = (ULONG *)Align4k((ULONG)mem);
    color_cpu = Align4k((ULONG)ring_cpu + RING_BYTES);
    depth_cpu = Align4k(color_cpu + COLOR_BYTES);
    heap_cpu = Align4k(depth_cpu + DEPTH_BYTES);
    ring_gpu = Gpu(c, (ULONG)ring_cpu);
    t.width = TRI_W;
    t.height = TRI_H;
    t.color_gpu = Gpu(c, color_cpu);
    t.color_pitch = c->bpr / 4;
    t.color_format = R_COLOR_FORMAT_ARGB8888;
    t.depth_gpu = Gpu(c, depth_cpu);
    t.depth_pitch = TRI_W;
    c->color_row = (color_cpu - (ULONG)mem) / c->bpr;
    Log_Printf("block 0x%08lx, %lu bytes a row\n", (ULONG)mem, c->bpr);
    Log_Printf("ring   GPU 0x%08lx\ncolour GPU 0x%08lx, pitch %lu, row %lu of the bitmap\n"
               "Z16    GPU 0x%08lx\nheap   GPU 0x%08lx, %lu bytes\n",
               ring_gpu, t.color_gpu, t.color_pitch, c->color_row, t.depth_gpu,
               Gpu(c, heap_cpu), HEAP_BYTES);
    if (heap_cpu + HEAP_BYTES > (ULONG)mem + BLOCK_BYTES
        || (color_cpu - (ULONG)mem) % c->bpr != 0 || c->bpr % 32 != 0 || c->bpr / 4 < TRI_W)
    {
        Log_Printf("the layout does not fit the block: nothing drawn\n");
        goto unlock;
    }
    if (InSurface(c, ring_gpu - c->fb_start, heap_cpu + HEAP_BYTES - (ULONG)ring_cpu))
    {
        Log_Printf("the block overlaps a swapping surface window: nothing drawn\n");
        goto unlock;
    }

    /* 1. the stream, in fast RAM, through the GL core */
    Log_Section("Stream (GL core, fast RAM)");
    R2Cs_InitBuffer(&bcs, buf, STREAM_DW, 1);
    env.alloc = EnvAlloc;
    env.free = EnvFree;
    env.wait_idle = EnvIdle;
    env.env = NULL;
    v = Core_Init(&core, &bcs, &env, &t, Gpu(c, heap_cpu),
                  (ULONG *)heap_cpu, HEAP_BYTES);
    if (v != GL_NO_ERROR)
    {
        Log_Printf("Core_Init: GL error 0x%04lx\n", v);
        Core_Free(&core);
        goto unlock;
    }
    c->draw(&core);
    n = (ULONG)(bcs.ptr - buf);
    v = Core_GetError(&core);
    Log_Printf("%lu dwords, stream error %ld, GL error 0x%04lx\n", n, bcs.error, v);
    Core_Free(&core);
    if (bcs.error != R2_OK || v != GL_NO_ERROR || n == 0 || n > RING_DW - 64)
        goto unlock;

    /* every packet through the checker before the CP sees one */
    for (i = 0; i < sizeof(cfg); i++)
        ((UBYTE *)&cfg)[i] = 0;
    cfg.range[0].gpu = t.color_gpu;
    cfg.range[0].size = c->bpr * TRI_H;
    cfg.range[0].kinds = R2K_COLOR;
    cfg.range[1].gpu = t.depth_gpu;
    cfg.range[1].size = DEPTH_BYTES;
    cfg.range[1].kinds = R2K_DEPTH;
    cfg.range[2].gpu = Gpu(c, heap_cpu);
    cfg.range[2].size = HEAP_BYTES;
    cfg.range[2].kinds = R2K_TEXTURE | R2K_VERTEX | R2K_INDEX;
    cfg.nranges = 3;
    cfg.fence_reg = FENCE_REG;
    R2Chk_Reset(&chk);
    e = R2Chk_Run(&cfg, &chk, buf, n, 1, 0, &err);
    Log_Printf("checker: %s (%lu packets, %lu draws, %lu vertices)\n",
               e == R2CE_OK ? (CONST_STRPTR)"clean" : (CONST_STRPTR)R2Chk_ErrorName(e),
               chk.packets, chk.draws, chk.vertices);
    if (e != R2CE_OK)
    {
        Log_Printf("  at dword %lu: what 0x%08lx, value 0x%08lx -- the CP is not started\n",
                   err.at, err.what, err.value);
        goto unlock;
    }

    /* 2. the CP, and the canary */
    Log_Section("Command processor");
    if (R2Cp_WaitIdle(&c->hw, TIMEOUT_US) != R2_OK)
    {
        Log_Printf("the engine is not idle (RBBM_STATUS 0x%08lx): nothing drawn\n",
                   HwRead(c, R_RBBM_STATUS));
        goto unlock;
    }
    R2Cp_LoadMicrocode(&c->hw, R2Cp_Microcode);
    Log_Printf("microcode: %lu of %lu words differ on read-back\n",
               R2Cp_CheckMicrocode(&c->hw, R2Cp_Microcode), (ULONG)R2CP_UCODE_WORDS);
    for (i = 0; i < RING_DW; i++)
        ring_cpu[i] = PAL_FILL;
    (void)HwReadBack(c, &ring_cpu[RING_DW - 1]);
    HwWrite(c, R_PP_TFACTOR_0, 0);
    /* from here on every way out passes stop: */
    e = R2Cp_Start(&c->hw, ring_gpu, RING_DW, 0);
    if (e != R2_OK)
    {
        Log_Printf("R2Cp_Start: %ld\n", e);
        goto stop;
    }
    e = R2Ring_Init(&ring, ring_cpu, ring_gpu, RING_DW, &c->hw, 1, FENCE_REG, TIMEOUT_US);
    if (e != R2_OK)
    {
        Log_Printf("R2Ring_Init: %ld\n", e);
        goto stop;
    }
    ring.kick = 0;
    p = R2Cs_Room(&ring.cs, 16);
    if (p == NULL)
        goto stop;
    p[0] = R2_OUT(&ring.cs, R2_PKT0(R_PP_TFACTOR_0, 0));
    p[1] = R2_OUT(&ring.cs, CANARY_VALUE);
    for (i = 2; i < 16; i++)
        p[i] = PAL_FILL;
    ring.cs.ptr = p + 16;
    t0 = HwClock(c);
    (void)R2Ring_Commit(&ring);
    do
    {
        rptr = HwRead(c, R_CP_RB_RPTR);
        dt = HwClock(c) - t0;
    } while (rptr != 16 && dt < TIMEOUT_US);
    v = HwRead(c, R_PP_TFACTOR_0);
    Log_Printf("canary: RPTR %lu after %lu us, PP_TFACTOR_0 0x%08lx: %s\n", rptr, dt, v,
               (rptr == 16 && v == CANARY_VALUE) ? (CONST_STRPTR)"the CP runs, byte order right"
                                                  : (CONST_STRPTR)"FAILED -- nothing drawn");
    if (rptr != 16 || v != CANARY_VALUE)
        goto stop;

    /* 3. the scene, then one fence */
    Log_Section("Draw");
    p = R2Cs_Room(&ring.cs, n);
    if (p == NULL)
    {
        Log_Printf("no ring room for %lu dwords: %ld\n", n, ring.cs.error);
        goto stop;
    }
    for (i = 0; i < n; i++)
        p[i] = buf[i];      /* already in the ring's byte order */
    ring.cs.ptr = p + n;
    t0 = HwClock(c);
    e = R2Ring_Fence(&ring, &seq);
    if (e == R2_OK)
        e = R2Ring_WaitFence(&ring, seq);
    dt = HwClock(c) - t0;
    Log_Printf("%lu dwords and a fence: %s after %lu us; RPTR %lu, WPTR %lu, RBBM_STATUS 0x%08lx\n",
               n, e == R2_OK ? (CONST_STRPTR)"passed" : (CONST_STRPTR)"TIMED OUT", dt,
               HwRead(c, R_CP_RB_RPTR), HwRead(c, R_CP_RB_WPTR), HwRead(c, R_RBBM_STATUS));
    if (e != R2_OK)
        goto stop;
    c->drawn = TRUE;

    /* the check pixels, through the aperture: ARGB8888 is stored B, G, R, A,
     * so a 68k longword reads 0xBBGGRRAA */
    col = (volatile ULONG *)color_cpu;
    for (i = 0; i < c->npix; i++)
    {
        v = col[c->pix[i].y * (c->bpr / 4) + c->pix[i].x];
        c->got[i] = (((v >> 8) & 0xffUL) << 16) | (((v >> 16) & 0xffUL) << 8) | (v >> 24);
    }
    if (c->rgb != NULL)
    {
        /* the whole buffer while the block is still locked (256 KB of
         * aperture reads, about 60 ms) */
        for (i = 0; i < TRI_W * TRI_H; i++)
        {
            v = col[(i / TRI_W) * (c->bpr / 4) + (i % TRI_W)];
            c->rgb[i * 3] = (UBYTE)(v >> 8);
            c->rgb[i * 3 + 1] = (UBYTE)(v >> 16);
            c->rgb[i * 3 + 2] = (UBYTE)(v >> 24);
        }
    }

stop:
    e = R2Cp_Stop(&c->hw, &c->saved, TIMEOUT_US, &reset);
    Log_Printf("CP stopped: %ld%s; RBBM_STATUS 0x%08lx\n", e,
               reset ? (CONST_STRPTR)" (the CP was soft-reset)" : (CONST_STRPTR)"",
               HwRead(c, R_RBBM_STATUS));
    if (e != R2_OK)
    {
        if (Reset3D(c))
            Log_Printf("the 3D blocks (SE, RE, PP) were reset: the engine is idle\n");
        else
            Log_Printf("THE ENGINE STAYS BUSY (RBBM_STATUS 0x%08lx): save your work and reboot\n",
                       HwRead(c, R_RBBM_STATUS));
    }
unlock:
    HwWrite(c, R_PP_TFACTOR_0, c->tf_orig);
    HwWrite(c, FENCE_REG, c->scr_orig);
    P96Board_Unlock(c->board);
    return c->drawn;
}

/* --- showing it ---------------------------------------------------------------- */

static void WritePpm(struct Ctx *c, CONST_STRPTR path)
{
    BPTR f;
    char hdr[20];
    LONG ok;

    f = Open(path, MODE_NEWFILE);
    if (f == 0)
    {
        Log_Printf("PPM: cannot write %s\n", path);
        return;
    }
    /* "P6\n256 256\n255\n" for TRI_W = TRI_H = 256 */
    CopyMem("P6\n256 256\n255\n", hdr, 15);
    ok = (Write(f, hdr, 15) == 15
          && Write(f, c->rgb, TRI_W * TRI_H * 3) == (LONG)(TRI_W * TRI_H * 3));
    Close(f);
    Log_Printf("PPM: %s %s\n", path, ok ? (CONST_STRPTR)"written" : (CONST_STRPTR)"FAILED");
}

static void Show(struct Ctx *c, LONG seconds)
{
    struct Screen *scr;
    struct Window *win;
    struct IntuiMessage *msg;
    LONG ticks;
    BOOL done;

    scr = LockPubScreen(NULL);
    if (scr == NULL)
        return;
    win = OpenWindowTags(NULL,
        WA_PubScreen, (ULONG)scr,
        WA_Left, 40,
        WA_Top, 40,
        WA_InnerWidth, (ULONG)TRI_W,
        WA_InnerHeight, (ULONG)TRI_H,
        WA_Title, (ULONG)(c->draw == TexScene_Draw ? "Tri TEX: drawn by the R200"
                                                   : "Tri: drawn by the R200"),
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE,
        WA_SmartRefresh, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW,
        TAG_DONE);
    UnlockPubScreen(NULL, scr);
    if (win == NULL)
    {
        Log_Printf("the window did not open\n");
        return;
    }
    BltBitMapRastPort(c->board->bm, 0, (WORD)c->color_row, win->RPort,
                      win->BorderLeft, win->BorderTop, TRI_W, TRI_H, 0xc0);
    Log_Printf("shown in a window for up to %ld s (close it to end sooner)\n", seconds);
    done = FALSE;
    for (ticks = 0; ticks < seconds * 50 && !done; ticks += 10)
    {
        Delay(10);
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL)
        {
            if (msg->Class == IDCMP_CLOSEWINDOW)
                done = TRUE;
            ReplyMsg((struct Message *)msg);
        }
    }
    CloseWindow(win);
}

int main(void)
{
    static struct P96Board board;     /* zero: P96Board_Close is right before Open */
    struct RDArgs *rda;
    struct Args args;
    struct PciCard card;
    struct Ctx c;
    ULONG *buf;
    ULONG t0;
    ULONG i;
    ULONG bad;
    int rc;

    args.log = NULL;
    args.wait = NULL;
    args.noshow = 0;
    args.ppm = NULL;
    args.scene = NULL;
    rda = ReadArgs(TEMPLATE, (LONG *)&args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "Tri");
        return RETURN_ERROR;
    }
    if (!Log_Open(args.log))
        Log_Printf("Tri: cannot write %s, console only\n", args.log);
    Log_Printf("%s -- the first picture from the R200\n", ver + 6);

    rc = RETURN_FAIL;
    buf = NULL;
    for (i = 0; i < sizeof(c); i++)
        ((UBYTE *)&c)[i] = 0;
    c.card = &card;
    c.board = &board;
    c.hw.read_reg = HwRead;
    c.hw.write_reg = HwWrite;
    c.hw.read_back = HwReadBack;
    c.hw.clock_us = HwClock;
    c.hw.hw = &c;
    if (args.scene != NULL && (args.scene[0] == 'T' || args.scene[0] == 't')
        && (args.scene[1] == 'E' || args.scene[1] == 'e'))
    {
        c.scene = "TEX";
        c.draw = TexScene_Draw;
        TexScene_Pixels(c.pix);
        c.npix = TEX_NPIXELS;
    }
    else
    {
        c.scene = "TRI";
        c.draw = TriScene_Draw;
        for (i = 0; i < TRI_NPIXELS; i++)
            c.pix[i] = TriScene_Pixels[i];
        c.npix = TRI_NPIXELS;
    }
    Log_Printf("scene %s, %lu check pixels\n", (CONST_STRPTR)c.scene, c.npix);

    if (!Clock_Open())
    {
        Log_Printf("Tri: timer.device did not open\n");
        goto out;
    }
    c.clock_base = Clock_Now();
    Log_Section("VRAM block");
    if (!P96Board_Open(&board, BLOCK_BYTES))
        goto out;
    P96Board_Report(&board);
    if (!P96Board_IsRadeon(&board) || board.memIoBase == NULL)
    {
        Log_Printf("Tri: the Workbench's board is not a Radeon: the card is not touched\n");
        goto out;
    }
    if (board.bpp != 4)
    {
        Log_Printf("Tri: the Workbench has %lu bytes a pixel; Tri needs a 32-bit Workbench\n",
                   board.bpp);
        goto out;
    }
    R200_CardFromP96(&card, &board);
    buf = (ULONG *)AllocVec(STREAM_DW * 4, MEMF_ANY | MEMF_CLEAR);
    if (buf == NULL)
    {
        Log_Printf("Tri: no fast RAM for the stream\n");
        goto out;
    }
    if (args.ppm != NULL)
        c.rgb = (UBYTE *)AllocVec(TRI_W * TRI_H * 3, MEMF_ANY);

    t0 = Clock_Now();
    RenderScene(&c, buf);
    t0 = Clock_Us(Clock_Since(t0));
    Log_Printf("\n(the board was locked for %lu ms)\n", t0 / 1000);

    Log_Section("Result");
    if (!c.drawn)
    {
        Log_Printf("nothing was drawn\n");
        goto out;
    }
    bad = 0;
    for (i = 0; i < c.npix; i++)
    {
        BOOL ok;

        ok = c.pix[i].exact ? (BOOL)(c.got[i] == c.pix[i].rgb)
                            : (BOOL)(c.got[i] != c.pix[i].rgb);
        if (!ok)
            bad++;
        Log_Printf("  (%3lu, %3lu) 0x%06lx %s %s0x%06lx  %s\n",
                   c.pix[i].x, c.pix[i].y, c.got[i],
                   ok ? (CONST_STRPTR)"ok  " : (CONST_STRPTR)"BAD ",
                   c.pix[i].exact ? (CONST_STRPTR)"want " : (CONST_STRPTR)"not ",
                   c.pix[i].rgb, c.pix[i].what);
    }
    Log_Printf("%lu of %lu pixels as the reference rasteriser draws them\n",
               c.npix - bad, c.npix);
    rc = (bad == 0) ? RETURN_OK : RETURN_WARN;
    if (c.rgb != NULL)
        WritePpm(&c, args.ppm);
    if (!args.noshow)
        Show(&c, (args.wait && *args.wait > 0) ? *args.wait : 20);

out:
    P96Board_Close(&board);
    if (buf != NULL)
        FreeVec(buf);
    if (c.rgb != NULL)
        FreeVec(c.rgb);
    Clock_Close();
    Log_Printf("\nTri: done (rc %ld)\n", (LONG)rc);
    Log_Close();
    FreeArgs(rda);
    return rc;
}
