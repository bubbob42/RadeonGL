# glquake/ -- the GLQuake test client

GLQuake for AmigaOS 3.x 68k as a **minigl.library ABI-5 dispatch client**: every
`gl*` and `mgl*` call goes through the library's dispatch table. One binary
therefore runs on any minigl.library 29.1+ -- PiStorm3D, Warp3D Classic, and
RadeonGL when it exists. It is the compatibility test for RadeonGL and the
base for the fast-path fork (docs/concept-radeongl.md, section 8).

## Where it comes from

- **Source:** Aminet `game/shoot/MiniGL_Library_Quake1.lha` (2026-10-07; Dennis Boon, Steffen Haeuser), the `src/` drawer.
  - Imported unmodified in commit `d34acdb`; four empty build drawers and an empty `debug` file were left out.
  - Upstream's README is kept as `README.upstream.txt`.
- **Licence:** GPL, see `COPYING`. Quake is copyright id Software.
- **Lineage:** id's GPL release -> Frank Wille / Christian Michael (BlitzQuake) -> Cowcat -> Boon/Haeuser.
- **Game data is not here and is never committed** (`*.pak` is git-ignored).

## Build

```sh
gmake glquake          # -> dist/Quake/GLQuake  (objects in build/glquake/)
gmake glquake-data     # copies PAK0/PAK1 from QUAKE_ID1 to dist/Quake/id1/ (54 MB)
gmake clean-glquake
```

`QUAKE_ID1` defaults to the registered data on the Quake data drawer,
`/path/to/quake/ID1`. That pak0 has demo1-demo3 for
`timedemo`. Override it on the command line for another copy.

Toolchain: m68k-amigaos-gcc 6.5 (`~/amiga-arm`), libnix (`-noixemul`), GAS
for the `*_gas.s` files. The import library and headers come from
`sdk/minigl-29.1`. AHI and CyberGraphX headers come from `sdk/ahi` and
`sdk/cgx`. The rules live in `Makefile.radeongl`; the top-level Makefile only
delegates to it.

## What RadeonGL changed, and why

The published source is upstream's **static** build: its own script
(`build_glquake_wsl_showcase.sh`) links MiniGLV3D into the executable. The
shipped `glquake` binary is a shared-library client, but the code that makes
it one is not in the archive. In particular, the binary's "Opening
minigl.library..." / "Unable to open minigl.library V%d or obtain a compatible
dispatch table" messages appear nowhere in `src/`. Built as a client, the
published source failed in three places. The changes:

| Where | Change |
|---|---|
| `gl_vidamiga.c` | Under `MINIGL_SHARED_CLIENT`: `MiniGLOpen()` replaces `MGLInit()`, and `MiniGLClose()` replaces `MGLTerm()`. Same messages as the shipped binary. `VID_Shutdown` touches the dispatch table only when it exists: `Host_Shutdown` runs it after a failed `MiniGLOpen` too, and a call through the NULL table jumps into the CPU vector table (guru `810000FF`, measured in FS-UAE). |
| `gl_rsurf.c` | Under `MINIGL_SHARED_CLIENT`: an empty `D()` replaces `#include "../MiniGLV3D/backend/hw/v3d_debug.h"`, a MiniGLV3D internal that is not in the client SDK. |
| `minigl_client_compat.h` (new, forced in by `-include`) | `miniglext.c`, `gl_warp.c`, `gl_mirror.c` and `gl_wiredraw.c` call MiniGLV3D's internal `GLVertex3fv(ctx, v)`-style functions and pass its global `mini_CurrentContext`. These map one-to-one onto the public calls, which fetch the current context themselves. Declares `strlcpy`/`strlcat`. |
| `minigl_client_compat.c` (new) | Inert stand-ins for two MiniGLV3D debug symbols (`g_mglv3d_last_render_wait_result`, `mglv3d_log`). `strlcpy`/`strlcat`: upstream had them from clib2, which libnix lacks. |

Everything else is upstream's code.

## Flags, and their known costs

- **CPU flags.** `-m68060 -m68881` for C, versus upstream's `-mcpu=68020 -m68881`.
  - The 68020 build is full of instructions the 68060 does not have (FScc, FMOVECR, 64-bit MULx), each a 68060.library trap.
  - This build passes `check_fpu060.py` (Reflector's tool: no FScc/FDBcc/FTRAPcc).
- **Known cost: float-to-int casts round.** GCC's 68060 tuning converts with a bare `fmove.l` in the FPCR's rounding mode, so `(int)` casts round to nearest instead of truncating (measured in Reflector, 2026-10-22).
  - mirq's BlitzQuake build has the same property. Demo playback is unaffected, because the demo carries the server's messages.
  - If a picture difference ever matters, build with `-m68020-60 -m68881`. Its casts are correct, but it traps on the 060.
- **`fsincos` stays.** `mathlib68k_gas.s` (with `M881=1`) uses three `fsincos` in `AngleVectors`. The 68060 emulates these through 68060.library; that is a few calls a frame, so it is left as upstream wrote it.
- **Upstream warnings are left alone.** Implicit declarations of `IN_InitJoy`, `R_LoadSkys`, plus pointer-type warnings in `net_bsdsocket.c` and `snd_ahi.c`.

## Running it on the Amiga

The binary has not been run yet. Copy `dist/Quake/` (with `id1/` from
`gmake glquake-data`) and a minigl.library 29.1+ into LIBS:, then run
`GLQuake` from that drawer. Without a working library back end it must stop
cleanly with the "Unable to open minigl.library" message. That is the first
thing to check in FS-UAE, where the PiStorm3D library cannot find its V3D.
