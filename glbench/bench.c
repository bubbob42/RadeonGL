/*
 * bench.c -- GLBench, a stress benchmark for minigl.library 29.1
 * (RadeonGL), 2026-11-08. docs/concept-benchmark.md; scenes in scene_*.c.
 *
 *   GLBench SCENE/K,FRAMES/N,BURN/N,SHELLS/N,WIDTH/N,HEIGHT/N,DEPTH/N,
 *           WINDOW/S,LEFT/N,TOP/N,NOHUD/S,HOLD/N,LOG/K,SHOT/K,SHOTS/K
 *
 * Bench (the default): the scenes in turn, FRAMES frames each (400), then
 * a result screen (HOLD seconds, 60; a key ends it sooner) and the log
 * (LOG, RAM:GLBench-Result.txt): frames a second, primitives a second,
 * bytes over the bus, and the score -- the geometric mean of each scene's
 * frames a second over the reference machine's, times 1000.
 *
 * Burn (BURN=<minutes>, 0 until Esc): one scene (SCENE=, Pelt) without
 * end; every 250 frames a reference frame is drawn, read back (64 x 64
 * from the middle) and compared with the first; a difference is a picture
 * error, logged with its frame.
 *
 * SCENE: numbers or names, comma-separated ("1,3", "pelt,chrome").
 * SHELLS: Pelt's shells (20). WIDTH x HEIGHT x DEPTH: 640 x 480 x 16, a
 * screen of its own, vertical sync off; WINDOW: in a window on the
 * Workbench at LEFT, TOP. NOHUD: no overlay. SHOT: the result screen as
 * a PPM file; SHOTS: a drawer for one PPM per scene (its middle frame).
 * NORGL: RadeonGL's static-array extension left unused.
 * Keys: Esc ends the run, the space bar skips to the next scene (a
 * skipped scene has no score). Ctrl-C as Esc.
 *
 * Return codes: 0 done; 5 done, with a GL error or a picture error;
 * 20 could not run (no minigl.library 29.1, no context, a scene that
 * could not start).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/datetime.h>
#include <dos/rdargs.h>
#include <devices/timer.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/minigl.h>
#include <math.h>

#include "bench.h"

#ifdef RGL_FOR_030
#define BENCH_CPU " for the 68020/68030"
#else
#define BENCH_CPU ""
#endif

static const char ver[] = "$VER: GLBench 0.1 (08.11.2026)" BENCH_CPU;

#define TEMPLATE "SCENE/K,FRAMES/N,BURN/N,SHELLS/N,WIDTH/N,HEIGHT/N,DEPTH/N,WINDOW/S," \
                 "LEFT/N,TOP/N,NOHUD/S,HOLD/N,LOG/K,SHOT/K,SHOTS/K,NORGL/S"

struct Args
{
    STRPTR scene;
    LONG *frames;
    LONG *burn;
    LONG *shells;
    LONG *width;
    LONG *height;
    LONG *depth;
    LONG window;
    LONG *left;
    LONG *top;
    LONG nohud;
    LONG *hold;
    STRPTR log;
    STRPTR shot;
    STRPTR shots;
    LONG norgl;
};

/* The reference figures: the A4000D (68060, Mediator, Radeon 9200) at 640
 * x 480 x 16. PROVISIONAL until measured there: the score says so. */
#define REF_PROVISIONAL 1

#define CHECK_EVERY 250         /* Burn: frames between picture checks */
#define CHECK_FRAME 120         /* Burn: the reference frame's number */
#define CHECK_SIZE 64           /* Burn: the square read back */
#define MAX_ERRORS_LOGGED 16

const struct BenchScene *const bench_scenes[BENCH_SCENES] =
{
    &scene_pelt, &scene_lanterns, &scene_chrome, &scene_embers, &scene_plasma, &scene_swarm,
    &scene_mirror
};

ULONG bench_prims;
ULONG bench_bytes;
ULONG bench_shells = 20;
ULONG bench_frames;
BOOL bench_rgl;

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;
struct Device *TimerBase = NULL;

static struct MsgPort *clk_port;
static struct timerequest *clk_req;
static ULONG clk_freq = 709379;

static BPTR log_fh;
static struct Window *win;
static LONG scr_w, scr_h;
static GLuint font_tex;
static BOOL font_ok;
static BOOL hud;

struct Result
{
    BOOL ran;                   /* started */
    BOOL full;                  /* every frame drawn */
    ULONG frames;
    float secs;
    float prims;                /* summed over its frames */
    float bytes;
    ULONG glerr;
};

static struct Result results[BENCH_SCENES];

/* --- the clock --------------------------------------------------------------- */

