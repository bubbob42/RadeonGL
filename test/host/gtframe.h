/*
 * gtframe.h -- a frame shaped like GLTron's (2026-11-11), for the polygon
 * mode regression test (test_core.c test_polymode_frames): its A/B on the
 * A4000D showed every pixel of the frame dithered differently once the
 * Recognizer outline went through polymode.c -- the current normal was left
 * at the outline's last vertex, and lit draws after it took that one.
 *
 * Included by test_core.c, and by a throwaway driver built against older
 * cores (GT_PLAIN: no ...X calls, which the library's hooks use since then).
 * Draws, in GLTron's order: a lit floor in immediate mode that relies on
 * the current normal (set once, before the frames), a lit body from locked
 * arrays with normals under the static hints and glPolygonOffset, the
 * Recognizer -- lighting off, red, blended, GL_LINE_SMOOTH, back faces
 * culled, glDrawElements(GL_QUADS, GL_UNSIGNED_SHORT) in a lock of its own,
 * in GL_LINE when asked -- a lit wall that takes the current normal, and
 * lit trails through glDrawArrays.
 */

#ifndef GTFRAME_H
#define GTFRAME_H

#ifdef GT_PLAIN
#define GT_BEGIN(c, m)              Core_Begin((c), (m))
#define GT_END(c)                   Core_End(c)
#define GT_ELEMENTS(c, m, n, t, i)  Core_DrawElements((c), (m), (n), (t), (i))
#define GT_ARRAYS(c, m, f, n)       Core_DrawArrays((c), (m), (f), (n))
#define GT_POLYMODE(c, f, m)        Core_PolygonMode((c), (f), (m))
#else
#define GT_BEGIN(c, m)              Core_BeginX((c), (m))
#define GT_END(c)                   Core_EndX(c)
#define GT_ELEMENTS(c, m, n, t, i)  Core_DrawElementsX((c), (m), (n), (t), (i))
#define GT_ARRAYS(c, m, f, n)       Core_DrawArraysX((c), (m), (f), (n))
#define GT_POLYMODE(c, f, m)        Core_PolygonModeX((c), (f), (m))
#endif

/* a box of 8 corners, 12 triangles, a normal per corner (pointing out) */
static const float gt_body_v[8 * 3] = {
    -1, -1, -1,   1, -1, -1,   1, 1, -1,   -1, 1, -1,
    -1, -1,  1,   1, -1,  1,   1, 1,  1,   -1, 1,  1 };
static const float gt_body_n[8 * 3] = {
    -0.577f, -0.577f, -0.577f,   0.577f, -0.577f, -0.577f,   0.577f, 0.577f, -0.577f,
    -0.577f, 0.577f, -0.577f,   -0.577f, -0.577f, 0.577f,   0.577f, -0.577f, 0.577f,
    0.577f, 0.577f, 0.577f,     -0.577f, 0.577f, 0.577f };
static const UWORD gt_body_i[36] = {
    0, 2, 1, 0, 3, 2,   4, 5, 6, 4, 6, 7,   0, 1, 5, 0, 5, 4,
    2, 3, 7, 2, 7, 6,   1, 2, 6, 1, 6, 5,   3, 0, 4, 3, 4, 7 };

