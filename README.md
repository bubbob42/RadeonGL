# AmigaOS 3.2 VS Code Cross-Development Starter

A macOS-based starter for writing, building, and testing classic **AmigaOS 3.2**
(Motorola 68000) applications from **Visual Studio Code**, with two compiler
paths and optional FS-UAE emulator testing.

- **m68k-amigaos-gcc** + **libnix** (`-noixemul`) — the main cross path.
- **SAS/C 6.58** run through **amitools/vamos** — a compatibility check that
  keeps code close to classic Amiga C conventions.

It deliberately avoids POSIX, SDL, and direct hardware access in normal
application code, and is set up to work well with Claude Code / AI assistance.

**Contents**

- [Overview & layout](#overview--layout)
- [Getting started](#getting-started) — prerequisites, first build
- [Building & running](#building--running) — targets, profiles, dev loop
- [Testing & debugging](#testing--debugging) — FS-UAE, MuForce, serial
- [Project capabilities](#project-capabilities) — new project, versioning, publish, staging, native build
- [Configuration & conventions](#configuration--conventions)
- [Troubleshooting](#troubleshooting)

---

## Overview & layout

`dist/` is the build output directory; FS-UAE mounts it as the Amiga volume
`DEV:`, so building and testing share one tree. The GCC build is the primary
target; the SAS/C build exists to catch portability drift.

```text
.
├── CLAUDE.md                 # project guidance for Claude Code / AI tools
├── Makefile                  # the whole build; derives PROJECT_NAME from the folder
├── new_project.sh            # scaffold a new sibling project from this template
├── setup-smb.sh              # configure `gmake push` for your own SMB share
├── .vamosrc.in               # vamos volume/assign template (-> .vamosrc)
├── .vscode/tasks.json
├── fsuae/
│   ├── project.fs-uae.in     # FS-UAE config template (-> fsuae/local/…)
│   └── mutools               # MuForce/MuGuardianAngel session starter
├── src/
│   ├── main.c                # GCC entry point (embeds the $VER cookie)
│   ├── main_sasc.c           # SAS/C entry point
│   ├── help.c, help.h        # OPTIONAL async AmigaGuide help scaffolding
│   ├── <project>_rev.h/.rev  # bumprev version include + counter (committed)
│   ├── smakefile.in          # native SAS/C build templates (-> DEV:src/)
│   └── Makefile.in
├── tools/                    # host-side Python helpers (icons, catalogs)
├── install/                  # generic Installer script + Workbench icon
├── guide/english/            # AmigaGuide online-help stub
├── docs/                     # inherited knowledge base (see docs/README.md)
├── reference/                # pulled reference PDFs (git-ignored)
├── build/  dist/             # build artifacts (git-ignored)
```

Generated / machine-specific files are git-ignored and never committed:
`.vamosrc`, `smbpush.conf`, `build/`, `dist/`, `fsuae/local/`, `src/*_rev.i`,
`reference/*.pdf`. The committed vs. generated split is enforced by
[`.gitignore`](.gitignore).

---

## Getting started

### Prerequisites

Host: **macOS on Apple Silicon**, Homebrew under `/opt/homebrew`, and GNU Make
as `gmake`. The Makefile expects these tool locations (override any with a
`gmake VAR=…` assignment):

| Path | What | Makefile var |
|---|---|---|
| `~/amiga-arm` | `m68k-amigaos-gcc` toolchain | `PREFIX` |
| `~/Developer/AmigaTools/venv-amitools` | `amitools`/`vamos` venv | `VAMOS` |
| `~/Developer/AmigaSDK/SASC/SC` | SAS/C 6.58 install | `SASC_PATH` |
| `~/Developer/AmigaEmu/ROMs/kick32.rom` | Kickstart 3.2 ROM | `KICKSTART_FILE` |
| `~/Developer/AmigaEmu/HDF/AmigaOS32.hdf` | AmigaOS 3.2 system disk | `SYSTEM_HDF` |
| `/opt/homebrew/bin/fs-uae` | FS-UAE (optional) | `FSUAE` |

> **Legal note:** the AmigaOS ROM, HDF/Workbench, NDK, and SAS/C are **not**
> included and must not be — supply them yourself from legal sources.

### First build

```sh
gmake check        # validate that every tool/path above is present
gmake all          # build both profiles + generate the dist/ helper scripts
gmake run-sasc     # run the SAS/C binary under vamos (quick CLI smoke test)
gmake run-fsuae    # build, generate the FS-UAE config, and launch the emulator
```

`gmake check` prints the resolved paths and fails on the first missing one, so
run it before anything else on a fresh machine.

---

## Building & running

### Make targets

| Target | Does |
|---|---|
| `all` | Both profiles, `dist/` helper scripts, and any [optional staging](#optional-staging-targets). |
| `gcc` / `sasc` | Build one profile → `dist/<project>_gcc` / `_sasc`. |
| `debug` | Serial-debug (kprintf) builds → `dist/<project>_{gcc,sasc}_debug`. See [serial output](#serial-debug-output). |
| `release` | Advance the revision (bumprev) and rebuild — see [versioning](#versioning-gmake-release). |
| `run-sasc` | Run the SAS/C binary under vamos. CLI programs only, not a full-OS test. |
| `run-fsuae` | `all` + generate the FS-UAE config + launch. Also `run-fsuae-gcc` / `run-fsuae-sasc`. |
| `fsuae-config` | Generate `fsuae/local/<project>.fs-uae` from the template only. |
| `push` | Publish `dist/` to an SMB share — see [publishing](#publish-to-a-share-gmake-push). |
| `info` | `file` + `objdump -f` on the built binaries. |
| `clean` | Remove `build/` and `dist/`. |
| `distclean` | `clean` + remove `.vamosrc` and `fsuae/local/`. |

### Compiler profiles

**GCC** builds with `-m68000 -Os -std=gnu99 -noixemul -lamiga`. Warnings are
`-Wall -Wextra` with `-Wno-pointer-sign` and `-Wno-int-conversion` suppressed
(the classic NDK inlines produce false positives on those), and
`-Werror=implicit-function-declaration` kept fatal.

**SAS/C** runs `sc`/`slink` through vamos. Both steps get `-H disable -m 16128`
to work around vamos's default RAM ceiling on larger sources (see the comment
above `$(TARGET_SASC)` in the Makefile).

#### SAS/C stack checking (`NOSTACKCHECK`) and GUI stacks

The SAS/C build compiles with `NOSTACKCHECK` (`SC_OPTS` in the Makefile,
`SCOPTS` in the `src/*.in` templates). SAS/C otherwise inserts a per-function
stack-overflow check whose handler (`__XCOVF`) is **unsafe inside an OS
callback** — most importantly a BOOPSI/ReAction dispatcher called by Intuition,
where it corrupts the task stack and shows up as an exec address-error crash
(`80000003`) instead of a clean diagnostic. GCC inserts no such check, so a
program can appear to work under GCC and crash only under SAS/C.

With the check off, the program owns its stack sizing. The default Shell stack
is small and BOOPSI GUIs (`window.class`, `texteditor.gadget`, …) are
stack-hungry, so a GUI program should embed a **stack cookie** — the Shell reads
it (`ScanStackToken()`) and sizes the process stack before `main()`:

```c
/* Note the space after the colon -- the exact format the Shell scans for. */
const char stack_cookie[] = "$STACK: 16384";
```

Put it in each entry point (add `__attribute__((used))` under GCC so it is not
stripped). Pure CLI tools that never enter Intuition callbacks can skip it.

### Typical development loop

```sh
gmake clean && gmake all
gmake run-sasc     # fast CLI check under vamos
gmake run-fsuae    # full AmigaOS test in the emulator
```

Round trip: VS Code → GCC/SAS-C build → vamos CLI test → FS-UAE AmigaOS test.

---

## Testing & debugging

### FS-UAE integration

`gmake run-fsuae` renders `fsuae/project.fs-uae.in` into
`fsuae/local/<project>.fs-uae` and launches it with `dist/` mounted as `DEV:`.
Inside AmigaOS:

```text
DEV:
execute runme        ; lists the binaries and how to start each
```

The config emulates a **68030 with an MMU** (not the A1200's stock 68EC020) so
MuForce/MuGuardianAngel have an MMU to use — a test-environment choice only;
your code is still compiled for a plain 68000 baseline (`CPUFLAGS`).
`bsdsocket_library = 1` is enabled for programs that need the Amiga TCP/IP
stack, and `joystick_port_1 = nothing` so cursor keys reach the guest.

### Serial-debug output

`gmake debug` builds with the `DBG()` trace (`src/debug.h`) enabled and links
`debug.lib` for `KPrintF()`. The FS-UAE config exposes the emulated serial port
over TCP, so read the trace from the host:

```sh
telnet 127.0.0.1 1234
```

Connect *before* triggering the code you're debugging so you don't miss early
output. (Same channel MuForce uses.) Based on
[this write-up](https://blog.arisamiga.rocks/post/debuggingamiga/).

### MuForce (`DEV:mutools`)

`dist/mutools` (from the committed `fsuae/mutools`) starts a
MuForce/MuGuardianAngel session that catches illegal memory access, stack
overflow, and register corruption. It needs the `mutools` package already
installed under `sys:tools/mutools/` in the guest. Run it before launching what
you're debugging:

```text
execute DEV:mutools
```

Edit `fsuae/mutools` to change its flags (`dist/mutools` is regenerated every
build).

### `dist/` helper scripts

Every build regenerates AmigaDOS scripts in `dist/` (= `DEV:`): `runme` (menu),
`run-gcc`, and `run-sasc` (each assigns `HELP:` and launches its binary). Run a
binary directly with `DEV:<project>_gcc` / `DEV:<project>_sasc`.

---

## Project capabilities

Reusable template features. The optional ones stay inert until you add their
inputs, and everything here is portable AmigaOS work built only from public
documentation (no AmigaOS source), so it's safe to grow in the shared knowledge
base.

### Creating a new project

`new_project.sh` clones this template into a **sibling** directory (under the
parent `AmigaApps/`) and can be run from anywhere:

```sh
starter/new_project.sh gadtools_test   # from AmigaApps/
./new_project.sh gadtools_test         # from inside any cloned project
```

The new clone is a **playground**: branch renamed to `playground`, pushing
disabled (a `pre-push` hook blocks accidents), generated files removed,
`CLAUDE.md` copied to `.claude/CLAUDE.md`, the version include reseeded for the
new name, the guide/install-icon rebranded, and the **RKRM: DOS** reference PDF
pulled into `reference/` (git-ignored, cached under `../.reference-cache/`).
Branch off a different sibling with `SOURCE_PROJECT=<name> new_project.sh …`.

### Versioning (`gmake release`)

`src/<project>_rev.h` (committed, seeded at rev 1) supplies the `$VER` cookie
embedded in both binaries via `src/main*.c`. Ordinary builds never touch it.
`gmake release` runs `bumprev` to advance the revision and rebuilds — run it by
hand when cutting a release.

### Publish to a share (`gmake push`)

`gmake push` mirrors the built `dist/` onto an SMB share so a real Amiga (or an
emulator with the share mounted) can pick it up. Configure the share once, or
override inline:

```sh
./setup-smb.sh                            # one-time; writes git-ignored smbpush.conf
gmake push                                # uses smbpush.conf
gmake push SMB_HOST=nas SMB_SHARE=amiga   # or set it on the command line
```

The copy is additive and uses `cp -X`; Finder cruft (`.DS_Store`, `._*`) is
purged from `dist/` first. macOS only.

### Optional staging targets

Drop the sources in and the matching target lights up (all fold into
`gmake all`); the pristine starter has none, so nothing extra is staged.

| Provide… | Target | Result on `DEV:` |
|---|---|---|
| `icons/<project>.info` (author with `tools/amiga_icon.py`) | `dist-icons` | a Workbench icon on each binary |
| `guide/<lang>/<project>.guide` | `guides` | AmigaGuide help in `PROGDIR:` + `HELP:` |
| `src/help.c` wired into your GUI | — | opens that help asynchronously |
| `install/Install(.info)` | `installer` | a double-clickable `Install` |
| `rexx/*.rexx` | `rexx-examples` | example ARexx macros in `DEV:Rexx/` |
| `catalogs/<project>.cd` + `<lang>/*.ct` | `catalogs` | locale message catalogs (+ `DEV:gencat`) |

See `docs/` (`reaction-layout-cookbook.md`, `amigaguide-format.md`,
`installer-scripting.md`, `localization-catcomp.md`, `arexx-programming.md`) and
`tools/README.md` for the details.

### Native on-Amiga build (`DEV:src/`)

Every build mirrors the sources into `dist/src/` with an `smakefile` and
`Makefile` generated from the `src/*.in` templates, so the project can also be
compiled natively on the Amiga:

```text
cd DEV:src
smake                ; incremental build     -> DEV:src/<project>
smake debug          ; DEBUG=SF rebuild       -> DEV:src/<project>.debug
cpr <project>.debug  ; source-level debug in CodeProbe
smake clean
```

The mirror is one-way (host wins) — treat `DEV:src/` as a scratch build area,
not a place to edit. When adding source files, extend `OBJS`/`SRCS` in both
`.in` templates alongside the host Makefile's source lists.

---

## Configuration & conventions

### `.vamosrc.in`

A portable template with `@SASC_PATH@` / `@PROJECT_PATH@` placeholders; the
Makefile renders it to `.vamosrc` (do not commit). It maps the `sasc:` and
`proj:` volumes and the SAS/C assigns vamos needs:

```ini
[volumes]
sasc=@SASC_PATH@
proj=@PROJECT_PATH@
tmp=/tmp
[assigns]
sc=sasc:
include=sasc:include
lib=sasc:lib
t=tmp:
[path]
path=sc:c
```

### VS Code

`.vscode/tasks.json` provides build/run tasks (Build GCC/SAS-C/All, Run SAS/C
via vamos, Run FS-UAE, Clean). Their `PATH` must include both the toolchain and
the vamos venv:

```json
"PATH": "${env:HOME}/Developer/AmigaTools/venv-amitools/bin:${env:HOME}/amiga-arm/bin:/opt/homebrew/bin:${env:PATH}"
```

### Claude Code / AI assistance

`CLAUDE.md` (root) guides AI tools; `new_project.sh` also copies it to
`.claude/CLAUDE.md` so it is found reliably. Confirm it loaded with `/memory`.
The key rules it enforces: use AmigaOS APIs via NDK/proto headers; no POSIX,
SDL, pthreads, or Unix paths; no direct chip/CIA/Copper/Blitter access in normal
apps; keep shared code C89-clean with explicit prototypes and clear resource
ownership. `docs/` is a project-agnostic knowledge base carried into every
clone — see `docs/README.md`.

### Coding style (shared GCC + SAS/C code)

C89/C90 structure: declarations at the top of a block, no mixed
declarations/statements, no GCC extensions, explicit prototypes, AmigaOS types,
and `CONST_STRPTR` casts on AmigaOS string APIs where the profiles disagree.

```c
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>

int main(void)
{
    Printf((CONST_STRPTR)"Hello AmigaOS 3.2\n");
    return RETURN_OK;
}
```

---

## Troubleshooting

**`vamos: No such file or directory`** — the venv is missing or was copied from
another Mac (venvs hold absolute paths). Recreate it:
`ls -l ~/Developer/AmigaTools/venv-amitools/bin/vamos`.

**`bad interpreter: /usr/bin/python`** — an old global `vamos` wrapper is on
`PATH`. Check `which vamos` / `head -1 $(which vamos)`; use the venv-local one.

**`machine68k.Traps` AttributeError** — `amitools` and `machine68k` are out of
sync. Reinstall both from source into the same venv in editable mode
(`pip install -U -e .`).

**SAS/C builds old code after cloning** — a stale `.vamosrc` points at the old
project dir. `gmake distclean && gmake sasc` regenerates it.

**vamos "NO MEMORY" while compiling** — vamos's RAM ceiling, not your source.
The Makefile already passes `-H disable -m 16128`; raise `-m` for much larger
sources.

**Cursor keys don't reach a program in FS-UAE** — the config sets
`joystick_port_1 = nothing`; also make sure the target window is the *active*
Intuition window (only it receives keyboard IDCMP).

**FS-UAE can't find the ROM or HDF** — run `gmake check`, or override
`KICKSTART_FILE=` / `SYSTEM_HDF=` on the command line.
