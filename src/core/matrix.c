/*
 * matrix.c -- matrix stacks and the GL matrix operations.
 *
 * Column-major as GL keeps them: element (row r, column c) is m[c * 4 + r].
 * One plain 4x4 multiply for everything (a few microseconds on the 060,
 * 10-13 matrix changes a GLQuake frame); MVP is formed when the matrix
 * block is emitted (hwstate.c). Trigonometry is float and needs no libm:
 * glRotatef(EXT) is called per entity, and MiniGLV3D's double sin/cos
 * there was measurable.
 */

#include "core.h"
#include "coreint.h"

#define PI_F 3.14159265358979f

/* sin(x) for x in [-pi/2, pi/2], Taylor to x^11 (error below 1e-7). */
static float SinCore(float x)
{
    float x2;
    float r;

    x2 = x * x;
    r = x * (1.0f - x2 / 6.0f * (1.0f - x2 / 20.0f * (1.0f - x2 / 42.0f
           * (1.0f - x2 / 72.0f * (1.0f - x2 / 110.0f)))));
    return r;
}

float Core_Sin(float deg)
{
    float x;
    LONG turns;

    /* reduce to [-180, 180) degrees, then fold to [-90, 90] */
    turns = Core_Floor((deg + 180.0f) / 360.0f);
    deg -= (float) turns * 360.0f;
    if (deg > 90.0f)
        deg = 180.0f - deg;
    else if (deg < -90.0f)
        deg = -180.0f - deg;
    x = deg * (PI_F / 180.0f);
    return SinCore(x);
}

float Core_Cos(float deg)
{
    float d;

    d = deg + 90.0f;
    return Core_Sin(d);
}

static float Sqrt(float v)
{
    float x;
    int i;

    if (!(v > 0.0f))
        return 0.0f;
    x = v > 1.0f ? v : 1.0f;
    for (i = 0; i < 20; i++)
        x = 0.5f * (x + v / x);
    return x;
}

void Mat_Mul(float *out, const float *a, const float *b)
{
    float t[16];
    ULONG r;
    ULONG k;

    for (k = 0; k < 4; k++)
        for (r = 0; r < 4; r++)
            t[k * 4 + r] = a[0 * 4 + r] * b[k * 4 + 0] + a[1 * 4 + r] * b[k * 4 + 1]
                         + a[2 * 4 + r] * b[k * 4 + 2] + a[3 * 4 + r] * b[k * 4 + 3];
    for (k = 0; k < 16; k++)
        out[k] = t[k];
}

