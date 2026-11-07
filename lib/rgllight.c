/*
 * rgllight.c -- the lighting hooks (2026-11-07, src/core/light.c does the
 * work).
 *
 * A file of their own, linked after everything else (Makefile RGL_OBJS):
 * MEASURED 2026-11-07 on the A4000D, the same hooks at the end of rgl.c
 * moved the core's hot code by 892 bytes and cost GLQuake ~0.8% (demo1
 * 33.1 -> 32.8 fps, interleaved A/B, the work dword for dword the same) --
 * the 68060's caches care where the vertex and array code sits. GLQuake
 * calls none of these.
 */

#include <exec/types.h>
#include <libraries/minigl_dispatch.h>

#include "rgl_dispatch.h"
#include "rgl.h"
#include "core.h"

void Hook_GLLightf(GLcontext context, GLenum light, GLenum pname, GLfloat param)
{
    struct Core *c;

    (void) context;
    if ((c = Rgl_Core()) == NULL)
        return;
    Core_Lightfv(c, light, pname, &param, 1);
}
void Hook_GLLightfv(GLcontext context, GLenum light, GLenum pname, const GLfloat *params)
{
    struct Core *c;

    (void) context;
    if ((c = Rgl_Core()) == NULL)
        return;
    Core_Lightfv(c, light, pname, params, 4);
}
void Hook_GLLightModelf(GLcontext context, GLenum pname, GLfloat param)
{
    struct Core *c;

    (void) context;
    if ((c = Rgl_Core()) == NULL)
        return;
    Core_LightModelfv(c, pname, &param, 1);
}
void Hook_GLLightModelfv(GLcontext context, GLenum pname, const GLfloat *params)
{
    struct Core *c;

    (void) context;
    if ((c = Rgl_Core()) == NULL)
        return;
    Core_LightModelfv(c, pname, params, 4);
}
void Hook_GLMaterialf(GLcontext context, GLenum face, GLenum pname, GLfloat param)
{
    struct Core *c;

    (void) context;
    if ((c = Rgl_Core()) == NULL)
        return;
    Core_Materialfv(c, face, pname, &param, 1);
}
void Hook_GLMaterialfv(GLcontext context, GLenum face, GLenum pname, const GLfloat *params)
{
    struct Core *c;

    (void) context;
    if ((c = Rgl_Core()) == NULL)
        return;
    Core_Materialfv(c, face, pname, params, 4);
}
void Hook_GLGetLightfv(GLcontext context, GLenum light, GLenum pname, GLfloat *params)
{
    struct Core *c;

    (void) context;
    if ((c = Rgl_Core()) == NULL)
        return;
    Core_GetLightfv(c, light, pname, params);
}
void Hook_GLGetMaterialfv(GLcontext context, GLenum face, GLenum pname, GLfloat *params)
{
    struct Core *c;

    (void) context;
    if ((c = Rgl_Core()) == NULL)
        return;
    Core_GetMaterialfv(c, face, pname, params);
}
void Hook_GLColorMaterial(GLcontext context, GLenum face, GLenum mode)
{
    struct Core *c;

    (void) context;
    if ((c = Rgl_Core()) == NULL)
        return;
    Core_ColorMaterial(c, face, mode);
}
void Hook_GLNormalPointer(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    struct Core *c;

    (void) context;
    if ((c = Rgl_Core()) == NULL)
        return;
    Core_NormalPointer(c, type, stride, pointer);
}