static BOOL ClockOpen(void)
{
    struct EClockVal ev;

    clk_port = CreateMsgPort();
    if (clk_port == NULL)
        return FALSE;
    clk_req = (struct timerequest *) CreateIORequest(clk_port, sizeof(struct timerequest));
    if (clk_req == NULL)
        return FALSE;
    if (OpenDevice((CONST_STRPTR) TIMERNAME, UNIT_MICROHZ, (struct IORequest *) clk_req, 0) != 0)
    {
        DeleteIORequest((struct IORequest *) clk_req);
        clk_req = NULL;
        return FALSE;
    }
    TimerBase = clk_req->tr_node.io_Device;
    clk_freq = ReadEClock(&ev);
    if (clk_freq == 0)
        clk_freq = 709379;
    return TRUE;
}

static void ClockClose(void)
{
    if (clk_req != NULL)
    {
        if (TimerBase != NULL)
            CloseDevice((struct IORequest *) clk_req);
        DeleteIORequest((struct IORequest *) clk_req);
    }
    if (clk_port != NULL)
        DeleteMsgPort(clk_port);
    clk_req = NULL;
    clk_port = NULL;
    TimerBase = NULL;
}

static float Since(const struct EClockVal *since)
{
    struct EClockVal now;
    ULONG lo;
    ULONG hi;

    ReadEClock(&now);
    lo = now.ev_lo - since->ev_lo;
    hi = now.ev_hi - since->ev_hi - ((now.ev_lo < since->ev_lo) ? 1UL : 0UL);
    return ((float) hi * 4294967296.0f + (float) lo) / (float) clk_freq;
}

/* --- text: the log and the console --------------------------------------------- */

/* fmt with up to four LONG arguments, to the console (if any) and the log */
static void Out(const char *fmt, LONG a, LONG b, LONG c, LONG d)
{
    LONG v[4];

    v[0] = a;
    v[1] = b;
    v[2] = c;
    v[3] = d;
    if (Output() != 0)
        VPrintf((CONST_STRPTR) fmt, v);
    if (log_fh != 0)
        VFPrintf(log_fh, (CONST_STRPTR) fmt, v);
}

/* v with one decimal ("12.3") into s */
static char *Tenths(char *s, float v)
{
    LONG n;
    LONG k;
    char d[12];
    char *p;

    n = B_Floor(v * 10.0f + 0.5f);
    if (n < 0)
        n = 0;
    k = 0;
    do
    {
        d[k++] = (char) ('0' + n % 10);
        n /= 10;
    } while (n != 0 && k < 11);
    if (k == 1)
        d[k++] = '0';
    p = s;
    while (k > 1)
        *p++ = d[--k];
    *p++ = '.';
    *p++ = d[0];
    *p = '\0';
    return s;
}

static ULONG Len(const char *s)
{
    ULONG n;

    for (n = 0; s[n] != '\0'; n++)
        ;
    return n;
}

static char Lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

static BOOL Contains(const char *hay, const char *needle)
{
    ULONG i;
    ULONG n;
    ULONG k;

    if (hay == NULL)
        return FALSE;
    n = Len(needle);
    for (i = 0; hay[i] != '\0'; i++)
    {
        for (k = 0; k < n && hay[i + k] == needle[k]; k++)
            ;
        if (k == n)
            return TRUE;
    }
    return FALSE;
}

/* --- the overlay font: topaz 8 from the ROM, into an AI88 texture ----------------- */

#define FONT_W 128
#define FONT_H 64

