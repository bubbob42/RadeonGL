/*
 * minigl_client_compat.c -- RadeonGL addition, not part of the upstream
 * MiniGL_Library_Quake1 source.
 *
 * gl_vidamiga.c references two MiniGLV3D debug symbols that exist only
 * when MiniGLV3D is linked into the executable. A shared-library client
 * gets inert stand-ins: the last render wait always reads as "no error",
 * and the log goes nowhere (upstream's showcase build links
 * mglv3d_log_noop.c for the same effect).
 *
 * Also strlcpy/strlcat, which upstream got from clib2 and libnix lacks.
 */

#include <string.h>
#include "minigl_client_compat.h"

int g_mglv3d_last_render_wait_result = 0;

void mglv3d_log(const char *msg)
{
    (void)msg;
}

/* Copies at most size-1 characters, always terminates when size > 0;
 * returns strlen(src) so the caller can detect truncation. */
size_t strlcpy(char *dst, const char *src, size_t size)
{
    size_t len;

    len = strlen(src);
    if (size > 0)
    {
        size_t n;

        n = (len >= size) ? size - 1 : len;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
}

/* Appends while the result fits in size (including the terminator);
 * returns the length it tried to create. */
size_t strlcat(char *dst, const char *src, size_t size)
{
    size_t dlen;

    dlen = 0;
    while (dlen < size && dst[dlen] != '\0')
        dlen++;
    if (dlen == size)
        return size + strlen(src);
    return dlen + strlcpy(dst + dlen, src, size - dlen);
}
