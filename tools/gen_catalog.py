#!/usr/bin/env python3
"""
gen_catalog.py -- bootstrap the CatComp catalog description (.cd) from
src/app_strings.h, and validate a translation (.ct) against it on the host
(CatComp itself needs iffparse.library and only runs in FS-UAE, so this
catches length / %-specifier / missing-name errors before the round trip).

Usage:
    tools/gen_catalog.py gen     # write .cd + src/<Project>_cat.h + src/app_msg.h
                                 # + src/app_catver.h; scaffold src/app_locale.[ch]
    tools/gen_catalog.py newlang <language>   # start a translation skeleton
    tools/gen_catalog.py check catalogs/<language>/<Project>.ct

Conventions (see docs/localization-catcomp.md):
  - src/app_strings.h is the human-edited seed: one #define per user-facing
    string, category-prefixed names (GAD_ LBL_ SEC_ WIN_ MSG_ MENU_ REQ_ ERR_
    CHO_ COL_).
  - The define NAME becomes the catcomp message id (prefixed, see CAT_PREFIX);
    the string value becomes the built-in English default. Min length is
    always 1; max length is by role.
  - The project name is derived from the repository root folder, like the
    Makefile's $(notdir $(CURDIR)).

`gen` also writes src/<Project>_cat.h: the numeric ids + a GetString()
accessor with the English defaults embedded, matching what `catcomp CFILE`
produces natively on the Amiga (interface-compatible: identical ids because
the .cd carries no explicit ids, so catcomp numbers them 0..N in file order
too, and the same LocaleInfo / GetString contract). The generated files are
committed so an ordinary build -- host cross-build or native SAS/C -- needs
no generator step.

Catalog version guard: ids are implicit (0..N in file order), so ANY insertion
or reorder in app_strings.h renumbers the ids after it -- a stale .catalog
against a newer binary then returns the WRONG string per id, and a shifted
format string feeds wrong arguments into RawDoFmt (observed as a MuForce hit
storm on real hardware). `gen` therefore derives a version number from the id
layout (names, order, % signatures), writes it to src/app_catver.h (pass it as
OC_Version in your locale module; OpenCatalog matches it EXACTLY) and stamps
the same number into every catalogs/<lang>/<Project>.ct '## version' line,
from which CatComp embeds it in the .catalog. Old catalog -> version mismatch
-> built-in English instead of a crash.
"""

import sys, os, re, math, zlib, glob, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.basename(ROOT)
APP_STRINGS = os.path.join(ROOT, "src", "app_strings.h")
CD_PATH = os.path.join(ROOT, "catalogs", PROJECT + ".cd")
CAT_H_PATH = os.path.join(ROOT, "src", PROJECT + "_cat.h")
MSG_H_PATH = os.path.join(ROOT, "src", "app_msg.h")
CATVER_H_PATH = os.path.join(ROOT, "src", "app_catver.h")
LOCALE_H_PATH = os.path.join(ROOT, "src", "app_locale.h")
LOCALE_C_PATH = os.path.join(ROOT, "src", "app_locale.c")
CT_GLOB = os.path.join(ROOT, "catalogs", "*", "*.ct")
CATALOG_FILE = PROJECT + ".catalog"

STR_LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')

# The catcomp message id token must differ from the call-site string token
# (app_strings.h keeps GAD_PRINT etc. as the redirect macro name), so the .cd
# entry / numeric id is prefixed. Derived from the project name's initials
# (MyApp -> MA_), which also avoids doubling an existing MSG_ category
# (MSG_NOTSET -> MA_MSG_NOTSET, not MSG_MSG_NOTSET). Override here if the
# derived prefix collides with something in your project.
CAT_PREFIX = "".join(w[0] for w in
                     re.findall(r'[A-Z][a-z0-9]*|[a-z0-9]+', PROJECT)
                     ).upper()[:4] + "_"

# The LocaleInfo global instantiated by the app's locale module (the one
# translation unit compiled with CATCOMP_CODE); see the doc.
LOCALEINFO_VAR = CAT_PREFIX + "LI"

def ident(name):
    """Sanitize a name into a C identifier fragment."""
    return re.sub(r'[^A-Za-z0-9_]', '_', name)


def cat_name(name):
    """app_strings.h define name -> catcomp .cd entry / numeric id name."""
    return CAT_PREFIX + name


def decoded_len(raw):
    """Byte length after collapsing C escapes to one byte each."""
    s = re.sub(r'\\x[0-9A-Fa-f]{1,2}', '.', raw)
    s = re.sub(r'\\[0-7]{1,3}', '.', s)
    s = re.sub(r'\\.', '.', s)
    return len(s)


def role(name):
    if name.startswith("REQ_TITLE_") or name.startswith("REQ_ASL_"):
        return "widget"
    if name.startswith("REQ_") or name.startswith("ERR_"):
        return "body"
    if name.startswith("TT_"):
        return "body"     # tooltip bubbles auto-size: no layout constraint
    if name.startswith("MENU_"):
        return "menu"
    return "widget"   # GAD_ LBL_ COL_ CHO_ SEC_ WIN_ MSG_


