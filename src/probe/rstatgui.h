/*
 * rstatgui.h -- RadeonStat's window (rstatgui.c) and the icon's tooltypes
 * (rstattt.c).
 */

#ifndef PROBE_RSTATGUI_H
#define PROBE_RSTATGUI_H

#include <exec/types.h>
#include <workbench/startup.h>

#include "rstat.h"

struct RStatGuiCfg
{
    ULONG interval10;       /* the update interval in 1/10 s: 5, 10, 20 or 50 */
    LONG left, top;         /* the window's place; -1: centred on the screen */
};

void RStatGui_Defaults(struct RStatGuiCfg *c);

/* INTERVAL, LEFT and TOP from the program's icon: the one Workbench
 * started (wbs), else PROGDIR:<program name>.info. Leaves c as it is where
 * the icon or a tooltype is missing. */
void RStatGui_ToolTypes(struct RStatGuiCfg *c, struct WBStartup *wbs);

/* Opens the window, samples until it is closed; a requester and
 * RETURN_FAIL when there is no Radeon under the Workbench. s zeroed. */
int RStatGui_Run(struct RStat *s, const struct RStatGuiCfg *c, CONST_STRPTR version);

#endif /* PROBE_RSTATGUI_H */
