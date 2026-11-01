#!/bin/bash
# glquake showcase build using the fresh WSL2 GCC toolchain (the project
# default since 2026-08-25, no toolchain marker needed) with -O2
# -finline-functions (the project-wide optimization standard adopted the
# same day -- matches full -O3 fps without -O3's 2 known miscompiles).
# Static build: MiniGLV3D gl/src + backend/hw compiled directly into the
# glquake executable, same structure as build_glquake_gcc_O3_nolog_
# showcase.sh (D:\amiga-gcc / -O3, superseded by this script), just ported
# to run inside WSL2 against ~/amiga-gcc-install instead.
set -e

# Embed the BUILD VERSION marker in the output filename so each rebuild
# produces a distinctly-named binary instead of silently overwriting the
# previous one (lost a real test binary to this exact gap once already --
# see feedback_backup_before_rebuild_not_after.md).
GLQUAKE_VER=$(grep -oP '(?<=MGLV3D_GLQUAKE_BUILD_VERSION )\d+' /mnt/d/v3d_driver/glquake/gl_vidamiga.c)

export PATH="$HOME/amiga-gcc-install/bin:$PATH"
GCC="$HOME/amiga-gcc-install/bin/m68k-amigaos-gcc"
AS="$HOME/amiga-gcc-install/bin/m68k-amigaos-as"

GLINC=/mnt/d/v3d_driver/MiniGLV3D/gl/include
BACKENDINC=/mnt/d/v3d_driver/MiniGLV3D/backend/include
GCCCOMPATINC=/mnt/d/v3d_driver/glquake/gcc_compat
INCFLAGS="-I$GCCCOMPATINC -I$GLINC -I$BACKENDINC"
OPTFLAGS="-fno-strict-aliasing -fno-builtin-cos -fno-builtin-sin -finline-functions"

GLSRC=/mnt/d/v3d_driver/MiniGLV3D/gl/src
HWSRC=/mnt/d/v3d_driver/MiniGLV3D/backend/hw
SRC=/mnt/d/v3d_driver/glquake
OUT_Q=/mnt/d/v3d_driver/glquake/build_output/obj_asm
OUT_M=/mnt/d/v3d_driver/glquake/build_output/obj_minigl
mkdir -p "$OUT_Q" "$OUT_M"

CFLAGS="-std=c99 -O2 $OPTFLAGS -mcpu=68020 -m68881 -mcrt=clib2 \
 -DLITFILES -DGLOWEFFECTS -DFASTBOPS -DGLQUAKE -DAMIGA -DAMIGAOS -DNDEBUG -DGLM68KASM -DMGLV3D_NO_LOGGING"

QFILES="chase cl_demo cl_input cl_main cl_parse cl_tent cmd common console crc cvar \
host host_cmd in_amiga in_amigajoy in_amigapsx in_amigamouse keys location menu mathlib \
net_amiga net_dgrm net_loop net_main net_vcr nonintel pr_cmds pr_edict pr_exec r_part \
gl_draw gl_mesh gl_model gl_refrag gl_rlight gl_rmain gl_fog gl_mirror gl_wiredraw \
gl_rmisc gl_rsurf miniglext gl_warp gl_screen gl_vidamiga sbar sv_main sv_phys sv_move \
sv_user zone view wad world cd_amiga twfsound_cd snd_dma snd_mix snd_mem sys_amiga \
sys_amigaclip snd_amiga snd_ahieff keys_amiga68k snd_paula snd_ahi"

cd "$SRC"
for f in $QFILES; do
  echo "=== glquake (-O2+finline): $f.c ==="
  "$GCC" $CFLAGS $INCFLAGS -c "$f.c" -o "$OUT_Q/$f.o"
done

echo "=== glquake (-O2+finline): net_bsdsocket.c ==="
"$GCC" $CFLAGS $INCFLAGS -D__NO_NET_API -c net_bsdsocket.c -o "$OUT_Q/net_bsdsocket.o"
QFILES="$QFILES net_bsdsocket"

