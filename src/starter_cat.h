#ifndef STARTER_CAT_H
#define STARTER_CAT_H

/*
 * starter_cat.h -- CatComp message ids + GetString() accessor.
 *
 * GENERATED. Host: tools/gen_catalog.py gen. Amiga (native, no host
 * tools): catcomp catalogs/starter.cd CFILE starter_cat.h.
 * The two are interface-compatible -- identical S_NAME ids (the .cd
 * carries no explicit ids, so both number 0..N in file order) and the
 * same struct LocaleInfo / GetString() contract. Do not hand-edit.
 *
 * Include with CATCOMP_CODE defined in exactly ONE .c (the locale
 * module) to instantiate GetString() and the default table; include
 * with CATCOMP_NUMBERS (or neither) elsewhere for ids and prototype.
 */

#include <exec/types.h>

struct LocaleInfo
{
    APTR li_LocaleBase;
    APTR li_Catalog;
};

STRPTR GetString(struct LocaleInfo *li, LONG stringNum);

/* Numbers are needed by the redirect header AND by the code section. */
#if defined(CATCOMP_NUMBERS) || defined(CATCOMP_CODE)
#define S_SIG_CATALOG 0
#define S_WIN_MAIN 1
#define S_SEC_OPTIONS 2
#define S_GAD_OK 3
#define S_GAD_CANCEL 4
#define S_GAD_SAVE 5
#define S_LBL_NAME 6
#define S_MENU_PROJECT 7
#define S_MENU_ABOUT 8
#define S_MENU_QUIT 9
#define S_MSG_READY 10
#define S_ERR_NO_MEMORY 11
#define S_ERR_OPEN_FAILED 12
#endif /* CATCOMP_NUMBERS || CATCOMP_CODE */

#ifdef CATCOMP_CODE

#include <libraries/locale.h>
#include <proto/locale.h>

struct S_CatCompArrayType
{
    LONG   cca_ID;
    STRPTR cca_Str;
};

static const struct S_CatCompArrayType S_CatCompArray[] =
{
    { S_SIG_CATALOG, (STRPTR)"SCAT-builtin" },
    { S_WIN_MAIN, (STRPTR)"MyApp" },
    { S_SEC_OPTIONS, (STRPTR)"Options" },
    { S_GAD_OK, (STRPTR)"OK" },
    { S_GAD_CANCEL, (STRPTR)"Cancel" },
    { S_GAD_SAVE, (STRPTR)"Save" },
    { S_LBL_NAME, (STRPTR)"Name" },
    { S_MENU_PROJECT, (STRPTR)"Project" },
    { S_MENU_ABOUT, (STRPTR)"About..." },
    { S_MENU_QUIT, (STRPTR)"Quit" },
    { S_MSG_READY, (STRPTR)"Ready." },
    { S_ERR_NO_MEMORY, (STRPTR)"Out of memory." },
    { S_ERR_OPEN_FAILED, (STRPTR)"Cannot open %s." },
};

STRPTR GetString(struct LocaleInfo *li, LONG stringNum)
{
    STRPTR builtIn;
    LONG   i;
    LONG   count;

    builtIn = (STRPTR)"";
    count = (LONG)(sizeof(S_CatCompArray) / sizeof(S_CatCompArray[0]));
    for (i = 0; i < count; i++)
    {
        if (S_CatCompArray[i].cca_ID == stringNum)
        {
            builtIn = S_CatCompArray[i].cca_Str;
            break;
        }
    }

    /* Consult the catalog only when locale.library is open AND a catalog is
     * actually loaded; otherwise the built-in English default is used. The
     * NULL check on the result additionally guards against a stub or broken
     * locale.library (e.g. an incomplete emulation) handing back NULL. */
    if (li != NULL && li->li_LocaleBase != NULL && li->li_Catalog != NULL)
    {
        STRPTR s;

        s = GetCatalogStr((struct Catalog *)li->li_Catalog, stringNum,
                          builtIn);
        if (s != NULL)
        {
            return s;
        }
    }
    return builtIn;
}

#endif /* CATCOMP_CODE */

#endif /* STARTER_CAT_H */
