/*
 * scene_mirror.c -- Mirror: a picture that falls into itself. Read-back
 * over the bus.
 *
 * Every frame is drawn in a 128 x 128 corner of the back buffer: last
 * frame's picture a little smaller, turned and dimmed, then something new
 * round its edge (a lit dodecahedron and a ring of coloured tiles). The
 * corner is read back (glReadPixels, 48 KB as RGB) and sent into the
 * texture again (glTexSubImage2D), and the texture fills the screen. So
 * every frame crosses the bus twice; on the A4000D a read runs at about
 * 4.4 MB/s. Its frames depend on the ones before: Burn cannot check it.
 */

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/minigl.h>

#include "bench.h"

#define FB 128               /* the corner and the texture: 48 KB each way a frame (256 made
                                 * 3 fps on the A4000D: uploads run at about 1 MB/s there) */
#define TILES 8

static UBYTE *mr_buf;
static GLuint mr_tex;
static BOOL mr_have;

static void Done(void);

static BOOL Init(LONG w, LONG h)
{
    (void) w;
    (void) h;
    mr_buf = (UBYTE *) B_Alloc(FB * FB * 3);
    if (mr_buf == NULL)
        return FALSE;
    mr_tex = B_Texture(GL_RGB, FB, FB, GL_RGB, mr_buf, FALSE, GL_CLAMP);
    mr_have = TRUE;
    return TRUE;
}

static void Frame(ULONG f, LONG w, LONG h)
{
    static const GLfloat lpos[4] = { -0.5f, 0.8f, 0.6f, 0.0f };
    static const GLfloat ldif[4] = { 1.0f, 0.95f, 0.88f, 1.0f };
    float s, a, x, y;
    LONG k, side, x0, y0;

    /* the corner: the last frame, smaller, turned, dimmed */
    glViewport(0, 0, FB, FB);
    glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, mr_tex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glRotatef(2.2f + 1.2f * B_SinT((LONG) f * 3), 0.0f, 0.0f, 1.0f);
    s = 0.92f;
    glColor4f(0.97f, 0.96f, 0.95f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(-s, -s);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(s, -s);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(s, s);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(-s, s);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    bench_prims += 2;

    /* something new round the edge: a ring of tiles in turning colours */
    glLoadIdentity();
    glBegin(GL_QUADS);
    for (k = 0; k < TILES; k++)
    {
        a = (float) k;
        x = 0.80f * B_CosT(k * B_TABLE / TILES + (LONG) f * 6);
        y = 0.80f * B_SinT(k * B_TABLE / TILES + (LONG) f * 6);
        glColor4f(0.5f + 0.5f * B_SinT((LONG) f * 5 + k * 128), 0.5f + 0.5f * B_SinT((LONG) f * 5 + k * 128 + 340),
                  0.5f + 0.5f * B_SinT((LONG) f * 5 + k * 128 + 680), 1.0f);
        glVertex2f(x - 0.06f, y - 0.06f);
        glVertex2f(x + 0.06f, y - 0.06f);
        glVertex2f(x + 0.06f, y + 0.06f);
        glVertex2f(x - 0.06f, y + 0.06f);
        (void) a;
    }
    glEnd();
    bench_prims += TILES * 2;
    /* and a lit dodecahedron going round */
    B_Perspective(40.0f, 1.0f, 1.0f, 10.0f);
    glLoadIdentity();
    glLightfv(GL_LIGHT0, GL_POSITION, lpos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, ldif);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glTranslatef(1.05f * B_CosT((LONG) f * -4), 1.05f * B_SinT((LONG) f * -4), -4.0f);
    glRotatef((float) f * 3.0f, 1.0f, 0.6f, 0.2f);
    glScalef(0.22f, 0.22f, 0.22f);
    glColor4f(0.95f, 0.75f, 0.35f, 1.0f);
    glutSolidDodecahedron();
    glDisable(GL_CULL_FACE);
    glDisable(GL_COLOR_MATERIAL);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    bench_prims += 36;

    /* back over the bus, and in again */
    glReadPixels(0, 0, FB, FB, GL_RGB, GL_UNSIGNED_BYTE, mr_buf);
    glBindTexture(GL_TEXTURE_2D, mr_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, FB, FB, GL_RGB, GL_UNSIGNED_BYTE, mr_buf);
    bench_bytes += 2 * FB * FB * 3;

    /* the screen: the texture as a square in the middle */
    glViewport(0, 0, (GLsizei) w, (GLsizei) h);
    glClearColor(0.03f, 0.03f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    B_Ortho2D(w, h);
    side = ((w < h) ? w : h) - 24;
    x0 = (w - side) / 2;
    y0 = (h - side) / 2 + 6;
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f((float) x0, (float) y0);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f((float) (x0 + side), (float) y0);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f((float) (x0 + side), (float) (y0 + side));
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f((float) x0, (float) (y0 + side));
    glEnd();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_TEXTURE_2D);
    bench_prims += 2;
}

static void Done(void)
{
    if (mr_have)
        glDeleteTextures(1, &mr_tex);
    mr_have = FALSE;
    B_Free(mr_buf);
    mr_buf = NULL;
}

const struct BenchScene scene_mirror =
{
    "Mirror", "read-back: every frame read from the card and sent back", 300, FALSE, Init, Frame, Done
};
