/*
 * GLBench -- what each kind of GL call costs in minigl.library on this
 * machine, so GLQuake's frame (docs/census.md) can be costed call by call
 * and every optimisation measured.
 *
 * Each test repeats one pattern GLQuake uses and prints microseconds per
 * operation (E clock). Run it on the null card (RadeonGLCard=null,
 * RadeonGLCheck=0: the core's CPU work alone) and on the Radeon (with the
 * flushes). The patterns, with their count in a demo1 frame:
 *
 *   fan       glBegin(TRIANGLE_FAN), 5 x (glTexCoord2f, glVertex3f), glEnd
 *             -- the world: 132 primitives, 696 vertices
 *   mtfan     the same with two glMultiTexCoord2fvARB and glVertex3fv
 *             -- demo2's multitexture world
 *   bind      glBindTexture between two textures before each fan
 *             -- 177 binds
 *   state     glEnable / glDisable(GL_BLEND) around each fan -- 219 calls
 *   matrix    glPushMatrix, glTranslatef, 3 x glRotatef, glPopMatrix, a fan
 *             -- 13 uploads
 *   locked    an alias model: 100 vertices (float xyz, ubyte ARGB colour,
 *             float st), glLockArrays, glDrawElements of 150 triangles,
 *             glUnlockArrays -- 31 draws, 1,498 vertices, 3,528 indices
 *   lightmap  glTexSubImage2D of a 16 x 16 lightmap in 4_4_4_4
 *             -- about 5,800 texels
 *   frame     mglSwitchDisplay of an empty frame
 *
 *   GLBench SCALE/N,ONLY/K,PROFILE/K
 *
 * SCALE multiplies the repetitions (1). ONLY runs one test by name.
 *
 * PROFILE=<file> samples where the benchmark spends its time, every
 * millisecond (pcprof.h); tools/profile.py reads the file.
 * GCC only: it links the MiniGL SDK's import library, as GLQuake does.
 */

#include <exec/types.h>
#include <exec/io.h>
#include <exec/tasks.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/minigl.h>
#include "pcprof.h"

static const char ver[] = "$VER: GLBench 0.1 (03.11.2026)";

#define TEMPLATE "SCALE/N,ONLY/K,PROFILE/K"

struct Device *TimerBase = NULL;
static ULONG clk_Freq = 709379;

static ULONG Now(void)
{
    struct EClockVal ev;

    ReadEClock(&ev);
    return ev.ev_lo;
}

/* tenths of a microsecond per op */
static ULONG TenthsPerOp(ULONG ticks, ULONG ops)
{
    double us;

    us = (double) ticks * 1000000.0 / (double) clk_Freq;
    return (ULONG) (us * 10.0 / (double) ops + 0.5);
}