def max_len(n, r):
    if r == "menu":
        return max(n + 12, math.ceil(n * 2.0))
    if r == "body":
        return max(n + 40, math.ceil(n * 2.5))
    return max(n + 8, math.ceil(n * 1.8))


def pct_tokens(raw):
    """Conversion types (ignoring %N$ position and %%), in order."""
    toks = re.findall(r'%(?:\d+\$)?[-+ 0#]*\d*(?:\.\d+)?l*([bcdeEfgGiopsuxXNT%])',
                      raw)
    return [t for t in toks if t != "%"]


def catalog_version(entries):
    """Version number derived from the catalog id LAYOUT: the string names,
    their order (= their implicit ids) and their % signatures. It changes
    exactly when an existing .catalog would become unsafe against the new
    binary (shifted ids / changed format arguments) and stays put for plain
    wording edits. Nonzero UWORD, so OC_Version's exact match can enforce it
    (0 would mean "accept any")."""
    sig = "\n".join(n + ":" + "".join(pct_tokens(v)) for n, v in entries)
    return zlib.crc32(sig.encode("utf-8")) % 65500 + 1


# Run-time catalog-signature layer. Field evidence (2026-07-18) showed a stale
# catalog can storm straight through the OC_Version guard, so a
# second, self-contained check is stamped in: catalog id 0 is a signature string
# carrying the id-layout fingerprint. app_locale.c compares GetCatalogStr(0)
# against <PREFIX>CATALOG_SIG and drops any catalog that disagrees. A catalog
# built for ANY other layout necessarily has some other string at id 0, so the
# check cannot be fooled.
SIG_NAME    = "SIG_CATALOG"
SIG_TOKEN   = CAT_PREFIX.rstrip("_") + "CAT"      # OML_ -> "OMLCAT"
SIG_BUILTIN = SIG_TOKEN + "-builtin"              # distinct 'no catalog' marker


def catalog_sig(entries):
    """The signature VALUE every .ct carries at catalog id 0 (stamp_ct_sig)."""
    return "%s-%d" % (SIG_TOKEN, catalog_version(entries))


def all_entries():
    """The id-0 signature entry + the real strings, in catalog-id order. The
    built-in value is a 'no catalog' marker distinct from the fingerprint each
    .ct carries at id 0 (which also stops CatComp omitting it as
    identical-to-English)."""
    return [(SIG_NAME, SIG_BUILTIN)] + parse_app_strings()


def parse_app_strings():
    """Yield (name, raw_value) in file order from app_strings.h."""
    entries = []
    with open(APP_STRINGS, encoding="utf-8") as f:
        lines = f.readlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        m = re.match(r'\s*#define\s+([A-Z][A-Z0-9_]*)\s+(.*)', line)
        if not m:
            i += 1
            continue
        name = m.group(1)
        rest = m.group(2)
        # Join C line-continuations (trailing backslash).
        while rest.rstrip().endswith("\\"):
            rest = rest.rstrip()[:-1]
            i += 1
            rest += lines[i]
        value = "".join(STR_LIT.findall(rest))
        # Skip defines with no string literal (e.g. the include guard).
        if STR_LIT.search(rest):
            entries.append((name, value))
        i += 1
    return entries


