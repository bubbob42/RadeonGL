/*
 * help.c -- asynchronous AmigaGuide online help (see help.h). [OPTIONAL]
 *
 * Not part of the default CLI build -- ready-made scaffolding for a GUI app.
 *
 * The async pattern here is the one documented in the amigaguide.library
 * autodocs: open the client with OpenAmigaGuideAsync(), fold AmigaGuideSignal()
 * into the program's Wait() mask, reply every AmigaGuideMsg, and
 * CloseAmigaGuide() on exit. Two deliberate choices: the client is opened lazily
 * on the first Help request (so it never spawns for users who never ask), and
 * its windows are placed on the application's own screen instead of always the
 * default public screen.
 */

#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <dos/dosextens.h>     /* struct Process, pr_WindowPtr */
#include <intuition/screens.h> /* struct Screen */
#include <libraries/amigaguide.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/amigaguide.h>

#include <string.h>

#include "help.h"
#include "debug.h"

/* The application name, used to build the guide-file name (<name>.guide) and as
 * the AmigaGuide base name. Set it to your project (e.g. pass
 * -DHELP_APPNAME='"MyApp"' from the Makefile, or edit here). Keep it in step
 * with the Makefile's PROJECT_NAME so the `guides` target's <name>.guide is
 * found. */
#ifndef HELP_APPNAME
#define HELP_APPNAME "App"
#endif

/* amigaguide.library is optional and owned entirely by this module: opened on
 * the first successful Help_Show(), closed by Help_Cleanup(). The
 * proto/amigaguide inline (GCC) and pragma (SAS/C) calls resolve against this
 * base symbol. */
struct Library *AmigaGuideBase = NULL;

/* Candidate locations for the guide, tried in order: next to the executable
 * first (the friendly default for a self-contained app), then the system
 * HELP: assign (where an installer would drop it). */
static const char * const Help_Paths[] =
{
    "PROGDIR:" HELP_APPNAME ".guide",
    "HELP:" HELP_APPNAME ".guide"
};

#define HELP_PATH_COUNT ((ULONG)(sizeof(Help_Paths) / sizeof(Help_Paths[0])))


/*
 * Locate a readable guide file and copy its path into help->hc_Path. DOS
 * requesters are suppressed for the probe (pr_WindowPtr = -1) so a missing
 * HELP: assign or an absent volume never pops a "Please insert..." requester.
 * Returns TRUE when a file was found.
 */
static BOOL Help_FindFile(struct HelpContext *help)
{
    struct Process *proc;
    APTR   oldWindow;
    BPTR   lock;
    ULONG  i;
    BOOL   found;

    proc = (struct Process *)FindTask(NULL);
    oldWindow = proc->pr_WindowPtr;
    proc->pr_WindowPtr = (APTR)-1L;   /* suppress DOS requesters for the probe */

    found = FALSE;
    for (i = 0; i < HELP_PATH_COUNT; i++)
    {
        lock = Lock((CONST_STRPTR)Help_Paths[i], ACCESS_READ);
        if (lock != (BPTR)0)
        {
            UnLock(lock);
            strcpy(help->hc_Path, Help_Paths[i]);
            found = TRUE;
            break;
        }
    }

    proc->pr_WindowPtr = oldWindow;
    return found;
}


/*
 * Open the AmigaGuide client if it is not already open (idempotent).
 * amigaguide.library is opened on demand at V39. The viewing windows are placed
 * on `screen` (NULL => default public screen). Returns TRUE once
 * help->hc_Handle is valid.
 */
static BOOL Help_Ensure(struct HelpContext *help, struct Screen *screen)
{
    if (help->hc_Handle != NULL)
    {
        return TRUE;
    }

    if (AmigaGuideBase == NULL)
    {
        AmigaGuideBase = OpenLibrary((CONST_STRPTR)"amigaguide.library", 39);
    }
    if (AmigaGuideBase == NULL)
    {
        DBG(("Help: amigaguide.library unavailable\n"));
        return FALSE;
    }

    if (!Help_FindFile(help))
    {
        DBG(("Help: no guide file found\n"));
        return FALSE;
    }

    /* nag_Node/nag_Line are ignored by the async open (autodoc); the starting
     * node is selected afterwards with a LINK command. Everything except the
     * name, the screen and the base name stays zero. */
    memset(&help->hc_Nag, 0, sizeof(help->hc_Nag));
    help->hc_Nag.nag_Name = (STRPTR)help->hc_Path;
    help->hc_Nag.nag_Screen = screen;
    help->hc_Nag.nag_BaseName = (STRPTR)HELP_APPNAME;
    help->hc_Nag.nag_Client = NULL;    /* private, must be NULL */

    help->hc_Handle = OpenAmigaGuideAsync(&help->hc_Nag, TAG_END);
    if (help->hc_Handle == NULL)
    {
        DBG(("Help: OpenAmigaGuideAsync failed (err %ld)\n", (LONG)IoErr()));
        return FALSE;
    }

    DBG(("Help: opened '%s'\n", (LONG)help->hc_Path));
    return TRUE;
}