/* the Recognizer: four quads, two facing the eye, two away; normals given */
static float gt_rec_v[16 * 3];
static float gt_rec_n[16 * 3];
static const UWORD gt_rec_i[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

/* trails: two quads standing on the floor */
static const float gt_trail_v[8 * 3] = {
    -3, -2, 0,   -1, -2, 0,   -1, -2, 1,   -3, -2, 1,
     1, -2, 0,    3, -2, 0,    3, -2, 1,    1, -2, 1 };
static const float gt_trail_n[8 * 3] = {
    0, -1, 0,  0, -1, 0,  0, -1, 0,  0, -1, 0,
    0, -1, 0,  0, -1, 0,  0, -1, 0,  0, -1, 0 };

static void GtSetup(struct Core *c)
{
    static const float pos[4] = { 2.0f, -3.0f, 6.0f, 0.0f };
    static const float dif[4] = { 0.8f, 0.7f, 0.9f, 1.0f };
    static const float amb[4] = { 0.25f, 0.0f, 0.25f, 1.0f };
    ULONG q;
    ULONG k;
    float x;
    float y;
    float z;

    for (q = 0; q < 4; q++)
    {
        x = -2.0f + (float) q;
        y = 1.0f;
        z = 2.0f;
        for (k = 0; k < 4; k++)
        {
            /* corners in order; quads 1 and 3 the other way round */
            ULONG kk = (q & 1) ? 3 - k : k;
            gt_rec_v[(q * 4 + k) * 3 + 0] = x + ((kk == 1 || kk == 2) ? 0.8f : 0.0f);
            gt_rec_v[(q * 4 + k) * 3 + 1] = y + ((kk >= 2) ? 0.8f : 0.0f);
            gt_rec_v[(q * 4 + k) * 3 + 2] = z;
            gt_rec_n[(q * 4 + k) * 3 + 0] = 0.0f;
            gt_rec_n[(q * 4 + k) * 3 + 1] = 0.3f * (float) q;
            gt_rec_n[(q * 4 + k) * 3 + 2] = (q & 1) ? -1.0f : 1.0f;
        }
    }
    Core_Lightfv(c, GL_LIGHT0, GL_POSITION, pos, 4);
    Core_Lightfv(c, GL_LIGHT0, GL_DIFFUSE, dif, 4);
    Core_LightModelfv(c, GL_LIGHT_MODEL_AMBIENT, amb, 4);
    Core_Enable(c, GL_LIGHT0, 1);
    Core_Enable(c, GL_DEPTH_TEST, 1);
    Core_Enable(c, GL_DITHER, 1);
    Core_Normal3f(c, 0.0f, 0.0f, 1.0f);     /* the floor's, once (GLTron leaves it) */
}

/* One frame; mode: the Recognizer's glPolygonMode (0: never called). The
 * stream positions around the Recognizer's draw into *from, *to. */
static void GtFrame(struct Core *c, ULONG frame, ULONG mode, ULONG **from, ULONG **to)
{
    float a;

    a = (float) (frame * 7 % 360);
    Core_ClearColor(c, 0.06f, 0.0f, 0.06f, 1.0f);
    Core_Clear(c, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Core_MatrixMode(c, GL_PROJECTION);
    Core_LoadIdentity(c);
    Core_Frustum(c, -1.0, 1.0, -0.75, 0.75, 1.0, 100.0);
    Core_MatrixMode(c, GL_MODELVIEW);
    Core_LoadIdentity(c);
    Core_Translatef(c, 0.0f, 0.0f, -8.0f);
    Core_Rotatef(c, -60.0f, 1.0f, 0.0f, 0.0f);
    Core_Rotatef(c, a, 0.0f, 0.0f, 1.0f);
    Core_Enable(c, GL_LIGHTING, 1);

    /* the floor: lit, no glNormal of its own */
    Core_Color4f(c, 0.3f, 0.1f, 0.3f, 1.0f);
    GT_BEGIN(c, GL_QUADS);
    Core_Vertex3f(c, -5, -5, 0); Core_Vertex3f(c, 5, -5, 0);
    Core_Vertex3f(c, 5, 5, 0); Core_Vertex3f(c, -5, 5, 0);
    GT_END(c);

    /* the body: lit, locked, static, offset */
    Core_VertexPointer(c, 3, GL_FLOAT, 0, gt_body_v);
    Core_NormalPointer(c, GL_FLOAT, 0, gt_body_n);
    Core_EnableClientState(c, GL_VERTEX_ARRAY, 1);
    Core_EnableClientState(c, GL_NORMAL_ARRAY, 1);
    Core_Hint(c, GL_RGL_STATIC_ARRAYS_HINT, GL_NICEST);
    Core_Hint(c, GL_RGL_STATIC_INDICES_HINT, GL_NICEST);
    Core_Enable(c, GL_POLYGON_OFFSET_FILL, 1);
    Core_PolygonOffset(c, 1.0f, 1.0f);
    Core_LockArrays(c, 0, 8);
    GT_ELEMENTS(c, GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, gt_body_i);
    Core_UnlockArrays(c);
    Core_Enable(c, GL_POLYGON_OFFSET_FILL, 0);

    /* the Recognizer's outline */
    Core_VertexPointer(c, 3, GL_FLOAT, 0, gt_rec_v);
    Core_NormalPointer(c, GL_FLOAT, 0, gt_rec_n);
    Core_Enable(c, GL_LIGHTING, 0);
    Core_Color4f(c, 1.0f, 0.0f, 0.0f, 1.0f);
    Core_Enable(c, GL_BLEND, 1);
    Core_BlendFunc(c, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    Core_Enable(c, GL_LINE_SMOOTH, 1);
    Core_Enable(c, GL_CULL_FACE, 1);
    if (mode != 0)
        GT_POLYMODE(c, GL_FRONT_AND_BACK, mode);
    Core_LockArrays(c, 0, 16);
    *from = c->cs->ptr;
    GT_ELEMENTS(c, GL_QUADS, 16, GL_UNSIGNED_SHORT, gt_rec_i);
    *to = c->cs->ptr;
    Core_UnlockArrays(c);
    if (mode != 0)
        GT_POLYMODE(c, GL_FRONT_AND_BACK, GL_FILL);
    Core_Enable(c, GL_CULL_FACE, 0);
    Core_Enable(c, GL_LINE_SMOOTH, 0);
    Core_Enable(c, GL_BLEND, 0);
    Core_Hint(c, GL_RGL_STATIC_ARRAYS_HINT, GL_DONT_CARE);
    Core_Hint(c, GL_RGL_STATIC_INDICES_HINT, GL_DONT_CARE);

    /* a wall: lit, immediate, the current normal again (what the outline
     * must leave as it found it) */
    Core_Enable(c, GL_LIGHTING, 1);
    Core_Color4f(c, 0.5f, 0.5f, 0.6f, 1.0f);
    GT_BEGIN(c, GL_TRIANGLE_STRIP);
    Core_Vertex3f(c, -5, 5, 0); Core_Vertex3f(c, -5, 5, 2);
    Core_Vertex3f(c, 5, 5, 0); Core_Vertex3f(c, 5, 5, 2);
    GT_END(c);

    /* the trails: lit, glDrawArrays with normals (they leave the current
     * normal at their last one, in every version) */
    Core_Enable(c, GL_LIGHTING, 1);
    Core_Color4f(c, 0.2f, 0.9f, 0.9f, 1.0f);
    Core_VertexPointer(c, 3, GL_FLOAT, 0, gt_trail_v);
    Core_NormalPointer(c, GL_FLOAT, 0, gt_trail_n);
    GT_ARRAYS(c, GL_QUADS, 0, 8);
    Core_EnableClientState(c, GL_NORMAL_ARRAY, 0);
    Core_EnableClientState(c, GL_VERTEX_ARRAY, 0);
    Core_GpuIdle(c);
}

#endif
