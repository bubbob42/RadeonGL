# AmigaOS 3.2 VS Code Cross-Development Starter

A practical macOS-based cross-development starter project for writing, building, testing, and iterating on classic **AmigaOS 3.2** applications from **Visual Studio Code**.

The project supports two compiler paths:

* **m68k-amigaos-gcc** with **libnix** for modern cross-compilation
* **SAS/C 6.58** executed through **amitools/vamos** for classic Amiga compiler compatibility

It also includes optional **FS-UAE** integration so the generated binaries can be tested inside an AmigaOS 3.2 emulator environment.

---

## Goals

This repository is intended as a reusable starter setup for system-friendly AmigaOS 3.x application development.

It is designed to support workflows involving:

* Visual Studio Code
* Claude Code or similar AI-assisted coding tools
* AmigaOS 3.2 NDK-based development
* GCC/libnix cross-compilation
* SAS/C compatibility checks
* FS-UAE based emulator testing

The setup deliberately avoids POSIX assumptions, SDL, Linux/macOS runtime dependencies, and direct hardware access in normal application code.

---

## Host Platform

The current setup assumes:

* macOS on Apple Silicon
* Homebrew installed natively under `/opt/homebrew`
* GNU Make available as `gmake`
* A working `m68k-amigaos-gcc` toolchain installed under:

```sh
~/amiga-arm
```

* `amitools` and `machine68k` installed from source into:

```sh
~/Developer/AmigaTools/venv-amitools
```

* SAS/C installed under:

```sh
~/Developer/AmigaSDK/SASC/SC
```

* Optional FS-UAE installation under:

```sh
/opt/homebrew/bin/fs-uae
```

---

## What Is Included

```text
.
├── CLAUDE.md
├── Makefile
├── README.md
├── .gitignore
├── .vamosrc.in
├── .vscode/
│   └── tasks.json
├── fsuae/
│   ├── project.fs-uae.in
│   └── mutools
├── src/
│   ├── main.c
│   ├── main_sasc.c
│   ├── smakefile.in
│   └── Makefile.in
├── build/
└── dist/
```

Generated files and local machine-specific files are not committed:

```text
.vamosrc
build/
dist/
fsuae/local/
```

---

## Compiler Profiles

### GCC Profile

The GCC profile builds with:

```sh
~/amiga-arm/bin/m68k-amigaos-gcc
```

Default options:

```sh
-m68000 -Os -std=gnu99 -noixemul -lamiga
```

Output:

```text
dist/<project-name>_gcc
```

This profile is the main development path.

---

### SAS/C Profile

The SAS/C profile runs the classic Amiga SAS/C compiler through `vamos`.

Compiler:

```text
SC:C/sc
```

Linker:

```text
SC:C/slink
```

Output:

```text
dist/<project-name>_sasc
```

This profile is useful for checking whether the code remains close to classic Amiga C conventions.

---

## Important Legal Note

This repository does not include and should not include:

* AmigaOS ROMs
* AmigaOS 3.2 HDF images
* AmigaOS NDK files
* SAS/C binaries or libraries
* Workbench files

You must provide these yourself from legal sources.

---

## Required Local Paths

By default, the Makefile expects the following local paths:

```sh
~/amiga-arm
~/Developer/AmigaTools/venv-amitools
~/Developer/AmigaSDK/SASC/SC
~/Developer/AmigaEmu/ROMs/kick32.rom
~/Developer/AmigaEmu/HDF/AmigaOS32.hdf
```

You can override most paths when invoking `gmake`.

Example:

```sh
gmake all SASC_PATH="$HOME/Somewhere/SASC/SC"
```

For FS-UAE:

```sh
gmake run-fsuae \
  KICKSTART_FILE="$HOME/ROMs/kick32.rom" \
  SYSTEM_HDF="$HOME/HDF/AmigaOS32.hdf"
```

---

## First-Time Setup After Cloning

Clone the repository:

```sh
git clone <repo-url>
cd <repo>
```

Check the expected tools and paths:

```sh
gmake check
```

Generate local configuration files and build everything:

```sh
gmake all
```

Run the SAS/C binary through `vamos`:

```sh
gmake run-sasc
```

Start FS-UAE with the project output mounted as `DEV:`:

```sh
gmake run-fsuae
```

---

## Build Targets

### Build Everything

```sh
gmake all
```

Builds both GCC and SAS/C outputs and creates helper scripts in `dist/`.

---

### Build GCC Version

```sh
gmake gcc
```

Output:

```text
dist/<project-name>_gcc
```

---

### Build SAS/C Version

```sh
gmake sasc
```

Output:

```text
dist/<project-name>_sasc
```

---

### Run SAS/C Version via vamos

```sh
gmake run-sasc
```

This is suitable for simple CLI programs. It is not a replacement for full AmigaOS testing.

---

### Generate FS-UAE Config

```sh
gmake fsuae-config
```

Creates:

```text
fsuae/local/starter.fs-uae
```

from:

```text
fsuae/project.fs-uae.in
```

---

### Build and Start FS-UAE

```sh
gmake run-fsuae
```