ASFILES="in_amigamouse68k in_amigamousewin68k snd_int68k sys_amiga68k snd_mixamiga68k common68k"
for f in $ASFILES; do
  echo "=== glquake asm (GAS): ${f}_gas.s ==="
  "$AS" -m68020 -m68881 "$SRC/${f}_gas.s" -o "$OUT_Q/$f.o"
done
echo "=== glquake asm (GAS): mathlib68k_gas.s (--defsym M881=1) ==="
"$AS" -m68020 -m68881 --defsym M881=1 "$SRC/mathlib68k_gas.s" -o "$OUT_Q/mathlib68k.o"
ASFILES="$ASFILES mathlib68k"
QFILES="$QFILES $ASFILES"

cd "$GLSRC"
for f in aclip context dlist draw fog glu hclip init light matrix others texture \
         vertexarray vertexbuffer_min vertexelements viewport; do
  echo "=== MiniGLV3D gl/src (-O2+finline): $f.c ==="
  "$GCC" -std=c99 -O2 $OPTFLAGS -mcpu=68020 -m68881 -mcrt=clib2 -DMGLV3D_NO_LOGGING $INCFLAGS \
      -c "$f.c" -o "$OUT_M/$f.o"
done
cd "$HWSRC"
for f in v3d_assembler v3d_clbuf v3d_commands v3d_context v3d_device v3d_frame v3d_hw \
         v3d_mem_allocvec v3d_submit_timeout v3d_texture; do
  echo "=== MiniGLV3D backend/hw (-O2+finline): $f.c ==="
  "$GCC" -std=c99 -O2 $OPTFLAGS -mcpu=68020 -m68881 -mcrt=clib2 -DMGLV3D_NO_LOGGING $INCFLAGS -c "$f.c" -o "$OUT_M/$f.o"
done
echo "=== MiniGLV3D backend/hw (-O2+finline): v3d_debug.c (-DDEBUG, real kprintf body needed at link time) ==="
"$GCC" -std=c99 -O2 $OPTFLAGS -mcpu=68020 -m68881 -mcrt=clib2 -DDEBUG $INCFLAGS -c v3d_debug.c -o "$OUT_M/v3d_debug.o"

# mglv3d_log() CONSOLIDATED (2026-09-02): glquake used to carry its own
# inline real implementation in gl_vidamiga.c (gated by MGLV3D_NO_LOGGING).
# Replaced with the same swappable-source convention Quake2/ioq3 already
# used -- mglv3d_log_noop.c here (showcase, no log output); swap to
# mglv3d_log_progdir.c (MiniGLV3D/demos/, writes PROGDIR:mglv3d_debug.log)
# for a debug build. See reference_mglv3d_log_progdir.md.
cd /mnt/d/v3d_driver/MiniGLV3D/demos
echo "=== MiniGLV3D demos (mglv3d_log_noop.c) ==="
"$GCC" -std=c99 -O2 $OPTFLAGS -mcpu=68020 -m68881 -mcrt=clib2 $INCFLAGS -c mglv3d_log_noop.c -o "$OUT_M/mglv3d_log_noop.o"

MFILES="v3d_device v3d_mem_allocvec v3d_hw v3d_commands v3d_debug v3d_assembler v3d_clbuf \
v3d_context v3d_frame v3d_submit_timeout v3d_texture aclip context dlist \
draw fog glu hclip init light matrix others texture vertexarray vertexbuffer_min vertexelements viewport \
mglv3d_log_noop"

echo "=== LINK ==="
QOBJS=""; for f in $QFILES; do QOBJS="$QOBJS $OUT_Q/$f.o"; done
MOBJS=""; for f in $MFILES; do MOBJS="$MOBJS $OUT_M/$f.o"; done
OUT_EXE="/mnt/d/v3d_driver/glquake/build_output/glquake_v${GLQUAKE_VER}"
"$GCC" -mcpu=68020 -m68881 -mcrt=clib2 -o "$OUT_EXE" $QOBJS $MOBJS \
    -L"$HOME/amiga-gcc-install/m68k-amigaos/clib2/lib/libm020" -lm881 -lamiga
echo "LINK exit=$? -- output: $OUT_EXE"