static int Lower(int ch)
{
    return (ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch;
}

static BOOL SameName(const char *a, const char *b)
{
    while (*a != 0 && Lower((UBYTE) *a) == Lower((UBYTE) *b))
    {
        a++;
        b++;
    }
    return (BOOL) (*a == 0 && *b == 0);
}

/* The memory region (MemHeader) an address lies in: its name, attributes
 * and priority. Code in memory the CPU does not cache runs many times
 * slower. */
static void Where(const char *what, APTR addr)
{
    struct MemHeader *mh;
    const char *name;

    Forbid();
    name = "(no MemHeader)";
    for (mh = (struct MemHeader *) SysBase->MemList.lh_Head; mh->mh_Node.ln_Succ != NULL;
         mh = (struct MemHeader *) mh->mh_Node.ln_Succ)
        if ((ULONG) addr >= (ULONG) mh->mh_Lower && (ULONG) addr < (ULONG) mh->mh_Upper)
        {
            name = mh->mh_Node.ln_Name ? mh->mh_Node.ln_Name : "(unnamed)";
            Permit();
            Printf("  %-22s at 0x%08lx: %s, attributes 0x%04lx, priority %ld, 0x%08lx-0x%08lx\n",
                   (LONG) what, (ULONG) addr, (LONG) name, (ULONG) mh->mh_Attributes,
                   (LONG) mh->mh_Node.ln_Pri, (ULONG) mh->mh_Lower, (ULONG) mh->mh_Upper);
            return;
        }
    Permit();
    Printf("  %-22s at 0x%08lx: %s\n", (LONG) what, (ULONG) addr, (LONG) name);
}

int main(void);

static float pos[100 * 3];
static UBYTE col[100 * 4];
static float st[100 * 2];
static UWORD idx[450];
static UWORD lm[16 * 16];
static UBYTE tex[64 * 64 * 3];

static void Fan(void)
{
    glBegin(GL_TRIANGLE_FAN);
    glTexCoord2f(0.0f, 0.0f);
    glVertex3f(-0.5f, -0.5f, 0.0f);
    glTexCoord2f(1.0f, 0.0f);
    glVertex3f(0.5f, -0.5f, 0.0f);
    glTexCoord2f(1.0f, 1.0f);
    glVertex3f(0.6f, 0.4f, 0.0f);
    glTexCoord2f(0.5f, 1.2f);
    glVertex3f(0.0f, 0.6f, 0.0f);
    glTexCoord2f(0.0f, 1.0f);
    glVertex3f(-0.6f, 0.4f, 0.0f);
    glEnd();
}

static void MtFan(void)
{
    static float v[5][3] = { { -0.5f, -0.5f, 0.0f }, { 0.5f, -0.5f, 0.0f },
                                   { 0.6f, 0.4f, 0.0f }, { 0.0f, 0.6f, 0.0f },
                                   { -0.6f, 0.4f, 0.0f } };
    static float t[5][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f },
                                   { 0.5f, 1.2f }, { 0.0f, 1.0f } };
    int k;

    glBegin(GL_TRIANGLE_FAN);
    for (k = 0; k < 5; k++)
    {
        glMultiTexCoord2fvARB(GL_TEXTURE0_ARB, t[k]);
        glMultiTexCoord2fvARB(GL_TEXTURE1_ARB, t[k]);
        glVertex3fv(v[k]);
    }
    glEnd();
}

static void Setup(void)
{
    ULONG i;

    for (i = 0; i < 100; i++)
    {
        pos[i * 3 + 0] = (float) (i % 10) * 0.1f - 0.5f;
        pos[i * 3 + 1] = (float) (i / 10) * 0.1f - 0.5f;
        pos[i * 3 + 2] = 0.0f;
        col[i * 4 + 0] = 255;
        col[i * 4 + 1] = (UBYTE) (i * 2);
        col[i * 4 + 2] = (UBYTE) (i * 3);
        col[i * 4 + 3] = (UBYTE) (255 - i);
        st[i * 2 + 0] = (float) (i % 10) * 0.1f;
        st[i * 2 + 1] = (float) (i / 10) * 0.1f;
    }
    for (i = 0; i < 450; i++)
        idx[i] = (UWORD) ((i * 37) % 100);
    for (i = 0; i < 16 * 16; i++)
        lm[i] = (UWORD) (0xf000 | (i * 13));
    for (i = 0; i < 64 * 64 * 3; i++)
        tex[i] = (UBYTE) (i * 7);

    glViewport(0, 0, 320, 240);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glBindTexture(GL_TEXTURE_2D, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 64, 64, 0, GL_RGB, GL_UNSIGNED_BYTE, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 2);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 64, 64, 0, GL_RGB, GL_UNSIGNED_BYTE, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 3);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, MGL_UNSIGNED_SHORT_4_4_4_4, lm);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 1);
}

struct Test
{
    const char *name;
    ULONG reps;
    ULONG ops_per_rep;
    const char *op;
};