static void FontOpen(void)
{
    struct TextAttr ta;
    struct TextFont *tf;
    struct RastPort rp;
    struct BitMap bm;
    PLANEPTR plane;
    UBYTE *tex;
    char ch;
    LONG c;
    LONG x;
    LONG y;
    UBYTE bit;

    font_ok = FALSE;
    ta.ta_Name = (STRPTR) "topaz.font";
    ta.ta_YSize = 8;
    ta.ta_Style = 0;
    ta.ta_Flags = 0;
    tf = OpenFont(&ta);
    if (tf == NULL)
        return;
    plane = AllocRaster(FONT_W, FONT_H);
    tex = (UBYTE *) B_Alloc(FONT_W * FONT_H * 2);
    if (plane != NULL && tex != NULL)
    {
        InitBitMap(&bm, 1, FONT_W, FONT_H);
        bm.Planes[0] = plane;
        InitRastPort(&rp);
        rp.BitMap = &bm;
        SetRast(&rp, 0);
        SetFont(&rp, tf);
        SetAPen(&rp, 1);
        SetDrMd(&rp, JAM1);
        for (c = 32; c < 128; c++)
        {
            ch = (char) c;
            Move(&rp, (WORD) (((c - 32) % 16) * 8), (WORD) (((c - 32) / 16) * 8 + tf->tf_Baseline));
            Text(&rp, (STRPTR) &ch, 1);
        }
        WaitBlit();
        for (y = 0; y < FONT_H; y++)
            for (x = 0; x < FONT_W; x++)
            {
                bit = (UBYTE) (plane[y * bm.BytesPerRow + (x >> 3)] & (0x80 >> (x & 7)));
                tex[(y * FONT_W + x) * 2] = 255;
                tex[(y * FONT_W + x) * 2 + 1] = (UBYTE) (bit ? 255 : 0);
            }
        font_tex = B_Texture(GL_LUMINANCE_ALPHA, FONT_W, FONT_H, GL_LUMINANCE_ALPHA, tex, FALSE,
                             GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        font_ok = TRUE;
    }
    B_Free(tex);
    if (plane != NULL)
        FreeRaster(plane, FONT_W, FONT_H);
    CloseFont(tf);
}

static void FontClose(void)
{
    if (font_ok)
        glDeleteTextures(1, &font_tex);
    font_ok = FALSE;
}

/* s at x, y (pixels from the bottom left), glyphs scale x 8 pixels */
static void TextAt(LONG x, LONG y, LONG scale, const char *s)
{
    LONG c;
    float s0;
    float t0;
    float gw;

    gw = (float) (8 * scale);
    glBegin(GL_QUADS);
    for (; *s != '\0'; s++, x += 8 * scale)
    {
        c = (LONG) (UBYTE) *s;
        if (c < 33 || c > 127)
            continue;
        s0 = (float) (((c - 32) % 16) * 8) / (float) FONT_W;
        t0 = (float) (((c - 32) / 16) * 8) / (float) FONT_H;
        glTexCoord2f(s0, t0 + 8.0f / FONT_H);
        glVertex2f((float) x, (float) y);
        glTexCoord2f(s0 + 8.0f / FONT_W, t0 + 8.0f / FONT_H);
        glVertex2f((float) x + gw, (float) y);
        glTexCoord2f(s0 + 8.0f / FONT_W, t0);
        glVertex2f((float) x + gw, (float) y + gw);
        glTexCoord2f(s0, t0);
        glVertex2f((float) x, (float) y + gw);
    }
    glEnd();
}

/* The state every scene starts from (and the overlay draws in) */
static void Reset(void)
{
    LONG u;

    for (u = 1; u >= 0; u--)
    {
        glActiveTextureARB((GLenum) (GL_TEXTURE0_ARB + u));
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_TEXTURE_GEN_S);
        glDisable(GL_TEXTURE_GEN_T);
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glClientActiveTextureARB((GLenum) (GL_TEXTURE0_ARB + u));
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisable(GL_LIGHTING);
    for (u = 0; u < 8; u++)
        glDisable((GLenum) (GL_LIGHT0 + u));
    glDisable(GL_COLOR_MATERIAL);
    glDisable(GL_NORMALIZE);
    glDisable(GL_FOG);
    glDisable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ZERO);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glShadeModel(GL_SMOOTH);
    glPointSize(1.0f);
    glLineWidth(1.0f);
    glViewport(0, 0, (GLsizei) scr_w, (GLsizei) scr_h);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* The overlay: a dark band at the bottom, a line of text on it */
static void Overlay(const char *line)
{
    if (!hud || !font_ok)
        return;
    Reset();
    B_Ortho2D(scr_w, scr_h);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 0.0f, 0.0f, 0.55f);
    glBegin(GL_QUADS);
    glVertex2f(0.0f, 0.0f);
    glVertex2f((float) scr_w, 0.0f);
    glVertex2f((float) scr_w, 14.0f);
    glVertex2f(0.0f, 14.0f);
    glEnd();
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, font_tex);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glColor4f(1.0f, 0.86f, 0.55f, 1.0f);
    TextAt(6, 3, 1, line);
    bench_prims += 2 + 2 * Len(line);
    Reset();
}

void B_Label(LONG x, LONG y, LONG scale, const char *s, float r, float g, float b)
{
    if (!font_ok)
        return;
    Reset();
    B_Ortho2D(scr_w, scr_h);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, font_tex);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glColor4f(r, g, b, 1.0f);
    TextAt(x, y, scale, s);
    bench_prims += 2 * Len(s);
    Reset();
}

/* --- keys ------------------------------------------------------------------- */

#define KEY_NONE 0
#define KEY_QUIT 1
#define KEY_NEXT 2
#define KEY_ANY  3

static ULONG Keys(void)
{
    struct IntuiMessage *im;
    ULONG what;

    what = KEY_NONE;
    if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
        what = KEY_QUIT;
    if (win == NULL || win->UserPort == NULL)
        return what;
    while ((im = (struct IntuiMessage *) GetMsg(win->UserPort)) != NULL)
    {
        if (im->Class == IDCMP_VANILLAKEY)
        {
            if (im->Code == 27)
                what = KEY_QUIT;
            else if (im->Code == ' ' && what == KEY_NONE)
                what = KEY_NEXT;
            else if (what == KEY_NONE)
                what = KEY_ANY;
        }
        ReplyMsg((struct Message *) im);
    }
    return what;
}

/* --- the line of the overlay ------------------------------------------------------ */

static char line[96];
static ULONG line_n;

static void LAdd(const char *s)
{
    while (*s != '\0' && line_n < sizeof(line) - 1)
        line[line_n++] = *s++;
    line[line_n] = '\0';
}

static void LNum(ULONG v)
{
    char d[12];
    LONG k;

    k = 0;
    do
    {
        d[k++] = (char) ('0' + v % 10);
        v /= 10;
    } while (v != 0 && k < 11);
    while (k > 0 && line_n < sizeof(line) - 1)
        line[line_n++] = d[--k];
    line[line_n] = '\0';
}

