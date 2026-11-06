# RadeonGL

A `minigl.library` for the ATI Radeon R200 family (Radeon 8500, 9000, 9100,
9200, 9250) on Picasso96, for AmigaOS 3.x on a 68040 or 68060 -- or, in a
second build, on a 68020 or 68030 with a 68881 or 68882.

RadeonGL draws with the Radeon's own 3D engine. It needs no Warp3D: it
programs the card's command processor through Picasso96, one lock a frame,
while the Workbench keeps running. It is a `minigl.library` 29.1 with the
dispatch interface (ABI 5) of the current MiniGL SDK, so programs built
with that SDK open it like any other `minigl.library` 29.

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
  world). `glHint(GL_RGL_STATIC_INDICES_HINT, GL_NICEST)` (`0x52474C02`)
  does the same for 16-bit index lists: keyed by address, count and lock,
  never read again -- for lists built once (GLQuake draws whole chunks of
  its world from them, and lets the card cull what is hidden). Both share
  `GL_FASTEST`'s forgetting. Other `minigl.library`s do not name the
  extension: test the extension string first.
- **Transformation and rasterising on the Radeon:** the full float matrix
  stack (`glFrustum`, `glOrtho`, `glRotatef`, `glTranslatef`, `glScalef`,
  push, pop, load, multiply), viewport, depth range.
- **Textures:** two units (multitexture), RGB565, ARGB4444, I8 and AI88 in
  the card's memory (up to 16 MB), nearest, linear and mipmap filters,
  repeat and clamp, the environments MODULATE, REPLACE, DECAL (RGB) and
  ADD, `glTexSubImage2D` updates.
- **Fragments:** blending, alpha test, depth test with every function,
  depth mask, polygon offset and `mglSetZOffset`, back-face culling,
  scissor, colour mask, flat and smooth shading, point size.
- **MiniGL 29.1 contexts:** a Picasso96 screen of its own, 16 or 32 bit, up
  to 2048 x 2048, double buffered, with MiniGL's lock modes.
- **Safety:** every command list is checked before the Radeon runs it
  (`RadeonGLCheck=0` turns it off for speed).
- **The package:** an Installer, manuals in English and German, a card test,
  a card report for bug reports and four demos (`demos/`).

## Not implemented yet

RadeonGL implements 100 of the 205 functions of `minigl.library` 29.1:
everything GLQuake uses, and more. A missing function draws nothing and
sets GL_INVALID_OPERATION; the library's log names every such call.

- Lighting and materials (`glLight`, `glMaterial`, `glLightModel`,
  `glColorMaterial`): not there (they set GL_INVALID_OPERATION).
- Fog, display lists, texture coordinate generation (`glTexGen`),
  `glTexCoord4f`.
- The texture environment `GL_BLEND` (drawn as `GL_MODULATE`) and
  `GL_DECAL` with an RGBA texture; 32-bit textures are stored with 16 bits.
- Stencil and accumulation buffers, line and polygon stipple, logic
  operations.
- `glReadPixels`, `glCopyTexImage2D`, `glCopyTexSubImage2D`,
  `glPixelStorei`, `glDrawBuffer`, `glReadBuffer`.
- The double-precision calls, `glArrayElement`, `glInterleavedArrays`,
  `glMultiDrawArrays`, `glNormalPointer`, edge flags, colour-index mode,
  `glBlendEquation`, `glBlendFuncSeparate`, `glLineWidth`, `glColorTable`.
- GLU and GLUT; MiniGL's own main loop and callbacks; contexts in a window
  or a bitmap.
- One program at a time; textures beyond 16 MB are not swapped out.

## GLQuake benchmarks

GLQuake for R200 68k, the RadeonGL edition of GLQuake for
`minigl.library` (1.0, a separate project and archive): its timedemos (all
frames, no sound) on the test machine, an A4000D with a 68060 at 50 MHz on
a CyberStorm MK II, Mediator, Radeon 9200, AmigaOS 3.2, Picasso96 3.6,
RadeonGL 0.7 with its command check off (`RadeonGLCheck=0`, as GLQuake's
start script sets it). Frames a second:

| Demo | 320 x 240 | 640 x 480 | 1280 x 720 |
|---|---:|---:|---:|
| demo1 | 29.4 | 28.7 | 28.0 |
| demo2 | 25.1 | | 21.1 |
| demo3 | 25.6 | | 24.0 |

- The same timedemos with the Aminet source (built for the 68060) on
  RadeonGL 0.6: demo1 24.4 / 23.7 / 23.3, demo2 21.7 / - / 17.9,
  demo3 21.4 / - / 19.9 -- the edition and 0.7 are 16 to 21% faster. The
  Aminet binary itself is a 68020 build and slower still on a 68060.
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
| `gmake demos` | `dist/Demos/`: Spinner, Glow, Landscape, Showcase |
| `gmake glsmoke probes` | GLSmoke (the card test), R200Info and the other probes |
| `gmake test` | the host tests (the core, the packet layer, the R200 simulator) |
| `gmake autotest T=<name>` | one FS-UAE test from `test/auto` (the head of each test says what it checks) |
| `gmake release-stage release-archive release-aminet release-drawer` | the release package: `dist/RadeonGL.lha`, the Aminet pair (copied to `release/`), `DEV:Release` |

GLQuake, the program RadeonGL is measured with, is not part of this
repository. To play it on RadeonGL, or to run its benchmarks, see
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
- **ATI Technologies, VA Linux Systems, Precision Insight, The Weather
  Channel and Tungsten Graphics, Red Hat**, and the authors of the Linux
  radeon and Mesa r200 drivers (among them Dave Airlie, Alex Deucher,
  Jerome Glisse, Kevin E. Martin, Gareth Hughes and Keith Whitwell), whose
  code is the public record of the R200.
- **The tools:** amiga-gcc (Stefan "Bebbo" Franke), amitools and vamos
  (Christian Vogelgsang), FS-UAE (Frode Solheim), libnix, SAS/C, and the
  AmigaOS 3.2 NDK (Hyperion).
