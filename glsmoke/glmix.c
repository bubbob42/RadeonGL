/*
 * GLMix -- DevilutionX's fight mix as a stress test (2026-11-24: the R200
 * hangs of its fights, RBBM_STATUS 0x83f96100, recovered only by the reset's
 * stage 2 -- the render backend and the CP). Every frame:
 *  - the uploads first (DevilutionX's deferred order): sprite cells into
 *    the atlas pages (RGBA bytes), the light map's changed square (RGB565),
 *    a strip of the UI texture (RGBA bytes);
 *  - CHUNKS static tile chunks of 96 quads, indexed triangles: positions and
 *    two texture units, no colour array, under GL_RGL_STATIC_ARRAYS_HINT and
 *    GL_RGL_STATIC_INDICES_HINT (from the upload cache in video RAM). Unit 0
 *    an NPOT 1024 x 960 ARGB1555 page, GL_NEAREST; unit 1 the 128 x 128
 *    RGB565 light map, GL_LINEAR; both GL_MODULATE, GL_CLAMP;
 *  - SPRITES blended quads, unit 0 only, from arrays rewritten every frame;
 *  - the 640 x 480 ARGB1555 UI overlay, glBegin(GL_QUADS), GL_REPLACE.
 * 0.2 (2026-11-24 evening, DevilutionX's death screen, where its warm
 * fights hung most -- GLMix 0.1 ran 900 frames clean on the warm card):
 * the dungeon pass under a scissor (640 x 352), the depth buffer cleared
 * to 0 and tested GEQUAL (floors -- the first half of the chunks -- with
 * the test off, walls with it on), the alpha test GREATER 0.5; the last
 * two chunks again as see-through parts (blended, DepthMask FALSE, alpha
 * 0.25, unit 1 on, locked); then the UI pass: the death tint (untextured,
 * glBlendFunc(GL_ZERO, GL_SRC_COLOR), glColor4f(1, 0.35, 0.35, 1), one
 * immediate quad over 640 x 352) and the overlay alpha-tested, not
 * blended (the game's B2).
 *
 *   GLMix FRAMES/N,LIGHT/N,STATIC/N,UPLOADS/N,SPRITES/N,CHUNKS/N,
 *         DEPTH/N,ALPHA/N,SCISSOR/N,TINT/N,SEE/N,POT/N,WRAP/N,LFMT/N,AREF/N,U1MODE/N,
 *         SEEB/N,SEEM/N
 *
 * 0.3 (the night of 2026-11-24, narrowing the R200 hang): POT=1 atlas pages
 * 1024 x 1024 (power of two) instead of 1024 x 960; WRAP 0 GL_CLAMP, 1
 * GL_REPEAT, 2 GL_CLAMP_TO_EDGE (both units); LFMT=1 the light map RGBA
 * bytes instead of RGB565; AREF the chunks' alpha reference in percent (50);
 * U1MODE=1 unit 1 GL_REPLACE instead of GL_MODULATE. The see-through pass
 * (the bisect: the hang needs it and unit 1): SEEB=0 without blending,
 * SEEM=0 with depth writes left on.
 *
 * FRAMES 3000. LIGHT 1 (the light map GL_LINEAR on unit 1), 2 (GL_NEAREST),
 * 0 (no unit 1 and no light upload: the chunks carry a colour array instead
 * and are not static -- as DEVILUTIONX_GLLIGHTUNIT=0). STATIC 1, 0: the
 * chunks without the hints. UPLOADS texels a frame into the pages (16384).
 * SPRITES 200. CHUNKS 16 (the screen covered about five times).
 * UPLOADS=0: no uploads at all (the death screen's). DEPTH, ALPHA,
 * SCISSOR, TINT, SEE 1 (0.2's death-screen parts, each off with 0).
 * The death screen as the game draws it: UPLOADS=0 SPRITES=9 CHUNKS=5.
 * A line every 250 frames; Ctrl-C stops. The library's log (RadeonGLLog)
 * counts the hangs ("frames that never ended") and the reset's stage.
 * GCC only (the MiniGL SDK's import library, as GLQuake).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/minigl.h>

static const char ver[] = "$VER: GLMix 0.3 (24.11.2026)";

#ifndef GL_RGL_STATIC_ARRAYS_HINT
#define GL_RGL_STATIC_ARRAYS_HINT  0x52474c01UL     /* RadeonGL: arrays never change */
#endif
#ifndef GL_RGL_STATIC_INDICES_HINT
#define GL_RGL_STATIC_INDICES_HINT 0x52474c02UL     /* RadeonGL: index lists never change */
#endif

