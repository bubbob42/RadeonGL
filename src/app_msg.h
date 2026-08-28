#ifndef STARTER_APP_MSG_H
#define STARTER_APP_MSG_H

/*
 * app_msg.h -- GENERATED localization redirect
 * (tools/gen_catalog.py gen). Do not hand-edit; edit the English
 * text in app_strings.h and rerun.
 *
 * Each user-facing string name is redefined as a runtime catalog
 * lookup: GetString(&S_LI, S_<name>) returns the translation for
 * the user's locale, or the built-in English default (embedded in
 * starter_cat.h) when there is no catalog. Call sites are unchanged;
 * the central header includes this in place of app_strings.h (which
 * remains the generator's seed).
 *
 * Because each name now expands to a function call it can no longer sit
 * in a static/global initializer; populate such tables (menu strips,
 * chooser label arrays) at runtime instead.
 */

#include "app_locale.h"    /* S_LI, S_* ids, GetString() */

#define SIG_CATALOG     GetString(&S_LI, S_SIG_CATALOG)
#define WIN_MAIN        GetString(&S_LI, S_WIN_MAIN)
#define SEC_OPTIONS     GetString(&S_LI, S_SEC_OPTIONS)
#define GAD_OK          GetString(&S_LI, S_GAD_OK)
#define GAD_CANCEL      GetString(&S_LI, S_GAD_CANCEL)
#define GAD_SAVE        GetString(&S_LI, S_GAD_SAVE)
#define LBL_NAME        GetString(&S_LI, S_LBL_NAME)
#define MENU_PROJECT    GetString(&S_LI, S_MENU_PROJECT)
#define MENU_ABOUT      GetString(&S_LI, S_MENU_ABOUT)
#define MENU_QUIT       GetString(&S_LI, S_MENU_QUIT)
#define MSG_READY       GetString(&S_LI, S_MSG_READY)
#define ERR_NO_MEMORY   GetString(&S_LI, S_ERR_NO_MEMORY)
#define ERR_OPEN_FAILED GetString(&S_LI, S_ERR_OPEN_FAILED)

#endif /* STARTER_APP_MSG_H */
