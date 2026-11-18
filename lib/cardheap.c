/*
 * cardheap.c -- the words when no texture heap fits on the board
 * (2026-11-18, Herald's blank cards under VRAM pressure). Linked late:
 * card.c calls it once, when a context is refused for lack of a heap.
 */

#include <exec/types.h>
#include <utility/tagitem.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <libraries/Picasso96.h>
#include <proto/Picasso96.h>

#include "card.h"

/* s into why[] from *pos on, never past len - 1; why[] stays terminated */
static void Put(char *why, ULONG len, ULONG *pos, const char *s)
{
    while (*s != 0 && *pos + 1 < len)
        why[(*pos)++] = *s++;
    why[*pos] = 0;
}

/* n KB (or MB from 1024 KB on, whole) as text */
static void PutKB(char *why, ULONG len, ULONG *pos, ULONG bytes)
{
    char d[12];
    ULONG n;
    int i;

    n = bytes >> 10;
    if (n >= 1024)
        n >>= 10;
    i = 11;
    d[i] = 0;
    do
    {
        d[--i] = (char) ('0' + n % 10);
        n /= 10;
    } while (n != 0 && i > 0);
    Put(why, len, pos, &d[i]);
    Put(why, len, pos, ((bytes >> 10) >= 1024) ? " MB" : " KB");
}

/* what + " (Picasso96: 14 MB free, the largest block 1 MB)".
 * P96BD_FreeMemory and _LargestFreeMemory change
 * constantly (P96API autodoc): a picture of the moment, not a promise. */
void Card_HeapWhy(struct Card *k, char *why, ULONG whylen, const char *what)
{
    struct TagItem t[3];
    ULONG freemem;
    ULONG largest;
    ULONG pos;

    if (why == NULL || whylen == 0)
        return;
    freemem = 0;
    largest = 0;
    t[0].ti_Tag = P96BD_FreeMemory;         t[0].ti_Data = (ULONG) &freemem;
    t[1].ti_Tag = P96BD_LargestFreeMemory;  t[1].ti_Data = (ULONG) &largest;
    t[2].ti_Tag = TAG_DONE;                 t[2].ti_Data = 0;
    p96GetBoardDataTagList(k->board, t);
    pos = 0;
    why[0] = 0;
    Put(why, whylen, &pos, what);
    if (freemem != 0)
    {
        Put(why, whylen, &pos, " (Picasso96: ");
        PutKB(why, whylen, &pos, freemem);
        Put(why, whylen, &pos, " free, the largest block ");
        PutKB(why, whylen, &pos, largest);
        Put(why, whylen, &pos, ")");
    }
}