#define PAGEW   1024
static LONG pageh = 960;        /* POT=1: 1024 */
#define PAGEH   pageh
#define CELLW   64
#define CELLH   32
#define LMAP    128
#define LSQ     24              /* the light map's square updated a frame */
#define UIW     640
#define UIH     480
#define UISTRIP 16
#define CQ      96              /* quads a chunk */
#define MAXCH   32
#define MAXSPR  1024
#define TEX_PAGE0 1
#define TEX_PAGE1 2
#define TEX_LIGHT 3
#define TEX_UI    4
#define VIEWH     352           /* the dungeon view over the panel */

struct Chunk
{
    float pos[CQ * 4 * 3];
    float st0[CQ * 4 * 2];
    float st1[CQ * 4 * 2];
    UBYTE col[CQ * 4 * 4];
    UWORD idx[CQ * 6];
};

static struct Chunk *chunk[MAXCH];
static float *spos;
static float *sst;
static UWORD *sidx;
static UBYTE *cell;             /* CELLW x CELLH RGBA bytes */
static UWORD *lsq;              /* LSQ x LSQ RGB565 */
static UBYTE *uistrip;          /* UIW x UISTRIP RGBA bytes */
static ULONG seed = 12345;
static LONG g_wrap = 0;         /* WRAP */
static LONG g_lfmt = 0;         /* LFMT */
static LONG g_u1mode = 0;       /* U1MODE */
static float g_aref = 0.5f;     /* AREF / 100 */
static UBYTE *lrgba;            /* LFMT=1: LSQ x LSQ RGBA bytes */

static ULONG Rnd(void)
{
    seed = seed * 1103515245UL + 12345UL;
    return (seed >> 8) & 0xffffffUL;
}

/* Ticks of 1/50 s since the start of the day */
static LONG Ticks(void)
{
    struct DateStamp d;

    DateStamp(&d);
    return d.ds_Minute * 60 * 50 + d.ds_Tick;
}

/* chunk c: CQ quads of 40 x 36 on a 32-pixel grid, a layer every 300 quads
 * (offset, so the layers overlap: about five over the screen with 16) */
