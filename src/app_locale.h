#ifndef STARTER_APP_LOCALE_H
#define STARTER_APP_LOCALE_H

/*
 * app_locale.h -- locale.library + message-catalog lifecycle.
 *
 * Scaffolded by tools/gen_catalog.py; hand-maintained from here on.
 *
 * Pulls in the generated id/accessor header (starter_cat.h) so every
 * translation unit that includes this sees the S_* string ids,
 * struct LocaleInfo and the GetString() prototype. The one shared
 * LocaleInfo instance (S_LI) and GetString()'s built-in default
 * table live in app_locale.c -- the single translation unit
 * compiled with CATCOMP_CODE.
 *
 * App_OpenCatalog() / App_CloseCatalog() are each called once, at
 * startup and shutdown. Opening is deliberately NON-FATAL: with no
 * locale.library, or no matching catalog on disk, GetString()
 * returns the built-in English defaults, so the program always runs
 * and the text simply stays English.
 */

#include <exec/types.h>

/* Emit the S_* id #defines from the generated header. app_locale.c
 * adds CATCOMP_CODE on top to also instantiate GetString() and the
 * built-in default table. */
#define CATCOMP_NUMBERS
#include "starter_cat.h"

/* Shared handle passed to every GetString(&S_LI, S_id) lookup
 * (defined in app_locale.c). li_LocaleBase == NULL means
 * locale.library is not open, so GetString() must not call into it
 * and returns the built-in default. */
extern struct LocaleInfo S_LI;

BOOL App_OpenCatalog(void);
void App_CloseCatalog(void);
CONST_STRPTR App_LanguageName(void);

#endif /* STARTER_APP_LOCALE_H */
