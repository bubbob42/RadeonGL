#ifndef APP_STRINGS_H
#define APP_STRINGS_H

/*
 * app_strings.h -- every user-facing string in the program, in one place.
 *
 * THIS IS THE HUMAN-EDITED SEED of the localization pipeline. Add a string
 * here, run `tools/gen_catalog.py gen`, and the generator writes the CatComp
 * descriptor (catalogs/<Project>.cd), the id/accessor header
 * (src/<Project>_cat.h), the call-site redirects (src/app_msg.h) and the
 * catalog version + signature (src/app_catver.h). See
 * docs/localization-catcomp.md.
 *
 * Conventions:
 *   - One #define per string, value = the built-in English default.
 *   - Category-prefixed names. The prefix picks the length budget a
 *     translation gets, so use the one that matches the ROLE:
 *
 *       GAD_   button / gadget label        MENU_  menu item
 *       LBL_   field label                  REQ_   requester body text
 *       SEC_   section (group) title        ERR_   error message
 *       WIN_   window title                 CHO_   chooser entry
 *       MSG_   status / informational       COL_   column heading
 *
 *   - No string literal belongs anywhere else in the program. A literal in a
 *     .c file is invisible to the generator and simply never translates.
 *
 * At the call site include app_msg.h and write the name: the redirect turns
 * GAD_OK into a GetString() lookup, so the same source serves the built-in
 * English and every catalog.
 *
 * The strings below are a starting seed -- replace them with the program's
 * own. Keep at least one, or there is no catalog to build.
 */

/* --- Window and section titles ------------------------------------------ */
#define WIN_MAIN            "MyApp"
#define SEC_OPTIONS         "Options"

/* --- Gadgets -------------------------------------------------------------*/
#define GAD_OK              "OK"
#define GAD_CANCEL          "Cancel"
#define GAD_SAVE            "Save"

/* --- Labels --------------------------------------------------------------*/
#define LBL_NAME            "Name"

/* --- Menus ---------------------------------------------------------------*/
#define MENU_PROJECT        "Project"
#define MENU_ABOUT          "About..."
#define MENU_QUIT           "Quit"

/* --- Messages and errors -------------------------------------------------*/
/* ⚠ A %-specifier is part of the id fingerprint: changing one changes the
 * catalog version, so an old .catalog can never feed the wrong argument type
 * into a format call. Keep the specifiers identical in every translation --
 * gen_catalog.py's `check` verifies that. */
#define MSG_READY           "Ready."
#define ERR_NO_MEMORY       "Out of memory."
#define ERR_OPEN_FAILED     "Cannot open %s."

#endif /* APP_STRINGS_H */