static const struct Test tests[] = {
    { "fan",      2000, 5,    "vertex" },
    { "mtfan",    2000, 5,    "vertex" },
    { "bind",     2000, 1,    "bind + fan" },
    { "state",    2000, 1,    "enable + disable + fan" },
    { "matrix",   1000, 1,    "push..pop + fan" },
    { "locked",   200,  1,    "100-vertex draw" },
    { "lightmap", 500,  256,  "texel" },
    { "frame",    100,  1,    "empty frame" },
    { "fanonly",  2000, 1,    "fan of 5" },
    { "call",     20000, 1,   "glTexCoord2f alone" },
    { "long",     40,   250,  "vertex of a 250-vertex fan" },
    { "wbig",     40000, 1,   "6 stores, 256 KB stream" },
    { "wsmall",   40000, 1,   "6 stores, 1 KB, cached" },
    { "rbig",     40000, 1,   "6 loads, 256 KB stream" },
    { "wm16",     40000, 1,   "6 stores to 1 KB + MOVE16 per 16 B" },
    { "fpureg",   40000, 1,   "fmul + fadd in registers" },
    { "fpustore", 40000, 1,   "fmove.s fp0 to memory (aligned)" },
    { "fpustor2", 40000, 1,   "fmove.s fp0 to memory, address 2 mod 4" },
    { "fpuload",  40000, 1,   "fmove.s memory to fp0" },
    { "hotonly",  40000, 1,   "6 loads from 6 hot lines" },
    { "hotsmall", 40000, 1,   "6 stores to 1 KB + 6 hot loads" },
    { "hotbig",   40000, 1,   "6 stores streamed + 6 hot loads" },
    { "intafter", 40000, 1,   "6 stores streamed + 1 int store, cached" },
    { "fpuafter", 40000, 1,   "6 stores streamed + 1 fmove.s store, cached" },
    { "bindonly", 4000, 1,    "glBindTexture, no draw" },
    { "beginend", 4000, 1,    "empty glBegin/glEnd" },
    { "bindbe",   4000, 1,    "glBindTexture + empty glBegin/glEnd" }
};

static ULONG *membuf;               /* 256 KB for the memory tests, 16-byte aligned */
static APTR membuf_mem;             /* as allocated */

