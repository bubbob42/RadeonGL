#ifndef STARTER_APP_CATVER_H
#define STARTER_APP_CATVER_H

/*
 * app_catver.h -- GENERATED catalog version
 * (tools/gen_catalog.py gen). Do not hand-edit.
 *
 * A fingerprint of the catalog id layout (string names, their order =
 * the implicit ids, and their %-format signatures). Pass it as
 * OC_Version, which OpenCatalog matches EXACTLY against the
 * .catalog's version -- so a stale catalog built for a different id
 * layout is rejected (built-in English is used) instead of returning
 * shifted strings that feed wrong arguments into format calls. The
 * generator stamps the same number into every .ct translation's
 * '## version' line, from which CatComp embeds it in the .catalog.
 */

#define S_CATALOG_VERSION 44839

/* Run-time catalog signature: the value every current-layout catalog
 * carries at id 0 (S_SIG_CATALOG). app_locale.c rejects any opened
 * catalog whose id-0 string differs -- the belt to OC_Version's braces. */
#define S_CATALOG_SIG "SCAT-44839"

#endif /* STARTER_APP_CATVER_H */
