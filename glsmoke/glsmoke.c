/*
 * GLSmoke -- the first frames through minigl.library on the Radeon
 * (docs/design-card-backend.md, work order step 5).
 *
 * A screen of WIDTH x HEIGHT (640 x 480), cleared every frame, with:
 *   - a textured quad on the left (a 64 x 64 red and white checker, linear
 *     filtering), behind the triangle in depth;
 *   - a Gouraud triangle (red, green, blue corners) spinning on the right,
 *     its tip crossing the quad's edge, so the depth test shows;
 *   - every frame an 8 x 8 patch of the texture rewritten with
 *     glTexSubImage2D (a changing colour, top left of the quad): a texture
 *     the GPU drew a moment ago changes, as GLQuake's lightmaps do, which
 *     exercises the library's texture-hazard flush and its heap lock.
 * FRAMES frames (300), then the frame rate. Esc on the GL screen (or
 * Ctrl-C in the Shell) stops it early. A Shell program: started from the
 * Workbench it quits at once.
 *
 *   GLSmoke FRAMES/N,DEPTH/N,WIDTH/N,HEIGHT/N,NOSUB/S,POLYMODE/K,TEX13/S
 *
 * TEX13 (2026-11-12): GL 1.3 phase 1 as well, every frame, in a row along the
 * bottom: six texture units, GL_COMBINE (interpolate, then dot3 reading
 * unit 0's texel through the crossbar), a cube map with reflection-map
 * texgen, an ARGB32 texture (GL_BGRA, 8_8_8_8_REV) rewritten every frame,
 * mirrored and anisotropic, an ARGB1555 one with border clamp and the alpha
 * test, and three units from client arrays (test/auto/GLSmoke13Null).
 *
 * POLYMODE=LINE or POINT (2026-11-11): both shapes drawn with that
 * glPolygonMode, back faces culled (both face the viewer), so the library's
 * polygon-mode way runs every frame (test/auto/GLSmokePolyNull).
 *
 * The library writes its summary to RadeonGLLog (PROGDIR:radeongl.log by
 * default): flushes, presents, lock times, dropped frames, checker.
 *
 * GCC only: it links the MiniGL SDK's import library (libminigl.a, a GCC
 * archive), as GLQuake does. C89 otherwise.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/minigl.h>

#ifdef RGL_FOR_030
static const char ver[] = "$VER: GLSmoke 0.3 (12.11.2026) for the 68020/68030";
#else
static const char ver[] = "$VER: GLSmoke 0.3 (12.11.2026)";
#endif

#define TEMPLATE "FRAMES/N,DEPTH/N,WIDTH/N,HEIGHT/N,NOSUB/S,POLYMODE/K,TEX13/S"

struct Args
{
    LONG *frames;
    LONG *depth;
    LONG *width;
    LONG *height;
    LONG nosub;
    STRPTR polymode;
    LONG tex13;
};

#define TEX 64

struct IntuitionBase *IntuitionBase = NULL;

/* a with b, ASCII letters in either case */
static BOOL SameWord(CONST_STRPTR a, const char *b)
{
    char x;
    char y;

    for (;;)
    {
        x = (char) *a++;
        y = *b++;
        if (x >= 'a' && x <= 'z')
            x = (char) (x - 'a' + 'A');
        if (y >= 'a' && y <= 'z')
            y = (char) (y - 'a' + 'A');
        if (x != y)
            return FALSE;
        if (x == 0)
            return TRUE;
    }
}

/* TRUE when Esc came in the library's window; every message replied */
static BOOL EscPressed(struct Window *win)
{
    struct IntuiMessage *im;
    BOOL esc;

    esc = FALSE;
    if (win == NULL || win->UserPort == NULL)
        return esc;
    while ((im = (struct IntuiMessage *) GetMsg(win->UserPort)) != NULL)
    {
        if (im->Class == IDCMP_VANILLAKEY && im->Code == 27)
            esc = TRUE;
        ReplyMsg((struct Message *) im);
    }
    return esc;
}

static UBYTE tex[TEX * TEX * 3];
static UBYTE patch[8 * 8 * 3];

static void MakeTexture(void)
{
    ULONG x;
    ULONG y;
    UBYTE v;

    for (y = 0; y < TEX; y++)
        for (x = 0; x < TEX; x++)
        {
            v = (UBYTE) ((((x >> 3) ^ (y >> 3)) & 1) ? 255 : 0);
            tex[(y * TEX + x) * 3 + 0] = 255;
            tex[(y * TEX + x) * 3 + 1] = v;
            tex[(y * TEX + x) * 3 + 2] = v;
        }
}

/* --- TEX13: GL 1.3 phase 1 ------------------------------------------------------------ */

