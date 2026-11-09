/*
 * MemMap -- every memory region exec knows: name, address range, priority,
 * attributes, free bytes, the largest free chunk and how many free chunks
 * there are (fragmentation). Read-only: walks SysBase->MemList under
 * Forbid() and copies the figures out before printing.
 *
 *   MemMap
 *
 * Why (2026-11-09): on the A4000D the CyberStorm MK II's RAM (priority 40)
 * streams 2.5-3 times faster than the motherboard's fast RAM (priority 30,
 * not copyback); GLQuake's game memory and RadeonGL's buffers land where a
 * large enough free chunk is, so fragmentation after many runs may push
 * them into the slow region.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <proto/dos.h>

static const char ver[] = "$VER: MemMap 0.1 (09.11.2026)";

#define MAX_REGIONS 16

struct Region
{
    char name[40];
    ULONG lower;
    ULONG upper;
    LONG pri;
    UWORD attr;
    ULONG free;
    ULONG largest;
    ULONG chunks;
};

static struct Region reg[MAX_REGIONS];

int main(void)
{
    struct ExecBase *eb;
    struct MemHeader *mh;
    struct MemChunk *mc;
    ULONG n;
    ULONG i;
    ULONG k;
    const char *s;

    eb = *((struct ExecBase **) 4);
    n = 0;
    Forbid();
    for (mh = (struct MemHeader *) eb->MemList.lh_Head;
         mh->mh_Node.ln_Succ != NULL && n < MAX_REGIONS;
         mh = (struct MemHeader *) mh->mh_Node.ln_Succ)
    {
        s = mh->mh_Node.ln_Name;
        for (k = 0; s != NULL && s[k] != 0 && k < sizeof(reg[n].name) - 1; k++)
            reg[n].name[k] = s[k];
        reg[n].name[k] = 0;
        reg[n].lower = (ULONG) mh->mh_Lower;
        reg[n].upper = (ULONG) mh->mh_Upper;
        reg[n].pri = mh->mh_Node.ln_Pri;
        reg[n].attr = mh->mh_Attributes;
        reg[n].free = mh->mh_Free;
        reg[n].largest = 0;
        reg[n].chunks = 0;
        for (mc = mh->mh_First; mc != NULL; mc = mc->mc_Next)
        {
            reg[n].chunks++;
            if (mc->mc_Bytes > reg[n].largest)
                reg[n].largest = mc->mc_Bytes;
        }
        n++;
    }
    Permit();

    Printf("%s\n", (LONG) (ver + 6));
    Printf("%-24s %-21s %4s %-5s %11s %11s %7s\n",
           (ULONG) "region", (ULONG) "address", (ULONG) "pri", (ULONG) "type",
           (ULONG) "free", (ULONG) "largest", (ULONG) "chunks");
    for (i = 0; i < n; i++)
        Printf("%-24s $%08lx-$%08lx %4ld %-5s %11lu %11lu %7lu\n",
               (ULONG) reg[i].name, reg[i].lower, reg[i].upper, reg[i].pri,
               (ULONG) ((reg[i].attr & MEMF_CHIP) ? "chip" : (reg[i].attr & MEMF_FAST) ? "fast" : "other"),
               reg[i].free, reg[i].largest, reg[i].chunks);
    return 0;
}
