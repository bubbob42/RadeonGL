/*
 * vram.c -- the core's VRAM heap: first fit over one block of card memory.
 *
 * Allocations are kept as a list sorted by address; the gaps between them
 * are the free space. Small and slow to search, which is fine: textures
 * are allocated at level load, not per frame.
 */

#include "core.h"

struct CoreHeapBlock
{
    struct CoreHeapBlock *next;
    ULONG offset;
    ULONG size;
};

LONG Heap_Init(struct CoreHeap *h, ULONG gpu, ULONG *cpu, ULONG size)
{
    h->gpu = gpu;
    h->cpu = cpu;
    h->size = size;
    h->used = 0;
    h->peak = 0;
    h->fails = 0;
    h->blocks = NULL;
    return 0;
}

ULONG Heap_Alloc(struct Core *c, ULONG size, ULONG align)
{
    struct CoreHeap *h;
    struct CoreHeapBlock *prev;
    struct CoreHeapBlock *b;
    struct CoreHeapBlock *n;
    ULONG start;
    ULONG end;

    h = &c->heap;
    if (size == 0 || align == 0 || (align & (align - 1)) != 0)
        return 0xffffffffUL;
    size = (size + 3) & ~3UL;
    prev = NULL;
    b = h->blocks;
    start = 0;
    for (;;)
    {
        /* candidate: the gap from start up to the next block (or the end);
         * the alignment is of the GPU address */
        start = ((h->gpu + start + align - 1) & ~(align - 1)) - h->gpu;
        end = (b != NULL) ? b->offset : h->size;
        if (start <= end && end - start >= size)
            break;
        if (b == NULL)
        {
            h->fails++;
            return 0xffffffffUL;
        }
        start = b->offset + b->size;
        prev = b;
        b = b->next;
    }
    n = (struct CoreHeapBlock *) c->env.alloc(c->env.env, sizeof(*n));
    if (n == NULL)
        return 0xffffffffUL;
    n->offset = start;
    n->size = size;
    n->next = b;
    if (prev != NULL)
        prev->next = n;
    else
        h->blocks = n;
    h->used += size;
    if (h->used > h->peak)
        h->peak = h->used;
    return start;
}

void Heap_Free(struct Core *c, ULONG offset)
{
    struct CoreHeapBlock **pp;
    struct CoreHeapBlock *b;

    for (pp = &c->heap.blocks; *pp != NULL; pp = &(*pp)->next)
    {
        if ((*pp)->offset == offset)
        {
            b = *pp;
            *pp = b->next;
            c->heap.used -= b->size;
            c->env.free(c->env.env, b, sizeof(*b));
            return;
        }
    }
}

ULONG Heap_Top(const struct Core *c)
{
    struct CoreHeapBlock *b;
    ULONG top;

    top = 0;
    for (b = c->heap.blocks; b != NULL; b = b->next)
        if (b->offset + b->size > top)
            top = b->offset + b->size;
    return top;
}

void Heap_FreeAll(struct Core *c)
{
    struct CoreHeapBlock *b;
    struct CoreHeapBlock *n;

    for (b = c->heap.blocks; b != NULL; b = n)
    {
        n = b->next;
        c->env.free(c->env.env, b, sizeof(*b));
    }
    c->heap.blocks = NULL;
    c->heap.used = 0;
}