/* --- scene picking -------------------------------------------------------------- */

/* SCENE= into picks[] (scene indices); the number of picks, 0 on an error */
static ULONG Pick(const char *spec, ULONG *picks)
{
    ULONG n;
    ULONG i;
    ULONG k;
    char word[16];
    const char *nm;
    BOOL same;

    if (spec == NULL || *spec == '\0')
    {
        for (i = 0; i < BENCH_SCENES; i++)
            picks[i] = i;
        return BENCH_SCENES;
    }
    n = 0;
    while (*spec != '\0' && n < BENCH_SCENES)
    {
        k = 0;
        while (*spec != '\0' && *spec != ',' && k < sizeof(word) - 1)
            word[k++] = Lower(*spec++);
        word[k] = '\0';
        if (*spec == ',')
            spec++;
        if (k == 0)
            continue;
        if (word[0] >= '1' && word[0] <= '0' + BENCH_SCENES && word[1] == '\0')
        {
            picks[n++] = (ULONG) (word[0] - '1');
            continue;
        }
        for (i = 0; i < BENCH_SCENES; i++)
        {
            nm = bench_scenes[i]->name;
            same = TRUE;
            for (k = 0; word[k] != '\0' || nm[k] != '\0'; k++)
                if (word[k] != Lower(nm[k]))
                {
                    same = FALSE;
                    break;
                }
            if (same)
                break;
        }
        if (i == BENCH_SCENES)
        {
            B_Say("GLBench: no scene \"%s\" (1-7, or pelt, lanterns, chrome, embers, plasma, "
                  "swarm, mirror)\n", (LONG) word, 0, 0, 0);
            return 0;
        }
        picks[n++] = i;
    }
    return n;
}

/* --- the machine, for the log ------------------------------------------------------- */

static void Machine(LONG w, LONG h, LONG depth, BOOL window)
{
    struct DateTime dt;
    char date[LEN_DATSTRING];
    char time[LEN_DATSTRING];
    UWORD af;
    const char *cpu;
    const char *fpu;

    DateStamp(&dt.dat_Stamp);
    dt.dat_Format = FORMAT_DOS;
    dt.dat_Flags = 0;
    dt.dat_StrDay = NULL;
    dt.dat_StrDate = (STRPTR) date;
    dt.dat_StrTime = (STRPTR) time;
    date[0] = time[0] = '\0';
    DateToStr(&dt);
    af = SysBase->AttnFlags;
    cpu = (af & 0x80) ? "68060" : (af & AFF_68040) ? "68040" : (af & AFF_68030) ? "68030"
        : (af & AFF_68020) ? "68020" : "68000/010";
    fpu = (af & AFF_FPU40) ? "internal FPU" : (af & AFF_68882) ? "68882" : (af & AFF_68881) ? "68881"
        : "no FPU";
    Out("%s, %s %s\n", (LONG) (ver + 6), (LONG) date, (LONG) time, 0);
    Out("CPU %s with %s\n", (LONG) cpu, (LONG) fpu, 0, 0);
    Out("GL_RENDERER %s\n", (LONG) glGetString(GL_RENDERER), 0, 0, 0);
    Out("GL_VERSION %s\n", (LONG) glGetString(GL_VERSION), 0, 0, 0);
    if (window)
        Out("a %ld x %ld window on the Workbench\n", w, h, 0, 0);
    else
        Out("a %ld x %ld x %ld screen\n", w, h, depth, 0);
    Out("GL_RGL_static_arrays %s\n\n", (LONG) (bench_rgl ? "used" : "not offered"), 0, 0, 0);
}

/* --- Bench -------------------------------------------------------------------------- */

/* One scene: FRAMES frames, timed from the first to the last swap. KEY_QUIT
 * when the run is to end. */