/*
 * Send "LINK <node>" to the guide client, which displays the node and opens or
 * reactivates the viewing window (AGA_Activate defaults on). Node names are
 * short; the copy is bounded to the fixed command buffer.
 */
static void Help_SendLink(struct HelpContext *help, CONST_STRPTR node)
{
    char cmd[64];

    strcpy(cmd, "LINK ");
    strncat(cmd, (const char *)node, sizeof(cmd) - 6);
    cmd[sizeof(cmd) - 1] = '\0';
    SendAmigaGuideCmd(help->hc_Handle, cmd, TAG_END);
}


BOOL Help_Show(struct HelpContext *help, struct Screen *screen,
               CONST_STRPTR node)
{
    if (help == NULL)
    {
        return FALSE;
    }
    if (!Help_Ensure(help, screen))
    {
        return FALSE;
    }

    if (node == NULL || node[0] == '\0')
    {
        node = (CONST_STRPTR)"Main";
    }

    /* A freshly spawned async client silently drops any command until it has
     * signalled that it is up (see Help_HandleMessages) -- which is why the
     * first Help request used to do nothing. Until the client is ready, remember
     * the wanted node and let Help_HandleMessages fire the LINK once the ready
     * message arrives; afterwards, LINK immediately. */
    if (help->hc_Ready)
    {
        Help_SendLink(help, node);
    }
    else
    {
        strncpy(help->hc_Pending, (const char *)node,
                sizeof(help->hc_Pending) - 1);
        help->hc_Pending[sizeof(help->hc_Pending) - 1] = '\0';
    }
    return TRUE;
}


ULONG Help_Signal(struct HelpContext *help)
{
    if (help == NULL || help->hc_Handle == NULL)
    {
        return 0;
    }
    return AmigaGuideSignal(help->hc_Handle);
}


void Help_HandleMessages(struct HelpContext *help)
{
    struct AmigaGuideMsg *agm;

    if (help == NULL || help->hc_Handle == NULL)
    {
        return;
    }

    /* Reply every queued SIPC message. The client is kept alive across window
     * closes for the whole run, so we act on only one thing: the arrival of an
     * ActiveToolID/ToolStatusID marks the point where the client has read the
     * NewAmigaGuide and will honour commands (autodoc). Every message must still
     * be replied so the guide process can proceed. */
    while ((agm = GetAmigaGuideMsg(help->hc_Handle)) != NULL)
    {
        if (agm->agm_Type == ActiveToolID || agm->agm_Type == ToolStatusID)
        {
            help->hc_Ready = TRUE;
        }
        ReplyAmigaGuideMsg(agm);
    }

    /* The client just became ready and a Help request is waiting: fire the
     * deferred LINK now that it will be honoured. */
    if (help->hc_Ready && help->hc_Pending[0] != '\0')
    {
        Help_SendLink(help, (CONST_STRPTR)help->hc_Pending);
        help->hc_Pending[0] = '\0';
    }
}


void Help_Close(struct HelpContext *help)
{
    if (help == NULL || help->hc_Handle == NULL)
    {
        return;
    }

    /* Closes the client and any windows it opened (which may be on our own
     * screen), so this must run before that screen is closed. */
    CloseAmigaGuide(help->hc_Handle);
    help->hc_Handle = NULL;
    /* A later re-open spawns a fresh client that must handshake again. */
    help->hc_Ready = FALSE;
    help->hc_Pending[0] = '\0';
}


void Help_Cleanup(struct HelpContext *help)
{
    Help_Close(help);
    if (AmigaGuideBase != NULL)
    {
        CloseLibrary(AmigaGuideBase);
        AmigaGuideBase = NULL;
    }
}
