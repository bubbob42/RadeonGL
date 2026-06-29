PREFIX ?= $(HOME)/amiga-arm

GCC_CC      := $(PREFIX)/bin/m68k-amigaos-gcc
GCC_STRIP   := $(PREFIX)/bin/m68k-amigaos-strip
GCC_OBJDUMP := $(PREFIX)/bin/m68k-amigaos-objdump

VAMOS      ?= $(HOME)/Developer/AmigaTools/venv-amitools/bin/vamos
VAMOSRC    := .vamosrc

SASC_PATH  ?= $(HOME)/Developer/AmigaSDK/SASC/SC

TARGET_GCC  := dist/triangle_gcc
TARGET_SASC := dist/triangle_sasc

SRC_GCC  := src/main.c
SRC_SASC := src/main_sasc.c
OBJ_SASC := build/main_sasc.o

CPUFLAGS ?= -m68000

GCC_CFLAGS  := -Wall -Wextra -Werror=implicit-function-declaration -Os -std=gnu99 $(CPUFLAGS)
GCC_LDFLAGS := -noixemul -lamiga

.PHONY: all gcc sasc run-sasc info check clean distclean

all: gcc sasc

gcc: $(TARGET_GCC)

$(TARGET_GCC): $(SRC_GCC)
	mkdir -p dist
	$(GCC_CC) $(GCC_CFLAGS) $(SRC_GCC) -o $(TARGET_GCC) $(GCC_LDFLAGS)

sasc: $(TARGET_SASC)

$(TARGET_SASC): $(SRC_SASC) $(VAMOSRC)
	mkdir -p build dist
	$(VAMOS) -c $(VAMOSRC) -q sc NOLINK IDIR=include: OBJNAME=proj:$(OBJ_SASC) proj:$(SRC_SASC)
	$(VAMOS) -c $(VAMOSRC) -q slink FROM lib:c.o proj:$(OBJ_SASC) TO proj:$(TARGET_SASC) LIB lib:sc.lib lib:amiga.lib

$(VAMOSRC): .vamosrc.in
	sed \
		-e 's|@SASC_PATH@|$(SASC_PATH)|g' \
		-e 's|@PROJECT_PATH@|$(CURDIR)|g' \
		.vamosrc.in > $(VAMOSRC)

run-sasc: $(TARGET_SASC)
	$(VAMOS) -c $(VAMOSRC) -q proj:$(TARGET_SASC)

info: all
	file $(TARGET_GCC)
	file $(TARGET_SASC)
	$(GCC_OBJDUMP) -f $(TARGET_GCC)

check:
	@echo "Project:   $(CURDIR)"
	@echo "GCC:       $(GCC_CC)"
	@echo "VAMOS:     $(VAMOS)"
	@echo "SASC_PATH: $(SASC_PATH)"
	@echo "VAMOSRC:   $(VAMOSRC)"
	@test -x "$(GCC_CC)" || (echo "Missing GCC: $(GCC_CC)" && exit 1)
	@test -x "$(GCC_STRIP)" || (echo "Missing strip: $(GCC_STRIP)" && exit 1)
	@test -x "$(GCC_OBJDUMP)" || (echo "Missing objdump: $(GCC_OBJDUMP)" && exit 1)
	@test -x "$(VAMOS)" || (echo "Missing vamos: $(VAMOS)" && exit 1)
	@test -d "$(SASC_PATH)" || (echo "Missing SAS/C path: $(SASC_PATH)" && exit 1)
	@test -f ".vamosrc.in" || (echo "Missing .vamosrc.in" && exit 1)

clean:
	rm -rf build dist

distclean: clean
	rm -f $(VAMOSRC)