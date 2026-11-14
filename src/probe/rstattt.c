/*
 * rstattt.c -- RadeonStat's settings from its icon (rstatgui.h).
 *
 * The icon is the settings; the program writes nothing:
 *   INTERVAL=1     seconds between updates: 0.5, 1, 2 or 5
 *   LEFT=-1        the window's place on the Workbench; -1: centred
 *   TOP=-1
 *
 * In a module of its own: <proto/icon.h> pulls in the datatypes headers,
 * whose GID_ macros collide with gadget IDs.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <workbench/workbench.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>

#include "rstatgui.h"

struct Library *IconBase = NULL;

/* "0.5", "1", "2.5" ... in tenths; -1 when it is not a number */
static LONG Tenths(CONST_STRPTR s)
{
    LONG v, frac;
    BOOL digits;

    v = 0;
    digits = FALSE;
    while (*s == ' ')
        s++;
    while (*s >= '0' && *s <= '9')
    {
        v = v * 10 + (*s++ - '0');
        digits = TRUE;
    }
    v *= 10;
    if (*s == '.' || *s == ',')
    {
        s++;
        if (*s >= '0' && *s <= '9')
        {
            frac = *s - '0';
            v += frac;
            digits = TRUE;
        }
    }
    return digits ? v : -1;
}

static BOOL Number(CONST_STRPTR s, LONG *out)
{
    LONG v;
    BOOL neg;

    while (*s == ' ')
        s++;
    neg = (*s == '-');
    if (neg)
        s++;
    if (*s < '0' || *s > '9')
        return FALSE;
    v = 0;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    *out = neg ? -v : v;
    return TRUE;
}

void RStatGui_Defaults(struct RStatGuiCfg *c)
{
    c->interval10 = 10;
    c->left = -1;
    c->top = -1;
}

void RStatGui_ToolTypes(struct RStatGuiCfg *c, struct WBStartup *wbs)
{
    struct DiskObject *dob;
    BPTR old, dir;
    CONST_STRPTR name;
    STRPTR tt;
    char prog[108];
    LONG v;

    IconBase = OpenLibrary((CONST_STRPTR) "icon.library", 39);
    if (IconBase == NULL)
        return;
    dob = NULL;
    if (wbs != NULL && wbs->sm_NumArgs > 0)
    {
        /* the tool's own icon (argument 0) */
        dir = wbs->sm_ArgList[0].wa_Lock;
        name = (CONST_STRPTR) wbs->sm_ArgList[0].wa_Name;
    }
    else
    {
        dir = GetProgramDir();
        name = NULL;
        if (GetProgramName((STRPTR) prog, sizeof(prog)))
            name = FilePart((STRPTR) prog);
    }
    if (dir != 0 && name != NULL && *name)
    {
        old = CurrentDir(dir);
        dob = GetDiskObject((STRPTR) name);
        CurrentDir(old);
    }
    if (dob != NULL)
    {
        tt = (STRPTR) FindToolType((CONST_STRPTR *) dob->do_ToolTypes, (CONST_STRPTR) "INTERVAL");
        if (tt != NULL)
        {
            v = Tenths(tt);
            if (v > 0)
                c->interval10 = v <= 7 ? 5 : v <= 15 ? 10 : v <= 35 ? 20 : 50;
        }
        tt = (STRPTR) FindToolType((CONST_STRPTR *) dob->do_ToolTypes, (CONST_STRPTR) "LEFT");
        if (tt != NULL && Number(tt, &v))
            c->left = v;
        tt = (STRPTR) FindToolType((CONST_STRPTR *) dob->do_ToolTypes, (CONST_STRPTR) "TOP");
        if (tt != NULL && Number(tt, &v))
            c->top = v;
        FreeDiskObject(dob);
    }
    CloseLibrary(IconBase);
    IconBase = NULL;
}
