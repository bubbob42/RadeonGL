/*
 * minigl_client_compat.h -- RadeonGL addition, not part of the upstream
 * MiniGL_Library_Quake1 source.
 *
 * The upstream source is the STATIC build: MiniGLV3D linked into the
 * executable. A few files there reach into MiniGLV3D's internals:
 *
 *   - miniglext.c / miniglext_gcc.h / gl_mirror.c call the internal
 *     context-first functions GLTexCoord2f(ctx,...), GLVertex3fv(ctx,...),
 *     GLMultiTexCoord2fARB(ctx,...);
 *   - gl_rsurf.c, gl_warp.c, gl_mirror.c, gl_wiredraw.c pass the library's
 *     global mini_CurrentContext to those helpers.
 *
 * A shared-library client cannot link either. Through the ABI-5 dispatch
 * table the public calls already fetch the current context themselves, so
 * the internal names map one-to-one onto them and the context argument is
 * dropped. Forced in after <proto/minigl.h> by glquake/Makefile.radeongl.
 */

#ifndef MINIGL_CLIENT_COMPAT_H
#define MINIGL_CLIENT_COMPAT_H

#ifdef MINIGL_SHARED_CLIENT

#include <proto/minigl.h>

/* Never dereferenced: the helpers only hand it on to the calls below. */
#define mini_CurrentContext ((GLcontext)0)

#define GLTexCoord2f(ctx, s, t)                glTexCoord2f((s), (t))
#define GLVertex3fv(ctx, v)                    glVertex3fv(v)
#define GLMultiTexCoord2fARB(ctx, unit, s, t)  glMultiTexCoord2fARB((unit), (s), (t))

#endif /* MINIGL_SHARED_CLIENT */

/* libnix has no strlcpy/strlcat (upstream built with clib2, which has them);
 * BSD semantics, in minigl_client_compat.c. */
#include <stddef.h>
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);

#endif /* MINIGL_CLIENT_COMPAT_H */