#ifndef GL_COMBINE
#define GL_COMBINE                      0x8570
#define GL_COMBINE_RGB                  0x8571
#define GL_CONSTANT                     0x8576
#define GL_PREVIOUS                     0x8578
#define GL_SOURCE0_RGB                  0x8580
#define GL_SOURCE1_RGB                  0x8581
#define GL_SOURCE2_RGB                  0x8582
#define GL_OPERAND2_RGB                 0x8592
#define GL_INTERPOLATE                  0x8575
#define GL_DOT3_RGBA                    0x86AF
#define GL_RGB_SCALE                    0x8573
#endif
#ifndef GL_TEXTURE_CUBE_MAP
#define GL_TEXTURE_CUBE_MAP             0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X  0x8515
#define GL_REFLECTION_MAP               0x8512
#endif
#ifndef GL_TEXTURE_GEN_R
#define GL_TEXTURE_GEN_R                0x0C62
#endif
#ifndef GL_R
#define GL_R                            0x2002
#endif
#ifndef GL_MIRRORED_REPEAT
#define GL_MIRRORED_REPEAT              0x8370
#endif
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER              0x812D
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT   0x84FE
#endif
#ifndef GL_BGRA
#define GL_BGRA                         0x80E1
#endif
#ifndef GL_UNSIGNED_INT_8_8_8_8_REV
#define GL_UNSIGNED_INT_8_8_8_8_REV     0x8367
#endif
#ifndef GL_TEXTURE4_ARB
#define GL_TEXTURE4_ARB                 0x84C4
#define GL_TEXTURE5_ARB                 0x84C5
#endif

static ULONG argb[16 * 16];

static void Tex13Setup(void)
{
    static const UBYTE solid[6][4] = { { 255, 255, 255, 255 }, { 255, 160, 160, 255 }, { 160, 255, 160, 255 },
                                       { 160, 160, 255, 255 }, { 255, 255, 160, 255 }, { 160, 255, 255, 255 } };
    static const UBYTE face[6][4] = { { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 },
                                      { 255, 255, 0, 255 }, { 0, 255, 255, 255 }, { 255, 0, 255, 255 } };
    static const UBYTE a1[2 * 4] = { 0, 255, 0, 255, 255, 0, 0, 0 };
    UBYTE px[8 * 8 * 4];
    int u, f, i;

    for (u = 0; u < 6; u++)
    {
        glBindTexture(GL_TEXTURE_2D, (GLuint) (20 + u));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, solid[u]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, 30);
    for (f = 0; f < 6; f++)
    {
        for (i = 0; i < 64; i++)
        {
            px[i * 4 + 0] = face[f][0];
            px[i * 4 + 1] = face[f][1];
            px[i * 4 + 2] = face[f][2];
            px[i * 4 + 3] = 255;
        }
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    for (i = 0; i < 16 * 16; i++)
        argb[i] = 0xff000000UL | ((ULONG) (i * 16) << 16) | ((ULONG) (255 - i) << 8) | (ULONG) (i & 0xff);
    glBindTexture(GL_TEXTURE_2D, 31);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, argb);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 4);
    glBindTexture(GL_TEXTURE_2D, 32);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, a1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glBindTexture(GL_TEXTURE_2D, 1);
}

