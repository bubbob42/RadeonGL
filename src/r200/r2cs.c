/*
 * r2cs.c -- see r2cs.h.
 */

#include "r2cs.h"

void R2Cs_InitBuffer(struct R2Cs *cs, ULONG *buf, ULONG ndw, ULONG swap)
{
    cs->ptr = buf;
    cs->end = buf + ndw;
    cs->swap = swap;
    cs->need = NULL;
    cs->owner = NULL;
    cs->error = R2_OK;
    cs->immd = NULL;
    cs->immd_vtx = 0;
}

ULONG *R2Cs_Room(struct R2Cs *cs, ULONG ndw)
{
    LONG r;

    if (cs->error != R2_OK)
        return NULL;
    if ((ULONG) (cs->end - cs->ptr) >= ndw)
        return cs->ptr;
    if (cs->need == NULL)
    {
        cs->error = R2_EFULL;
        return NULL;
    }
    r = cs->need(cs, ndw);
    if (r != R2_OK)
    {
        cs->error = r;
        return NULL;
    }
    if ((ULONG) (cs->end - cs->ptr) < ndw)
    {
        /* an owner that says yes must mean it */
        cs->error = R2_EFULL;
        return NULL;
    }
    return cs->ptr;
}

ULONG R2Cs_Swap32(ULONG v)
{
    return ((v >> 24) & 0x000000ffUL)
         | ((v >>  8) & 0x0000ff00UL)
         | ((v <<  8) & 0x00ff0000UL)
         | ((v << 24) & 0xff000000UL);
}

ULONG R2Cs_FloatBits(float f)
{
    ULONG u;
    const UBYTE *s;
    UBYTE *d;

    /* bytes, not a union: SAS/C warns about punning through one */
    s = (const UBYTE *) &f;
    d = (UBYTE *) &u;
    d[0] = s[0];
    d[1] = s[1];
    d[2] = s[2];
    d[3] = s[3];
    return u;
}
