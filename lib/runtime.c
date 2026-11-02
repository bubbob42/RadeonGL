/*
 * runtime.c -- the two C library functions GCC calls on its own (struct
 * copies and clears) in a library linked -nostdlib. Dword-wise when both
 * ends are aligned; Haeuser's shim copied byte by byte (concept 7.1).
 */

#include <exec/types.h>

void *memcpy(void *dst, const void *src, unsigned long n)
{
    UBYTE *d;
    const UBYTE *s;

    d = (UBYTE *) dst;
    s = (const UBYTE *) src;
    if ((((ULONG) d | (ULONG) s) & 3) == 0)
    {
        while (n >= 4)
        {
            *(ULONG *) d = *(const ULONG *) s;
            d += 4;
            s += 4;
            n -= 4;
        }
    }
    while (n-- > 0)
        *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, unsigned long n)
{
    UBYTE *d;
    ULONG v;

    d = (UBYTE *) dst;
    v = (UBYTE) c;
    v |= v << 8;
    v |= v << 16;
    if (((ULONG) d & 3) == 0)
    {
        while (n >= 4)
        {
            *(ULONG *) d = v;
            d += 4;
            n -= 4;
        }
    }
    while (n-- > 0)
        *d++ = (UBYTE) c;
    return dst;
}
