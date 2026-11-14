# --- GLBench: the stress benchmark (glbench/, docs/concept-benchmark.md) -------
# One program, seven scenes, its own archive (the user, 2026-11-08). GCC
# only, as the demos: it links the MiniGL SDK's import library. Two builds:
# the 68040/68060 one (C/GLBench) and the 68020/68030 one with a 68881/68882
# (C/GLBench030); the launchers pick by CPU.
#
#   gmake glbench           dist/GLBench/C/GLBench + GLBench030, stripped,
#                           checked for FPU instructions the CPU would trap
#   gmake glbench-archive   build/GLBench.lha: the drawer GLBench (launchers,
#                           C/, the guide, the readme)
#   gmake autotest T=GLBenchSmoke   every scene a few frames on the null card

GLBENCH_MODS   := bench util scene_pelt scene_lanterns scene_chrome scene_embers scene_plasma \
                  scene_swarm scene_mirror
GLBENCH_CFLAGS := -std=gnu99 -noixemul -m68060 -m68881 -O2 -Wall -Wextra -Wno-pointer-sign \
                  -I$(MGL_SDK_INC) -Iglbench
GLBENCH_LIBS   := -Lsdk/minigl-29.1/lib -lminigl -lm -lamiga
GLBENCH_BIN    := dist/GLBench/C/GLBench
GLBENCH030_BIN := dist/GLBench/C/GLBench030
GLBENCH030_CFLAGS := $(subst -m68060,-m68030,$(GLBENCH_CFLAGS)) -DRGL_FOR_030

build/glbench/%.o: glbench/%.c glbench/bench.h glbench/glbench.mk
	@mkdir -p build/glbench
	$(GCC_CC) $(GLBENCH_CFLAGS) -c $< -o $@

build/glbench030/%.o: glbench/%.c glbench/bench.h glbench/glbench.mk
	@mkdir -p build/glbench030
	$(GCC_CC) $(GLBENCH030_CFLAGS) -c $< -o $@

$(GLBENCH_BIN): $(addprefix build/glbench/,$(addsuffix .o,$(GLBENCH_MODS)))
	@mkdir -p dist/GLBench/C
	$(GCC_CC) -noixemul -m68060 -m68881 -s -o $@ $^ $(GLBENCH_LIBS)
	GCC_OBJDUMP=$(GCC_OBJDUMP) $(PYTHON) -I tools/check_fpu060.py $@

$(GLBENCH030_BIN): $(addprefix build/glbench030/,$(addsuffix .o,$(GLBENCH_MODS)))
	@mkdir -p dist/GLBench/C
	$(GCC_CC) -noixemul -m68030 -m68881 -s -o $@ $^ $(GLBENCH_LIBS)
	GCC_OBJDUMP=$(GCC_OBJDUMP) $(PYTHON) -I tools/check_fpu881.py $@

.PHONY: glbench
glbench: $(GLBENCH_BIN) $(GLBENCH030_BIN)
	@ls -l $(GLBENCH_BIN) $(GLBENCH030_BIN)

# The archive: one drawer GLBench, what a user opens in its window (the
# guide, the two launchers, the readme), the programs in C/ and the sources
# in Source/ (no icons). Amiga LhA through vamos, as release-archive; the
# readme beside it for Aminet.
GLB_STAGE := build/glbench-stage
GLB_DIR   := $(GLB_STAGE)/GLBench

.PHONY: glbench-archive
glbench-archive: glbench
	rm -rf $(GLB_STAGE) dist/GLBench.lha dist/GLBench.readme
	mkdir -p $(GLB_DIR)/C $(GLB_DIR)/Source/scripts
	cp $(GLBENCH_BIN) $(GLBENCH030_BIN) $(GLB_DIR)/C/
	cp glbench/scripts/GLBench glbench/scripts/GLBench-Burn $(GLB_DIR)/
	cp icons/glbench.info $(GLB_DIR)/GLBench.info
	cp icons/glbench-burn.info $(GLB_DIR)/GLBench-Burn.info
	cp glbench/GLBench.guide glbench/GLBench.readme $(GLB_DIR)/
	cp icons/guide.info $(GLB_DIR)/GLBench.guide.info
	cp icons/readme.info $(GLB_DIR)/GLBench.readme.info
	cp glbench/*.c glbench/*.h glbench/glbench.mk tools/make_glbench_icon.py $(GLB_DIR)/Source/
	cp glbench/scripts/* $(GLB_DIR)/Source/scripts/
	cp $(REL_DRAWER_ICON) $(GLB_STAGE)/GLBench.info
	chmod -R u+w $(GLB_STAGE)
	$(SETPOS) $(GLB_DIR)/GLBench.guide.info --center-x 60 8
	$(SETPOS) $(GLB_DIR)/GLBench.info --center-x 160 8
	$(SETPOS) $(GLB_DIR)/GLBench-Burn.info --center-x 260 8
	$(SETPOS) $(GLB_DIR)/GLBench.readme.info --center-x 360 8
	$(SETPOS) $(GLB_STAGE)/GLBench.info --no-pos --window 120 50 430 90 $(REL_DRAWER_WIN)
	@find $(GLB_STAGE) \( -name '._*' -o -name '.DS_Store' \) -delete
	$(PYTHON) tools/make_aminet_readme.py --check $(GLB_DIR)/GLBench.readme
	cd $(GLB_STAGE) && $(VAMOS_LHA) $(AMIGA_LHA) -r a proj:dist/GLBench.lha "#?" >/dev/null
	$(PYTHON) -I tools/check_lha_bits.py dist/GLBench.lha `find $(GLB_STAGE) -type f | wc -l`
	cp glbench/GLBench.readme dist/GLBench.readme
	@ls -l dist/GLBench.lha dist/GLBench.readme
