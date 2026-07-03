PROJECT_NAME := $(notdir $(CURDIR))

FSUAE ?= /opt/homebrew/bin/fs-uae

FSUAE_TEMPLATE := fsuae/project.fs-uae.in
FSUAE_CONFIG   := fsuae/local/$(PROJECT_NAME).fs-uae

KICKSTART_FILE ?= $(HOME)/Developer/AmigaEmu/ROMs/kick32.rom
SYSTEM_HDF     ?= $(HOME)/Developer/AmigaEmu/HDF/AmigaOS32.hdf
PROJECT_DIST   := $(CURDIR)/dist

PREFIX ?= $(HOME)/amiga-arm

GCC_CC      := $(PREFIX)/bin/m68k-amigaos-gcc
GCC_STRIP   := $(PREFIX)/bin/m68k-amigaos-strip
GCC_OBJDUMP := $(PREFIX)/bin/m68k-amigaos-objdump

VAMOS   ?= $(HOME)/Developer/AmigaTools/venv-amitools/bin/vamos
VAMOSRC := .vamosrc

# -m 16128: vamos's default RAM size is too small to compile larger source
# files with sc (observed as a vamos-internal "NO MEMORY" error while sc
# loads its own support libraries, not a problem with the source itself).
# -H disable is required alongside the larger RAM size -- vamos refuses
# "too much RAM allocated with hw access enabled" otherwise. Give both the
# compile and link steps the same generous allowance.
VAMOS_SC    := $(VAMOS) -c $(VAMOSRC) -q -H disable -m 16128
VAMOS_SLINK := $(VAMOS) -c $(VAMOSRC) -q -H disable -m 16128

# NOSTACKCHECK: SAS/C inserts a per-function stack-overflow check that calls
# an overflow handler (__XCOVF) when it thinks the stack is low. That handler
# is unsafe when the function is a callback invoked by the OS -- most notably
# a BOOPSI/ReAction class dispatcher called by Intuition -- where it corrupts
# the task's user stack (seen as an exec address-error crash). Programs that
# install such callbacks should turn the check off and instead guarantee an
# adequate stack with a "$STACK: <bytes>" cookie (see README). Harmless for
# plain CLI tools. Applied to every sc compile below.
SC_OPTS := NOSTACKCHECK

SASC_PATH ?= $(HOME)/Developer/AmigaSDK/SASC/SC

TARGET_GCC  := dist/$(PROJECT_NAME)_gcc
TARGET_SASC := dist/$(PROJECT_NAME)_sasc

SRC_GCC  := src/main.c
SRC_SASC := src/main_sasc.c
OBJ_SASC := build/main_sasc.o

CPUFLAGS ?= -m68000

GCC_WARNINGS := -Wall -Wextra -Wno-pointer-sign -Werror=implicit-function-declaration
GCC_CFLAGS   := $(GCC_WARNINGS) -Os -std=gnu99 $(CPUFLAGS)
GCC_LDFLAGS  := -noixemul -lamiga

.PHONY: all gcc sasc run-sasc fsuae-config launch-fsuae run-fsuae run-fsuae-gcc run-fsuae-sasc info check clean distclean mirror-src

all: gcc sasc dist/runme dist/mutools mirror-src

gcc: $(TARGET_GCC)

$(TARGET_GCC): $(SRC_GCC)
	mkdir -p dist
	$(GCC_CC) $(GCC_CFLAGS) $(SRC_GCC) -o $(TARGET_GCC) $(GCC_LDFLAGS)

sasc: $(TARGET_SASC)

# -H disable -m 16128: works around a vamos/slink interaction where slink
# transiently dereferences an out-of-bounds pointer while resolving a large
# amiga.lib symbol set. Real hardware tolerates this silently; vamos's default
# strict memory map does not. See the VAMOS_SC/VAMOS_SLINK definitions above
# for why both the compile and link steps need this.
$(TARGET_SASC): $(SRC_SASC) $(VAMOSRC)
	mkdir -p build dist
	$(VAMOS_SC) sc $(SC_OPTS) NOLINK IDIR=include: OBJNAME=proj:$(OBJ_SASC) proj:$(SRC_SASC)
	$(VAMOS_SLINK) slink FROM lib:c.o proj:$(OBJ_SASC) TO proj:$(TARGET_SASC) LIB lib:sc.lib lib:amiga.lib

$(VAMOSRC): .vamosrc.in
	sed \
		-e 's|@SASC_PATH@|$(SASC_PATH)|g' \
		-e 's|@PROJECT_PATH@|$(CURDIR)|g' \
		.vamosrc.in > $(VAMOSRC)

dist/run-gcc: $(TARGET_GCC)
	mkdir -p dist
	printf 'DEV:%s\n' "$(notdir $(TARGET_GCC))" > dist/run-gcc

dist/run-sasc: $(TARGET_SASC)
	mkdir -p dist
	printf 'DEV:%s\n' "$(notdir $(TARGET_SASC))" > dist/run-sasc

