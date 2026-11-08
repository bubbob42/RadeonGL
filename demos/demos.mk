# --- The demonstrators (demos/README.md) ---------------------------------------
# Spinner, Glow, Landscape and Showcase: small original programs that show the
# library off, for the release package. Trio starts three of them at once,
# each in a window of its own on the Workbench (WINDOW LEFT TOP). GCC only, as GLSmoke: they link the
# MiniGL SDK's import library (a GCC archive). Each program is demo.c (the
# frame loop), its scene(s) and its main_<name>.c (the scene list, $VER).
#
#   gmake demos            dist/Demos/<Name>, stripped, checked for 68060 FPU
#                          instructions (tools/check_fpu060.py)
#   demos/scripts/<Name>   the IconX launchers the package copies beside a
#                          C/ drawer holding the programs
#   gmake autotest T=DemoSmoke   every program on the null card, checker on
#                          (needs gmake demos rgl-lib)
DEMO_NAMES  := Spinner Glow Landscape Showcase Trio
DEMO_BIN    := $(addprefix dist/Demos/,$(DEMO_NAMES))
DEMO_CFLAGS := -std=gnu99 -noixemul -m68060 -m68881 -O2 -Wall -Wextra -Wno-pointer-sign \
               -I$(MGL_SDK_INC) -Idemos
DEMO_LIBS   := -Lsdk/minigl-29.1/lib -lminigl -lm -lamiga

build/demos/%.o: demos/%.c demos/demo.h demos/demos.mk
	@mkdir -p build/demos
	$(GCC_CC) $(DEMO_CFLAGS) -c $< -o $@

# linked stripped (-s at the link, never strip -o: hunk trouble, GLQuake);
# then no FPU instruction the 68060 would trap
define DEMO_LINK
	@mkdir -p dist/Demos
	$(GCC_CC) -noixemul -m68060 -m68881 -s -o $@ $^ $(DEMO_LIBS)
	GCC_OBJDUMP=$(GCC_OBJDUMP) $(PYTHON) -I tools/check_fpu060.py $@
endef

dist/Demos/Spinner: build/demos/demo.o build/demos/scene_spinner.o build/demos/main_spinner.o
	$(DEMO_LINK)

dist/Demos/Glow: build/demos/demo.o build/demos/scene_glow.o build/demos/main_glow.o
	$(DEMO_LINK)

dist/Demos/Landscape: build/demos/demo.o build/demos/scene_landscape.o build/demos/main_landscape.o
	$(DEMO_LINK)

dist/Demos/Showcase: build/demos/demo.o build/demos/scene_spinner.o build/demos/scene_glow.o \
                     build/demos/scene_landscape.o build/demos/main_showcase.o
	$(DEMO_LINK)

# Trio: no minigl.library of its own, only the three programs above
dist/Demos/Trio: build/demos/trio.o
	$(DEMO_LINK)

# The same programs for the 68020/68030 library (with a 68881/68882): the
# builds above hold 68040 FPU instructions (tools/check_fpu881.py). The
# Installer copies these over Demos/C when that library is chosen.
DEMO030_BIN    := $(addprefix dist/For030/Demos/C/,$(DEMO_NAMES))
DEMO030_CFLAGS := $(subst -m68060,-m68030,$(DEMO_CFLAGS)) -DRGL_FOR_030

build/demos030/%.o: demos/%.c demos/demo.h demos/demos.mk
	@mkdir -p build/demos030
	$(GCC_CC) $(DEMO030_CFLAGS) -c $< -o $@

define DEMO030_LINK
	@mkdir -p dist/For030/Demos/C
	$(GCC_CC) -noixemul -m68030 -m68881 -s -o $@ $^ $(DEMO_LIBS)
	GCC_OBJDUMP=$(GCC_OBJDUMP) $(PYTHON) -I tools/check_fpu881.py $@
endef

dist/For030/Demos/C/Spinner: build/demos030/demo.o build/demos030/scene_spinner.o build/demos030/main_spinner.o
	$(DEMO030_LINK)

dist/For030/Demos/C/Glow: build/demos030/demo.o build/demos030/scene_glow.o build/demos030/main_glow.o
	$(DEMO030_LINK)

dist/For030/Demos/C/Landscape: build/demos030/demo.o build/demos030/scene_landscape.o \
                               build/demos030/main_landscape.o
	$(DEMO030_LINK)

dist/For030/Demos/C/Showcase: build/demos030/demo.o build/demos030/scene_spinner.o build/demos030/scene_glow.o \
                              build/demos030/scene_landscape.o build/demos030/main_showcase.o
	$(DEMO030_LINK)

dist/For030/Demos/C/Trio: build/demos030/trio.o
	$(DEMO030_LINK)

.PHONY: demos
demos: $(DEMO_BIN) $(DEMO030_BIN)
	@ls -l $(DEMO_BIN) $(DEMO030_BIN)