static ULONG RunScene(ULONG si, ULONG idx, ULONG nsel, ULONG frames, const char *shots)
{
    const struct BenchScene *s;
    struct Result *r;
    struct EClockVal t0;
    struct EClockVal tf;
    float fps;
    float paused;
    ULONG f;
    ULONG key;
    char shot[160];
    char num[16];
    ULONG n;
    ULONG k;
    float ft[16];
    float last;
    float now;

    s = bench_scenes[si];
    r = &results[si];
    r->ran = TRUE;
    r->full = FALSE;
    r->frames = 0;
    r->secs = 0.0f;
    r->prims = 0.0f;
    r->bytes = 0.0f;
    Reset();
    if (!s->init(scr_w, scr_h))
    {
        Out("%s could not start\n", (LONG) s->name, 0, 0, 0);
        r->ran = FALSE;
        Reset();
        return KEY_QUIT;
    }
    (void) glGetError();
    bench_frames = frames;
    key = KEY_NONE;
    paused = 0.0f;
    for (n = 0; n < 16; n++)
        ft[n] = 0.0f;
    ReadEClock(&t0);
    last = 0.0f;
    for (f = 0; f < frames; f++)
    {
        key = Keys();
        if (key == KEY_QUIT || key == KEY_NEXT)
            break;
        bench_prims = 0;
        bench_bytes = 0;
        s->frame(f, scr_w, scr_h);
        r->prims += (float) bench_prims;
        r->bytes += (float) bench_bytes;
        now = Since(&t0) - paused;
        ft[f & 15] = now - last;
        last = now;
        fps = 0.0f;
        for (n = 0; n < 16; n++)
            fps += ft[n];
        fps = (fps > 0.0f) ? (float) (f < 16 ? f + 1 : 16) / fps : 0.0f;
        line_n = 0;
        line[0] = '\0';
        LAdd("GLBench ");
        LNum(idx + 1);
        LAdd("/");
        LNum(nsel);
        LAdd("  ");
        LAdd(s->name);
        LAdd("  frame ");
        LNum(f + 1);
        LAdd("/");
        LNum(frames);
        LAdd("  ");
        LAdd(Tenths(num, fps));
        LAdd(" fps");
        Overlay(line);
        if (shots != NULL && f == frames / 2)
        {
            /* the middle frame to a file; its time not counted */
            ReadEClock(&tf);
            n = 0;
            while (shots[n] != '\0' && n < sizeof(shot) - 40)
            {
                shot[n] = shots[n];
                n++;
            }
            if (n > 0 && shot[n - 1] != ':' && shot[n - 1] != '/')
                shot[n++] = '/';
            shot[n] = '\0';
            line_n = 0;
            line[0] = '\0';
            LAdd("GLBench-");
            LNum(si + 1);
            LAdd("-");
            LAdd(s->name);
            LAdd(".ppm");
            for (k = 0; line[k] != '\0' && n < sizeof(shot) - 1; k++)
                shot[n++] = line[k];
            shot[n] = '\0';
            mglWriteShotPPM(shot);
            paused += Since(&tf);
        }
        mglSwitchDisplay();
        r->frames = f + 1;
    }
    r->secs = Since(&t0) - paused;
    r->full = (r->frames == frames);
    r->glerr = (ULONG) glGetError();
    s->done();
    Reset();
    return key == KEY_QUIT ? KEY_QUIT : KEY_NONE;
}

static float Fps(const struct Result *r)
{
    return (r->secs > 0.0f) ? (float) r->frames / r->secs : 0.0f;
}

/* The table, to the console and the log; the score (0 when it has none) */
static float Report(const ULONG *picks, ULONG n, BOOL *partial)
{
    const struct BenchScene *s;
    const struct Result *r;
    char a[16];
    char b[16];
    char c[16];
    double lnsum;
    ULONG k;
    ULONG scored;
    float fps;

    Out("scene      frames    fps   Mprim/s   MB/s  points\n", 0, 0, 0, 0);
    lnsum = 0.0;
    scored = 0;
    for (k = 0; k < n; k++)
    {
        s = bench_scenes[picks[k]];
        r = &results[picks[k]];
        if (!r->ran)
            continue;
        fps = Fps(r);
        Tenths(a, fps);
        Tenths(b, (r->secs > 0.0f) ? r->prims / r->secs / 1000000.0f : 0.0f);
        Tenths(c, (r->secs > 0.0f) ? r->bytes / r->secs / 1048576.0f : 0.0f);
        Out("%-9s %7ld %6s", (LONG) s->name, (LONG) r->frames, (LONG) a, 0);
        Out("  %6s  %6s", (LONG) b, (LONG) c, 0, 0);
        if (r->full)
        {
            Out("  %6ld\n", (LONG) B_Floor(fps * 10000.0f / (float) s->ref10 + 0.5f), 0, 0, 0);
            lnsum += log((double) fps * 10.0 / (double) s->ref10);
            scored++;
        }
        else
            Out("  skipped\n", 0, 0, 0, 0);
        if (r->glerr != GL_NO_ERROR)
            Out("          GL error 0x%lx\n", (LONG) r->glerr, 0, 0, 0);
    }
    *partial = (scored != BENCH_SCENES);
    if (scored == 0)
        return 0.0f;
    return (float) (1000.0 * exp(lnsum / (double) scored));
}

static void Rect(float x0, float y0, float x1, float y1)
{
    glBegin(GL_QUADS);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
}

