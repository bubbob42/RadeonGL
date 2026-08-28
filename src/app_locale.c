/*
 * app_locale.c -- open/close locale.library and the starter message
 * catalog, and -- as the single CATCOMP_CODE translation unit --
 * instantiate GetString() together with the built-in English
 * default table from starter_cat.h. No other module defines
 * CATCOMP_CODE, so GetString() and the default array have exactly
 * one definition program-wide.
 *
 * Scaffolded by tools/gen_catalog.py; hand-maintained from here on.
 * See app_locale.h and docs/localization-catcomp.md.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <utility/tagitem.h>
#include <libraries/locale.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/locale.h>

/* CATCOMP_CODE must be defined in EXACTLY ONE translation unit:
 * it instantiates GetString() and the built-in default strings. */
#define CATCOMP_CODE
#include "app_locale.h"
#include "app_catver.h"   /* S_CATALOG_VERSION, S_CATALOG_SIG */

/* locale.library base for the proto/locale calls and GetString()'s
 * catalog fetch; opened and closed by this module. Typed
 * struct LocaleBase * to match the extern in <proto/locale.h>.
 * Initialised to NULL deliberately: a tentative definition
 * without it makes libnix link an auto-open stub instead. */
struct LocaleBase *LocaleBase = NULL;

/* The one LocaleInfo every GetString() call is handed. */
struct LocaleInfo S_LI = { NULL, NULL };

static struct Locale *S_Locale = NULL;

#define CATALOG_NAME        "starter.catalog"

/* Own compare rather than strcmp: this module may be linked into a
 * library or a device segment, where pulling an sc.lib module in can
 * rearrange the link and move a structure that must sit at segment
 * offset 0. No libc, no reordering. */
static BOOL SameStr(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b)
    {
        a++;
        b++;
    }
    return (BOOL)(*a == *b);
}


/*
 * Open locale.library and the catalog. NON-FATAL by design: a missing
 * library or catalog is not an error and must not stop the program or
 * pop a requester. Returns TRUE when a catalog was opened, FALSE when
 * the built-in English strings will be used -- callers may ignore it.
 */
BOOL App_OpenCatalog(void)
{
    struct TagItem tags[3];

    /* V38+. Absent on 2.04 and older: stay English, keep running. */
    LocaleBase = (struct LocaleBase *)
        OpenLibrary((STRPTR)"locale.library", 38);
    if (LocaleBase == NULL)
    {
        return FALSE;
    }
    S_LI.li_LocaleBase = (APTR)LocaleBase;

    /* NULL = the user's current preferred locale. */
    S_Locale = OpenLocale(NULL);

    tags[0].ti_Tag  = OC_BuiltInLanguage;
    tags[0].ti_Data = (ULONG)"english";
    tags[1].ti_Tag  = OC_Version;
    /* EXACT match: the ids are implicit (0..N in .cd order), so a
     * catalog built for a different layout would return shifted
     * strings -- wrong text, and wrong argument types in format
     * calls. Rejecting it means falling back to English. */
    tags[1].ti_Data = S_CATALOG_VERSION;
    tags[2].ti_Tag  = TAG_DONE;

    /* A NULL catalog is normal (no translation installed, or the
     * catalog on disk is stale). */
    S_LI.li_Catalog = (APTR)OpenCatalogA(S_Locale,
                                       (STRPTR)CATALOG_NAME, tags);

    /* Belt to OC_Version's braces: that guard needs CatComp and
     * locale.library to cooperate on the FVER version, and a stale
     * catalog has been seen to storm straight through it in the
     * field. Catalog id 0 is a signature string carrying the
     * id-layout fingerprint (S_SIG_CATALOG, value S_CATALOG_SIG --
     * gen_catalog.py stamps it into every .ct). A catalog built for
     * ANY other layout necessarily has some other string at id 0, so
     * the comparison cannot be fooled: mismatch -> drop the catalog
     * and use the built-in English strings. */
    if (S_LI.li_Catalog != NULL)
    {
        CONST_STRPTR sig;

        sig = (CONST_STRPTR)GetCatalogStr(
            (struct Catalog *)S_LI.li_Catalog, S_SIG_CATALOG,
            (STRPTR)"SCAT-builtin");
        if (!SameStr((const char *)sig, S_CATALOG_SIG))
        {
            CloseCatalog((struct Catalog *)S_LI.li_Catalog);
            S_LI.li_Catalog = NULL;
        }
    }

    return (BOOL)(S_LI.li_Catalog != NULL);
}


/*
 * The user's first preferred language, as locale.library reports it.
 * That is the same name the catalog search uses
 * (LOCALE:Catalogs/<language>) and the name the guides are installed
 * under (HELP:<language>). loc_PrefLanguages[0] is the user's first
 * choice; loc_LanguageName is the fallback for a locale with an empty
 * preference list. Returns NULL when no locale is open.
 */
CONST_STRPTR App_LanguageName(void)
{
    if (S_Locale == NULL)
    {
        return NULL;
    }
    if (S_Locale->loc_PrefLanguages[0] != NULL &&
        S_Locale->loc_PrefLanguages[0][0] != '\0')
    {
        return (CONST_STRPTR)S_Locale->loc_PrefLanguages[0];
    }
    return (CONST_STRPTR)S_Locale->loc_LanguageName;
}


/*
 * Release everything App_OpenCatalog() took, in reverse order. Safe
 * to call after a partial or failed open -- every step is guarded.
 */
void App_CloseCatalog(void)
{
    if (S_LI.li_Catalog != NULL)
    {
        CloseCatalog((struct Catalog *)S_LI.li_Catalog);
        S_LI.li_Catalog = NULL;
    }
    if (S_Locale != NULL)
    {
        CloseLocale(S_Locale);
        S_Locale = NULL;
    }
    if (LocaleBase != NULL)
    {
        CloseLibrary((struct Library *)LocaleBase);
        LocaleBase = NULL;
        S_LI.li_LocaleBase = NULL;
    }
}