dist/runme: dist/run-gcc dist/run-sasc
	mkdir -p dist
	printf 'echo "Available binaries on DEV:"\n' > dist/runme
	printf 'list DEV:\n' >> dist/runme
	printf 'echo ""\n' >> dist/runme
	printf 'echo "Run GCC build:"\n' >> dist/runme
	printf 'echo "  DEV:%s"\n' "$(notdir $(TARGET_GCC))" >> dist/runme
	printf 'echo "Run SAS/C build:"\n' >> dist/runme
	printf 'echo "  DEV:%s"\n' "$(notdir $(TARGET_SASC))" >> dist/runme
	printf 'echo ""\n' >> dist/runme
	printf 'echo "Or execute helper scripts:"\n' >> dist/runme
	printf 'echo "  execute DEV:run-gcc"\n' >> dist/runme
	printf 'echo "  execute DEV:run-sasc"\n' >> dist/runme

# MuForce/MuGuardianAngel debug-session starter (run via "execute DEV:mutools"
# inside AmigaOS). Static file, copied verbatim -- not templated.
dist/mutools: fsuae/mutools
	mkdir -p dist
	cp fsuae/mutools dist/mutools

# Mirror all sources into dist/src (DEV:src inside AmigaOS), along with
# an smakefile (SAS/C smake) and a Makefile (Amiga make ports) generated
# from the .in templates in src/, so the project can also be compiled
# and debugged natively on the Amiga. The mirror is one-way: host wins.
MIRROR_SRC := $(wildcard src/*.c) $(wildcard src/*.h)

mirror-src:
	mkdir -p dist/src
	cp $(MIRROR_SRC) dist/src/
	sed -e 's|@PROJECT_NAME@|$(PROJECT_NAME)|g' src/smakefile.in > dist/src/smakefile
	sed -e 's|@PROJECT_NAME@|$(PROJECT_NAME)|g' src/Makefile.in > dist/src/Makefile

run-sasc: $(TARGET_SASC)
	$(VAMOS) -c $(VAMOSRC) -q proj:$(TARGET_SASC)

fsuae-config: $(FSUAE_CONFIG)

$(FSUAE_CONFIG): $(FSUAE_TEMPLATE)
	mkdir -p fsuae/local
	sed \
		-e 's|@KICKSTART_FILE@|$(KICKSTART_FILE)|g' \
		-e 's|@SYSTEM_HDF@|$(SYSTEM_HDF)|g' \
		-e 's|@PROJECT_DIST@|$(PROJECT_DIST)|g' \
		-e 's|@PROJECT_NAME@|$(PROJECT_NAME)|g' \
		$(FSUAE_TEMPLATE) > $(FSUAE_CONFIG)

launch-fsuae: $(FSUAE_CONFIG)
	@echo "Starting FS-UAE with config: $(abspath $(FSUAE_CONFIG))"
	@echo "FS-UAE binary: $(FSUAE)"
	@test -f "$(FSUAE_CONFIG)" || (echo "Missing FS-UAE config: $(FSUAE_CONFIG)" && exit 1)
	@CFG="$(abspath $(FSUAE_CONFIG))"; \
	"$(FSUAE)" "$$CFG" & \
	FSUAE_PID=$$!; \
	sleep 1; \
	osascript -e 'tell application "System Events" to set frontmost of the first process whose unix id is '"$$FSUAE_PID"' to true' || true

run-fsuae: all fsuae-config launch-fsuae

run-fsuae-gcc: gcc dist/run-gcc dist/mutools mirror-src fsuae-config launch-fsuae

run-fsuae-sasc: sasc dist/run-sasc dist/mutools mirror-src fsuae-config launch-fsuae

info: all
	file $(TARGET_GCC)
	file $(TARGET_SASC)
	$(GCC_OBJDUMP) -f $(TARGET_GCC)

check:
	@echo "Project:        $(CURDIR)"
	@echo "PROJECT_NAME:   $(PROJECT_NAME)"
	@echo "GCC:            $(GCC_CC)"
	@echo "VAMOS:          $(VAMOS)"
	@echo "SASC_PATH:      $(SASC_PATH)"
	@echo "FSUAE:          $(FSUAE)"
	@echo "FSUAE_CONFIG:   $(FSUAE_CONFIG)"
	@echo "KICKSTART_FILE: $(KICKSTART_FILE)"
	@echo "SYSTEM_HDF:     $(SYSTEM_HDF)"
	@test -x "$(GCC_CC)" || (echo "Missing GCC: $(GCC_CC)" && exit 1)
	@test -x "$(GCC_STRIP)" || (echo "Missing strip: $(GCC_STRIP)" && exit 1)
	@test -x "$(GCC_OBJDUMP)" || (echo "Missing objdump: $(GCC_OBJDUMP)" && exit 1)
	@test -x "$(VAMOS)" || (echo "Missing vamos: $(VAMOS)" && exit 1)
	@test -d "$(SASC_PATH)" || (echo "Missing SAS/C path: $(SASC_PATH)" && exit 1)
	@test -x "$(FSUAE)" || (echo "Missing FS-UAE: $(FSUAE)" && exit 1)
	@test -f "$(KICKSTART_FILE)" || (echo "Missing Kickstart ROM: $(KICKSTART_FILE)" && exit 1)
	@test -f "$(SYSTEM_HDF)" || (echo "Missing AmigaOS HDF: $(SYSTEM_HDF)" && exit 1)
	@test -f ".vamosrc.in" || (echo "Missing .vamosrc.in" && exit 1)
	@test -f "$(FSUAE_TEMPLATE)" || (echo "Missing FS-UAE template: $(FSUAE_TEMPLATE)" && exit 1)

clean:
	rm -rf build dist

distclean: clean
	rm -f $(VAMOSRC)
	rm -rf fsuae/local