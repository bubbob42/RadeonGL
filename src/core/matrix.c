/*
 * matrix.c -- matrix stacks and the GL matrix operations.
 *
 * Column-major as GL keeps them: element (row r, column c) is m[c * 4 + r].
 * A plain 4x4 multiply for the general case; MVP is formed when the
 * matrix block is emitted (hwstate.c). Trigonometry is float and needs no
 * libm: glRotatef(EXT) is called per entity, and MiniGLV3D's double
 * sin/cos there was measurable.
 *
 * GLQuake turns every entity with glTranslatef and three glRotatef about
 * the unit axes. On the 060 at 50 MHz an FDIV costs about 37 cycles, and
 * the first Rotatef here spent 30 of them (a Newton square root, the
 * sine's divisions): about 80 us a call. Now a unit axis needs no square
 * root and no division, the sine multiplies by reciprocal constants, and a
 * rotation about a unit axis or a translation changes only the columns it
 * touches (M x R: two columns; M x T: the last), with the same products in
 * the same order as the full multiply, so the result is the same.
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
    r = x * (1.0f - x2 * (1.0f / 6.0f) * (1.0f - x2 * (1.0f / 20.0f) * (1.0f - x2 * (1.0f / 42.0f)
           * (1.0f - x2 * (1.0f / 72.0f) * (1.0f - x2 * (1.0f / 110.0f))))));
    return r;
}

float Core_Sin(float deg)
{
    float x;
    LONG turns;

    /* reduce to [-180, 180) degrees, then fold to [-90, 90] */
    turns = Core_Floor((deg + 180.0f) * (1.0f / 360.0f));
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
#if !(defined(__GNUC__) && defined(__mc68000__) && defined(__HAVE_68881__))
    int i;
#endif

    if (!(v > 0.0f))
        return 0.0f;
#if defined(__GNUC__) && defined(__mc68000__) && defined(__HAVE_68881__)
    /* FSQRT is in the 040's and the 060's FPU, no trap */
    __asm__("fsqrt.x %1,%0" : "=f" (x) : "f" (v));
#else
    x = v > 1.0f ? v : 1.0f;
    for (i = 0; i < 20; i++)
        x = 0.5f * (x + v / x);
#endif
    return x;
}

float Core_Sqrt(float v)
{
    return Sqrt(v);
}

/* An affine matrix: the bottom row exactly 0 0 0 1, read as bits (a -0.0f
 * takes the general way, which is merely slower). */
static ULONG Affine(const float *m)
{
    const ULONG *u;

    u = (const ULONG *) (const void *) m;
    return (u[3] == 0 && u[7] == 0 && u[11] == 0 && u[15] == 0x3f800000UL) ? 1UL : 0UL;
}

/* out = a b. With b affine (every modelview, GLQuake's rotations and
 * translations) the products with its zero bottom row are left out, and
 * the last column adds a's fourth column itself: 48 multiplications
 * instead of 64, 36 when a is affine as well (its bottom row is then b's).
 * The sums keep their order, so the results are the general way's. */
