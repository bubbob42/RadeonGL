/*
 * rgltable.h -- hooks the task's table points at while a state needs them
 * (rgltable.c, linked last): lit vertices, polygons not filled.
 */

#ifndef RADEONGL_RGLTABLE_H
#define RADEONGL_RGLTABLE_H

#include "rglint.h"

/* The context's first lighting call (rgllight.c): from now on its task's
 * table follows its state (once per context; cheap afterwards) */
void Rgltab_Arm(struct Core *c);
/* glPolygonMode (rgl.c's hook): arms, then the core's call */
void Rgltab_PolygonMode(struct Core *c, ULONG face, ULONG mode);
/* rgldlist.c changed the task's slots (a list begun or ended): set them
 * again from the context's state */
void Rgltab_TableReset(struct Rgl *r);
/* The context goes (rgldlist.c Rgldl_Free): the library's hooks back */
void Rgltab_Free(struct Rgl *r);
/* glBegin/glEnd with polygon mode honoured: the PM slots, and what a
 * display list records and replays (rgldlist.c) */
void Rgltab_GLBegin(GLcontext context, GLenum mode);
void Rgltab_GLEnd(GLcontext context);

#endif /* RADEONGL_RGLTABLE_H */
