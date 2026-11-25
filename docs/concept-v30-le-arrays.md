# V30's little-endian arrays (scheduled item 1, 2026-11-25)

The user, 2026-11-25 01:20, after the V30 comparison: "Schedule 1-4 for
tomorrow". Item 1: `glDisable(MGL_ARRAY_TRANSFORMATIONS)` means "my vertex
arrays are little-endian" in V30; RadeonGL should read them so, then raise
the library version 29 -> 30.

## What V30 does (PiStorm3D gl/src)

- `vertexarray.c:97-117`, `:653-680`, `vertexelements.c:139-214`: with
  MGL_ARRAY_TRANSFORMATIONS off, every array reader loads positions,
  normals, texture coordinates and float colours byte-swapped
  (`v_LoadU32(p, le)`). Bytes (RGBA/ARGB/BGRA colours) are read as they
  are. **Index lists stay native** (no swap in the index reads).
- The default is on (normal big-endian arrays).

## The old meaning, and why it does not stand in the way

In MiniGL 1.x/2.x (mglQ3, `src/vertexarray.c:2186-2215`, `:2418-2450`,
`:2660-2680`) the same switch meant "no pipeline": the arrays are already
Warp3D device coordinates (w in the texture coordinates' last element)
and go straight to W3D_DrawArray. mglQ3's demos use it (`demos/varray.c:154`).

Neither 29.1 nor RadeonGL honours that meaning: both store the flag and
transform the arrays anyway. A program that relies on it draws wrongly
today and will draw wrongly (differently) with V30's meaning, so nothing
that works now breaks. If one ever turns up, the way out is a gate on the
version the program asked for in OpenLibrary (the library's Open vector
sees it in D0): below 30, the old (stored-only) behaviour.

## How RadeonGL reads them

**One route for every array draw, nothing in the hot files.** With the
switch off, Tex13_Update sets `wide_on`, the existing GL 1.3 route that
array.c's Core_DrawArrays/Core_DrawElements and compat.c's
Core_ArrayElement already hand to tex13.c (Tex13_DrawArrays,
Tex13_DrawElements, Tex13_ArrayElement). Those three read every vertex
through one function (FetchWide, plus FetchNormal). There, with the
switch off, each enabled array's element is byte-swapped into a buffer in
struct Core (per component: 2-byte types swap 2, 4-byte swap 4, doubles
reverse 8; byte types point at the element itself), the arrays point at
the copies while FetchWide reads element 0, and the program's pointers go
back at once.

What that covers:

- glDrawArrays, glDrawElements (and glDrawRangeElements, glMultiDrawArrays,
  which end there), glArrayElement, glInterleavedArrays' arrays;
- polygon modes (lines/points): their draws go through glArrayElement;
  their static-arrays shortcut (polymode.c Static, which reads positions
  itself and draws through the upload cache) steps aside while the switch
  is off;
- display lists: array draws run at once (as 29.1), so they take the same route.

What it costs, and why that is acceptable:

- The wide route draws vertex by vertex (no locked-array upload cache, no
  packed fast paths), and wide_on widens the vertex format and takes
  ValidateOnce_W. A V30 program with little-endian arrays is slower here
  than one with ordinary arrays. That is correctness first; a fast path
  can follow if a real program needs one.
- **Not a byte in array.c, vertex.c, hwstate.c or any other hot object**:
  tex13.c, compat.c and polymode.c are linked after the hot code
  (CORE_LATE), so GLQuake's layout cannot move. GLQuake never touches the
  switch.

## The library version (separate commit)

After the arrays work: lib/shell.c VERSION 29 -> 30 (REVISION from 1),
so programs that open minigl.library 30 get RadeonGL. Check before that
commit: what V30's own library reports, the Installer's version
comparison (install/Install:19 assumes every 29.1 has the same version),
the guides/readme wording, and that clients opening version 29 still
open 30 (OpenLibrary takes "at least").

## Tests

- Host (test/host/test_tex13 or test_core): the same quad drawn from a
  big-endian array with the switch on and from its byte-swapped copy with
  it off must produce the same packets; glDrawArrays, glDrawElements,
  glArrayElement; float positions, float/short texture coordinates, float
  normals, byte colours, a 4-component position; glIsEnabled answers;
  switching back on returns to the narrow route (wide_on 0).
- On the A4000D (when Hamiller is back): a GLMix/GLSmoke case drawing a
  little-endian array, compared by eye with its big-endian twin.

## Open questions (for the user)

1. The version gate above: only if a program with the old meaning turns up?
2. Is the slow route acceptable for now, or does a V30 port already exist
   that needs speed with little-endian arrays?