static void Run(ULONG t, ULONG reps)
{
    ULONG r;

    switch (t)
    {
    case 0:
    case 8:
        for (r = 0; r < reps; r++)
            Fan();
        break;
    case 1:
        for (r = 0; r < reps; r++)
            MtFan();
        break;
    case 2:
        for (r = 0; r < reps; r++)
        {
            glBindTexture(GL_TEXTURE_2D, (r & 1) ? 2 : 1);
            Fan();
        }
        break;
    case 3:
        for (r = 0; r < reps; r++)
        {
            glEnable(GL_BLEND);
            Fan();
            glDisable(GL_BLEND);
        }
        break;
    case 4:
        for (r = 0; r < reps; r++)
        {
            glPushMatrix();
            glTranslatef(0.1f, 0.0f, 0.0f);
            glRotatef(10.0f, 1.0f, 0.0f, 0.0f);
            glRotatef(20.0f, 0.0f, 1.0f, 0.0f);
            glRotatef(30.0f, 0.0f, 0.0f, 1.0f);
            Fan();
            glPopMatrix();
        }
        break;
    case 5:
        glVertexPointer(3, GL_FLOAT, 0, pos);
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, col);
        glTexCoordPointer(2, GL_FLOAT, 0, st);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        for (r = 0; r < reps; r++)
        {
            glLockArrays(0, 100);
            glDrawElements(GL_TRIANGLES, 450, GL_UNSIGNED_SHORT, idx);
            glUnlockArrays();
        }
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        break;
    case 6:
        glBindTexture(GL_TEXTURE_2D, 3);
        for (r = 0; r < reps; r++)
        {
            lm[r & 255] ^= 0x0111;
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 16, 16, GL_RGBA,
                            MGL_UNSIGNED_SHORT_4_4_4_4, lm);
            if ((r & 15) == 15)
                Fan();      /* drawn with it now and then, as GLQuake does */
        }
        glBindTexture(GL_TEXTURE_2D, 1);
        break;
    case 7:
        for (r = 0; r < reps; r++)
            mglSwitchDisplay();
        break;
    case 9:
        for (r = 0; r < reps; r++)
            glTexCoord2f(0.5f, 0.5f);
        break;
    case 15:
        {
            float x;

            x = 1.0f;
            for (r = 0; r < reps; r++)
                __asm__ volatile ("fmul.s #0x3f800347,%0\n\tfadd.s #0x3f000000,%0" : "+f" (x));
            if (x == 12345.0f)
                Printf("x\n");
        }
        break;
    case 16:
    case 17:
        {
            UBYTE *q;
            float x;

            x = 0.5f;
            q = (UBYTE *) membuf + (t == 17 ? 2 : 0);
            for (r = 0; r < reps; r++)
                __asm__ volatile ("fmove.s %1,(%0)" : : "a" (q), "f" (x) : "memory");
        }
        break;
    case 18:
        {
            float x;

            x = 0.0f;
            ((float *) membuf)[0] = 0.5f;
            for (r = 0; r < reps; r++)
                __asm__ volatile ("fmove.s (%1),%0" : "=f" (x) : "a" (membuf) : "memory");
            if (x == 12345.0f)
                Printf("x\n");
        }
        break;
    case 19:
    case 20:
    case 21:
        {
            volatile ULONG *b;
            volatile ULONG *hot;
            ULONG mask;
            ULONG sum;
            ULONG w;

            /* six longwords on six cache lines of a 4 KB "core struct" at
             * the end of the buffer, read every iteration, as the core reads
             * its state for every vertex */
            b = (volatile ULONG *) membuf;
            hot = (volatile ULONG *) membuf + 70000;
            mask = (t == 21) ? 65535UL : 255UL;
            sum = 0;
            w = 0;
            for (r = 0; r < reps; r++)
            {
                if (t != 19)
                {
                    b[w & mask] = r;
                    b[(w + 1) & mask] = r;
                    b[(w + 2) & mask] = r;
                    b[(w + 3) & mask] = r;
                    b[(w + 4) & mask] = r;
                    b[(w + 5) & mask] = r;
                }
                sum += hot[0] + hot[100] + hot[250] + hot[480] + hot[700] + hot[960];
                w += 6;
            }
            if (sum == 0x12345678UL)
                Printf("%ld\n", sum);
        }
        break;
    case 24:
        for (r = 0; r < reps; r++)
            glBindTexture(GL_TEXTURE_2D, (r & 1) ? 2 : 1);
        glBindTexture(GL_TEXTURE_2D, 1);
        break;
    case 25:
    case 26:
        for (r = 0; r < reps; r++)
        {
            if (t == 26)
                glBindTexture(GL_TEXTURE_2D, (r & 1) ? 2 : 1);
            glBegin(GL_TRIANGLE_FAN);
            glEnd();
        }
        glBindTexture(GL_TEXTURE_2D, 1);
        break;
    case 22:
    case 23:
        {
            volatile ULONG *b;
            ULONG *cached;
            float x;
            ULONG w;

            b = (volatile ULONG *) membuf;
            cached = (ULONG *) membuf + 70000;
            x = 0.5f;
            w = 0;
            for (r = 0; r < reps; r++)
            {
                b[w & 65535] = r;
                b[(w + 1) & 65535] = r;
                b[(w + 2) & 65535] = r;
                b[(w + 3) & 65535] = r;
                b[(w + 4) & 65535] = r;
                b[(w + 5) & 65535] = r;
                if (t == 22)
                    __asm__ volatile ("move.l %1,(%0)" : : "a" (cached), "d" (r) : "memory");
                else
                    __asm__ volatile ("fmove.s %1,(%0)" : : "a" (cached), "f" (x) : "memory");
                w += 6;
            }
        }
        break;
    case 11:
    case 12:
    case 13:
    case 14:
        {
            volatile ULONG *b;
            ULONG mask;
            ULONG sum;
            ULONG w;

            b = (volatile ULONG *) membuf;
            mask = (t == 12 || t == 14) ? 255UL : 65535UL;     /* 1 KB or 256 KB of longs */
            sum = 0;
            w = 0;
            for (r = 0; r < reps; r++)
            {
                if (t == 13)
                {
                    sum += b[w & mask] + b[(w + 1) & mask] + b[(w + 2) & mask]
                         + b[(w + 3) & mask] + b[(w + 4) & mask] + b[(w + 5) & mask];
                }
                else
                {
                    b[w & mask] = r;
                    b[(w + 1) & mask] = r;
                    b[(w + 2) & mask] = r;
                    b[(w + 3) & mask] = r;
                    b[(w + 4) & mask] = r;
                    b[(w + 5) & mask] = r;
                }
                w += 6;
                if (t == 14 && (w & 3) < 2)
                {
                    /* a 16-byte line out of the cache-resident buffer, MOVE16:
                     * a write of a whole line, no fill (68040/060) */
                    ULONG *src;
                    ULONG *dst;

                    src = (ULONG *) membuf + ((w & 255) & ~3UL);
                    dst = (ULONG *) membuf + 4096 + ((w * 4) & 0xffffUL & ~3UL);
                    __asm__ volatile ("move16 (%0)+,(%1)+" : "+a" (src), "+a" (dst) : : "memory");
                }
            }
            if (sum == 0x12345678UL)
                Printf("%ld\n", sum);
        }
        break;
    case 10:
        for (r = 0; r < reps; r++)
        {
            ULONG k;

            glBegin(GL_TRIANGLE_FAN);
            for (k = 0; k < 250; k++)
            {
                glTexCoord2f(0.5f, 0.5f);
                glVertex3f(st[(k % 100) * 2], st[(k % 100) * 2 + 1], 0.0f);
            }
            glEnd();
        }
        break;
    }
}