static void Ident(float *m)
{
    ULONG i;

    for (i = 0; i < 16; i++)
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

struct CoreMat *Core_TopMatrix(struct Core *c, ULONG mode)
{
    switch (mode)
    {
        case GL_MODELVIEW:  return &c->mv[c->mv_top];
        case GL_PROJECTION: return &c->proj[c->proj_top];
        case GL_TEXTURE:    return &c->texm[c->active_unit][c->tex_top[c->active_unit]];
    }
    return NULL;
}

static void Changed(struct Core *c)
{
    if (c->matrix_mode != GL_TEXTURE)
        c->dirty |= D_MATRIX;
}

/* current = current x m */
static void Mult(struct Core *c, const float *m)
{
    struct CoreMat *top;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    top = Core_TopMatrix(c, c->matrix_mode);
    Mat_Mul(top->m, top->m, m);
    Changed(c);
}

void Core_MatrixMode(struct Core *c, ULONG mode)
{
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    if (mode != GL_MODELVIEW && mode != GL_PROJECTION && mode != GL_TEXTURE)
    {
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    c->matrix_mode = mode;
}

void Core_LoadIdentity(struct Core *c)
{
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    Ident(Core_TopMatrix(c, c->matrix_mode)->m);
    Changed(c);
}

void Core_LoadMatrixf(struct Core *c, const float *m)
{
    struct CoreMat *top;
    ULONG i;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    top = Core_TopMatrix(c, c->matrix_mode);
    for (i = 0; i < 16; i++)
        top->m[i] = m[i];
    Changed(c);
}

void Core_MultMatrixf(struct Core *c, const float *m)
{
    Mult(c, m);
}

void Core_PushMatrix(struct Core *c)
{
    ULONG *top;
    ULONG depth;
    struct CoreMat *stack;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    switch (c->matrix_mode)
    {
        case GL_MODELVIEW:  top = &c->mv_top; depth = CORE_MV_DEPTH; stack = c->mv; break;
        case GL_PROJECTION: top = &c->proj_top; depth = CORE_P_DEPTH; stack = c->proj; break;
        default:
            top = &c->tex_top[c->active_unit];
            depth = CORE_T_DEPTH;
            stack = c->texm[c->active_unit];
            break;
    }
    if (*top + 1 >= depth)
    {
        Core_Error(c, GL_STACK_OVERFLOW);
        return;
    }
    stack[*top + 1] = stack[*top];
    (*top)++;
}

void Core_PopMatrix(struct Core *c)
{
    ULONG *top;

    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return;
    }
    switch (c->matrix_mode)
    {
        case GL_MODELVIEW:  top = &c->mv_top; break;
        case GL_PROJECTION: top = &c->proj_top; break;
        default:            top = &c->tex_top[c->active_unit]; break;
    }
    if (*top == 0)
    {
        Core_Error(c, GL_STACK_UNDERFLOW);   /* MiniGLV3D said INVALID_OPERATION */
        return;
    }
    (*top)--;
    Changed(c);
}

void Core_Translatef(struct Core *c, float x, float y, float z)
{
    float t[16];

    Ident(t);
    t[12] = x;
    t[13] = y;
    t[14] = z;
    Mult(c, t);
}

void Core_Scalef(struct Core *c, float x, float y, float z)
{
    float t[16];

    Ident(t);
    t[0] = x;
    t[5] = y;
    t[10] = z;
    Mult(c, t);
}

/* The rotation by (s, co) about the unit axis (x, y, z): GL's formula. */
static void RotMat(float *t, float s, float co, float x, float y, float z)
{
    float one_c;

    one_c = 1.0f - co;
    Ident(t);
    t[0] = x * x * one_c + co;
    t[1] = y * x * one_c + z * s;
    t[2] = x * z * one_c - y * s;
    t[4] = x * y * one_c - z * s;
    t[5] = y * y * one_c + co;
    t[6] = y * z * one_c + x * s;
    t[8] = x * z * one_c + y * s;
    t[9] = y * z * one_c - x * s;
    t[10] = z * z * one_c + co;
}

void Core_Rotatef(struct Core *c, float angle, float x, float y, float z)
{
    float t[16];
    float mag;
    float s;
    float co;

    mag = x * x + y * y + z * z;
    mag = Sqrt(mag);
    if (mag == 0.0f)
        return;
    s = Core_Sin(angle);
    co = Core_Cos(angle);
    x /= mag;
    y /= mag;
    z /= mag;
    RotMat(t, s, co, x, y, z);
    Mult(c, t);
}

void Core_RotateAxis(struct Core *c, float s, float co, LONG axis)
{
    float t[16];

    if (axis == (LONG) GLROT_001)
        RotMat(t, s, co, 0.0f, 0.0f, 1.0f);
    else if (axis == (LONG) GLROT_010)
        RotMat(t, s, co, 0.0f, 1.0f, 0.0f);
    else if (axis == (LONG) GLROT_100)
        RotMat(t, s, co, 1.0f, 0.0f, 0.0f);
    else
    {
        /* only the single-axis selectors exist (MiniGLV3D refuses the rest too) */
        Core_Error(c, GL_INVALID_ENUM);
        return;
    }
    Mult(c, t);
}

void Core_RotateAxisDeg(struct Core *c, float angle, LONG axis)
{
    float s;
    float co;

    s = Core_Sin(angle);
    co = Core_Cos(angle);
    Core_RotateAxis(c, s, co, axis);
}

void Core_Ortho(struct Core *c, double l, double r, double b, double t, double n, double f)
{
    float m[16];

    if (l == r || b == t || n == f)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    Ident(m);
    m[0] = (float) (2.0 / (r - l));
    m[5] = (float) (2.0 / (t - b));
    m[10] = (float) (-2.0 / (f - n));
    m[12] = (float) (-(r + l) / (r - l));
    m[13] = (float) (-(t + b) / (t - b));
    m[14] = (float) (-(f + n) / (f - n));
    Mult(c, m);
}

void Core_Frustum(struct Core *c, double l, double r, double b, double t, double n, double f)
{
    float m[16];
    ULONG i;

    if (n <= 0.0 || f <= 0.0 || l == r || b == t || n == f)
    {
        Core_Error(c, GL_INVALID_VALUE);
        return;
    }
    for (i = 0; i < 16; i++)
        m[i] = 0.0f;
    m[0] = (float) (2.0 * n / (r - l));
    m[5] = (float) (2.0 * n / (t - b));
    m[8] = (float) ((r + l) / (r - l));
    m[9] = (float) ((t + b) / (t - b));
    m[10] = (float) (-(f + n) / (f - n));
    m[11] = -1.0f;
    m[14] = (float) (-2.0 * f * n / (f - n));
    Mult(c, m);
}