void Mat_Mul(float *out, const float *a, const float *b)
{
    float t[16];
    ULONG r;
    ULONG k;
    ULONG rows;

    if (!Affine(b))
    {
        for (k = 0; k < 4; k++)
            for (r = 0; r < 4; r++)
                t[k * 4 + r] = a[0 * 4 + r] * b[k * 4 + 0] + a[1 * 4 + r] * b[k * 4 + 1]
                             + a[2 * 4 + r] * b[k * 4 + 2] + a[3 * 4 + r] * b[k * 4 + 3];
    }
    else
    {
        rows = 4;
        if (Affine(a))
        {
            rows = 3;
            t[3] = 0.0f;
            t[7] = 0.0f;
            t[11] = 0.0f;
            t[15] = 1.0f;
        }
        for (k = 0; k < 3; k++)
            for (r = 0; r < rows; r++)
                t[k * 4 + r] = a[0 * 4 + r] * b[k * 4 + 0] + a[1 * 4 + r] * b[k * 4 + 1]
                             + a[2 * 4 + r] * b[k * 4 + 2];
        for (r = 0; r < rows; r++)
            t[12 + r] = a[0 * 4 + r] * b[12] + a[1 * 4 + r] * b[13] + a[2 * 4 + r] * b[14]
                      + a[3 * 4 + r];
    }
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

/* The current matrix changed: a new version (the MVP upload compares
 * versions, hwstate.c). */
static void Changed(struct Core *c)
{
    if (c->matrix_mode == GL_TEXTURE)
        return;
    c->dirty |= D_MATRIX;
    Core_TopMatrix(c, c->matrix_mode)->ver = ++c->mat_ver;
}

/* The current matrix of the mode, or NULL (an error raised) inside
 * glBegin. */
static float *Current(struct Core *c)
{
    if (IN_BEGIN(c))
    {
        Core_Error(c, GL_INVALID_OPERATION);
        return NULL;
    }
    return Core_TopMatrix(c, c->matrix_mode)->m;
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
    /* the saved matrix comes back with its own version: after GLQuake's
     * push, entity, pop, the world's MVP is still in the TCL and is not
     * sent again (MEASURED: 32 uploads a frame, half of them repeats) */
    if (c->matrix_mode != GL_TEXTURE)
        c->dirty |= D_MATRIX;
}

void Core_Translatef(struct Core *c, float x, float y, float z)
{
    float *m;
    ULONG r;

    /* M x T: column 3 = x c0 + y c1 + z c2 + c3, in Mat_Mul's order */
    m = Current(c);
    if (m == NULL)
        return;
    for (r = 0; r < 4; r++)
        m[12 + r] = m[r] * x + m[4 + r] * y + m[8 + r] * z + m[12 + r];
    Changed(c);
}

void Core_Scalef(struct Core *c, float x, float y, float z)
{
    float *m;
    ULONG r;

    m = Current(c);
    if (m == NULL)
        return;
    for (r = 0; r < 4; r++)
    {
        m[r] *= x;
        m[4 + r] *= y;
        m[8 + r] *= z;
    }
    Changed(c);
}

/* m = m x R for R the rotation by (s, co) about a unit axis: columns a
 * and b change (z: 0, 1; y: 2, 0; x: 1, 2), a = co a + s b, b = co b - s a.
 * The full multiply forms the same sums (the other products are exact
 * zeros). */
static void Rot2(float *m, ULONG a, ULONG b, float s, float co)
{
    float *pa;
    float *pb;
    float va;
    float vb;
    ULONG r;

    pa = m + a * 4;
    pb = m + b * 4;
    for (r = 0; r < 4; r++)
    {
        va = pa[r];
        vb = pb[r];
        pa[r] = va * co + vb * s;
        pb[r] = vb * co - va * s;
    }
}

/* A rotation about a unit axis: 0 (x), 1 (y), 2 (z). */
static void RotUnit(struct Core *c, ULONG axis, float s, float co)
{
    float *m;

    m = Current(c);
    if (m == NULL)
        return;
    if (axis == 2)
        Rot2(m, 0, 1, s, co);
    else if (axis == 1)
        Rot2(m, 2, 0, s, co);
    else
        Rot2(m, 1, 2, s, co);
    Changed(c);
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
    if (mag == 0.0f)
        return;
    s = Core_Sin(angle);
    co = Core_Cos(angle);
    if (mag == 1.0f && (x == 1.0f || y == 1.0f || z == 1.0f))
    {
        /* a unit axis (GLQuake's entities: always) */
        RotUnit(c, (x == 1.0f) ? 0 : (y == 1.0f) ? 1 : 2, s, co);
        return;
    }
    if (mag != 1.0f)
    {
        mag = 1.0f / Sqrt(mag);
        x *= mag;
        y *= mag;
        z *= mag;
    }
    RotMat(t, s, co, x, y, z);
    Mult(c, t);
}

void Core_RotateAxis(struct Core *c, float s, float co, LONG axis)
{
    if (axis == (LONG) GLROT_001)
        RotUnit(c, 2, s, co);
    else if (axis == (LONG) GLROT_010)
        RotUnit(c, 1, s, co);
    else if (axis == (LONG) GLROT_100)
        RotUnit(c, 0, s, co);
    else
    {
        /* only the single-axis selectors exist (MiniGLV3D refuses the rest too) */
        Core_Error(c, GL_INVALID_ENUM);
    }
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