int main(void)
{
    struct RDArgs *rda;
    struct MsgPort *port;
    struct timerequest *req;
    struct EClockVal ev;
    LONG args[3];
    ULONG scale;
    ULONG t;
    ULONG reps;
    ULONG t0;
    ULONG ticks;
    ULONG tenths;

    args[0] = 0;
    args[1] = 0;
    args[2] = 0;
    rda = ReadArgs(TEMPLATE, args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "GLBench");
        return RETURN_ERROR;
    }
    scale = (args[0] && *(LONG *) args[0] > 0) ? (ULONG) *(LONG *) args[0] : 1;
    port = CreateMsgPort();
    req = port ? (struct timerequest *) CreateIORequest(port, sizeof(*req)) : NULL;
    if (req == NULL || OpenDevice((STRPTR) TIMERNAME, UNIT_MICROHZ, (struct IORequest *) req, 0) != 0)
    {
        Printf("GLBench: timer.device did not open\n");
        return RETURN_FAIL;
    }
    TimerBase = req->tr_node.io_Device;
    clk_Freq = ReadEClock(&ev);
    Printf("%s\n", (LONG) (ver + 6));
    if (!MiniGLOpen())
    {
        Printf("GLBench: minigl.library 29.1 did not open\n");
        return RETURN_FAIL;
    }
    mglChoosePixelDepth(16);
    if (!mglCreateContext(0, 0, 320, 240))
    {
        Printf("GLBench: no context\n");
        MiniGLClose();
        return RETURN_FAIL;
    }
    Printf("GL_RENDERER: %s\n", (LONG) glGetString(GL_RENDERER));
    Where("the dispatch table", (APTR) MiniGLDispatch);
    Where("a hook in the library", (APTR) MiniGLDispatch->GLTexCoord2f);
    Where("GLBench's code", (APTR) main);
    Where("GLBench's data", (APTR) pos);
    Setup();
    membuf_mem = AllocVec(320 * 1024, MEMF_ANY | MEMF_CLEAR);
    if (membuf_mem == NULL)
        return RETURN_FAIL;
    membuf = (ULONG *) (((ULONG) membuf_mem + 15) & ~15UL);
    mglSwitchDisplay();
    if (args[2] && !PcProf_Start(1000, 8000, 64))
    {
        Printf("GLBench: the sampler did not start\n");
        args[2] = 0;
    }
    for (t = 0; t < sizeof(tests) / sizeof(tests[0]); t++)
    {
        if (args[1] && !SameName((const char *) args[1], tests[t].name))
            continue;
        reps = tests[t].reps * scale;
        mglSwitchDisplay();
        t0 = Now();
        Run(t, reps);
        mglSwitchDisplay();         /* everything built is run */
        ticks = Now() - t0;
        tenths = TenthsPerOp(ticks, reps * tests[t].ops_per_rep);
        Printf("  %-9s %5ld.%ld us a %s  (%ld ms in all)\n", (LONG) tests[t].name,
               (LONG) (tenths / 10), (LONG) (tenths % 10), (LONG) tests[t].op,
               (LONG) TenthsPerOp(ticks, 10000) );
    }
    if (args[2])
        PcProf_Stop((const char *) args[2], (ULONG) MiniGLDispatch->GLTexCoord2f, (ULONG) main);
    Printf("GL error 0x%lx\n", (LONG) glGetError());
    mglDeleteContext();
    MiniGLClose();
    FreeVec(membuf_mem);
    CloseDevice((struct IORequest *) req);
    DeleteIORequest((struct IORequest *) req);
    DeleteMsgPort(port);
    FreeArgs(rda);
    return RETURN_OK;
}