static void BuildChunk(struct Chunk *k, LONG c)
{
    LONG q, g, layer, gx, gy, v, cx, cy;
    float x, y, s0, t0, s1, t1;
    static const float cxs[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
    static const float cys[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

    for (q = 0; q < CQ; q++)
    {
        g = c * CQ + q;
        layer = g / 300;
        gx = (g % 300) % 20;
        gy = (g % 300) / 20;
        x = (float) (gx * 32 + (layer * 7) % 24 - 8);
        y = (float) (gy * 32 + (layer * 5) % 20 - 8);
        cx = (LONG) (Rnd() % (PAGEW / CELLW));
        cy = (LONG) (Rnd() % (PAGEH / CELLH));
        s0 = (float) (cx * CELLW) / (float) PAGEW;
        t0 = (float) (cy * CELLH) / (float) PAGEH;
        s1 = (float) (cx * CELLW + CELLW) / (float) PAGEW;
        t1 = (float) (cy * CELLH + CELLH) / (float) PAGEH;
        for (v = 0; v < 4; v++)
        {
            k->pos[(q * 4 + v) * 3 + 0] = x + cxs[v] * 40.0f;
            k->pos[(q * 4 + v) * 3 + 1] = y + cys[v] * 36.0f;
            k->pos[(q * 4 + v) * 3 + 2] = (float) (layer % 8) * 0.1f - 0.35f;
            k->st0[(q * 4 + v) * 2 + 0] = cxs[v] != 0.0f ? s1 : s0;
            k->st0[(q * 4 + v) * 2 + 1] = cys[v] != 0.0f ? t1 : t0;
            /* the light map's 112 tiles of 128, a little outside at the
             * edges (as DevilutionX near the map's border) */
            k->st1[(q * 4 + v) * 2 + 0] = (x + cxs[v] * 40.0f) / 640.0f * (112.0f / 128.0f) + 0.02f;
            k->st1[(q * 4 + v) * 2 + 1] = (y + cys[v] * 36.0f) / 480.0f * (112.0f / 128.0f) + 0.02f;
            k->col[(q * 4 + v) * 4 + 0] = (UBYTE) (0x80 + Rnd() % 0x80);
            k->col[(q * 4 + v) * 4 + 1] = (UBYTE) (0x80 + Rnd() % 0x80);
            k->col[(q * 4 + v) * 4 + 2] = (UBYTE) (0x80 + Rnd() % 0x80);
            k->col[(q * 4 + v) * 4 + 3] = 0xff;
        }
        k->idx[q * 6 + 0] = (UWORD) (q * 4 + 0);
        k->idx[q * 6 + 1] = (UWORD) (q * 4 + 1);
        k->idx[q * 6 + 2] = (UWORD) (q * 4 + 2);
        k->idx[q * 6 + 3] = (UWORD) (q * 4 + 0);
        k->idx[q * 6 + 4] = (UWORD) (q * 4 + 2);
        k->idx[q * 6 + 5] = (UWORD) (q * 4 + 3);
    }
}

static void Params(GLenum filter)
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint) filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint) filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                    g_wrap == 1 ? GL_REPEAT : g_wrap == 2 ? 0x812F : GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                    g_wrap == 1 ? GL_REPEAT : g_wrap == 2 ? 0x812F : GL_CLAMP);
}

static void Uploads(LONG f, LONG cells, LONG light)
{
    LONG i, cx, cy, lx, ly;

    glActiveTextureARB(GL_TEXTURE0_ARB);
    for (i = 0; i < cells; i++)
    {
        cell[(i * 4) % (CELLW * CELLH * 4)] = (UBYTE) f;
        cx = (LONG) (Rnd() % (PAGEW / CELLW));
        cy = (LONG) (Rnd() % (PAGEH / CELLH));
        glBindTexture(GL_TEXTURE_2D, (i & 1) ? TEX_PAGE1 : TEX_PAGE0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, cx * CELLW, cy * CELLH, CELLW, CELLH, GL_RGBA,
                        GL_UNSIGNED_BYTE, cell);
    }
    if (light)
    {
        lsq[f % (LSQ * LSQ)] = (UWORD) (f * 2654435761UL);
        lx = (LONG) ((f * 3) % (LMAP - LSQ));
        ly = (LONG) ((f * 5) % (LMAP - LSQ));
        glBindTexture(GL_TEXTURE_2D, TEX_LIGHT);
        if (g_lfmt)
        {
            lrgba[(f * 4) % (LSQ * LSQ * 4)] = (UBYTE) f;
            glTexSubImage2D(GL_TEXTURE_2D, 0, lx, ly, LSQ, LSQ, GL_RGBA, GL_UNSIGNED_BYTE, lrgba);
        }
        else
            glTexSubImage2D(GL_TEXTURE_2D, 0, lx, ly, LSQ, LSQ, GL_RGB, MGL_UNSIGNED_SHORT_5_6_5, lsq);
    }
    glBindTexture(GL_TEXTURE_2D, TEX_UI);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, (f * UISTRIP) % UIH, UIW, UISTRIP, GL_RGBA, GL_UNSIGNED_BYTE,
                    uistrip);
}

/* chunks first..last-1; depth: floors (the first half of all nch) with
 * the depth test off, walls with it on (GEQUAL) */
