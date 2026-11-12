/*
 * rgltable.c -- hooks the task's dispatch table points at only while a
 * state needs them (2026-11-12): lit vertices, polygons not filled.
 *
 * A program calls every GL function through its task's own copy of the
 * table (lib/shell.c, W3). The library's own hooks (rgl.c) are what GLQuake
 * runs, and they stay exactly as they were: the 68060's caches punish both
 * moved hot code and calls into code linked far away (Makefile, rgllight.c;
 * MEASURED 2026-11-08: ...X wrappers in front of glEnable cost GLQuake 1.1%,
 * and on 2026-11-12 polymode's in front of glBegin/glEnd and the draws cost
 * it 1.2-1.7%). A state that needs other hooks points the table's slots
 * here while it lasts, and back when it ends. This file is linked last.
 *
 * Two groups of four slots:
 *
 *   LIT  glVertex4f/3fv, glNormal3f/3fv while the vertices carry a normal
 *        (c->vnorm: lighting, sphere maps). With lighting on, the hot vertex
 *        path (vtxfast.h, vertex.c Emit) sends every vertex its slow way --
 *        rgl.c's hook, Core_VertexBits, Emit's lit branch, EmitLit's staging
 *        buffer: ~6 us a vertex on the A4000D's 68060/50 (GLTron's explosion
 *        fragments, 3,600 lit vertices a frame). These keep the normal as
 *        bits (no FPU) and write the vertex straight into the open packet
 *        (src/core/vtxlit.h); what that cannot take goes to
 *        Core_VertexBits, w != 1 to the library's own hook. The stream is
 *        the same dword for dword.
 *   PM   glBegin, glEnd, glDrawElements, glDrawArrays while a polygon mode
 *        other than GL_FILL is set (c->pm_mask): the core's ...X calls
 *        (polymode.c), which draw such polygons as edges or points.
 *
 * Which slots the table holds follows the core: texgen.c (vnorm) and
 * polymode.c (pm_mask) report every change through Core_SetStateHook once a
 * context is armed. Its first glLight/glLightModel/glMaterial/
 * glColorMaterial/glTexGeni (rgllight.c) or glPolygonMode (here) arms it: a
 * program that lights without any of those calls draws its lit vertices
 * the old way, correctly. A display list being made owns the table's slots
 * (rgldlist.c): its recorders call Rgltab_GLBegin/End, its end hands the
 * slots back. A task on the shared table (lib/shell.c) never gets these
 * hooks: the library's own draw a lit vertex right but slowly; polygon
 * mode needs the PM group, so such a task draws polygons filled. Every hook
 * here checks the state itself, so a second context of the same task in
 * another state still draws right through them.
 */

#include <exec/types.h>
#include <libraries/minigl_dispatch.h>

#include "rgl_dispatch.h"
#include "rgl.h"
#include "rglint.h"
#include "rgltable.h"
#include "core.h"
#include "vtxlit.h"

extern const MGLDispatchTable MiniGLDispatchTable;
#define LIB MiniGLDispatchTable     /* the library's own hooks */

#define FB(f) R2_FBITS(f)
#define FV(v, i) (((const ULONG *) (const void *) (v))[i])

/* after a call that takes an enum: remember who raised the first error
 * (as rgl.c's NOTE) */
#define NOTE(c, name, arg) \
    if ((c)->error != 0 && ((struct Rgl *) (c))->err_where == NULL) \
    { \
        ((struct Rgl *) (c))->err_where = name; \
        ((struct Rgl *) (c))->err_arg = (ULONG) (arg); \
    }

#define TAB_LIT     1UL
#define TAB_PM      2UL
#define TAB_UNKNOWN 0x80000000UL    /* a list's recorders changed slots */

/* --- LIT ---------------------------------------------------------------------- */

static void Lit_GLVertex4f(GLcontext context, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    struct Core *c;
    ULONG bx, by, bz;

    c = (struct Core *) context;
    if (c != NULL && FB(w) == 0x3f800000UL)
    {
        bx = FB(x);
        by = FB(y);
        bz = FB(z);
        CORE_VERTEX_LIT(c, bx, by, bz);     /* glVertex3f arrives here (w 1) */
    }
    else
        LIB.GLVertex4f(context, x, y, z, w);
}

static void Lit_GLVertex3fv(GLcontext context, GLfloat *v)
{
    struct Core *c;
    ULONG bx, by, bz;

    if ((c = (struct Core *) context) == NULL)
        return;
    bx = FV(v, 0);
    by = FV(v, 1);
    bz = FV(v, 2);
    CORE_VERTEX_LIT(c, bx, by, bz);
}