/* The result screen: the table drawn, until a key or hold seconds */
static void ResultScreen(const ULONG *picks, ULONG n, float score, BOOL partial, LONG hold,
                         const char *shotfile)
{
    struct EClockVal t0;
    const struct BenchScene *s;
    const struct Result *r;
    char num[16];
    LONG y;
    LONG sc;
    ULONG k;
    float fps;
    float bar;
    BOOL shot;

    if (!font_ok || (hold <= 0 && shotfile == NULL))
        return;
    sc = (scr_w >= 600) ? 2 : 1;
    ReadEClock(&t0);
    shot = (shotfile != NULL);
    for (;;)
    {
        Reset();
        glClearColor(0.07f, 0.09f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        B_Ortho2D(scr_w, scr_h);
        glDisable(GL_DEPTH_TEST);
        /* the bars first, untextured */
        for (k = 0; k < n; k++)
        {
            s = bench_scenes[picks[k]];
            r = &results[picks[k]];
            if (!r->ran)
                continue;
            fps = Fps(r);
            bar = fps * 1000.0f / (float) s->ref10;      /* 100 points = 10 pixels x sc */
            if (bar > (float) (scr_w / 3))
                bar = (float) (scr_w / 3);
            y = scr_h - (LONG) (56 * sc / 2) - (LONG) k * 18 * sc;
            glColor4f(0.17f, 0.20f, 0.26f, 1.0f);
            Rect((float) (scr_w * 3 / 5), (float) y, (float) (scr_w * 3 / 5 + scr_w / 3),
                    (float) (y + 7 * sc));
            glColor4f(r->full ? 0.93f : 0.45f, r->full ? 0.48f : 0.45f, r->full ? 0.36f : 0.45f, 1.0f);
            Rect((float) (scr_w * 3 / 5), (float) y, (float) (scr_w * 3 / 5) + bar * (float) sc / 2.0f,
                    (float) (y + 7 * sc));
        }
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, font_tex);
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.5f);
        glColor4f(0.93f, 0.48f, 0.36f, 1.0f);
        TextAt(16, scr_h - 20 * sc, sc, "GLBench");
        glColor4f(0.60f, 0.65f, 0.71f, 1.0f);
        TextAt(16 + 8 * 8 * sc, scr_h - 20 * sc, 1, ver + 14);
        for (k = 0; k < n; k++)
        {
            s = bench_scenes[picks[k]];
            r = &results[picks[k]];
            if (!r->ran)
                continue;
            y = scr_h - (LONG) (56 * sc / 2) - (LONG) k * 18 * sc;
            line_n = 0;
            line[0] = '\0';
            LNum(picks[k] + 1);
            LAdd(" ");
            LAdd(s->name);
            glColor4f(0.89f, 0.91f, 0.94f, 1.0f);
            TextAt(16, y, sc, line);
            line_n = 0;
            line[0] = '\0';
            LAdd(Tenths(num, Fps(r)));
            LAdd(r->full ? " fps" : " fps skipped");
            TextAt(16 + 13 * 8 * sc, y, sc, line);
        }
        line_n = 0;
        line[0] = '\0';
        LAdd("Score ");
        LNum((ULONG) B_Floor(score + 0.5f));
        if (partial)
            LAdd(" (not all scenes)");
#if REF_PROVISIONAL
        LAdd(" *");
#endif
        glColor4f(0.96f, 0.97f, 0.98f, 1.0f);
        TextAt(16, 16 * sc, sc + (sc > 1 ? 1 : 0), line);
#if REF_PROVISIONAL
        glColor4f(0.60f, 0.65f, 0.71f, 1.0f);
        TextAt(16, 4, 1, "* against provisional reference figures");
#endif
        Reset();
        if (shot)
        {
            mglWriteShotPPM((char *) shotfile);
            shot = FALSE;
        }
        mglSwitchDisplay();
        if (hold <= 0 || Keys() != KEY_NONE || Since(&t0) >= (float) hold)
            break;
        Delay(5);
    }
}

/* --- Burn --------------------------------------------------------------------------- */

static ULONG Hash(const UBYTE *p, ULONG n)
{
    ULONG h;

    h = 2166136261UL;
    while (n-- > 0)
    {
        h ^= *p++;
        h *= 16777619UL;
    }
    return h;
}

/* The reference frame drawn and read back: its hash */
static ULONG Check(const struct BenchScene *s, UBYTE *buf)
{
    bench_prims = 0;
    bench_bytes = 0;
    s->frame(CHECK_FRAME, scr_w, scr_h);
    glReadPixels((GLint) (scr_w / 2 - CHECK_SIZE / 2), (GLint) (scr_h / 2 - CHECK_SIZE / 2),
                 CHECK_SIZE, CHECK_SIZE, GL_RGB, GL_UNSIGNED_BYTE, buf);
    return Hash(buf, CHECK_SIZE * CHECK_SIZE * 3);
}

