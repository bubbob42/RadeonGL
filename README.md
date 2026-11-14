# RadeonGL

A `minigl.library` for the ATI Radeon R200 family (Radeon 8500, 9000, 9100,
9200, 9250) on Picasso96, for AmigaOS 3.x on a 68040 or 68060 -- or, in a
second build, on a 68020 or 68030 with a 68881 or 68882.

RadeonGL draws with the Radeon's own 3D engine. It needs no Warp3D: it
programs the card's command processor through Picasso96, one lock a frame,
while the Workbench keeps running. It is a `minigl.library` 29.1 with the
dispatch interface (ABI 5) of the current MiniGL SDK, so programs built
with that SDK open it like any other `minigl.library` 29.

## Contents

- [Download](#download)
- [Features](#features)
- [Contexts](#contexts): the four kinds, each frame, input, size, ending and
  restarting, several at once, watching from outside
- [Not implemented yet](#not-implemented-yet)
- [GLQuake benchmarks](#glquake-benchmarks)
- [Requirements](#requirements)
- [Building](#building)
- [Layout](#layout)
- [Licence](#licence)
- [Made with AI](#made-with-ai)
- [Credits](#credits)

## Download

The binary release is in [`release/`](release): `RadeonGL.lha`, the
package (Installer, manuals in English and German, card test, demos, the
licences and the source), and `RadeonGL.readme`, what it is and what it
needs.

## Features

- **Drawing:** every GL primitive (points, lines, line strips and loops,
  triangles, strips, fans, quads, quad strips, polygons), in immediate mode
  and from vertex arrays (`glDrawArrays`, `glDrawElements` with 8-, 16- and
  32-bit indices).
- **The fast path:** compiled vertex arrays (`glLockArraysEXT`) go to the
  card as indexed draws; an upload cache keeps what a program sends again
  unchanged in the card's memory.
- **Static arrays (`GL_RGL_static_arrays`, RadeonGL's own extension):** a
  program whose arrays never change says so with
  `glHint(GL_RGL_STATIC_ARRAYS_HINT, GL_NICEST)` (`0x52474C01`); the upload
  cache then knows them by address instead of reading their bytes every
  frame. `GL_DONT_CARE` switches it off again, `GL_FASTEST` forgets every
  such array (before the program frees or refills one). It covers arrays
  of positions plus two texture units without a colour array (GLQuake's
  world), GLQuake's models, and every lit layout (with normals). `glHint(GL_RGL_STATIC_INDICES_HINT, GL_NICEST)` (`0x52474C02`)
  does the same for 16-bit index lists: keyed by address, count and lock,
  never read again -- for lists built once (GLQuake draws whole chunks of
  its world from them, and lets the card cull what is hidden). Both share
  `GL_FASTEST`'s forgetting. Other `minigl.library`s do not name the
  extension: test the extension string first.
- **Transformation and rasterising on the Radeon:** the full float matrix
  stack (`glFrustum`, `glOrtho`, `glRotatef`, `glTranslatef`, `glScalef`,
  push, pop, load, multiply), viewport, depth range.
- **Textures:** two units (multitexture: `GL_ARB_multitexture`, also
  under MiniGL's name `GL_MGL_ARB_multitexture`), RGB565, ARGB4444, ARGB8888 (for
  `GL_RGB8` and `GL_RGBA8`), I8 and AI88 in
  the card's memory (up to 16 MB), nearest, linear and mipmap filters,
  repeat and clamp, the environments MODULATE, REPLACE, DECAL, BLEND and
  ADD (`GL_EXT_texture_env_add`), `glTexSubImage2D` updates. 32-bit
  textures also take `GL_BGRA` with `GL_UNSIGNED_INT_8_8_8_8_REV`: native
  0xAARRGGBB words, the card's own texel, copied as they are (a 422 x 102
  texture in 22 ms on a Mediator, the bus speed).
- **Lighting on the Radeon's TCL:** `glLight`, `glMaterial`,
  `glLightModel` (ambient, local viewer, two-sided), `glColorMaterial`,
  `GL_NORMALIZE`; eight lights, directional, local and spot, with
  attenuation; normals from `glNormal` and `glNormalPointer`. Lit compiled
  vertex arrays take the fast path too, with their normals in the stream.
- **Texture coordinates from the TCL:** `glTexGen` with `GL_OBJECT_LINEAR`,
  `GL_EYE_LINEAR` and `GL_SPHERE_MAP` (S and T, per unit), and texture
  matrices (`glMatrixMode(GL_TEXTURE)`).
- **Fog and lines:** `glFog` (linear, exp, exp2; computed by the TCL from
  the eye depth), `glLineWidth` up to 64 pixels.
- **Reading back:** `glReadPixels` (RGBA, RGB, red, green, blue, alpha,
  luminance; every GL 1.1 type; the whole pack state of `glPixelStorei`)
  and MiniGL's `mglWriteShotPPM`,
  through Picasso96 from the card's memory (about 4 MB a second on a
  Mediator).
- **The rest of MiniGL 29.1's calls:** GLU (`gluPerspective`,
  `gluLookAt`, `gluBuild2DMipmaps`, spheres, cylinders and disks,
  `gluErrorString`), `glArrayElement`, `glInterleavedArrays`,
  `glMultiDrawArrays`, the double-precision matrix calls, the texture
  environments `GL_BLEND` (with `glTexEnvfv`'s colour) and RGBA `GL_DECAL`,
  texture uploads in every GL 1.1 layout and type (`glPixelStorei`) and
  `glReadPixels` in every type, the `glGet` queries 29.1 answers, and
  MiniGL's own main loop with its key, mouse and idle callbacks.
- **Fragments:** blending, alpha test, depth test with every function,
  depth mask, polygon offset and `mglSetZOffset`, back-face culling,
  scissor, colour mask, flat and smooth shading, point size; polygons drawn
  as lines or points (`glPolygonMode`).
- **MiniGL 29.1 contexts:** a Picasso96 screen of its own, 16 or 32 bit, up
  to 2048 x 2048, with MiniGL's lock modes; every mode of the card
  (`mglGetSupportedScreenModes`), a context in a chosen mode, the size
  switched while the program runs (`mglResizeContext`), vertical sync
  without tearing (`mglEnableSync`: the copy goes down the screen ahead of
  the beam).
- **Two or three buffers:** two copied onto the screen, or three
  (`mglChooseNumberOfBuffers(3)` or `RadeonGLBuffers=3`) shown by panning a
  screen three times as tall or by Intuition's `ChangeScreenBuffer`
  (`RadeonGLFlip=csb`; `csbnowait` skips the wait for each switch's
  DispMessage: not safe on Picasso96, for tests only); a program may
  switch while it runs (GLQuake's Video Options > Display sync). Panning
  is the fastest way without tearing: GLQuake's demo1 at 1280 x 720 runs
  at 32.1 fps (the copy with vertical sync 29.9, without it 32.2), at
  1920 x 1080 at 30.6 (22.4, 28.7).
- **Windows and bitmaps:** a context in the program's window
  (`mglCreateContextFromWindow`), in a window of its own on the default
  public screen (`mglChooseWindowMode`), or drawing straight into the
  program's board bitmap (`mglCreateContextFromBitMap`, the program shows
  it and can draw into it too), on any 15-, 16- or 32-bit screen of the
  Radeon; resizable with `mglResizeContext`.
- **Several programs at once:** each its own context (a screen, a window or
  a bitmap) and its own dispatch table; they take turns on the Radeon flush
  by flush. While more than one draws, each waits for its frame and sends
  its whole state again; one program alone runs exactly as before.
- **"Is anything drawing?":** a public port named `RadeonGL`
  (`lib/rglport.h`) tells other programs how many contexts draw on the
  Radeon, how many of them are games on a screen of their own, and when the
  last command list went to the card. Read it under `Forbid()` and copy
  what you need before `Permit()`; no port means RadeonGL is not loaded.
  A program that shares the card can stay off it while a game runs.
  `RglPort` (`src/probe/rglport.c`) shows it.
- **Safety:** every command list is checked before the Radeon runs it
  (`RadeonGLCheck=0` turns it off for speed).
- **The package:** an Installer, manuals in English and German, a card test,
  a card report for bug reports and four demos (`demos/`), and Trio, which
  runs three of them at once, each a program of its own in a window of its
  own on the Workbench, placed by `mglCreateContext`'s coordinates.
- **GLBench** (its own archive, `glbench/`): a stress benchmark in seven
  scenes that together use every feature above -- fur shells, eight
  lights, sphere maps and display lists, all five blend equations, every
  texture format and environment, a flock of a thousand darts, read-back
  feedback -- with a score against a reference machine, and Burn: ten
  minutes of one scene with a picture check every 250 frames.
- **RadeonStat:** what the card is doing, in a window on the Workbench
  (from its icon) or a line a second (from a Shell): how busy the engine,
  the 3D pipeline, the 2D engine and the command processor are, the
  command queue, the card memory Picasso96 has handed out, the front
  screen's measured refresh and pixel clock, and a busy history graph.
  Read-only; its settings are the icon's Tool Types (`src/probe/rstat*.c`).

## Contexts

How a program gets a context, draws with it, and ends it. This is MiniGL
29.1's interface; RadeonGL's own additions are named as such.

### The four kinds

| Call | What the program gets |
|---|---|
| `mglCreateContext(x, y, w, h)` | a Picasso96 screen of its own on the Radeon, in the mode that fits w x h best (16 bit if there is one, else 32) |
| `mglCreateContextFromID(id, &w, &h)` | the same in exactly that mode (`mglGetSupportedScreenModes` lists them; `w` and `h` come back as its size); `MGL_SM_BESTMODE` acts as `mglCreateContext`, `MGL_SM_WINDOWMODE` gives a window as in the next line |
| `mglChooseWindowMode(GL_TRUE)`, then `mglCreateContext(x, y, w, h)` | a window of its own on the default public screen, at x, y |
| `mglCreateContextFromWindow(window)` | drawing in the program's window: each frame is drawn on the card and copied into the inner area at `mglSwitchDisplay`; the window stays the program's |
| `mglCreateContextFromBitMap(bitmap)` | drawing straight into the program's bitmap on the Radeon; the program shows it, and may draw into it too |

A window or bitmap must be on a screen of the Radeon board in 15, 16 or
32 bits (R5G5B5PC, R5G6B5PC, B8G8R8A8). Anything else -- another board,
8 or 24 bits -- gets NULL and no requester; the log file says why. The
largest context is 2048 x 2048.

Before the create a program may call `mglChoosePixelDepth` (16 or 32 bit
for a screen), `mglChooseNumberOfBuffers` (2, or 3: shown by panning or
`ChangeScreenBuffer`, see [Features](#features)) and `mglChooseWindowMode`.
The window choice belongs to the program that made it: it is forgotten
when a context is deleted.

### Each frame

Draw, then `mglSwitchDisplay(context)`: one flush sends the frame to the
card and shows it -- copied or switched on a screen, copied into a window,
left in place in a bitmap. `mglEnableSync(context, GL_TRUE)` holds the
copy until the beam has passed (no tearing). `mglLockDisplay` and
`mglUnlockDisplay` are accepted and do nothing; `mglLockBack` is refused,
as 29.1 does when it has no lock to hand out, with the size filled in.

### Input

`mglGetWindowHandle(context)` (and `mglGetInputWindowHandle`) is the window
to take input from: the screen context's own window, or the window the
context draws in. A resize replaces it, and `mglDeleteContext` closes it.

### Changing the size

`mglResizeContext(context, w, h)` opens a new screen (or back buffer) at
the new size; textures and static arrays survive. Ask for the window again
afterwards. `GL_FALSE` means the size stayed: `glGetString(GL_RGL_RESIZE_ERROR)`
(`0x52474C05`, RadeonGL's extension `GL_RGL_resize_error`) says why, for
example "not enough memory for the back buffer". The same size asked for
again reopens the screen when the program changed its buffering (two or
three buffers, `RadeonGLFlip`).

### Ending, and starting again

`mglDeleteContext(context)` frees the context with its screen or window
and its textures. Only the task that created a context can delete it; a
context its task never deleted goes at that task's last `CloseLibrary`.

A program may delete its context and create a new one while
`minigl.library` stays open, to restart with new settings. Its textures
must then be uploaded again, and the old window is gone: a loop that holds
the window across `GetMsg` while a key callback restarts the context ends
up on a freed message port. Leave the input loop when the context changed.

### Several at once

Each program gets its own context and dispatch table; they take turns on
the Radeon flush by flush (see [Features](#features)). Calls without a
context argument use the calling task's newest context. Texture memory:
up to 16 MB for a screen context, 8 MB for a window or bitmap context
unless `RadeonGLHeapMax` (2 to 16) says otherwise.

### Watching from outside

The public port `RadeonGL` (`lib/rglport.h`, under "Is anything drawing?"
in [Features](#features)) counts the contexts that draw on the Radeon and
how many of them are games on a screen of their own; the counts change
when a context is created or deleted, not at each frame.

## Not implemented yet

RadeonGL answers all 205 functions of `minigl.library` 29.1 (a few
accepted without effect). Not there yet:

- Stencil and accumulation buffers, line and polygon stipple, logic operations,
  reading the depth buffer.
- Generic 32-bit textures (`GL_RGBA`, 4) are stored with 16 bits -- ask
  for `GL_RGBA8` or `GL_RGB8` to keep 8 bits.
- Contexts on a 24-bit or 8-bit screen (the Radeon draws 15, 16 and 32
  bit). Textures beyond 16 MB are not swapped out.
- Accepted without effect: edge flags and colour-index values,
  `glDrawBuffer` and `glReadBuffer`, texture priorities. `mglLockBack`
  always refuses. `glTexCoord4f` divides by q at each corner.
- Paletted textures as in MiniGL 29.1: one palette a context, applied when
  a `GL_COLOR_INDEX` image is uploaded; a `GL_RGBA` palette is read as
  A, R, G, B (MiniGL's order).
- Display lists keep the calls themselves (textures, matrices, materials,
  states, nested lists; 29.1 keeps only glBegin/glEnd vertices). Not kept,
  as in 29.1: `glTexImage2D`/`glTexSubImage2D` and vertex array draws run
  at once.
- GLUT as in 29.1: one window a program or a game-mode screen, the
  callbacks, `glutGet`, the five solid shapes; resizing calls reshape,
  the close gadget ends `glutMainLoop`.

## GLQuake benchmarks

[GLQuake for R200 68k](https://github.com/bubbob42/RadeonGL-GLQuake), the
RadeonGL edition of GLQuake for `minigl.library` (a separate project and
archive, [release 1.3](https://github.com/bubbob42/RadeonGL-GLQuake/releases/tag/v1.3)): its timedemos (all
frames, no sound) on the test machine, an A4000D with a 68060 at 50 MHz on
a CyberStorm MK II, Mediator, Radeon 9200, AmigaOS 3.2, Picasso96 3.6,
RadeonGL 0.11 with its command check off (`RadeonGLCheck=0`, as GLQuake's
start window sets it) and vertical sync off; 9 November 2026, each the
mean of three runs at different load addresses. Frames a second:

| Demo | 320 x 240 | 640 x 480 | 1280 x 720 |
|---|---:|---:|---:|
| demo1 | 33.0 | 32.2 | 31.1 |
| demo2 | 29.6 | | 28.4 |
| demo3 | 28.6 | | 25.4 |

**1920 x 1080** (11 November 2026): GLQuake 1.3 and RadeonGL 0.11 as above,
but each figure one run at one load address, not the mean of three, and not
after a fresh boot. Picasso96's 1920 x 1080 modes ran at 50 Hz with reduced
blanking (115 MHz pixel clock). Frames a second:

| Demo | 16 bit | 32 bit |
|---|---:|---:|
| demo1 | 28.6 | 21.8 |
| demo2 | 25.7 | |
| demo3 | 23.6 | |

GLQuake's `-bpp 15` opens the same 16-bit screen (R5G6B5) on the Radeon. The
32-bit figure is a single run earlier that evening in the same mode. The test
machine's monitor (DVI) blacked out briefly several times during the 16-bit
runs, but not during the 32-bit one, at the same pixel clock; the series was
stopped there. That machine's 5 V supply sags under heavy load (under
investigation), so these blackouts are not taken as RadeonGL's.

GLQuake 1.3 brings back the textures under water: 1.2 drew those surfaces
from misread vertices (a compiler fault). demo2 in 1.3: 29.8 fps at
320 x 240 and 28.3 at 1280 x 720, against 1.2's 29.5 and 24.6 in the same
series on RadeonGL 0.11 -- 15% faster at 1280 x 720. GLQuake 1.2 on
RadeonGL 0.8 (7 November): demo1 33.5 / 32.5 / 32.9, demo2 30.6 / 26.0,
demo3 28.9 / 27.5.

Where code lands in memory matters as much as what it does. GLQuake runs
about 1.5% slower on 0.11 than on 0.8 (demo1 at 320 x 240, interleaved:
29.53 against 29.03 seconds), yet profiles show the same work in the
library and an unchanged routine taking twice the time at another
address: the library grew from 118 to 191 KB, its hot code moved, and on
the 68060's 8 KB caches GLQuake's and the library's hot code collide more.
A change that only moved data cost 0.8% by itself. The machine also
varies by a few per cent from one session to the next, so a single run,
or figures from different days, decide nothing.

- The same timedemos with the Aminet source (built for the 68060) on
  RadeonGL 0.6: demo1 24.4 / 23.7 / 23.3, demo2 21.7 / - / 17.9,
  demo3 21.4 / - / 19.9 -- the edition 1.2 and 0.8 are 35 to 45% faster.
  The Aminet binary itself is a 68020 build and slower still on a 68060.
- Vertical sync (on in GLQuake by default) makes a frame wait for the
  beam: about 2% in play at 800 x 600.
- **With the command check on** (RadeonGL as installed) every command
  list is checked first: in 0.4 that took demo1 at 320 x 240 from 24.4
  to 19.1 fps.
- **The 68020/68030 build** (0.4, on the same 68060 for comparison):
  demo1 17.1 with the check on, 22.8 with it off. A real 68030 is far
  slower than that.

The 68060 sets the pace, not the Radeon: sixteen times the pixels cost
about one frame a second.

A tester's A4000D (BFG9060, 68060 at 75 MHz, Prometheus Firestorm,
RadeonGL 0.6, check off, 8-bit lightmaps, demo1 without sound): 43.0 fps at
320 x 240, 41.5 at 640 x 480; direct 43.7 and 42.2. Its bridge writes to the
card as fast as the Mediator (8.6 MB/s); its memory is 2.4 times as fast as
the CyberStorm MK II's, its clock 1.5 times.

## Requirements

- A Radeon 8500, 9000, 9100, 9200 or 9250, driven by Picasso96 3.6 or
  newer with its Radeon driver (`Radeon.card`).
- A 68040 or 68060 with FPU (built for the 68060), or a 68020 or 68030 with
  a 68881 or 68882 (the second build, `dist/Libs/RadeonGL030`). The
  Installer picks the right one; a build refuses to open on a CPU it cannot
  run on.
- AmigaOS 3.1 or newer (tested on 3.2).
- A PCI bridge for the card: tested with the Mediator and, by a tester,
  the Prometheus Firestorm.

## Building

The toolchain is the AmigaOS cross-development starter this project grew
from: `m68k-amigaos-gcc` (libnix) for the library, SAS/C 6.58 through
`vamos` for the C89 check of the core, FS-UAE for unattended tests, a host
`cc` and Python 3 for the tests and tools.

Not in this repository (or the release archive's Source drawer), to be
fetched from Aminet:

- **The Picasso96 developer files** (`P96_SDK=`, default
  `~/Developer/AmigaSDK/Picasso96Develop`): the library and the probes.
- **Thomas Richter's openpci kit** (`driver/other/openpci68k.lha`), for the
  probes only: its `Include` tree goes to `sdk/openpci/include`, and
  `fd2pragma --infile openpci.fd --clib include/clib/openpci_protos.h
  --special 40` makes the GCC inline header. Its licence lets it travel only
  as Thomas Richter's whole archive.

| Command | What |
|---|---|
| `gmake rgl-lib` | `dist/Libs/RadeonGL/minigl.library` |
| `gmake demos` | `dist/Demos/`: Spinner, Glow, Landscape, Showcase, Trio |
| `gmake glsmoke probes` | GLSmoke (the card test), R200Info, RadeonStat and the other probes |
| `gmake glbench` | `dist/GLBench/C/`: GLBench, the stress benchmark (seven scenes, a score, Burn), and its 68030 build |
| `gmake glbench-archive` | `dist/GLBench.lha` and its Aminet readme: GLBench's own archive |
| `gmake test` | the host tests (the core, the packet layer, the R200 simulator) |
| `gmake autotest T=<name>` | one FS-UAE test from `test/auto` (the head of each test says what it checks) |
| `gmake release-stage release-archive release-aminet release-drawer` | the release package: `dist/RadeonGL.lha`, the Aminet pair (copied to `release/`), `DEV:Release` |

GLQuake, the program RadeonGL is measured with, is not part of this
repository: its RadeonGL edition, **GLQuake for R200 68k**, lives in
[bubbob42/RadeonGL-GLQuake](https://github.com/bubbob42/RadeonGL-GLQuake)
(source, Installer, the binary release `GLQuakeR200.lha`). It uses RadeonGL's
static-arrays extension and is the fastest way to play Quake on RadeonGL.
To play the Aminet GLQuake on RadeonGL, or to run its benchmarks, see
[`glquake-bench/`](glquake-bench): start scripts with sound and LAN, where
to get GLQuake, how to install it. For development, GLQuake's source lives
beside this repository (`GLQUAKE_DIR`, default
`../RadeonGL-GLQuake`); `gmake glquake glquake-data` builds it and copies
the game data (never committed).

## Layout

| Path | What |
|---|---|
| `lib/` | the library: the MiniGL shell, the hooks, the card back end, the null card |
| `src/core/` | the GL core: state, matrices, vertices, arrays, textures |
| `src/r200/` | the R200 layer: packets, emitter, ring, command processor, checker |
| `src/probe/` | the hardware probes (R200Info, BusBench, ...) |
| `demos/` | the demonstrators |
| `glquake-bench/` | start scripts for GLQuake on RadeonGL, and how to install it |
| `install/` | the Installer script and the drawer's scripts |
| `guide/` | the manuals (English, German) and the Aminet readme |
| `test/` | host tests and FS-UAE tests |
| `tools/` | build, packaging and analysis tools |
| `sdk/` | the MiniGL 29.1 SDK, the R200 microcode and other headers |

## Licence

`minigl.library` contains MiniGL (the library shell and the 29.1
programming interface) and is therefore under the Hyperion MiniGL Open
Source License: it may only be used on AmigaOS, and the source code of
every change has to be published. RadeonGL's own code is also available
under the MIT licence. The R200 microcode is AMD's (MIT); the register
definitions follow the MIT-licensed Linux radeon, Mesa r200 and
xf86-video-ati drivers. Everything is in [LICENSE](LICENSE).

## Made with AI

RadeonGL was made with the help of AI. Claude, an AI model by Anthropic,
wrote most of its code, its tests and its documentation, working under the
direction of Marcus Gerards, who made the design decisions. Every release
was tested on real hardware.

## Credits

- **Thomas Richter:** Picasso96 3 and its Radeon driver, `openpci.library`,
  and his advice on using Picasso96 with the Radeon; his `68060.library`
  and `mmu.library` ran on the test machine, his LhA builds the package.
- **Alexander Kneer and Tobias Abt:** Picasso96.
- **Hyperion Entertainment (Hans-Jörg and Thomas Frieden):** MiniGL.
- **Steffen "MagicSN" Häuser:** `minigl.library` 29.1, its shell, its
  dispatch interface and its SDK.
- **Frank Wille, Steffen Häuser and Dennis Boon:** GLQuake for
  `minigl.library`, after id Software's Quake.
- **Advanced Micro Devices:** the R200 microcode.
- **Martin "Mason" Merz:** the Install, AmigaGuide and text icons, AmigaOS
  3.2's own, used with his permission.
- **ATI Technologies, VA Linux Systems, Precision Insight, The Weather
  Channel and Tungsten Graphics, Red Hat**, and the authors of the Linux
  radeon and Mesa r200 drivers (among them Dave Airlie, Alex Deucher,
  Jerome Glisse, Kevin E. Martin, Gareth Hughes and Keith Whitwell), whose
  code is the public record of the R200.
- **The tools:** amiga-gcc (Stefan "Bebbo" Franke), amitools and vamos
  (Christian Vogelgsang), FS-UAE (Frode Solheim), libnix, SAS/C, and the
  AmigaOS 3.2 NDK (Hyperion).