Builds both compiler variants, generates helper scripts, creates the local FS-UAE config, and starts the emulator.

---

### Build GCC Version and Start FS-UAE

```sh
gmake run-fsuae-gcc
```

---

### Build SAS/C Version and Start FS-UAE

```sh
gmake run-fsuae-sasc
```

---

### Show Binary Information

```sh
gmake info
```

---

### Clean Build Outputs

```sh
gmake clean
```

Removes:

```text
build/
dist/
```

---

### Full Local Cleanup

```sh
gmake distclean
```

Removes:

```text
build/
dist/
.vamosrc
fsuae/local/
```

---

## Generated AmigaDOS Helper Scripts

The build creates helper files in `dist/`.

Inside AmigaOS, after FS-UAE has mounted `dist/` as `DEV:`, you can run:

```text
DEV:
execute runme
```

or directly:

```text
execute DEV:run-gcc
execute DEV:run-sasc
```

You can also run the binaries manually:

```text
DEV:<project-name>_gcc
DEV:<project-name>_sasc
```

Example:

```text
DEV:starter_gcc
DEV:starter_sasc
```

### `mutools` -- memory-debugging session starter

`dist/mutools` (copied verbatim from the committed `fsuae/mutools`) starts a MuForce/MuGuardianAngel debugging session -- catches illegal memory accesses, stack overflows, and register corruption while a program runs. Requires the `mutools` package (`MuForce`, `MuGuardianAngel`) already installed under `sys:tools/mutools/` inside the AmigaOS install. Run it before launching whatever you're debugging:

```text
execute DEV:mutools
```

Edit `fsuae/mutools` (not `dist/mutools`, which is regenerated on every build) to change its flags.

### `DEV:src/` -- native on-Amiga build

