/*
 * GLGrow -- RadeonGL's growing upload cache on the card (2026-11-14,
 * docs/concept-dynamic-upload-cache.md): BLOCKS arrays of 16384 points,
 * each its own locked draw under GL_RGL_STATIC_ARRAYS_HINT, drawn every
 * frame -- more than the 4 MB the cache starts with, so it misses blocks it
 * had to drop and must grow. Then 512 x 512 textures until the heap is
 * full: the cache must give memory back down to its start, and the frames
 * after that must still draw.
 *
 *   GLGrow BLOCKS/N,FRAMES/N
 *
 * BLOCKS 24 (default), FRAMES 40 (default). Prints each phase's time; the
 * library's log (SetEnv RadeonGLLog <file>) has the cache's line: started,
 * now, largest, capacity misses, grown, shrunk, and the checker's count.
 * GCC only (the MiniGL SDK's import library, as GLQuake).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/minigl.h>

static const char ver[] = "$VER: GLGrow 0.1 (14.11.2026)";

#ifndef GL_RGL_STATIC_ARRAYS_HINT
#define GL_RGL_STATIC_ARRAYS_HINT 0x52474c01UL      /* RadeonGL: arrays never change */
#endif

#define BV      16384           /* points a block (16-bit indices) */
#define MAXB    64
#define TEXW    512

static float *blk[MAXB];
static UWORD *idx;
static UBYTE *texels;

/* Ticks of 1/50 s since the start of the day */
static LONG Ticks(void)
{
    struct DateStamp d;

    DateStamp(&d);
    return d.ds_Minute * 60 * 50 + d.ds_Tick;
}

static void Frames(LONG nb, LONG n, const char *what)
{
    LONG f, b, t0, t;

    t0 = Ticks();
    for (f = 0; f < n; f++)
    {
        glClear(GL_COLOR_BUFFER_BIT);
        for (b = 0; b < nb; b++)
        {
            glVertexPointer(3, GL_FLOAT, 0, blk[b]);
            glLockArraysEXT(0, BV);
            glDrawElements(GL_POINTS, BV, GL_UNSIGNED_SHORT, idx);
            glUnlockArraysEXT();
        }
        mglSwitchDisplay();
        if ((f + 1) % 10 == 0)
        {
            t = Ticks() - t0;
            Printf("%s: frames %ld-%ld, %ld ms\n", (LONG) what, f - 8, f + 1, t * 20);
            t0 = Ticks();
        }
    }
}

int main(int argc, char **argv)
{
    struct RDArgs *rda;
    LONG args[2];
    LONG nb, frames, b, i, ntex;
    GLenum err;
    int lib, ctx;

    (void) argv;
    if (argc == 0)
        return RETURN_FAIL;
    args[0] = args[1] = 0;
    rda = ReadArgs((CONST_STRPTR) "BLOCKS/N,FRAMES/N", args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "GLGrow");
        return RETURN_ERROR;
    }
    nb = args[0] ? *(LONG *) args[0] : 24;
    frames = args[1] ? *(LONG *) args[1] : 40;
    if (nb < 1)
        nb = 1;
    if (nb > MAXB)
        nb = MAXB;
    lib = ctx = 0;
    err = GL_NO_ERROR;
    Printf("%s: %ld blocks of %ld points (%ld KB of floats), %ld frames\n", (LONG) (ver + 6), nb, (LONG) BV,
           nb * BV * 12 / 1024, frames);
    idx = (UWORD *) AllocVec(BV * sizeof(UWORD), MEMF_ANY);
    texels = (UBYTE *) AllocVec(TEXW * TEXW * 4, MEMF_ANY | MEMF_CLEAR);
    for (b = 0; b < nb; b++)
        blk[b] = (float *) AllocVec(BV * 3 * sizeof(float), MEMF_ANY);
    for (b = 0; b < nb; b++)
        if (blk[b] == NULL)
            break;
    if (idx == NULL || texels == NULL || b < nb)
    {
        Printf("GLGrow: no memory for the arrays\n");
        goto out;
    }
    for (i = 0; i < BV; i++)
        idx[i] = (UWORD) i;
    for (b = 0; b < nb; b++)
        for (i = 0; i < BV; i++)
        {
            blk[b][i * 3 + 0] = (float) (i % 128) * 5.0f + (float) (b % 4);
            blk[b][i * 3 + 1] = (float) (i / 128) * 3.7f + (float) (b / 4);
            blk[b][i * 3 + 2] = 0.0f;
        }
    if (!MiniGLOpen())
    {
        Printf("GLGrow: minigl.library missing\n");
        goto out;
    }
    lib = 1;
    mglChoosePixelDepth(16);
    if (!mglCreateContext(0, 0, 640, 480))
    {
        Printf("GLGrow: no context\n");
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
    glColor3f(1.0f, 1.0f, 1.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_NICEST);

    /* 1. the scene, more than the start's 4 MB: the cache grows */
    Frames(nb, frames, "scene");

    /* 2. textures until the heap is full: the cache gives back */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    for (ntex = 0; ntex < 200; ntex++)
    {
        glBindTexture(GL_TEXTURE_2D, (GLuint) (100 + ntex));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEXW, TEXW, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
        err = glGetError();
        if (err != GL_NO_ERROR)
            break;
    }
    Printf("textures: %ld of %ldx%ld taken, then error 0x%lx\n", ntex, (LONG) TEXW, (LONG) TEXW, (ULONG) err);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* 3. the scene again, from the cache that is left */
    Frames(nb, 20, "after the textures");
    err = glGetError();
    Printf("GL error at the end: 0x%lx\n", (ULONG) err);

out:
    if (ctx)
        mglDeleteContext();
    if (lib)
        MiniGLClose();
    for (b = 0; b < nb; b++)
        if (blk[b] != NULL)
            FreeVec(blk[b]);
    if (texels != NULL)
        FreeVec(texels);
    if (idx != NULL)
        FreeVec(idx);
    FreeArgs(rda);
    return (ctx && err == GL_NO_ERROR) ? RETURN_OK : RETURN_ERROR;
}