static void Chunks(LONG first, LONG last, LONG nch, LONG light, LONG stat, LONG depth)
{
    LONG c;
    struct Chunk *k;

    glActiveTextureARB(GL_TEXTURE0_ARB);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glClientActiveTextureARB(GL_TEXTURE0_ARB);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    if (light)
    {
        glActiveTextureARB(GL_TEXTURE1_ARB);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, TEX_LIGHT);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, g_u1mode ? GL_REPLACE : GL_MODULATE);
        glClientActiveTextureARB(GL_TEXTURE1_ARB);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        if (stat)
        {
            glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_NICEST);
            glHint(GL_RGL_STATIC_INDICES_HINT, GL_NICEST);
        }
    }
    else
        glEnableClientState(GL_COLOR_ARRAY);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    for (c = first; c < last; c++)
    {
        k = chunk[c];
        if (depth)
        {
            if (c < nch / 2)
                glDisable(GL_DEPTH_TEST);
            else
                glEnable(GL_DEPTH_TEST);
        }
        glBindTexture(GL_TEXTURE_2D, (c & 1) ? TEX_PAGE1 : TEX_PAGE0);
        glVertexPointer(3, GL_FLOAT, 0, k->pos);
        glClientActiveTextureARB(GL_TEXTURE0_ARB);
        glTexCoordPointer(2, GL_FLOAT, 0, k->st0);
        if (light)
        {
            glClientActiveTextureARB(GL_TEXTURE1_ARB);
            glTexCoordPointer(2, GL_FLOAT, 0, k->st1);
        }
        else
            glColorPointer(4, GL_UNSIGNED_BYTE, 0, k->col);
        glLockArraysEXT(0, CQ * 4);
        glDrawElements(GL_TRIANGLES, CQ * 6, GL_UNSIGNED_SHORT, k->idx);
        glUnlockArraysEXT();
    }
    if (light)
    {
        glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_DONT_CARE);
        glHint(GL_RGL_STATIC_INDICES_HINT, GL_DONT_CARE);
        glActiveTextureARB(GL_TEXTURE1_ARB);
        glDisable(GL_TEXTURE_2D);
        glClientActiveTextureARB(GL_TEXTURE1_ARB);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glActiveTextureARB(GL_TEXTURE0_ARB);
        glClientActiveTextureARB(GL_TEXTURE0_ARB);
    }
    else
        glDisableClientState(GL_COLOR_ARRAY);
    if (depth)
        glDisable(GL_DEPTH_TEST);
}

