FSUAE        ?= /opt/homebrew/bin/fs-uae
FSUAE_CONFIG := fsuae/local/starter.fs-uae

KICKSTART_FILE ?= $(HOME)/Developer/AmigaEmu/ROMs/kick32.rom
SYSTEM_HDF     ?= $(HOME)/Developer/AmigaEmu/HDF/AmigaOS32.hdf
PROJECT_DIST   := $(CURDIR)/dist

PREFIX ?= $(HOME)/amiga-arm

GCC_CC      := $(PREFIX)/bin/m68k-amigaos-gcc
GCC_STRIP   := $(PREFIX)/bin/m68k-amigaos-strip
GCC_OBJDUMP := $(PREFIX)/bin/m68k-amigaos-objdump

VAMOS      ?= $(HOME)/Developer/AmigaTools/venv-amitools/bin/vamos
VAMOSRC    := .vamosrc

VAMOS_SC    := $(VAMOS) -c $(VAMOSRC) -q
VAMOS_SLINK := $(VAMOS) -c $(VAMOSRC) -q -H disable -m 16128

SASC_PATH  ?= $(HOME)/Developer/AmigaSDK/SASC/SC

PROJECT_NAME := $(notdir $(CURDIR))

TARGET_GCC  := dist/$(PROJECT_NAME)_gcc
TARGET_SASC := dist/$(PROJECT_NAME)_sasc

SRC_GCC  := src/main.c src/app.c
SRC_SASC := src/main_sasc.c
SRC_APP  := src/app.c
OBJ_SASC := build/main_sasc.o

CPUFLAGS ?= -m68000

GCC_WARNINGS := -Wall -Wextra -Wno-pointer-sign -Werror=implicit-function-declaration
GCC_CFLAGS  := $(GCC_WARNINGS) -Os -std=gnu99 $(CPUFLAGS)
GCC_LDFLAGS := -noixemul -lamiga

.PHONY: all gcc sasc run-sasc run-fsuae run-fsuae-gcc run-fsuae-sasc fsuae-config info check clean distclean

all: gcc sasc dist/runme

gcc: $(TARGET_GCC)

$(TARGET_GCC): $(SRC_GCC)
	mkdir -p dist
	$(GCC_CC) $(GCC_CFLAGS) $(SRC_GCC) -o $(TARGET_GCC) $(GCC_LDFLAGS)

sasc: $(TARGET_SASC)

# -H disable -m 16128: works around a vamos/slink interaction where slink
# transiently dereferences an out-of-bounds pointer while resolving a large
# amiga.lib symbol set; real hardware tolerates this silently, vamos's
# default strict memory map does not. Widening the mapped RAM lets slink
# read garbage instead of trapping, same as it would on real hardware.
$(TARGET_SASC): $(SRC_SASC) $(SRC_APP) $(VAMOSRC)
	mkdir -p build dist
	$(VAMOS_SC) sc NOLINK IDIR=include: OBJNAME=proj:$(OBJ_SASC) proj:$(SRC_SASC)
	$(VAMOS_SLINK) slink FROM lib:c.o proj:$(OBJ_SASC) TO proj:$(TARGET_SASC) LIB lib:sc.lib lib:amiga.lib
$(VAMOSRC): .vamosrc.in
	sed \
		-e 's|@SASC_PATH@|$(SASC_PATH)|g' \
		-e 's|@PROJECT_PATH@|$(CURDIR)|g' \
		.vamosrc.in > $(VAMOSRC)

dist/runme: $(TARGET_GCC) $(TARGET_SASC)
	mkdir -p dist
	printf 'echo "Available binaries on DEV:"\n' > dist/runme
	printf 'list DEV:\n' >> dist/runme
	printf 'echo ""\n' >> dist/runme
	printf 'echo "Run GCC build:"\n' >> dist/runme
	printf 'echo "  DEV:%s_gcc"\n' "$(PROJECT_NAME)" >> dist/runme
	printf 'echo "Run SAS/C build:"\n' >> dist/runme
	printf 'echo "  DEV:%s_sasc"\n' "$(PROJECT_NAME)" >> dist/runme
	printf 'echo ""\n' >> dist/runme
	printf 'echo "Or execute helper scripts:"\n' >> dist/runme
	printf 'echo "  execute DEV:run-gcc"\n' >> dist/runme
	printf 'echo "  execute DEV:run-sasc"\n' >> dist/runme
	printf 'DEV:%s_gcc\n' "$(PROJECT_NAME)" > dist/run-gcc
	printf 'DEV:%s_sasc\n' "$(PROJECT_NAME)" > dist/run-sasc

run-sasc: $(TARGET_SASC)
	$(VAMOS) -c $(VAMOSRC) -q proj:$(TARGET_SASC)

fsuae-config: $(FSUAE_CONFIG)

$(FSUAE_CONFIG): fsuae/starter.fs-uae.in
	mkdir -p fsuae/local
	sed \
		-e 's|@KICKSTART_FILE@|$(KICKSTART_FILE)|g' \
		-e 's|@SYSTEM_HDF@|$(SYSTEM_HDF)|g' \
		-e 's|@PROJECT_DIST@|$(PROJECT_DIST)|g' \
		fsuae/starter.fs-uae.in > $(FSUAE_CONFIG)

run-fsuae: all fsuae-config
	$(FSUAE) $(FSUAE_CONFIG)

run-fsuae-gcc: gcc dist/runme fsuae-config
	$(FSUAE) $(FSUAE_CONFIG)

run-fsuae-sasc: sasc dist/runme fsuae-config
	$(FSUAE) $(FSUAE_CONFIG)

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
	@test -f "fsuae/starter.fs-uae.in" || (echo "Missing fsuae/starter.fs-uae.in" && exit 1)

clean:
	rm -rf build dist

distclean: clean
	rm -f $(VAMOSRC)
	rm -rf fsuae/local