/* a square of side d at (x, y), units 0 .. n - 1 with coordinates 0..k */
static void Sq(GLfloat x, GLfloat y, GLfloat d, int n, GLfloat k)
{
    static const GLfloat c[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    int i, u;

    glBegin(GL_QUADS);
    for (i = 0; i < 4; i++)
    {
        for (u = 0; u < n; u++)
            glMultiTexCoord2fARB(GL_TEXTURE0_ARB + u, k * c[i][0], k * c[i][1]);
        glNormal3f(c[i][0] - 0.5f, c[i][1] - 0.5f, 1.0f);
        glVertex3f(x + d * c[i][0], y + d * c[i][1], 0.2f);
    }
    glEnd();
}

static void UnitsOff(int n)
{
    int u;

    for (u = n - 1; u >= 0; u--)
    {
        glActiveTextureARB(GL_TEXTURE0_ARB + u);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glDisable(GL_TEXTURE_2D);
    }
}

static void Tex13Frame(ULONG f)
{
    static const GLfloat cc[4] = { 0.25f, 0.5f, 0.75f, 0.5f };
    static GLfloat pos[4 * 3] = { 0.62f, -0.72f, 0.2f, 0.8f, -0.72f, 0.2f, 0.8f, -0.54f, 0.2f, 0.62f, -0.54f, 0.2f };
    static GLfloat tc[3][4 * 2] = { { 0, 0, 1, 0, 1, 1, 0, 1 }, { 0, 0, 2, 0, 2, 2, 0, 2 }, { 1, 1, 0, 1, 0, 0, 1, 0 } };
    ULONG i;
    int u;

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    /* six units, modulated */
    for (u = 0; u < 6; u++)
    {
        glActiveTextureARB(GL_TEXTURE0_ARB + u);
        glBindTexture(GL_TEXTURE_2D, (GLuint) (20 + u));
        glEnable(GL_TEXTURE_2D);
    }
    Sq(-0.95f, -0.72f, 0.18f, 6, 1.0f);
    UnitsOff(6);
    /* GL_COMBINE: interpolate with the constant, then dot3 through the crossbar */
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glBindTexture(GL_TEXTURE_2D, 1);
    glEnable(GL_TEXTURE_2D);
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, cc);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_CONSTANT);
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glBindTexture(GL_TEXTURE_2D, 21);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_DOT3_RGBA);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE0_ARB);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PREVIOUS);
    glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 2);
    Sq(-0.72f, -0.72f, 0.18f, 2, 1.0f);
    glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 1);
    UnitsOff(2);
    /* a cube map, reflection-map texgen (normals in the vertices) */
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 30);
    glEnable(GL_TEXTURE_CUBE_MAP);
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP);
    glTexGeni(GL_R, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP);
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);
    glEnable(GL_TEXTURE_GEN_R);
    glPushMatrix();
    glRotatef((float) (f % 360), 0.0f, 1.0f, 0.0f);
    Sq(-0.49f, -0.72f, 0.18f, 0, 1.0f);
    glPopMatrix();
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
    glDisable(GL_TEXTURE_GEN_R);
    glDisable(GL_TEXTURE_CUBE_MAP);
    /* the ARGB32 texture, a row rewritten every frame; mirrored, anisotropic */
    for (i = 0; i < 16; i++)
        argb[(f % 16) * 16 + i] = 0xff000000UL | ((ULONG) (f * 7 & 0xff) << 16) | (i * 16);
    glBindTexture(GL_TEXTURE_2D, 31);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, (GLint) (f % 16), 16, 1, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
                    argb + (f % 16) * 16);
    glEnable(GL_TEXTURE_2D);
    Sq(-0.26f, -0.72f, 0.18f, 1, 2.0f);
    /* ARGB1555, border clamp, the alpha test */
    glBindTexture(GL_TEXTURE_2D, 32);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    Sq(-0.03f, -0.72f, 0.18f, 1, 1.5f);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_TEXTURE_2D);
    /* three units from client arrays (unit 2 among them: the wide way) */
    glVertexPointer(3, GL_FLOAT, 0, pos);
    glEnableClientState(GL_VERTEX_ARRAY);
    for (u = 0; u < 3; u++)
    {
        glActiveTextureARB(GL_TEXTURE0_ARB + u);
        glBindTexture(GL_TEXTURE_2D, (GLuint) (21 + u));
        glEnable(GL_TEXTURE_2D);
        glClientActiveTextureARB(GL_TEXTURE0_ARB + u);
        glTexCoordPointer(2, GL_FLOAT, 0, tc[u]);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    glDrawArrays(GL_QUADS, 0, 4);
    for (u = 2; u >= 0; u--)
    {
        glClientActiveTextureARB(GL_TEXTURE0_ARB + u);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    glDisableClientState(GL_VERTEX_ARRAY);
    UnitsOff(3);
    glBindTexture(GL_TEXTURE_2D, 1);
}

static BOOL tex13;

static void Frame(ULONG f, BOOL sub)
{
    ULONG i;

    if (sub)
    {
        for (i = 0; i < 8 * 8; i++)
        {
            patch[i * 3 + 0] = (UBYTE) (f * 5);
            patch[i * 3 + 1] = (UBYTE) (f * 3);
            patch[i * 3 + 2] = (UBYTE) (255 - f * 5);
        }
        glBindTexture(GL_TEXTURE_2D, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, TEX - 8, 8, 8, GL_RGB, GL_UNSIGNED_BYTE, patch);
    }

    glClearColor(0.0f, 0.0f, 0.25f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /* the textured quad, behind (z = -0.5) */
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 1);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex3f(-0.9f, -0.6f, -0.5f);
    glTexCoord2f(1.0f, 0.0f);
    glVertex3f(0.1f, -0.6f, -0.5f);
    glTexCoord2f(1.0f, 1.0f);
    glVertex3f(0.1f, 0.6f, -0.5f);
    glTexCoord2f(0.0f, 1.0f);
    glVertex3f(-0.9f, 0.6f, -0.5f);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    /* the spinning triangle, in front (z = 0) */
    glPushMatrix();
    glTranslatef(0.35f, 0.0f, 0.0f);
    glRotatef((float) (f * 3 % 360), 0.0f, 0.0f, 1.0f);
    glBegin(GL_TRIANGLES);
    glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    glVertex3f(0.0f, 0.55f, 0.0f);
    glColor4f(0.0f, 1.0f, 0.0f, 1.0f);
    glVertex3f(-0.48f, -0.28f, 0.0f);
    glColor4f(0.0f, 0.0f, 1.0f, 1.0f);
    glVertex3f(0.48f, -0.28f, 0.0f);
    glEnd();
    glPopMatrix();

    if (tex13)
        Tex13Frame(f);
    mglSwitchDisplay();
}

int main(int argc, char **argv)
{
    struct RDArgs *rda;
    struct Args args;
    struct DateStamp d0;
    struct DateStamp d1;
    struct Window *win;
    LONG frames;
    LONG depth;
    LONG w;
    LONG h;
    LONG f;
    LONG ticks;
    int rc;
    BOOL opened;

    (void) argv;
    if (argc == 0)
        return RETURN_FAIL;     /* the Workbench: no console to report to */
    args.frames = NULL;
    args.depth = NULL;
    args.width = NULL;
    args.height = NULL;
    args.nosub = 0;
    args.polymode = NULL;
    args.tex13 = 0;
    rda = ReadArgs(TEMPLATE, (LONG *) &args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "GLSmoke");
        return RETURN_ERROR;
    }
    frames = (args.frames && *args.frames > 0) ? *args.frames : 300;
    depth = (args.depth && *args.depth == 32) ? 32 : 16;
    w = (args.width && *args.width >= 64) ? *args.width : 640;
    h = (args.height && *args.height >= 64) ? *args.height : 480;
    Printf("%s: %ld x %ld x %ld, %ld frames%s\n", (LONG) (ver + 6), w, h, depth, frames,
           (LONG) (args.nosub ? " (no texture updates)" : ""));

    rc = RETURN_FAIL;
    opened = FALSE;
    IntuitionBase = (struct IntuitionBase *) OpenLibrary((CONST_STRPTR) "intuition.library", 39);
    if (IntuitionBase == NULL)
    {
        Printf("GLSmoke: intuition.library 39 did not open\n");
        goto out;
    }
    if (!MiniGLOpen())
    {
        Printf("GLSmoke: minigl.library 29.1 did not open\n");
        goto out;
    }
    opened = TRUE;
    mglChoosePixelDepth((int) depth);
    if (!mglCreateContext(0, 0, (int) w, (int) h))
    {
        Printf("GLSmoke: no context (see the library's log, RadeonGLLog)\n");
        goto out;
    }
    Printf("GL_RENDERER: %s\n", (LONG) glGetString(GL_RENDERER));
    /* Esc in the library's window (it opens it with no IDCMP): the Shell
     * that took Ctrl-C is hidden behind the GL screen */
    win = (struct Window *) mglGetWindowHandle();
    if (win != NULL && !ModifyIDCMP(win, IDCMP_VANILLAKEY))
        win = NULL;

    glViewport(0, 0, (GLsizei) w, (GLsizei) h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -0.75, 0.75, -1.0, 1.0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    MakeTexture();
    glBindTexture(GL_TEXTURE_2D, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, TEX, TEX, 0, GL_RGB, GL_UNSIGNED_BYTE, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    if (args.polymode != NULL)
    {
        if (SameWord(args.polymode, "LINE"))
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        else if (SameWord(args.polymode, "POINT"))
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
            glPointSize(3.0f);
        }
        else
            Printf("GLSmoke: POLYMODE is LINE or POINT; drawn filled\n");
        glEnable(GL_CULL_FACE);
        Printf("glPolygonMode %s, back faces culled\n", (LONG) args.polymode);
    }
    if (args.tex13)
    {
        tex13 = TRUE;
        Tex13Setup();
        Printf("GL 1.3 phase 1 (TEX13): six units, GL_COMBINE, a cube map, ARGB32, ARGB1555; GL error 0x%lx\n",
               (LONG) glGetError());
    }

    DateStamp(&d0);
    for (f = 0; f < frames; f++)
    {
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            break;
        if (EscPressed(win))
            break;
        Frame((ULONG) f, (BOOL) !args.nosub);
    }
    DateStamp(&d1);
    ticks = (d1.ds_Days - d0.ds_Days) * 24L * 60L * 50L * 60L
          + (d1.ds_Minute - d0.ds_Minute) * 50L * 60L + (d1.ds_Tick - d0.ds_Tick);
    if (ticks <= 0)
        ticks = 1;
    Printf("%ld frames in %ld.%02ld s: %ld.%ld fps; GL error 0x%lx\n", f, ticks / 50,
           (ticks % 50) * 2, f * 50L / ticks, (f * 500L / ticks) % 10, (LONG) glGetError());
    rc = RETURN_OK;

    if (win != NULL)
        ModifyIDCMP(win, 0);
    mglDeleteContext();

out:
    if (opened)
        MiniGLClose();
    if (IntuitionBase != NULL)
        CloseLibrary((struct Library *) IntuitionBase);
    FreeArgs(rda);
    return rc;
}
