# GLQuake on RadeonGL

Start scripts for GLQuake on a Radeon through RadeonGL: with sound, with
LAN play, and at the fastest RadeonGL settings, in three screen sizes.
Each script also runs GLQuake's timedemos, the benchmark RadeonGL is
measured with.

| Script | Screen |
|---|---|
| `PlayQuake` | 320 x 240 |
| `PlayQuake640` | 640 x 480 |
| `PlayQuake1280` | 1280 x 720 |

## What you need

- **RadeonGL**, installed with its Installer: its `minigl.library` in
  `LIBS:`, a Radeon 8500 to 9250 driven by Picasso96.
- **GLQuake for minigl.library**:
  [MiniGL_Library_Quake1](https://aminet.net/package/game/shoot/MiniGL_Library_Quake1)
  on Aminet (`game/shoot`), by Steffen Häuser and Dennis Boon, after Frank
  Wille's Amiga port of id Software's Quake. GPL, with its source.
- **The Quake data**, which no archive here contains: `pak0.pak` from the
  shareware or the full game, and `pak1.pak` for the full game.
- **A Picasso96 screen mode on the Radeon** for each size you play at:
  320 x 240, 640 x 480, 1280 x 720 (made with the Picasso96Mode
  preferences).
- **For LAN play** a TCP/IP stack that is running (Roadshow, AmiTCP,
  Miami). **For sound** nothing else: GLQuake plays through Paula, or
  through AHI with `-ahi`.

## Installing

1. Download `MiniGL_Library_Quake1.lha` from the link above and unpack it
   where you keep your games, for example:

       LhA x MiniGL_Library_Quake1.lha Work:Games/

   It makes a drawer with the program `glquake` and a drawer `id1`.
2. Copy `pak0.pak`, and `pak1.pak` if you have it, into that `id1` drawer.
3. Copy the three scripts and their icons from this folder into the
   GLQuake drawer, beside `glquake`.
4. Double-click `PlayQuake`, `PlayQuake640` or `PlayQuake1280`. From a
   Shell: `CD` to the GLQuake drawer, then `Execute PlayQuake`.

`PlayQuake` sets RadeonGL's variables only for its own run (`Set`, not
`SetEnv`), so other programs keep the defaults.

## The settings

| Setting | What it does |
|---|---|
| `RadeonGLCheck 0` | turns the command check off: about a fifth of the frame rate |
| `RadeonGLDirect 1` | writes the vertex stream straight into video RAM: a few short Picasso96 locks a frame instead of one |
| `-lm_LUMINANCE` | 8-bit lightmaps, id's own default (the Amiga port uses 16 bit): half the upload bytes |
| `-nocdaudio` | no CD music |
| `Stack 1000000` | GLQuake needs a large stack |

Sound plays through Paula at 11025 Hz, the cheapest for the CPU. For the
AHI music unit (a sound card, for example), add `-ahi` to the `glquake`
line in `PlayQuake`. Sound costs about 1.5 frames a second.

LAN play goes through the TCP/IP stack's `bsdsocket.library`, UDP port
26000. Start or join a game under Multiplayer in GLQuake's menu.

## Benchmarking

Anything after a script's name goes to GLQuake, so a script also runs a
timedemo:

    Execute PlayQuake +timedemo demo1
    Execute PlayQuake640 +timedemo demo2

At the end the console (`~`) shows the frames, the seconds and the frame
rate. For the numbers below sound was off (`-nosound` after the name).

demo1 to demo3 on an A4000D (68060 at 50 MHz, CyberStorm MK II, Mediator,
Radeon 9200, AmigaOS 3.2) with these settings, frames a second:

| Demo | 320 x 240 | 640 x 480 | 1280 x 720 |
|---|---:|---:|---:|
| demo1 | 24.9 | 22.5 | 19.8 |
| demo2 | 21.7 | | |
| demo3 | 22.3 | | |

With sound, demo1 at 320 x 240: 23.2. These figures come from a build of
GLQuake's source for the 68060. The Aminet program is built for the
68020; on a 68060 it may be slower. The RadeonGL manual's Speed page has
the other settings.
