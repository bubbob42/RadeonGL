#ifndef HELP_H
#define HELP_H

/*
 * help.h -- asynchronous AmigaGuide online help.  [OPTIONAL SCAFFOLDING]
 *
 * This module is NOT compiled by the default (CLI demo) build. It is ready-made
 * scaffolding for a GUI project: add help.c to your source lists, set
 * HELP_APPNAME (see help.c) to your project name, ship a <name>.guide (the
 * Makefile's `guides` target stages one from guide/<lang>/), and wire the four
 * calls below into your event loop.
 *
 * Design (all from the public amigaguide.library autodocs): amigaguide.library
 * is opened lazily on the first help request and the client is then kept open
 * for the program's whole run; its viewing window comes and goes as the user
 * browses or closes it, and a repeat request re-links to the wanted node (which
 * reactivates the window). The async open ignores nag_Node/nag_Line, so the
 * node is selected afterwards with a LINK command, and the NewAmigaGuide
 * structure must stay valid for the client's life -- hence it lives inside this
 * struct, not on a caller's stack.
 *
 * A freshly opened async client cannot accept commands until it has spawned and
 * signalled that it is up (an ActiveToolID/ToolStatusID SIPC message). A LINK
 * sent before then is silently lost. The first LINK is therefore deferred:
 * Help_Show remembers the wanted node and Help_HandleMessages fires the LINK
 * once the ready message arrives.
 *
 * The whole feature is optional: when amigaguide.library or the guide file is
 * missing, Help_Show() returns FALSE and the caller reports that no help is
 * available. The module depends only on exec/dos/intuition + amigaguide.library,
 * stays C89, and builds under both compiler profiles.
 */

#include <exec/types.h>
#include <libraries/amigaguide.h>   /* struct NewAmigaGuide; also pulls in
                                     * intuition/screens.h for struct Screen */

struct HelpContext
{
    struct NewAmigaGuide hc_Nag;       /* referenced by the guide process until
                                        * it acknowledges; must outlive the
                                        * OpenAmigaGuideAsync call -- keep here */
    AMIGAGUIDECONTEXT    hc_Handle;    /* NULL until the client is opened */
    BOOL                 hc_Ready;     /* TRUE once the async client has signalled
                                        * it is up (ActiveToolID/ToolStatusID); a
                                        * command sent before then is lost, so the
                                        * first LINK waits for this */
    char                 hc_Pending[64];/* node whose LINK is deferred until
                                        * hc_Ready; empty string when none */
    char                 hc_Path[256]; /* resolved guide-file path (kept live
                                        * because nag_Name points into it) */
};

/* Bring up the given node (NULL or "" => "Main"), opening the client on first
 * use. The viewing windows go on `screen` (NULL => default public screen).
 * Returns TRUE when help was shown, FALSE when it is unavailable (no
 * amigaguide.library, or no guide file found). */
BOOL  Help_Show(struct HelpContext *help, struct Screen *screen,
                CONST_STRPTR node);

/* Signal bit to OR into the Wait() mask; 0 while the client is not open. */
ULONG Help_Signal(struct HelpContext *help);

/* Reply every pending AmigaGuide message. Call when the help signal fires
 * (cheap no-op when the client is not open). */
void  Help_HandleMessages(struct HelpContext *help);

/* Close the client and any windows it opened, leaving amigaguide.library open
 * for a later re-open. Safe if never opened. Must run before the screen the
 * client opened its windows on is closed (e.g. on a screen swap). */
void  Help_Close(struct HelpContext *help);

/* Close the client and drop amigaguide.library. Call once at shutdown. */
void  Help_Cleanup(struct HelpContext *help);

#endif /* HELP_H */