static int Burn(ULONG si, LONG minutes)
{
    const struct BenchScene *s;
    struct EClockVal t0;
    struct EClockVal tw;
    UBYTE *buf;
    char num[16];
    ULONG f;
    ULONG wf;
    ULONG checks;
    ULONG errors;
    ULONG ref;
    ULONG h;
    ULONG key;
    ULONG secs;
    float wfps;
    float minfps;
    float maxfps;
    float el;
    BOOL have_ref;

    s = bench_scenes[si];
    bench_frames = 0;
    buf = (UBYTE *) B_Alloc(CHECK_SIZE * CHECK_SIZE * 3);
    if (buf == NULL)
        return RETURN_FAIL;
    Reset();
    if (!s->init(scr_w, scr_h))
    {
        Out("%s could not start\n", (LONG) s->name, 0, 0, 0);
        B_Free(buf);
        return RETURN_FAIL;
    }
    Out("Burn: %s, ", (LONG) s->name, 0, 0, 0);
    if (minutes > 0)
        Out("%ld minutes", minutes, 0, 0, 0);
    else
        Out("until Esc", 0, 0, 0, 0);
    Out(s->pure ? "; a picture check every %ld frames\n" : "; no picture check (the scene's frames "
        "depend on the ones before)\n", CHECK_EVERY, 0, 0, 0);
    checks = errors = 0;
    ref = 0;
    have_ref = FALSE;
    minfps = 1.0e9f;
    maxfps = 0.0f;
    wfps = 0.0f;
    ReadEClock(&t0);
    tw = t0;
    wf = 0;
    key = KEY_NONE;
    for (f = 0;; f++)
    {
        key = Keys();
        if (key == KEY_QUIT)
            break;
        el = Since(&t0);
        if (minutes > 0 && el >= (float) minutes * 60.0f)
            break;
        if (s->pure && f > 0 && f % CHECK_EVERY == 0)
        {
            h = Check(s, buf);
            checks++;
            if (!have_ref)
            {
                ref = h;
                have_ref = TRUE;
            }
            else if (h != ref)
            {
                errors++;
                if (errors <= MAX_ERRORS_LOGGED)
                    Out("picture error at frame %ld (%ld s): hash %08lx, the first %08lx\n", (LONG) f,
                        (LONG) B_Floor(el), (LONG) h, (LONG) ref);
            }
        }
        bench_prims = 0;
        bench_bytes = 0;
        s->frame(f, scr_w, scr_h);
        /* frames a second over 5-second windows: the slowest and fastest */
        wf++;
        if (Since(&tw) >= 5.0f)
        {
            wfps = (float) wf / Since(&tw);
            if (wfps < minfps)
                minfps = wfps;
            if (wfps > maxfps)
                maxfps = wfps;
            ReadEClock(&tw);
            wf = 0;
        }
        secs = (ULONG) B_Floor(el);
        line_n = 0;
        line[0] = '\0';
        LAdd("GLBench Burn  ");
        LAdd(s->name);
        LAdd("  ");
        LNum(secs / 60);
        LAdd(":");
        LNum((secs % 60) / 10);
        LNum(secs % 10);
        if (minutes > 0)
        {
            LAdd(" of ");
            LNum((ULONG) minutes);
            LAdd(":00");
        }
        LAdd("  ");
        LAdd(Tenths(num, wfps));
        LAdd(" fps  checks ");
        LNum(checks);
        LAdd("  errors ");
        LNum(errors);
        Overlay(line);
        mglSwitchDisplay();
    }
    el = Since(&t0);
    s->done();
    Reset();
    B_Free(buf);
    Out("Burn ended after %ld s, %ld frames", (LONG) B_Floor(el), (LONG) f, 0, 0);
    Out(", %s fps on average", (LONG) Tenths(num, el > 0.0f ? (float) f / el : 0.0f), 0, 0, 0);
    if (maxfps > 0.0f)
    {
        Out(" (5-second windows: %s to ", (LONG) Tenths(num, minfps), 0, 0, 0);
        Out("%s)", (LONG) Tenths(num, maxfps), 0, 0, 0);
    }
    Out("\npicture checks %ld, errors %ld%s\n", (LONG) checks, (LONG) errors,
        (LONG) (key == KEY_QUIT ? "; ended by Esc" : ""), 0);
    return errors != 0 ? RETURN_WARN : RETURN_OK;
}

