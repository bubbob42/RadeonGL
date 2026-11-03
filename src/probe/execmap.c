/*
 * ExecMap -- where exec's functions are: every library vector of
 * exec.library with the address its JMP goes to, sorted, so a PC sample in
 * ROM (or in a patch in RAM) can be named. Read-only.
 *
 *   ExecMap
 *
 * A profile of GLQuake on the A4000D (2026-11-03) put 4% of the frame in
 * one 4 KB page of the Kickstart ROM; this tells which functions live
 * there. Prints "LVO -offset address" lines, address order.
 */

#include <exec/types.h>
#include <exec/execbase.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>

static const char ver[] = "$VER: ExecMap 0.1 (03.11.2026)";

#define MAX_LVO 256

static ULONG addr[MAX_LVO];
static ULONG lvo[MAX_LVO];

int main(void)
{
    UBYTE *base;
    ULONG n;
    ULONG i;
    ULONG j;
    ULONG t;
    UWORD op;

    if (((struct Process *) FindTask(NULL))->pr_CLI == 0)
        return RETURN_FAIL;     /* the Workbench: no console to report to */
    Printf("%s\n", (LONG) (ver + 6));
    base = (UBYTE *) SysBase;
    n = ((struct Library *) SysBase)->lib_NegSize / 6;
    if (n > MAX_LVO)
        n = MAX_LVO;
    for (i = 1; i <= n; i++)
    {
        /* a vector is JMP abs.l (0x4EF9) and the address */
        op = *(UWORD *) (base - 6 * i);
        lvo[i - 1] = 6 * i;
        addr[i - 1] = (op == 0x4EF9) ? *(ULONG *) (base - 6 * i + 2) : 0;
    }
    for (i = 0; i < n; i++)
        for (j = i + 1; j < n; j++)
            if (addr[j] < addr[i])
            {
                t = addr[i]; addr[i] = addr[j]; addr[j] = t;
                t = lvo[i]; lvo[i] = lvo[j]; lvo[j] = t;
            }
    for (i = 0; i < n; i++)
        if (addr[i] != 0)
            Printf("LVO -%ld 0x%08lx\n", (LONG) lvo[i], (LONG) addr[i]);
    return RETURN_OK;
}
