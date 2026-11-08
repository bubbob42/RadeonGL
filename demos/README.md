# The RadeonGL demonstrators

Four small, original programs that show RadeonGL's `minigl.library` at work,
for the release package. They open the library like any minigl.library 29.1
client (`MiniGLOpen`), draw on a screen of their own and print the frame rate
when they end.

- **Build:** `gmake demos`. The programs go to `dist/Demos/<Name>`, stripped
  and checked by `tools/check_fpu060.py`.
- **Code:** GCC only, because they link the MiniGL SDK's import library;
  C89 style otherwise.
- **Launchers:** `demos/scripts/<Name>` are the IconX scripts the package
  copies beside a `C/` drawer that holds the programs. Use `icons/run.info`
  for their icons, whose output window waits.
- **Test:** `gmake demos rgl-lib && gmake autotest T=DemoSmoke` runs every
  program on the null card with the packet checker on, then ends Spinner
  with the Esc key.

## Arguments (all four)

    <Name> FRAMES/N,WIDTH/N,HEIGHT/N,DEPTH/N,SCENESECS/N,SCENEFRAMES/N,WINDOW/S,LEFT/N,TOP/N

| Argument | Meaning | Default |
|---|---|---|
| `FRAMES` | frames to draw; 0 means until Esc or Ctrl-C | 0 |
| `WIDTH`, `HEIGHT` | the screen size | 640 x 480 |
| `DEPTH` | 16 or 32 bits | 16 |
| `SCENESECS` | Showcase only: seconds per scene | 12 |
| `SCENEFRAMES` | Showcase only: frames per scene, used instead of `SCENESECS` | not set |
| `WINDOW` | a window on the default public screen instead of a screen of its own (`mglChooseWindowMode(GL_TRUE)`); `WIDTH` x `HEIGHT` is then its inner size, and `DEPTH` the screen's | off |
| `LEFT`, `TOP` | with `WINDOW`: the window's place, given to `mglCreateContext` as `offx`, `offy` | 30, 30 |

- **Keys:** Esc ends a program; in Showcase the space bar skips to the next
  scene.
- **From Workbench:** the defaults.
- **Return codes:** 0 drawn; 5 drawn, but GL reported an error; 20 could not
  draw (no minigl.library 29.1, no screen, a scene that could not start).

## The programs

### Spinner

English:
A textured ring (a torus) turns about three axes in front of a colour
gradient. Its colours are lit per vertex against a light that stays where
it is while the ring turns.

Deutsch:
Ein texturierter Ring (ein Torus) dreht sich vor einem Farbverlauf um drei
Achsen. Seine Farben werden an jeder Ecke beleuchtet, von einem Licht, das
stehen bleibt, während sich der Ring dreht.

Exercises: immediate mode (`glBegin(GL_QUAD_STRIP)`, a colour and a texture
coordinate for every vertex: Gouraud shading), the depth test, back-face
culling, a repeating RGB texture with linear filtering, the matrix stack
(`glFrustum`, `glTranslatef`, `glRotatef`).

### Glow

English:
Sparks with comet tails circle inside a slowly turning crystal of tinted
glass panes. The glass is see-through, the sparks add up their light.

Deutsch:
Funken mit Kometenschweifen kreisen in einem sich langsam drehenden
Kristall aus getönten Glasscheiben. Das Glas ist durchscheinend, das Licht
der Funken addiert sich.

Exercises: blending, both alpha blending (`GL_SRC_ALPHA`,
`GL_ONE_MINUS_SRC_ALPHA`) and additive blending (`GL_ONE`, `GL_ONE`); a
luminance texture (I8 on the card) modulated by each spark's colour;
`glDepthMask`. It also sends many small quads in one `glBegin`.

### Landscape

English:
A flight low over rolling hills under a dusk sky. The ground is drawn with
two textures, a pattern of grass and earth and a light texture shaded from
the slopes.

Deutsch:
Ein Flug dicht über sanfte Hügel unter einem Abendhimmel. Der Boden wird
mit zwei Texturen gezeichnet: einem Muster aus Gras und Erde und einer
Lichttextur, die aus den Hängen schattiert ist.

Exercises: the library's fast path for vertex arrays, the one GLQuake's
world takes.
- **The array:** one interleaved array of x y z, s t, s t floats, 28 bytes
  a vertex.
- **The draws:** `glLockArrays` once a frame, then `glDrawElements` with
  16-bit indices for each of the 3 x 3 tiles.
- **The textures:** two texture units, the ground texture modulated by a
  `GL_LUMINANCE` light texture.
- **The upload cache:** every tile draws the same locked vertices, so the
  card receives them only once.

### Showcase

English:
Spinner, Glow and Landscape in turn, twelve seconds each. The space bar
skips to the next one.

Deutsch:
Spinner, Glow und Landscape nacheinander, je zwölf Sekunden. Die Leertaste
springt zum nächsten.

Exercises: all of the above in one context. Each scene gives back its
textures (`glDeleteTextures`) and its states before the next one makes its
own.

### Trio

English:
Spinner, Glow and Landscape at the same time, each a program of its own in
a window of its own on the Workbench. Each window opens where its
program's `mglCreateContext` asked for it. Ctrl-C ends all three, Esc in
a window ends that one.

Deutsch:
Spinner, Glow und Landscape gleichzeitig, jedes ein eigenes Programm in
einem eigenen Fenster auf der Workbench. Jedes Fenster öffnet sich dort,
wo der `mglCreateContext` seines Programms es verlangt hat. Ctrl-C beendet
alle drei, Esc in einem Fenster nur dieses.

    Trio SECONDS/N,LEFT/N,TOP/N,WIDTH/N,HEIGHT/N,GAP/N,STEP/N

Trio (`trio.c`) needs no `minigl.library` itself. It starts the three
from its own drawer as asynchronous shells (`SystemTags`, `SYS_Asynch`)
named "Trio: <Name>", with `WINDOW LEFT x TOP y WIDTH w HEIGHT h`: side by
side from `LEFT`, `TOP` (40, 40), `WIDTH` x `HEIGHT` (320 x 240), `GAP`
(30) pixels apart, each `STEP` (60) pixels lower. Their output goes to
Trio's console. Ctrl-C, or `SECONDS`, sends each shell a Ctrl-C; Trio
ends when all three have (or after ten more seconds).

Exercises: several programs at once (RadeonGL 0.9: a dispatch table per
task, a context each, the Radeon taken flush by flush), and RadeonGL's
reading of `mglCreateContext(offx, offy, w, h)` in window mode as the
window's place (MiniGLV3D opens its window at 30,30 and takes them as the
viewport's corner). Each window's title names its program and that place.

## In the package (a suggestion)

    Demos/                 a drawer with an icon
      Spinner Glow Landscape Showcase Trio   demos/scripts/*, icons/run.info
      C/                   no icon: the five programs from dist/Demos/

Each launcher sets `Stack 32000`, runs `C/<Name>` from its own drawer (IconX
makes that the current directory), and leaves the frame rate in its window.