/* --- main --------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    struct RDArgs *rda;
    struct Args args;
    ULONG picks[BENCH_SCENES];
    ULONG npicks;
    ULONG k;
    ULONG frames;
    LONG depth;
    LONG left;
    LONG top;
    LONG hold;
    LONG burn;
    float score;
    char num[16];
    int rc;
    BOOL opened;
    BOOL partial;
    BOOL anyerr;
    const char *logname;

    (void) argv;
    rda = NULL;
    args.scene = NULL;
    args.frames = args.burn = args.shells = NULL;
    args.width = args.height = args.depth = NULL;
    args.window = 0;
    args.left = args.top = NULL;
    args.nohud = 0;
    args.hold = NULL;
    args.log = args.shot = args.shots = NULL;
    args.norgl = 0;
    if (argc != 0)
    {
        rda = ReadArgs((CONST_STRPTR) TEMPLATE, (LONG *) &args, NULL);
        if (rda == NULL)
        {
            PrintFault(IoErr(), (CONST_STRPTR) "GLBench");
            return RETURN_ERROR;
        }
    }
    frames = (args.frames && *args.frames > 0) ? (ULONG) *args.frames : 400;
    burn = (args.burn != NULL) ? (*args.burn > 0 ? *args.burn : 0) : -1;
    if (args.shells && *args.shells >= 1 && *args.shells <= 64)
        bench_shells = (ULONG) *args.shells;
    scr_w = (args.width && *args.width >= 160 && *args.width <= 2048) ? *args.width : 640;
    scr_h = (args.height && *args.height >= 120 && *args.height <= 2048) ? *args.height : 480;
    depth = (args.depth && *args.depth == 32) ? 32 : 16;
    left = (args.left && *args.left >= 0) ? *args.left : 30;
    top = (args.top && *args.top >= 0) ? *args.top : 30;
    hold = (args.hold != NULL) ? *args.hold : 60;
    hud = !args.nohud;
    logname = args.log ? (const char *) args.log : "RAM:GLBench-Result.txt";
    npicks = Pick((const char *) args.scene, picks);
    if (npicks == 0)
    {
        if (rda != NULL)
            FreeArgs(rda);
        return RETURN_ERROR;
    }
    for (k = 0; k < BENCH_SCENES; k++)
        results[k].ran = FALSE;

    rc = RETURN_FAIL;
    opened = FALSE;
    win = NULL;
    IntuitionBase = (struct IntuitionBase *) OpenLibrary((CONST_STRPTR) "intuition.library", 39);
    GfxBase = (struct GfxBase *) OpenLibrary((CONST_STRPTR) "graphics.library", 39);
    if (IntuitionBase == NULL || GfxBase == NULL || !ClockOpen())
    {
        B_Say("GLBench: no intuition/graphics.library 39 or timer.device\n", 0, 0, 0, 0);
        goto out;
    }
    B_TableInit();
    if (!MiniGLOpen())
    {
        B_Say("GLBench: minigl.library 29.1 did not open\n", 0, 0, 0, 0);
        goto out;
    }
    opened = TRUE;
    if (args.window)
    {
        mglChooseWindowMode(GL_TRUE);
        if (!mglCreateContext((int) left, (int) top, (int) scr_w, (int) scr_h))
        {
            B_Say("GLBench: no window context: it needs a 15-, 16- or 32-bit screen on the "
                  "card\n", 0, 0, 0, 0);
            goto out;
        }
    }
    else
    {
        mglChoosePixelDepth((int) depth);
        if (!mglCreateContext(0, 0, (int) scr_w, (int) scr_h))
        {
            B_Say("GLBench: no context (see the library's log)\n", 0, 0, 0, 0);
            goto out;
        }
    }
    mglEnableSync(GL_FALSE);
    win = (struct Window *) mglGetWindowHandle();
    if (win != NULL && args.window)
        SetWindowTitles(win, (CONST_STRPTR) "GLBench", (CONST_STRPTR) ~0);
    if (win != NULL && !ModifyIDCMP(win, IDCMP_VANILLAKEY))
        win = NULL;
    bench_rgl = !args.norgl && Contains((const char *) glGetString(GL_EXTENSIONS), "GL_RGL_static_arrays");
    FontOpen();
    Reset();

    log_fh = Open((CONST_STRPTR) logname, MODE_NEWFILE);
    if (log_fh == 0)
        B_Say("GLBench: cannot write %s, console only\n", (LONG) logname, 0, 0, 0);
    Machine(scr_w, scr_h, depth, (BOOL) (args.window != 0));

    if (burn >= 0)
        rc = Burn(picks[0], burn);
    else
    {
        Out("Bench: %ld frames a scene%s\n", (LONG) frames,
            (LONG) (bench_shells != 20 ? ", Pelt with other SHELLS" : ""), 0, 0);
        rc = RETURN_OK;
        for (k = 0; k < npicks; k++)
        {
            if (RunScene(picks[k], k, npicks, frames, (const char *) args.shots) == KEY_QUIT)
            {
                if (!results[picks[k]].ran)
                    rc = RETURN_FAIL;
                break;
            }
        }
        Out("\n", 0, 0, 0, 0);
        score = Report(picks, npicks, &partial);
        Out("\nscore %ld%s\n", (LONG) B_Floor(score + 0.5f),
            (LONG) (partial ? " (not all seven scenes ran in full: not comparable)" : ""), 0, 0);
#if REF_PROVISIONAL
        Out("(against provisional reference figures)\n", 0, 0, 0, 0);
#endif
        anyerr = FALSE;
        for (k = 0; k < npicks; k++)
            if (results[picks[k]].ran && results[picks[k]].glerr != GL_NO_ERROR)
                anyerr = TRUE;
        if (rc == RETURN_OK && anyerr)
            rc = RETURN_WARN;
        ResultScreen(picks, npicks, score, partial, hold, (const char *) args.shot);
        (void) num;
    }

    if (log_fh != 0)
        Close(log_fh);
    log_fh = 0;
    FontClose();
    if (win != NULL)
        ModifyIDCMP(win, 0);
    mglDeleteContext();

out:
    if (opened)
        MiniGLClose();
    ClockClose();
    if (GfxBase != NULL)
        CloseLibrary((struct Library *) GfxBase);
    if (IntuitionBase != NULL)
        CloseLibrary((struct Library *) IntuitionBase);
    if (rda != NULL)
        FreeArgs(rda);
    return rc;
}
