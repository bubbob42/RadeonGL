/*
 * glextra.h -- GL 1.1 / ARB constants the core uses that the MiniGL SDK's
 * gl.h does not define. Values from the GL specification and the ARB
 * extension registry.
 */

#ifndef CORE_GLEXTRA_H
#define CORE_GLEXTRA_H

#ifndef GL_ACCUM_BUFFER_BIT
#define GL_ACCUM_BUFFER_BIT             0x00000200UL
#endif
#ifndef GL_STENCIL_BUFFER_BIT
#define GL_STENCIL_BUFFER_BIT           0x00000400UL
#endif
#ifndef GL_MAX_MODELVIEW_STACK_DEPTH
#define GL_MAX_MODELVIEW_STACK_DEPTH    0x0d36UL
#endif
#ifndef GL_MAX_PROJECTION_STACK_DEPTH
#define GL_MAX_PROJECTION_STACK_DEPTH   0x0d38UL
#endif
#ifndef GL_MAX_TEXTURE_STACK_DEPTH
#define GL_MAX_TEXTURE_STACK_DEPTH      0x0d39UL
#endif
#ifndef GL_TEXTURE_BINDING_2D
#define GL_TEXTURE_BINDING_2D           0x8069UL
#endif
#ifndef GL_COLOR_INDEX8_EXT
#define GL_COLOR_INDEX8_EXT             0x80e5UL
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE                0x812fUL
#endif
#ifndef GL_ACTIVE_TEXTURE_ARB
#define GL_ACTIVE_TEXTURE_ARB           0x84e0UL
#endif
#ifndef GL_CLIENT_ACTIVE_TEXTURE_ARB
#define GL_CLIENT_ACTIVE_TEXTURE_ARB    0x84e1UL
#endif
/* MiniGL's gl.h spells this one GL_TEXTURE_DOOR_ARRAY_POINTER (the same value) */
#ifndef GL_TEXTURE_COORD_ARRAY_POINTER
#define GL_TEXTURE_COORD_ARRAY_POINTER  0x8092UL
#endif

#endif /* CORE_GLEXTRA_H */