Every build also mirrors all project sources into `dist/src/` (`DEV:src/` inside AmigaOS), together with an `smakefile` (for SAS/C's `smake`) and a `Makefile` (for Amiga `make` ports), both generated from the `.in` templates in `src/` with the project name substituted. With SAS/C installed on the Amiga side, the project can be compiled and debugged entirely natively:

```text
cd DEV:src
smake              ; incremental build        -> DEV:src/<project>
smake debug        ; DEBUG=SF rebuild          -> DEV:src/<project>.debug
cpr <project>.debug ; source-level debugging in CodeProbe
smake clean
```

The native build uses the SAS/C installation's own include/library configuration (no `IDIR=` forced, unlike the host-side vamos build). The mirror is one-way -- host wins: edits made under `DEV:src/` are overwritten by the next host-side build, so treat it as a scratch build area, not a place to develop in. When adding source files to a derived project, extend `OBJS`/`SRCS` in both `.in` templates alongside the host Makefile's source lists.

---

## FS-UAE Integration

The project uses a template file:

```text
fsuae/project.fs-uae.in
```

The Makefile generates a local config file:

```text
fsuae/local/starter.fs-uae
```

The generated config mounts:

```text
dist/
```

as the Amiga volume:

```text
DEV:
```

Typical AmigaOS workflow after launching FS-UAE:

```text
DEV:
dir
execute runme
```

The config emulates a **68030 CPU** (rather than the A1200's stock 68EC020) specifically so an MMU is available for MuForce/MuGuardianAngel to use -- this is a test-environment choice only and doesn't change what your code is compiled for (still a plain 68000 baseline, see `CPUFLAGS` in the Makefile).

It also exposes the emulated serial port as a TCP socket (`serial_port = tcp://127.0.0.1:1234`), so MuForce's serial debug output can be read from the host:

```sh
telnet 127.0.0.1 1234
```

Connect *before* triggering whatever you're debugging, so you don't miss early output. See [this write-up](https://blog.arisamiga.rocks/post/debuggingamiga/) for the full workflow this is based on.

`bsdsocket_library = 1` is also enabled, giving the emulated Amiga TCP/IP stack access (`bsdsocket.library`) for programs that need it.

---

## `.vamosrc.in`

The file `.vamosrc.in` is a portable template.

It contains placeholders:

```text
@SASC_PATH@
@PROJECT_PATH@
```

The Makefile generates `.vamosrc` automatically.

Do not commit `.vamosrc`.

Example template:

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

---

## VS Code Integration

The project can be built from VS Code using the configured tasks.

Recommended tasks:

* `Amiga: Build GCC`
* `Amiga: Build SAS/C`
* `Amiga: Build All`
* `Amiga: Run SAS/C via vamos`
* `Amiga: Run FS-UAE`
* `Amiga: Clean`

The VS Code tasks should include both the Amiga GCC path and the `vamos` venv path in `PATH`.

Example:

```json
"PATH": "${env:HOME}/Developer/AmigaTools/venv-amitools/bin:${env:HOME}/amiga-arm/bin:/opt/homebrew/bin:${env:PATH}"
```

---

## Claude Code / AI-Assisted Development

The repository includes a `CLAUDE.md` file to guide Claude Code.

The file should be placed in the project root:

```text
CLAUDE.md
Makefile
src/
```

Claude Code should be started with the project root as the VS Code workspace folder.

To verify that Claude Code has loaded the file, use:

```text
/memory
```

Recommended AI development rules:

* Use AmigaOS APIs through NDK/proto headers
* Avoid POSIX APIs unless explicitly requested
* Avoid SDL, pthreads, fork, mmap, Unix sockets, and Unix paths
* Avoid direct custom chip, CIA, Copper, Blitter, and audio DMA access for normal Workbench applications
* Keep SAS/C-compatible modules close to C89/C90
* Keep function prototypes explicit
* Avoid GCC-only extensions in shared code
* Prefer small modules with clear ownership of resources

---

## Coding Style

For code intended to compile with both GCC and SAS/C:

* Prefer C89/C90-compatible structure
* Declare variables at the beginning of blocks
* Avoid mixed declarations and statements
* Avoid GCC extensions
* Use explicit prototypes
* Use AmigaOS types where appropriate
* Use `CONST_STRPTR` casts for AmigaOS string APIs when needed
* Keep platform-specific compiler handling isolated

Example:

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

### `vamos: No such file or directory`

Check whether the venv exists:

```sh
ls -l ~/Developer/AmigaTools/venv-amitools/bin/vamos
```

If the venv was copied from another Mac, recreate it. Python virtual environments contain absolute paths and should not be copied between machines.

---

### `bad interpreter: /usr/bin/python: no such file or directory`

You are probably running an old global `vamos` wrapper.

Check:

```sh
which vamos
head -1 $(which vamos)
```

If it points to `/usr/local/bin/vamos`, remove or rename the old wrapper and use the venv-local `vamos`.

---

### `machine68k.Traps` AttributeError

`amitools` and `machine68k` are out of sync.

Recommended fix:

* install both from source
* install both into the same venv
* reinstall both in editable mode

Example:

```sh
cd ~/Developer/AmigaTools/machine68k
python -m pip install -U -e .

cd ~/Developer/AmigaTools/amitools
python -m pip install -U -e .
```

---

### SAS/C builds old code after cloning

Check `.vamosrc`.

If `.vamosrc` points to an old project directory, remove it and regenerate it:

```sh
gmake distclean
gmake sasc
```

`.vamosrc` is generated from `.vamosrc.in` and should not be committed.

---

### `vamos` fails with a "NO MEMORY" internal error while compiling

This is a `vamos`-internal RAM ceiling, not a problem with your source: as source files grow, `sc` needs more room to load its own support libraries than `vamos`'s default RAM size provides. `VAMOS_SC`/`VAMOS_SLINK` in the Makefile already pass `-H disable -m 16128` to work around this (also required for a separate, older `slink`/`vamos` pointer-corruption issue -- see the comment above `$(TARGET_SASC)`). If it recurs with much larger sources, try raising `-m` further.

---

### Cursor keys or other input don't reach a program in FS-UAE

Some FS-UAE setups default to mapping the keyboard's cursor keys to emulated joystick port 1 instead of passing them through as real keystrokes to the guest OS. The template config already sets `joystick_port_1 = nothing` to disable that; if input still isn't arriving, double check the target window is actually the *active* window in Intuition (only the active window receives keyboard IDCMP messages).

---

### FS-UAE cannot find the ROM or HDF

Check:

```sh
gmake check
```

Override paths if needed:

```sh
gmake run-fsuae \
  KICKSTART_FILE="$HOME/ROMs/kick32.rom" \
  SYSTEM_HDF="$HOME/HDF/AmigaOS32.hdf"
```

---

## Recommended Repository Hygiene

Commit:

```text
Makefile
README.md
CLAUDE.md
.vamosrc.in
.gitignore
.vscode/tasks.json
fsuae/project.fs-uae.in
fsuae/mutools
src/
```

Do not commit:

```text
.vamosrc
build/
dist/
fsuae/local/
ROMs
HDFs
SAS/C installation
NDK installation
Python venv
```

Recommended `.gitignore`:

```gitignore
# Build artifacts
build/
dist/

# Generated local config
.vamosrc
fsuae/local/

# Emulator/private files
*.rom
*.hdf
*.adf

# macOS
.DS_Store

# Backups
*.bak
*.bak.*
*.old
*.tmp
```

---

## Typical Development Loop

```sh
gmake clean
gmake all
gmake run-sasc
gmake run-fsuae
```

Inside AmigaOS:

```text
DEV:
execute runme
```

This gives a compact workflow:

```text
VS Code → GCC/SAS-C build → vamos CLI test → FS-UAE AmigaOS test
```

---

## Status

Current project capabilities:

* GCC cross-build for AmigaOS 3.2
* SAS/C build via vamos (with the RAM-size workaround needed for larger sources)
* Generated local `.vamosrc`
* Generated FS-UAE config, emulating a 68030+MMU for memory debugging and exposing the serial port over TCP
* `mutools` MuForce/MuGuardianAngel debug-session starter, copied into `dist/` on every build
* `dist/` mounted as `DEV:`
* VS Code build tasks
* Claude Code project guidance
* Portable starter structure for cloned projects
