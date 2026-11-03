/*
 * MemBench -- how fast the fast RAM streams: sequential longword reads,
 * writes and copies over 1 MB blocks (far more than the 060's 8 KB data
 * cache), MOVE16 (GCC build, 68040 and up) and CopyMemQuick, best of
 * three, in every fast-RAM region with room for two blocks. Read-only for
 * the system: two blocks taken with AllocAbs from a region's largest free
 * chunk and given back.
 *
 *   MemBench
 *
 * Why: on the A4000D (CyberStorm MK II 060) GLBench's streamed loads and
 * stores ran at about 15 MB/s, BusBench's CopyMemQuick at 12.7 MB/s. This
 * names the figure per region and per access kind. All rates are bytes of
 * data a second (a copy counts the bytes copied once).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "eclock.h"

static const char ver[] = "$VER: MemBench 0.1 (03.11.2026)";

#define BLOCK   (1024UL * 1024UL)
#define ROUNDS  3

static volatile ULONG sink;

/* the regions already measured or skipped */
static ULONG done[16];
static ULONG ndone;

static void ReadBlock(const ULONG *p, ULONG n)
{
    ULONG s;

    s = 0;
    for (; n >= 8; n -= 8, p += 8)
        s += p[0] + p[1] + p[2] + p[3] + p[4] + p[5] + p[6] + p[7];
    sink = s;
}

static void WriteBlock(ULONG *p, ULONG n)
{
    for (; n >= 8; n -= 8, p += 8)
    {
        p[0] = n;
        p[1] = n;
        p[2] = n;
        p[3] = n;
        p[4] = n;
        p[5] = n;
        p[6] = n;
        p[7] = n;
    }
}

static void CopyBlock(ULONG *d, const ULONG *s, ULONG n)
{
    for (; n >= 8; n -= 8, d += 8, s += 8)
    {
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = s[3];
        d[4] = s[4];
        d[5] = s[5];
        d[6] = s[6];
        d[7] = s[7];
    }
}

#if defined(__GNUC__) && defined(__mc68000__)
/* MOVE16 (A0)+,(A1)+ line by line; both 16-byte aligned. Encoded as words:
 * the probes are built for the 68020. */
static void Move16Block(ULONG *dst, const ULONG *src, ULONG lines)
{
    register const ULONG *s __asm__("a0") = src;
    register ULONG *d __asm__("a1") = dst;
    register ULONG n __asm__("d0") = lines;

    __asm__ volatile("1:\n\t.word 0xf620,0x9000\n\tsubq.l #1,%2\n\tbne.s 1b"
                     : "+a" (s), "+a" (d), "+d" (n) : : "memory", "cc");
}
#define HAVE_MOVE16 1
#else
#define HAVE_MOVE16 0
#endif

/* Kilobytes a second of the best of ROUNDS runs of kind k. */
static ULONG Rate(ULONG k, ULONG *a, ULONG *b)
{
    ULONG r;
    ULONG t;
    ULONG best;

    best = 0xffffffffUL;
    for (r = 0; r < ROUNDS; r++)
    {
        Forbid();
        t = Clock_Now();
        switch (k)
        {
            case 0: ReadBlock(a, BLOCK / 4); break;
            case 1: WriteBlock(a, BLOCK / 4); break;
            case 2: CopyBlock(b, a, BLOCK / 4); break;
#if HAVE_MOVE16
            case 3: Move16Block(b, a, BLOCK / 16); break;
#endif
            default: CopyMemQuick(a, b, BLOCK); break;
        }
        t = Clock_Us(Clock_Since(t));
        Permit();
        if (t < best)
            best = t;
    }
    return Clock_KBps(BLOCK, best ? best : 1);
}

/* The start of a free chunk of at least bytes in mh (16-byte aligned), or
 * NULL. Read under Forbid: the list changes otherwise. */
static APTR FreeChunk(struct MemHeader *mh, ULONG bytes)
{
    struct MemChunk *mc;
    ULONG a;

    for (mc = mh->mh_First; mc != NULL; mc = mc->mc_Next)
    {
        a = ((ULONG) mc + 15) & ~15UL;
        if (a + bytes <= (ULONG) mc + mc->mc_Bytes)
            return (APTR) a;
    }
    return NULL;
}

int main(void)
{
    struct MemHeader *mh;
    APTR want;
    ULONG *a;
    ULONG *b;
    static const char *const kind[5] = { "read", "write", "copy (move.l)", "copy (MOVE16)",
                                         "CopyMemQuick" };
    ULONG k;
    BOOL m16;

    if (((struct Process *) FindTask(NULL))->pr_CLI == 0)
        return RETURN_FAIL;     /* the Workbench: no console to report to */
    Printf("%s\n", (LONG) (ver + 6));
    if (!Clock_Open())
    {
        Printf("MemBench: no timer.device\n");
        return RETURN_FAIL;
    }
    m16 = HAVE_MOVE16 && (SysBase->AttnFlags & AFF_68040) != 0;
    for (;;)
    {
        /* the regions, one at a time: the list is walked again after each
         * test, because AllocAbs and FreeMem change it */
        ULONG i;

        want = NULL;
        Forbid();
        for (mh = (struct MemHeader *) SysBase->MemList.lh_Head;
             mh->mh_Node.ln_Succ != NULL; mh = (struct MemHeader *) mh->mh_Node.ln_Succ)
        {
            if (!(mh->mh_Attributes & MEMF_FAST))
                continue;
            for (i = 0; i < ndone; i++)
                if (done[i] == (ULONG) mh)
                    break;
            if (i < ndone)
                continue;
            want = FreeChunk(mh, 2 * BLOCK + 16);
            if (ndone < 16)
                done[ndone++] = (ULONG) mh;
            if (want != NULL)
                break;
            Permit();
            Printf("region 0x%08lx-0x%08lx (%s, pri %ld): no 2 MB free chunk, skipped\n",
                   (LONG) mh->mh_Lower, (LONG) mh->mh_Upper,
                   (LONG) (mh->mh_Node.ln_Name ? mh->mh_Node.ln_Name : "?"),
                   (LONG) mh->mh_Node.ln_Pri);
            Forbid();
        }
        if (want != NULL)
            want = AllocAbs(2 * BLOCK, want);
        Permit();
        if (mh->mh_Node.ln_Succ == NULL)
            break;
        if (want == NULL)
            continue;
        a = (ULONG *) want;
        b = a + BLOCK / 4;
        Printf("region 0x%08lx-0x%08lx (%s, pri %ld), blocks at 0x%08lx:\n",
               (LONG) mh->mh_Lower, (LONG) mh->mh_Upper,
               (LONG) (mh->mh_Node.ln_Name ? mh->mh_Node.ln_Name : "?"),
               (LONG) mh->mh_Node.ln_Pri, (LONG) a);
        for (k = 0; k < 5; k++)
        {
            if (k == 3 && !m16)
                continue;
            Printf("  %-15s %6ld KB/s\n", (LONG) kind[k], (LONG) Rate(k, a, b));
        }
        FreeMem(want, 2 * BLOCK);
    }
    Clock_Close();
    return RETURN_OK;
}