def gen_cd():
    entries = all_entries()
    os.makedirs(os.path.dirname(CD_PATH), exist_ok=True)
    prev_prefix = None
    out = []
    out.append("; %s.cd -- CatComp catalog description" % PROJECT)
    out.append("; GENERATED by tools/gen_catalog.py from src/app_strings.h -- do")
    out.append("; not hand-edit ids; edit the strings in app_strings.h and rerun.")
    out.append("; Entry: %sNAME (/min/max) then the built-in English string." % CAT_PREFIX)
    out.append("; Catalog version (id-layout fingerprint): %d -- see"
               % catalog_version(parse_app_strings()))
    out.append("; src/app_catver.h; each .ct's '## version' line must carry it.")
    out.append("; %s (id 0) is the run-time signature app_locale.c checks;"
               % cat_name(SIG_NAME))
    out.append("; each .ct carries the fingerprint value there (auto-stamped).")
    out.append(";")
    for name, value in entries:
        prefix = name.split("_")[0]
        if prefix != prev_prefix:
            out.append(";")
            out.append("; ---- %s ----" % prefix)
            prev_prefix = prefix
        r = role(name)
        mx = max_len(decoded_len(value), r)
        out.append("%s (/1/%d)" % (cat_name(name), mx))
        out.append(value)
    with open(CD_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("wrote %s (%d strings)" % (os.path.relpath(CD_PATH, ROOT),
                                     len(entries)))


def gen_header():
    """Emit src/<Project>_cat.h: numeric ids + GetString() with the English
    defaults embedded. Interface-compatible with `catcomp CFILE` output."""
    entries = all_entries()
    guard = ident(PROJECT).upper() + "_CAT_H"
    arr = CAT_PREFIX + "CatCompArray"
    out = []
    out.append("#ifndef %s" % guard)
    out.append("#define %s" % guard)
    out.append("")
    out.append("/*")
    out.append(" * %s_cat.h -- CatComp message ids + GetString() accessor." % PROJECT)
    out.append(" *")
    out.append(" * GENERATED. Host: tools/gen_catalog.py gen. Amiga (native, no host")
    out.append(" * tools): catcomp catalogs/%s.cd CFILE %s_cat.h." % (PROJECT, PROJECT))
    out.append(" * The two are interface-compatible -- identical %sNAME ids (the .cd"
               % CAT_PREFIX)
    out.append(" * carries no explicit ids, so both number 0..N in file order) and the")
    out.append(" * same struct LocaleInfo / GetString() contract. Do not hand-edit.")
    out.append(" *")
    out.append(" * Include with CATCOMP_CODE defined in exactly ONE .c (the locale")
    out.append(" * module) to instantiate GetString() and the default table; include")
    out.append(" * with CATCOMP_NUMBERS (or neither) elsewhere for ids and prototype.")
    out.append(" */")
    out.append("")
    out.append("#include <exec/types.h>")
    out.append("")
    out.append("struct LocaleInfo")
    out.append("{")
    out.append("    APTR li_LocaleBase;")
    out.append("    APTR li_Catalog;")
    out.append("};")
    out.append("")
    out.append("STRPTR GetString(struct LocaleInfo *li, LONG stringNum);")
    out.append("")
    out.append("/* Numbers are needed by the redirect header AND by the code section. */")
    out.append("#if defined(CATCOMP_NUMBERS) || defined(CATCOMP_CODE)")
    for i, (name, _value) in enumerate(entries):
        out.append("#define %s %d" % (cat_name(name), i))
    out.append("#endif /* CATCOMP_NUMBERS || CATCOMP_CODE */")
    out.append("")
    out.append("#ifdef CATCOMP_CODE")
    out.append("")
    out.append("#include <libraries/locale.h>")
    out.append("#include <proto/locale.h>")
    out.append("")
    out.append("struct %sCatCompArrayType" % CAT_PREFIX)
    out.append("{")
    out.append("    LONG   cca_ID;")
    out.append("    STRPTR cca_Str;")
    out.append("};")
    out.append("")
    out.append("static const struct %sCatCompArrayType %s[] =" % (CAT_PREFIX, arr))
    out.append("{")
    for name, value in entries:
        out.append('    { %s, (STRPTR)"%s" },' % (cat_name(name), value))
    out.append("};")
    out.append("")
    out.append("STRPTR GetString(struct LocaleInfo *li, LONG stringNum)")
    out.append("{")
    out.append("    STRPTR builtIn;")
    out.append("    LONG   i;")
    out.append("    LONG   count;")
    out.append("")
    out.append("    builtIn = (STRPTR)\"\";")
    out.append("    count = (LONG)(sizeof(%s) / sizeof(%s[0]));" % (arr, arr))
    out.append("    for (i = 0; i < count; i++)")
    out.append("    {")
    out.append("        if (%s[i].cca_ID == stringNum)" % arr)
    out.append("        {")
    out.append("            builtIn = %s[i].cca_Str;" % arr)
    out.append("            break;")
    out.append("        }")
    out.append("    }")
    out.append("")
    out.append("    /* Consult the catalog only when locale.library is open AND a catalog is")
    out.append("     * actually loaded; otherwise the built-in English default is used. The")
    out.append("     * NULL check on the result additionally guards against a stub or broken")
    out.append("     * locale.library (e.g. an incomplete emulation) handing back NULL. */")
    out.append("    if (li != NULL && li->li_LocaleBase != NULL && li->li_Catalog != NULL)")
    out.append("    {")
    out.append("        STRPTR s;")
    out.append("")
    out.append("        s = GetCatalogStr((struct Catalog *)li->li_Catalog, stringNum,")
    out.append("                          builtIn);")
    out.append("        if (s != NULL)")
    out.append("        {")
    out.append("            return s;")
    out.append("        }")
    out.append("    }")
    out.append("    return builtIn;")
    out.append("}")
    out.append("")
    out.append("#endif /* CATCOMP_CODE */")
    out.append("")
    out.append("#endif /* %s */" % guard)
    with open(CAT_H_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("wrote %s (%d ids)" % (os.path.relpath(CAT_H_PATH, ROOT), len(entries)))


def gen_msg():
    """Emit src/app_msg.h: redefine every app_strings.h name as a runtime
    catalog lookup, so call sites stay unchanged while text becomes localized."""
    entries = all_entries()
    guard = ident(PROJECT).upper() + "_APP_MSG_H"
    width = max(len(name) for name, _ in entries) if entries else 0
    out = []
    out.append("#ifndef %s" % guard)
    out.append("#define %s" % guard)
    out.append("")
    out.append("/*")
    out.append(" * app_msg.h -- GENERATED localization redirect")
    out.append(" * (tools/gen_catalog.py gen). Do not hand-edit; edit the English")
    out.append(" * text in app_strings.h and rerun.")
    out.append(" *")
    out.append(" * Each user-facing string name is redefined as a runtime catalog")
    out.append(" * lookup: GetString(&%s, %s<name>) returns the translation for"
               % (LOCALEINFO_VAR, CAT_PREFIX))
    out.append(" * the user's locale, or the built-in English default (embedded in")
    out.append(" * %s_cat.h) when there is no catalog. Call sites are unchanged;" % PROJECT)
    out.append(" * the central header includes this in place of app_strings.h (which")
    out.append(" * remains the generator's seed).")
    out.append(" *")
    out.append(" * Because each name now expands to a function call it can no longer sit")
    out.append(" * in a static/global initializer; populate such tables (menu strips,")
    out.append(" * chooser label arrays) at runtime instead.")
    out.append(" */")
    out.append("")
    out.append('#include "app_locale.h"    /* %s, %s* ids, GetString() */'
               % (LOCALEINFO_VAR, CAT_PREFIX))
    out.append("")
    for name, _value in entries:
        out.append("#define %-*s GetString(&%s, %s)"
                    % (width, name, LOCALEINFO_VAR, cat_name(name)))
    out.append("")
    out.append("#endif /* %s */" % guard)
    with open(MSG_H_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("wrote %s (%d redirects)" % (os.path.relpath(MSG_H_PATH, ROOT),
                                       len(entries)))


def gen_catver():
    """Emit src/app_catver.h: the catalog version the locale module passes as
    OC_Version. Exact-match semantics reject any stale .catalog."""
    entries = parse_app_strings()
    ver = catalog_version(entries)
    guard = ident(PROJECT).upper() + "_APP_CATVER_H"
    out = []
    out.append("#ifndef %s" % guard)
    out.append("#define %s" % guard)
    out.append("")
    out.append("/*")
    out.append(" * app_catver.h -- GENERATED catalog version")
    out.append(" * (tools/gen_catalog.py gen). Do not hand-edit.")
    out.append(" *")
    out.append(" * A fingerprint of the catalog id layout (string names, their order =")
    out.append(" * the implicit ids, and their %-format signatures). Pass it as")
    out.append(" * OC_Version, which OpenCatalog matches EXACTLY against the")
    out.append(" * .catalog's version -- so a stale catalog built for a different id")
    out.append(" * layout is rejected (built-in English is used) instead of returning")
    out.append(" * shifted strings that feed wrong arguments into format calls. The")
    out.append(" * generator stamps the same number into every .ct translation's")
    out.append(" * '## version' line, from which CatComp embeds it in the .catalog.")
    out.append(" */")
    out.append("")
    out.append("#define %sCATALOG_VERSION %d" % (CAT_PREFIX, ver))
    out.append("")
    out.append("/* Run-time catalog signature: the value every current-layout catalog")
    out.append(" * carries at id 0 (%sSIG_CATALOG). app_locale.c rejects any opened"
               % CAT_PREFIX)
    out.append(" * catalog whose id-0 string differs -- the belt to OC_Version's braces. */")
    out.append("#define %sCATALOG_SIG \"%s\"" % (CAT_PREFIX, catalog_sig(entries)))
    out.append("")
    out.append("#endif /* %s */" % guard)
    with open(CATVER_H_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("wrote %s (catalog version %d)"
          % (os.path.relpath(CATVER_H_PATH, ROOT), ver))


def gen_locale():
    """SCAFFOLD src/app_locale.h + src/app_locale.c, the run-time half.

    Unlike the other generated files this is written ONCE, only when absent,
    and is then a hand-maintained module: a project grows its own additions
    there (an About-box language line, a locale-aware date format), and
    regenerating over that would throw them away. Delete the files and re-run
    `gen` to get a fresh copy.

    It is scaffolded rather than shipped as a fixed file because every
    identifier in it carries the per-project CAT_PREFIX, which is derived from
    the project name -- only the generator knows it.
    """
    made = []

    if not os.path.exists(LOCALE_H_PATH):
        guard = ident(PROJECT).upper() + "_APP_LOCALE_H"
        out = []
        out.append("#ifndef %s" % guard)
        out.append("#define %s" % guard)
        out.append("")
        out.append("/*")
        out.append(" * app_locale.h -- locale.library + message-catalog lifecycle.")
        out.append(" *")
        out.append(" * Scaffolded by tools/gen_catalog.py; hand-maintained from here on.")
        out.append(" *")
        out.append(" * Pulls in the generated id/accessor header (%s_cat.h) so every"
                   % PROJECT)
        out.append(" * translation unit that includes this sees the %s* string ids,"
                   % CAT_PREFIX)
        out.append(" * struct LocaleInfo and the GetString() prototype. The one shared")
        out.append(" * LocaleInfo instance (%s) and GetString()'s built-in default"
                   % LOCALEINFO_VAR)
        out.append(" * table live in app_locale.c -- the single translation unit")
        out.append(" * compiled with CATCOMP_CODE.")
        out.append(" *")
        out.append(" * App_OpenCatalog() / App_CloseCatalog() are each called once, at")
        out.append(" * startup and shutdown. Opening is deliberately NON-FATAL: with no")
        out.append(" * locale.library, or no matching catalog on disk, GetString()")
        out.append(" * returns the built-in English defaults, so the program always runs")
        out.append(" * and the text simply stays English.")
        out.append(" */")
        out.append("")
        out.append("#include <exec/types.h>")
        out.append("")
        out.append("/* Emit the %s* id #defines from the generated header. app_locale.c"
                   % CAT_PREFIX)
        out.append(" * adds CATCOMP_CODE on top to also instantiate GetString() and the")
        out.append(" * built-in default table. */")
        out.append("#define CATCOMP_NUMBERS")
        out.append("#include \"%s_cat.h\"" % PROJECT)
        out.append("")
        out.append("/* Shared handle passed to every GetString(&%s, %sid) lookup"
                   % (LOCALEINFO_VAR, CAT_PREFIX))
        out.append(" * (defined in app_locale.c). li_LocaleBase == NULL means")
        out.append(" * locale.library is not open, so GetString() must not call into it")
        out.append(" * and returns the built-in default. */")
        out.append("extern struct LocaleInfo %s;" % LOCALEINFO_VAR)
        out.append("")
        out.append("BOOL App_OpenCatalog(void);")
        out.append("void App_CloseCatalog(void);")
        out.append("CONST_STRPTR App_LanguageName(void);")
        out.append("")
        out.append("#endif /* %s */" % guard)
        with open(LOCALE_H_PATH, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(out) + "\n")
        made.append(LOCALE_H_PATH)

    if not os.path.exists(LOCALE_C_PATH):
        p = CAT_PREFIX
        out = []
        out.append("/*")
        out.append(" * app_locale.c -- open/close locale.library and the %s message"
                   % PROJECT)
        out.append(" * catalog, and -- as the single CATCOMP_CODE translation unit --")
        out.append(" * instantiate GetString() together with the built-in English")
        out.append(" * default table from %s_cat.h. No other module defines" % PROJECT)
        out.append(" * CATCOMP_CODE, so GetString() and the default array have exactly")
        out.append(" * one definition program-wide.")
        out.append(" *")
        out.append(" * Scaffolded by tools/gen_catalog.py; hand-maintained from here on.")
        out.append(" * See app_locale.h and docs/localization-catcomp.md.")
        out.append(" */")
        out.append("")
        out.append("#include <exec/types.h>")
        out.append("#include <dos/dos.h>")
        out.append("#include <utility/tagitem.h>")
        out.append("#include <libraries/locale.h>")
        out.append("")
        out.append("#include <proto/exec.h>")
        out.append("#include <proto/dos.h>")
        out.append("#include <proto/locale.h>")
        out.append("")
        out.append("/* CATCOMP_CODE must be defined in EXACTLY ONE translation unit:")
        out.append(" * it instantiates GetString() and the built-in default strings. */")
        out.append("#define CATCOMP_CODE")
        out.append("#include \"app_locale.h\"")
        out.append("#include \"app_catver.h\"   /* %sCATALOG_VERSION, %sCATALOG_SIG */"
                   % (p, p))
        out.append("")
        out.append("/* locale.library base for the proto/locale calls and GetString()'s")
        out.append(" * catalog fetch; opened and closed by this module. Typed")
        out.append(" * struct LocaleBase * to match the extern in <proto/locale.h>.")
        out.append(" * Initialised to NULL deliberately: a tentative definition")
        out.append(" * without it makes libnix link an auto-open stub instead. */")
        out.append("struct LocaleBase *LocaleBase = NULL;")
        out.append("")
        out.append("/* The one LocaleInfo every GetString() call is handed. */")
        out.append("struct LocaleInfo %s = { NULL, NULL };" % LOCALEINFO_VAR)
        out.append("")
        out.append("static struct Locale *%sLocale = NULL;" % p)
        out.append("")
        out.append("#define CATALOG_NAME        \"%s\"" % CATALOG_FILE)
        out.append("")
        out.append("/* Own compare rather than strcmp: this module may be linked into a")
        out.append(" * library or a device segment, where pulling an sc.lib module in can")
        out.append(" * rearrange the link and move a structure that must sit at segment")
        out.append(" * offset 0. No libc, no reordering. */")
        out.append("static BOOL SameStr(const char *a, const char *b)")
        out.append("{")
        out.append("    while (*a != '\\0' && *a == *b)")
        out.append("    {")
        out.append("        a++;")
        out.append("        b++;")
        out.append("    }")
        out.append("    return (BOOL)(*a == *b);")
        out.append("}")
        out.append("")
        out.append("")
        out.append("/*")
        out.append(" * Open locale.library and the catalog. NON-FATAL by design: a missing")
        out.append(" * library or catalog is not an error and must not stop the program or")
        out.append(" * pop a requester. Returns TRUE when a catalog was opened, FALSE when")
        out.append(" * the built-in English strings will be used -- callers may ignore it.")
        out.append(" */")
        out.append("BOOL App_OpenCatalog(void)")
        out.append("{")
        out.append("    struct TagItem tags[3];")
        out.append("")
        out.append("    /* V38+. Absent on 2.04 and older: stay English, keep running. */")
        out.append("    LocaleBase = (struct LocaleBase *)")
        out.append("        OpenLibrary((STRPTR)\"locale.library\", 38);")
        out.append("    if (LocaleBase == NULL)")
        out.append("    {")
        out.append("        return FALSE;")
        out.append("    }")
        out.append("    %s.li_LocaleBase = (APTR)LocaleBase;" % LOCALEINFO_VAR)
        out.append("")
        out.append("    /* NULL = the user's current preferred locale. */")
        out.append("    %sLocale = OpenLocale(NULL);" % p)
        out.append("")
        out.append("    tags[0].ti_Tag  = OC_BuiltInLanguage;")
        out.append("    tags[0].ti_Data = (ULONG)\"english\";")
        out.append("    tags[1].ti_Tag  = OC_Version;")
        out.append("    /* EXACT match: the ids are implicit (0..N in .cd order), so a")
        out.append("     * catalog built for a different layout would return shifted")
        out.append("     * strings -- wrong text, and wrong argument types in format")
        out.append("     * calls. Rejecting it means falling back to English. */")
        out.append("    tags[1].ti_Data = %sCATALOG_VERSION;" % p)
        out.append("    tags[2].ti_Tag  = TAG_DONE;")
        out.append("")
        out.append("    /* A NULL catalog is normal (no translation installed, or the")
        out.append("     * catalog on disk is stale). */")
        out.append("    %s.li_Catalog = (APTR)OpenCatalogA(%sLocale," % (LOCALEINFO_VAR, p))
        out.append("                                       (STRPTR)CATALOG_NAME, tags);")
        out.append("")
        out.append("    /* Belt to OC_Version's braces: that guard needs CatComp and")
        out.append("     * locale.library to cooperate on the FVER version, and a stale")
        out.append("     * catalog has been seen to storm straight through it in the")
        out.append("     * field. Catalog id 0 is a signature string carrying the")
        out.append("     * id-layout fingerprint (%sSIG_CATALOG, value %sCATALOG_SIG --"
                   % (p, p))
        out.append("     * gen_catalog.py stamps it into every .ct). A catalog built for")
        out.append("     * ANY other layout necessarily has some other string at id 0, so")
        out.append("     * the comparison cannot be fooled: mismatch -> drop the catalog")
        out.append("     * and use the built-in English strings. */")
        out.append("    if (%s.li_Catalog != NULL)" % LOCALEINFO_VAR)
        out.append("    {")
        out.append("        CONST_STRPTR sig;")
        out.append("")
        out.append("        sig = (CONST_STRPTR)GetCatalogStr(")
        out.append("            (struct Catalog *)%s.li_Catalog, %sSIG_CATALOG,"
                   % (LOCALEINFO_VAR, p))
        out.append("            (STRPTR)\"%s-builtin\");" % SIG_TOKEN)
        out.append("        if (!SameStr((const char *)sig, %sCATALOG_SIG))" % p)
        out.append("        {")
        out.append("            CloseCatalog((struct Catalog *)%s.li_Catalog);"
                   % LOCALEINFO_VAR)
        out.append("            %s.li_Catalog = NULL;" % LOCALEINFO_VAR)
        out.append("        }")
        out.append("    }")
        out.append("")
        out.append("    return (BOOL)(%s.li_Catalog != NULL);" % LOCALEINFO_VAR)
        out.append("}")
        out.append("")
        out.append("")
        out.append("/*")
        out.append(" * The user's first preferred language, as locale.library reports it.")
        out.append(" * That is the same name the catalog search uses")
        out.append(" * (LOCALE:Catalogs/<language>) and the name the guides are installed")
        out.append(" * under (HELP:<language>). loc_PrefLanguages[0] is the user's first")
        out.append(" * choice; loc_LanguageName is the fallback for a locale with an empty")
        out.append(" * preference list. Returns NULL when no locale is open.")
        out.append(" */")
        out.append("CONST_STRPTR App_LanguageName(void)")
        out.append("{")
        out.append("    if (%sLocale == NULL)" % p)
        out.append("    {")
        out.append("        return NULL;")
        out.append("    }")
        out.append("    if (%sLocale->loc_PrefLanguages[0] != NULL &&" % p)
        out.append("        %sLocale->loc_PrefLanguages[0][0] != '\\0')" % p)
        out.append("    {")
        out.append("        return (CONST_STRPTR)%sLocale->loc_PrefLanguages[0];" % p)
        out.append("    }")
        out.append("    return (CONST_STRPTR)%sLocale->loc_LanguageName;" % p)
        out.append("}")
        out.append("")
        out.append("")
        out.append("/*")
        out.append(" * Release everything App_OpenCatalog() took, in reverse order. Safe")
        out.append(" * to call after a partial or failed open -- every step is guarded.")
        out.append(" */")
        out.append("void App_CloseCatalog(void)")
        out.append("{")
        out.append("    if (%s.li_Catalog != NULL)" % LOCALEINFO_VAR)
        out.append("    {")
        out.append("        CloseCatalog((struct Catalog *)%s.li_Catalog);"
                   % LOCALEINFO_VAR)
        out.append("        %s.li_Catalog = NULL;" % LOCALEINFO_VAR)
        out.append("    }")
        out.append("    if (%sLocale != NULL)" % p)
        out.append("    {")
        out.append("        CloseLocale(%sLocale);" % p)
        out.append("        %sLocale = NULL;" % p)
        out.append("    }")
        out.append("    if (LocaleBase != NULL)")
        out.append("    {")
        out.append("        CloseLibrary((struct Library *)LocaleBase);")
        out.append("        LocaleBase = NULL;")
        out.append("        %s.li_LocaleBase = NULL;" % LOCALEINFO_VAR)
        out.append("    }")
        out.append("}")
        with open(LOCALE_C_PATH, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(out) + "\n")
        made.append(LOCALE_C_PATH)

    for path in made:
        print("scaffolded %s (hand-maintained from here on)"
              % os.path.relpath(path, ROOT))
    if not made:
        print("app_locale.c/.h already present -- left alone "
              "(delete them to re-scaffold)")


def new_lang(lang):
    """Write catalogs/<lang>/<Project>.ct: a ready-to-translate skeleton.

    Every id in catalog order, each with the English text as its value, so a
    translator overwrites lines rather than reconstructing the file -- and the
    ids cannot end up in the wrong order, which would silently mistranslate
    every string after the mistake.

    Refuses to overwrite an existing translation.
    """
    path = os.path.join(ROOT, "catalogs", lang, PROJECT + ".ct")
    if os.path.exists(path):
        print("%s already exists -- not touched" % os.path.relpath(path, ROOT))
        return 1

    entries = all_entries()
    ver = catalog_version(parse_app_strings())
    date = time.strftime("%d.%m.%Y")

    out = []
    out.append("## version $VER: %s %d.0 (%s)" % (CATALOG_FILE, ver, date))
    out.append("## language %s" % lang)   # the language's OWN name, not English
    out.append("## codeset 0")
    out.append(";")
    out.append("; Translation of %s. Each entry is the id line followed by the"
               % PROJECT)
    out.append("; translated string on the NEXT line. Both lines are required,")
    out.append("; and the ORDER MUST NOT CHANGE -- catalog ids are implicit.")
    out.append(";")
    out.append("; Values start as the English text: overwrite each one. A value")
    out.append("; left identical to English is omitted from the built .catalog,")
    out.append("; which is correct and harmless (the built-in default is used).")
    out.append(";")
    out.append("; Validate with:  tools/gen_catalog.py check %s"
               % os.path.relpath(path, ROOT))
    out.append(";")
    out.append("; The '## version' line and the %s entry below are AUTO-MANAGED"
               % cat_name(SIG_NAME))
    out.append("; by `gen_catalog.py gen` -- do not translate or edit them.")
    out.append("")

    for name, value in entries:
        if name == SIG_NAME:
            out.append("; run-time catalog signature (id 0) -- auto-managed by")
            out.append("; gen_catalog.py 'gen'; do not translate or remove.")
            out.append(cat_name(name))
            out.append(catalog_sig(parse_app_strings()))
            out.append("")
            continue
        out.append(cat_name(name))
        out.append(value)
        out.append("")

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("wrote %s (%d entries, catalog version %d)"
          % (os.path.relpath(path, ROOT), len(entries), ver))
    print("  translate the values, then: tools/gen_catalog.py check %s"
          % os.path.relpath(path, ROOT))
    print("  and add '%s' to LOCALE_LANGS in the Makefile." % lang)
    return 0


def ct_version_of(line):
    """Extract the version number from a '## version $VER: ...' line, or None."""
    m = re.match(r'##\s*version\s+\$VER:\s*\S+\s+(\d+)\.', line)
    return int(m.group(1)) if m else None


def stamp_ct_versions():
    """Rewrite each .ct's '## version' line to the current catalog version
    (keeping it untouched -- including its date -- when already current)."""
    entries = parse_app_strings()
    ver = catalog_version(entries)
    now = time.localtime()
    vline = ("## version $VER: %s %d.0 (%d.%d.%d)"
             % (CATALOG_FILE, ver, now.tm_mday, now.tm_mon, now.tm_year))
    for path in sorted(glob.glob(CT_GLOB)):
        with open(path, encoding="utf-8") as f:
            lines = f.readlines()
        changed = False
        found = False
        for i, s in enumerate(lines):
            if s.startswith("## version"):
                found = True
                if ct_version_of(s) != ver:
                    lines[i] = vline + "\n"
                    changed = True
                break
        if not found:
            lines.insert(0, vline + "\n")
            changed = True
        if changed:
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.writelines(lines)
            print("stamped %s (catalog version %d) -- REBUILD its .catalog"
                  % (os.path.relpath(path, ROOT), ver))


def stamp_ct_sig():
    """Insert or refresh each .ct's SIG_CATALOG entry (the id-0 run-time
    signature, value = the current layout fingerprint). Auto-managed: the
    translators never touch it."""
    sig = catalog_sig(parse_app_strings())
    token = cat_name(SIG_NAME)
    for path in sorted(glob.glob(CT_GLOB)):
        with open(path, encoding="utf-8") as f:
            lines = f.readlines()
        changed = False
        idx = None
        for i, s in enumerate(lines):
            if s.strip() == token:
                idx = i
                break
        if idx is not None and idx + 1 < len(lines):
            if lines[idx + 1].rstrip("\n") != sig:
                lines[idx + 1] = sig + "\n"
                changed = True
        else:
            at = 0
            while at < len(lines) and lines[at].startswith("##"):
                at += 1
            lines[at:at] = [
                "; run-time catalog signature (id 0) -- auto-managed by\n",
                "; gen_catalog.py 'gen'; do not translate or remove.\n",
                token + "\n",
                sig + "\n",
            ]
            changed = True
        if changed:
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.writelines(lines)
            print("stamped %s signature %s -- REBUILD its .catalog"
                  % (os.path.relpath(path, ROOT), sig))


def parse_ct(path):
    """Return (commands, {name: raw_value}) from a .ct file (UTF-8)."""
    cmds, trans = [], {}
    with open(path, encoding="utf-8") as f:
        lines = f.readlines()
    i = 0
    while i < len(lines):
        s = lines[i].rstrip("\n")
        if s.startswith("##"):
            cmds.append(s)
            i += 1
            continue
        if s.startswith(";") or s.strip() == "":
            i += 1
            continue
        name = s.strip()
        i += 1
        val = lines[i].rstrip("\n") if i < len(lines) else ""
        while val.endswith("\\"):
            i += 1
            val = val[:-1] + (lines[i].rstrip("\n") if i < len(lines) else "")
        trans[name] = val
        i += 1
    return cmds, trans


def check_ct(path):
    real = parse_app_strings()
    entries = all_entries()
    info = {}
    for name, value in entries:
        r = role(name)
        info[cat_name(name)] = (value, max_len(decoded_len(value), r),
                                pct_tokens(value))
    cmds, trans = parse_ct(path)
    errors, warns = [], []

    ver = catalog_version(real)

    # The id-0 signature must carry the CURRENT fingerprint value.
    sig_have = trans.get(cat_name(SIG_NAME))
    sig_want = catalog_sig(real)
    if sig_have != sig_want:
        errors.append("%s: '%s' != expected '%s' (run 'gen' to restamp)"
                      % (cat_name(SIG_NAME), sig_have, sig_want))
    ctver = None
    for c in cmds:
        v = ct_version_of(c)
        if v is not None:
            ctver = v
    if ctver is None:
        errors.append("missing '## version $VER: ...' line "
                      "(run 'gen' to stamp it)")
    elif ctver != ver:
        errors.append("catalog version %d != current id layout %d "
                      "(run 'gen', then rebuild the .catalog)" % (ctver, ver))

    for name, _ in entries:
        if cat_name(name) not in trans:
            errors.append("missing translation: %s" % cat_name(name))
    for name, tval in trans.items():
        if name not in info:
            errors.append("unknown token (not in .cd): %s" % name)
            continue
        eng, mx, epct = info[name]
        dl = decoded_len(tval)
        if dl < 1:
            errors.append("%s: empty (min 1)" % name)
        if dl > mx:
            errors.append("%s: len %d > max %d  [%s]" % (name, dl, mx, tval))
        if pct_tokens(tval) != epct:
            errors.append("%s: %% specifiers %s != english %s"
                          % (name, pct_tokens(tval), epct))
        if tval == eng:
            warns.append("%s: identical to English (catcomp will omit it)"
                         % name)
    for w in warns:
        print("WARN  " + w)
    for e in errors:
        print("ERROR " + e)
    print("%s: %d strings, %d error(s), %d warning(s)"
          % (os.path.relpath(path, ROOT), len(trans), len(errors), len(warns)))
    return 1 if errors else 0


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "gen":
        gen_cd()
        gen_header()
        gen_msg()
        gen_catver()
        gen_locale()
        stamp_ct_versions()
        stamp_ct_sig()
        return 0
    if len(sys.argv) >= 2 and sys.argv[1] == "header":
        gen_header()
        gen_msg()
        gen_catver()
        return 0
    if len(sys.argv) >= 3 and sys.argv[1] == "newlang":
        return new_lang(sys.argv[2])
    if len(sys.argv) >= 3 and sys.argv[1] == "check":
        return check_ct(sys.argv[2])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