/* the game's B1: the death tint, untextured, multiplied in */
static void Tint(void)
{
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    glColor4f(1.0f, 0.35f, 0.35f, 1.0f);
    glBegin(GL_QUADS);
    glVertex2f(0.0f, 0.0f);
    glVertex2f(640.0f, 0.0f);
    glVertex2f(640.0f, (float) VIEWH);
    glVertex2f(0.0f, (float) VIEWH);
    glEnd();
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

static void Sprites(LONG f, LONG ns)
{
    LONG i, v;
    float x, y;
    static const float cxs[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
    static const float cys[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

    for (i = 0; i < ns; i++)
    {
        x = (float) ((i * 37 + f * 3) % 600);
        y = (float) ((i * 53 + f * 2) % 420);
        for (v = 0; v < 4; v++)
        {
            spos[(i * 4 + v) * 3 + 0] = x + cxs[v] * 48.0f;
            spos[(i * 4 + v) * 3 + 1] = y + cys[v] * 64.0f;
            spos[(i * 4 + v) * 3 + 2] = 0.0f;
        }
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBindTexture(GL_TEXTURE_2D, TEX_PAGE1);
    glVertexPointer(3, GL_FLOAT, 0, spos);
    glTexCoordPointer(2, GL_FLOAT, 0, sst);
    glDrawElements(GL_TRIANGLES, ns * 6, GL_UNSIGNED_SHORT, sidx);
}

static void Overlay(LONG alpha)
{
    if (alpha)
    {
        glDisable(GL_BLEND);    /* the game's B2: alpha-tested, not blended */
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.5f);
    }
    glBindTexture(GL_TEXTURE_2D, TEX_UI);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(0.0f, 0.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(640.0f, 0.0f, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(640.0f, 480.0f, 0.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(0.0f, 480.0f, 0.0f);
    glEnd();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
}

int main(int argc, char **argv)
{
    struct RDArgs *rda;
    LONG args[18];
    LONG frames, light, stat, cells, ns, nch, depth, alpha, scis, tint, see, ups, seeb, seem;
    LONG f, i, c, cx, cy, t0, t;
    GLenum err;
    int lib, ctx;

    (void) argv;
    if (argc == 0)
        return RETURN_FAIL;
    for (i = 0; i < 18; i++)
        args[i] = 0;
    rda = ReadArgs((CONST_STRPTR) "FRAMES/N,LIGHT/N,STATIC/N,UPLOADS/N,SPRITES/N,CHUNKS/N,"
                   "DEPTH/N,ALPHA/N,SCISSOR/N,TINT/N,SEE/N,POT/N,WRAP/N,LFMT/N,AREF/N,U1MODE/N,"
                   "SEEB/N,SEEM/N", args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "GLMix");
        return RETURN_ERROR;
    }
    frames = args[0] ? *(LONG *) args[0] : 3000;
    light = args[1] ? *(LONG *) args[1] : 1;
    stat = args[2] ? *(LONG *) args[2] : 1;
    ups = args[3] ? *(LONG *) args[3] : 16384;
    cells = ups / (CELLW * CELLH);
    ns = args[4] ? *(LONG *) args[4] : 200;
    nch = args[5] ? *(LONG *) args[5] : 16;
    depth = args[6] ? *(LONG *) args[6] : 1;
    alpha = args[7] ? *(LONG *) args[7] : 1;
    scis = args[8] ? *(LONG *) args[8] : 1;
    tint = args[9] ? *(LONG *) args[9] : 1;
    see = args[10] ? *(LONG *) args[10] : 1;
    if (args[11] && *(LONG *) args[11])
        pageh = 1024;
    g_wrap = args[12] ? *(LONG *) args[12] : 0;
    g_lfmt = args[13] ? *(LONG *) args[13] : 0;
    g_aref = (args[14] ? (float) *(LONG *) args[14] : 50.0f) / 100.0f;
    g_u1mode = args[15] ? *(LONG *) args[15] : 0;
    seeb = args[16] ? *(LONG *) args[16] : 1;
    seem = args[17] ? *(LONG *) args[17] : 1;
    if (ns < 0)
        ns = 0;
    if (ns > MAXSPR)
        ns = MAXSPR;
    if (nch < 1)
        nch = 1;
    if (nch > MAXCH)
        nch = MAXCH;
    if (light < 0 || light > 2)
        light = 1;
    lib = ctx = 0;
    err = GL_NO_ERROR;
    f = 0;
    Printf("%s: %ld frames, light %s, chunks %ld x %ld quads%s, %ld sprite cells and %ld sprites a frame\n",
           (LONG) (ver + 6), frames,
           (LONG) (light == 1 ? "LINEAR on unit 1" : light == 2 ? "NEAREST on unit 1" : "off (colour array)"),
           nch, (LONG) CQ, (LONG) ((light && stat) ? " static" : ""), cells, ns);
    Printf("death screen parts: depth %ld, alpha test %ld, scissor %ld, tint %ld, see-through %ld%s\n",
           depth, alpha, scis, tint, see, (LONG) (ups == 0 ? ", no uploads" : ""));
    Printf("see-through: blend %ld, depth mask off %ld\n", seeb, seem);
    Printf("0.3: pages 1024 x %ld, wrap %ld, light map %s, alpha ref %ld %%, unit 1 %s\n", pageh, g_wrap,
           (LONG) (g_lfmt ? "RGBA" : "RGB565"), (LONG) (g_aref * 100.0f + 0.5f),
           (LONG) (g_u1mode ? "REPLACE" : "MODULATE"));

    for (c = 0; c < nch; c++)
        chunk[c] = (struct Chunk *) AllocVec(sizeof(struct Chunk), MEMF_ANY);
    spos = (float *) AllocVec(MAXSPR * 4 * 3 * sizeof(float), MEMF_ANY);
    sst = (float *) AllocVec(MAXSPR * 4 * 2 * sizeof(float), MEMF_ANY);
    sidx = (UWORD *) AllocVec(MAXSPR * 6 * sizeof(UWORD), MEMF_ANY);
    cell = (UBYTE *) AllocVec(CELLW * CELLH * 4, MEMF_ANY);
    lsq = (UWORD *) AllocVec(LMAP * LMAP * sizeof(UWORD), MEMF_ANY);
    uistrip = (UBYTE *) AllocVec(UIW * UISTRIP * 4, MEMF_ANY);
    lrgba = (UBYTE *) AllocVec(LMAP * LMAP * 4, MEMF_ANY);
    for (c = 0; c < nch; c++)
        if (chunk[c] == NULL)
            break;
    if (c < nch || spos == NULL || sst == NULL || sidx == NULL || cell == NULL || lsq == NULL
        || uistrip == NULL || lrgba == NULL)
    {
        Printf("GLMix: no memory for the arrays\n");
        goto out;
    }
    for (c = 0; c < nch; c++)
        BuildChunk(chunk[c], c);
    for (i = 0; i < MAXSPR; i++)
    {
        cx = (LONG) (Rnd() % (PAGEW / 64));
        cy = (LONG) (Rnd() % (PAGEH / 64));
        sst[i * 8 + 0] = (float) (cx * 64) / PAGEW;      sst[i * 8 + 1] = (float) (cy * 64) / PAGEH;
        sst[i * 8 + 2] = (float) (cx * 64 + 48) / PAGEW; sst[i * 8 + 3] = (float) (cy * 64) / PAGEH;
        sst[i * 8 + 4] = (float) (cx * 64 + 48) / PAGEW; sst[i * 8 + 5] = (float) (cy * 64 + 64) / PAGEH;
        sst[i * 8 + 6] = (float) (cx * 64) / PAGEW;      sst[i * 8 + 7] = (float) (cy * 64 + 64) / PAGEH;
        sidx[i * 6 + 0] = (UWORD) (i * 4 + 0);
        sidx[i * 6 + 1] = (UWORD) (i * 4 + 1);
        sidx[i * 6 + 2] = (UWORD) (i * 4 + 2);
        sidx[i * 6 + 3] = (UWORD) (i * 4 + 0);
        sidx[i * 6 + 4] = (UWORD) (i * 4 + 2);
        sidx[i * 6 + 5] = (UWORD) (i * 4 + 3);
    }
    for (i = 0; i < CELLW * CELLH * 4; i++)
        cell[i] = (UBYTE) ((i & 3) == 3 ? ((i >> 6) & 1 ? 0xff : 0x00) : Rnd());
    for (i = 0; i < LMAP * LMAP; i++)
        lsq[i] = (UWORD) Rnd();
    for (i = 0; i < LMAP * LMAP * 4; i++)
        lrgba[i] = (UBYTE) ((i & 3) == 3 ? 0xff : Rnd());
    for (i = 0; i < UIW * UISTRIP * 4; i++)
        uistrip[i] = (UBYTE) ((i & 3) == 3 ? ((i / 4) % 97 < 9 ? 0xff : 0x00) : Rnd());

    if (!MiniGLOpen())
    {
        Printf("GLMix: minigl.library missing\n");
        goto out;
    }
    lib = 1;
    mglChoosePixelDepth(16);
    if (!mglCreateContext(0, 0, 640, 480))
    {
        Printf("GLMix: no context\n");
        goto out;
    }
    ctx = 1;
    mglEnableSync(GL_FALSE);
    Printf("GL_RENDERER: %s\n", (LONG) glGetString(GL_RENDERER));
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 640.0, 480.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(0.0);
    glDepthFunc(GL_GEQUAL);

    /* the textures: the pages as storage first (NULL), then filled cell by cell */
    glActiveTextureARB(GL_TEXTURE0_ARB);
    for (i = TEX_PAGE0; i <= TEX_PAGE1; i++)
    {
        glBindTexture(GL_TEXTURE_2D, (GLuint) i);
        Params(GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, PAGEW, PAGEH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        for (cy = 0; cy < PAGEH / CELLH; cy++)
            for (cx = 0; cx < PAGEW / CELLW; cx++)
                glTexSubImage2D(GL_TEXTURE_2D, 0, cx * CELLW, cy * CELLH, CELLW, CELLH, GL_RGBA,
                                GL_UNSIGNED_BYTE, cell);
    }
    glBindTexture(GL_TEXTURE_2D, TEX_LIGHT);
    Params(light == 2 ? GL_NEAREST : GL_LINEAR);
    if (g_lfmt)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, LMAP, LMAP, 0, GL_RGBA, GL_UNSIGNED_BYTE, lrgba);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, LMAP, LMAP, 0, GL_RGB, MGL_UNSIGNED_SHORT_5_6_5, lsq);
    glBindTexture(GL_TEXTURE_2D, TEX_UI);
    Params(GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, UIW, UIH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    err = glGetError();
    if (err != GL_NO_ERROR)
    {
        Printf("GLMix: GL error 0x%lx setting up the textures\n", (ULONG) err);
        goto out;
    }
    glEnableClientState(GL_VERTEX_ARRAY);

    t0 = Ticks();
    for (f = 0; f < frames; f++)
    {
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
        {
            Printf("GLMix: stopped at frame %ld\n", f);
            break;
        }
        if (ups != 0)
            Uploads(f, cells, light);
        /* A: the dungeon pass */
        if (scis)
        {
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 480 - VIEWH, 640, VIEWH);
        }
        glClear(depth ? (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT) : GL_COLOR_BUFFER_BIT);
        if (alpha)
        {
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER, g_aref);
        }
        Chunks(0, nch, nch, light, stat, depth);
        glDisable(GL_ALPHA_TEST);
        Sprites(f, ns);
        if (see && nch >= 2)
        {
            /* see-through parts: blended, no depth writes, alpha 0.25
             * (SEEB=0 no blending, SEEM=0 depth writes on) */
            if (seeb)
            {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }
            if (seem)
                glDepthMask(GL_FALSE);
            if (alpha)
            {
                glEnable(GL_ALPHA_TEST);
                glAlphaFunc(GL_GREATER, 0.25f);
            }
            Chunks(nch - 2, nch, nch, light, stat, depth);
            glDisable(GL_ALPHA_TEST);
            glDepthMask(GL_TRUE);
        }
        glDisable(GL_BLEND);
        if (scis)
            glDisable(GL_SCISSOR_TEST);
        /* B: the UI pass */
        if (tint)
            Tint();
        Overlay(alpha);
        mglSwitchDisplay();
        if ((f + 1) % 250 == 0)
        {
            t = Ticks() - t0;
            Printf("frames %ld-%ld: %ld ms, %ld ms a frame\n", f - 248, f + 1, t * 20, t * 20 / 250);
            t0 = Ticks();
        }
    }
    err = glGetError();
    Printf("%ld frames, GL error 0x%lx\n", f, (ULONG) err);

out:
    if (ctx)
        mglDeleteContext();
    if (lib)
        MiniGLClose();
    for (c = 0; c < nch; c++)
        if (chunk[c] != NULL)
            FreeVec(chunk[c]);
    if (spos != NULL)
        FreeVec(spos);
    if (sst != NULL)
        FreeVec(sst);
    if (sidx != NULL)
        FreeVec(sidx);
    if (cell != NULL)
        FreeVec(cell);
    if (lsq != NULL)
        FreeVec(lsq);
    if (uistrip != NULL)
        FreeVec(uistrip);
    if (lrgba != NULL)
        FreeVec(lrgba);
    FreeArgs(rda);
    return (ctx && err == GL_NO_ERROR) ? RETURN_OK : RETURN_ERROR;
}