/* glNormal: the bits, without the FPU (as rgl.c's hooks move vertices) */
static void Lit_GLNormal3f(GLcontext context, GLfloat x, GLfloat y, GLfloat z)
{
    struct Core *c;

    if ((c = (struct Core *) context) == NULL)
        return;
    CORE_NORMAL_BITS(c, FB(x), FB(y), FB(z));
}

static void Lit_GLNormal3fv(GLcontext context, GLfloat *n)
{
    struct Core *c;

    if ((c = (struct Core *) context) == NULL)
        return;
    CORE_NORMAL_BITS(c, FV(n, 0), FV(n, 1), FV(n, 2));
}

/* --- PM ----------------------------------------------------------------------- */

void Rgltab_GLBegin(GLcontext context, GLenum mode)
{
    struct Core *c;

    if ((c = (struct Core *) context) == NULL)
        return;
    Core_BeginX(c, mode);
    NOTE(c, "glBegin", mode);
}

void Rgltab_GLEnd(GLcontext context)
{
    if (context != NULL)
        Core_EndX((struct Core *) context);
}

static void Pm_GLDrawElements(GLcontext context, GLenum mode, GLsizei count, GLenum type,
                              const GLvoid *indices)
{
    struct Core *c;

    if ((c = (struct Core *) context) == NULL)
        return;
    Core_DrawElementsX(c, mode, count, type, indices);
    NOTE(c, "glDrawElements", type);
}

static void Pm_GLDrawArrays(GLcontext context, GLenum mode, GLint first, GLsizei count)
{
    struct Core *c;

    if ((c = (struct Core *) context) == NULL)
        return;
    Core_DrawArraysX(c, mode, first, count);
    NOTE(c, "glDrawArrays", mode);
}

/* --- the table ------------------------------------------------------------------ */

/* The slots a list's recorders do not own (rgldlist.c): the array draws */
static void AimArrays(MGLDispatchTable *t, ULONG want)
{
    t->GLDrawElements = (want & TAB_PM) ? Pm_GLDrawElements : LIB.GLDrawElements;
    t->GLDrawArrays = (want & TAB_PM) ? Pm_GLDrawArrays : LIB.GLDrawArrays;
}

static void Aim(MGLDispatchTable *t, ULONG want)
{
    BOOL lit;

    lit = (BOOL) ((want & TAB_LIT) != 0);
    t->GLVertex4f = lit ? Lit_GLVertex4f : LIB.GLVertex4f;
    t->GLVertex3fv = lit ? Lit_GLVertex3fv : LIB.GLVertex3fv;
    t->GLNormal3f = lit ? Lit_GLNormal3f : LIB.GLNormal3f;
    t->GLNormal3fv = lit ? Lit_GLNormal3fv : LIB.GLNormal3fv;
    t->GLBegin = (want & TAB_PM) ? Rgltab_GLBegin : LIB.GLBegin;
    t->GLEnd = (want & TAB_PM) ? Rgltab_GLEnd : LIB.GLEnd;
    AimArrays(t, want);
}

/* Core_SetStateHook's function, and the first look when armed: the calling
 * task's table follows the context's state */
static void Sync(struct Core *c)
{
    struct Rgl *r;
    MGLDispatchTable *t;
    ULONG want;
    BOOL rec;

    r = (struct Rgl *) c;
    want = (c->vnorm ? TAB_LIT : 0UL) | (c->pm_mask ? TAB_PM : 0UL);
    rec = Rgldl_Recording(r);
    if (!rec && want == r->tab_hooks)
        return;
    t = Shell_TaskTable();
    if (t == NULL)
        return;                     /* the shared table: every task's */
    r->tab_table = t;
    if (rec)
    {
        /* the recorders own the rest; Hook_GLEndList asks again */
        AimArrays(t, want);
        r->tab_hooks = TAB_UNKNOWN;
        return;
    }
    Aim(t, want);
    r->tab_hooks = want;
}

void Rgltab_Arm(struct Core *c)
{
    struct Rgl *r;

    r = (struct Rgl *) c;
    if (r->tab_armed)
        return;
    r->tab_armed = 1;
    Core_SetStateHook(Sync);
    Sync(c);
}

void Rgltab_PolygonMode(struct Core *c, ULONG face, ULONG mode)
{
    Rgltab_Arm(c);
    Core_PolygonModeX(c, face, mode);   /* a change of pm_mask: Sync */
}

void Rgltab_TableReset(struct Rgl *r)
{
    r->tab_hooks = TAB_UNKNOWN;
    if (r->tab_armed)
        Sync(&r->core);
}

void Rgltab_Free(struct Rgl *r)
{
    if (r->tab_hooks != 0 && r->tab_table != NULL && Shell_TaskTable() == r->tab_table)
        Aim(r->tab_table, 0);       /* a list being made: Rgldl_Free swaps after this */
    r->tab_hooks = 0;
    r->tab_table = NULL;
}
