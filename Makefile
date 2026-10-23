# aitd - a reimplementation of the Alone in the Dark engine
#
# The engine is plain C99. Each host has a backend in src/platforms/ and a
# target here; every target builds the same sources (sources.mk).
#
#   make               the host's own build: macOS or Linux (./aitd)
#   make linux         Linux, X11 and ALSA (also the Steam Deck)
#   make windows       Windows, 64 bit, with MinGW-w64
#   make web           the browser, with Emscripten
#   make dos           MS-DOS, 32 bit protected mode, with Open Watcom
#   make win9x         Windows 95 and 98, with Open Watcom
#   make psp           PlayStation Portable, with the pspdev toolchain
#   make vita          PlayStation Vita, with VitaSDK
#   make dist          every target that builds here, packaged in dist/
#
# The version comes from the VERSION file; CHANGELOG.md lists the changes.

include sources.mk

VERSION  := $(shell cat VERSION 2>/dev/null || echo dev)
TARGET   := aitd
SRCDIR   := src
BUILD    := build
DIST     := dist

HOST := $(shell uname -s)
ifeq ($(HOST),Darwin)
PLATFORM ?= macos
else
PLATFORM ?= linux
endif

WARN     := -Wall -Wextra -Wno-unused-parameter
DEFS     := -DAITD_VERSION='"$(VERSION)"'

ifeq ($(PLATFORM),macos)
CC        := clang
CFLAGS    := $(DEFS) -std=c99 $(WARN) -g -O1 -I$(SRCDIR)
OBJCFLAGS := $(DEFS) -fobjc-arc $(WARN) -g -O1 -I$(SRCDIR)
HOST_SRC  := src/platforms/macos.m
LDFLAGS   := -framework AppKit -framework CoreGraphics -framework AudioToolbox -framework QuartzCore
else
CC        ?= cc
CFLAGS    := $(DEFS) -std=c99 $(WARN) -g -O2 -I$(SRCDIR)
HOST_SRC  := src/platforms/linux.c $(SOUNDFONT_SRC) $(PAD_SRC)
LDFLAGS   := -lX11 -lasound -lpthread -lm
endif

OBJ := $(patsubst src/%.c,$(BUILD)/%.o,$(patsubst src/%.m,$(BUILD)/%.o,$(ENGINE_SRC) $(HOST_SRC)))

.PHONY: all clean run linux

all: $(TARGET)

# Linux builds natively on Linux, and in a Docker container elsewhere.
LINUX_OUT := $(BUILD)/linux/$(TARGET)
linux:
ifeq ($(HOST),Linux)
	$(MAKE) PLATFORM=linux BUILD=$(BUILD)/linux TARGET=$(LINUX_OUT)
else
	docker build -q -t aitd-linux platforms/linux
	docker run --rm -v "$(CURDIR)":/src -w /src aitd-linux \
	    make PLATFORM=linux BUILD=$(BUILD)/linux TARGET=$(LINUX_OUT)
endif

$(TARGET): $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $@

$(BUILD)/%.o: $(SRCDIR)/%.c $(wildcard $(SRCDIR)/*.h $(SRCDIR)/platforms/*.h) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRCDIR)/%.m | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(OBJCFLAGS) -c $< -o $@

# Windows, 64 bit, cross-compiled with MinGW-w64.
MINGW      ?= x86_64-w64-mingw32-gcc
WINDRES    ?= x86_64-w64-mingw32-windres
WIN_BUILD  := $(BUILD)/windows
WIN_SRC    := $(ENGINE_SRC) src/platforms/windows.c $(PAD_SRC)
WIN_OBJ    := $(patsubst src/%.c,$(WIN_BUILD)/%.o,$(WIN_SRC))
WIN_EXE    := $(WIN_BUILD)/aitd.exe

.PHONY: windows
windows: $(WIN_EXE)

$(WIN_EXE): $(WIN_OBJ)
	$(MINGW) $(WIN_OBJ) -mwindows -static -lwinmm -o $@

$(WIN_BUILD)/%.o: src/%.c $(wildcard src/*.h src/platforms/*.h) VERSION
	@mkdir -p $(dir $@)
	$(MINGW) $(DEFS) -std=c99 $(WARN) -O2 -Isrc -c $< -o $@

# Windows 95 and 98, and DOS, with Open Watcom (32 bit, flat model).
WATCOM     ?= $(HOME)/watcom
ifeq ($(HOST),Darwin)
WATCOM_BIN := $(WATCOM)/bino64
else
WATCOM_BIN := $(WATCOM)/binl64
endif
WCC        := WATCOM=$(WATCOM) $(WATCOM_BIN)/wcc386
WLINK      := WATCOM=$(WATCOM) $(WATCOM_BIN)/wlink
WCC_FLAGS  := -za99 -6s -otexan -zp4 -zq -wcd=201 -DAITD_VERSION='"$(VERSION)"' -i=src -i=$(WATCOM)/h

W9X_BUILD  := $(BUILD)/win9x
W9X_SRC    := $(ENGINE_SRC) src/platforms/windows.c $(PAD_SRC)
W9X_OBJ    := $(patsubst src/%.c,$(W9X_BUILD)/%.obj,$(W9X_SRC))
W9X_EXE    := $(W9X_BUILD)/aitd.exe

.PHONY: win9x
win9x: $(W9X_EXE)

$(W9X_EXE): $(W9X_OBJ)
	$(WLINK) option quiet system nt_win name $@ \
	    libpath $(WATCOM)/lib386 libpath $(WATCOM)/lib386/nt \
	    library user32.lib,gdi32.lib,winmm.lib \
	    $(foreach o,$(W9X_OBJ),file $(o))

$(W9X_BUILD)/%.obj: src/%.c $(wildcard src/*.h src/platforms/*.h) VERSION
	@mkdir -p $(dir $@)
	$(WCC) -bt=nt -DAITD_WIN9X -i=$(WATCOM)/h/nt $(WCC_FLAGS) $< -fo=$@

# MS-DOS: a DOS/4G-style LE program with the DOS/32A stub, which loads
# dos32a.exe from the program's folder.
DOS_BUILD  := $(BUILD)/dos
DOS_SRC    := $(ENGINE_SRC) src/platforms/dos.c
DOS_OBJ    := $(patsubst src/%.c,$(DOS_BUILD)/%.obj,$(DOS_SRC))
DOS_EXE    := $(DOS_BUILD)/aitd.exe

.PHONY: dos
dos: $(DOS_EXE)

$(DOS_EXE): $(DOS_OBJ)
	$(WLINK) option quiet system dos4g option stub=$(WATCOM)/binw/stub32a.exe \
	    option stack=256k name $@ $(foreach o,$(DOS_OBJ),file $(o))
	cp $(WATCOM)/binw/dos32a.exe $(DOS_BUILD)/

$(DOS_BUILD)/%.obj: src/%.c $(wildcard src/*.h src/platforms/*.h) VERSION
	@mkdir -p $(dir $@)
	$(WCC) -bt=dos -DAITD_NO_THREADS $(WCC_FLAGS) $< -fo=$@

# The browser, with Emscripten: a static site in build/web/.
EMCC       ?= emcc
WEB_BUILD  := $(BUILD)/web
WEB_SRC    := $(ENGINE_SRC) src/platforms/web.c $(SOUNDFONT_SRC) $(PAD_SRC)
WEB_OBJ    := $(patsubst src/%.c,$(WEB_BUILD)/obj/%.o,$(WEB_SRC))
WEB_JS     := $(WEB_BUILD)/aitd.js

.PHONY: web web-serve
web: $(WEB_JS) $(WEB_BUILD)/index.html

$(WEB_JS): $(WEB_OBJ)
	$(EMCC) $(WEB_OBJ) -O2 -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sALLOW_MEMORY_GROWTH \
	    -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=1048576 -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 \
	    -sEXPORTED_FUNCTIONS=_main,_web_event,_web_pad,_web_audio_render,_web_setenv,_malloc,_free \
	    -sEXPORTED_RUNTIME_METHODS=callMain,ccall,FS,IDBFS -lidbfs.js -o $@

$(WEB_BUILD)/index.html: platforms/web/index.html
	@mkdir -p $(dir $@)
	cp $< $@

$(WEB_BUILD)/obj/%.o: src/%.c $(wildcard src/*.h src/platforms/*.h) VERSION
	@mkdir -p $(dir $@)
	$(EMCC) $(DEFS) -DAITD_NO_THREADS -std=c99 $(WARN) -O2 -Isrc -c $< -o $@

# EM_ASM needs the GNU dialect.
$(WEB_BUILD)/obj/platforms/web.o: src/platforms/web.c $(wildcard src/*.h src/platforms/*.h) VERSION
	@mkdir -p $(dir $@)
	$(EMCC) $(DEFS) -DAITD_NO_THREADS -std=gnu99 $(WARN) -O2 -Isrc -c $< -o $@

web-serve: web
	cd $(WEB_BUILD) && python3 -m http.server 8080

# The version is compiled into main.c.
$(BUILD)/main.o: VERSION

$(BUILD):
	mkdir -p $(BUILD)

# One target per title; each sets AITD_DATA_DIR to its data/<GAME> folder.
#   make run-aitd1 | run-jack | run-aitd2 | run-aitd3 | run-timegate | run-knights
#   make run GAME=aitd2          (run alone is AITD3)
#   make run-knights RES=320     (Windows Time Gate resolution, 320 or 640)
#   make run-aitd1 ARGS=--stage=game
# `make check` runs every title unattended for FRAMES frames (TIMEOUT
# seconds at most, documents skipped) and reports each exit status.
GAMES   := aitd1 jack aitd2 aitd3 timegate knights
DIR_aitd1    := INDARK
DIR_jack     := JACK
DIR_aitd2    := INDARK2
DIR_aitd3    := INDARK3
DIR_timegate := TIMEGATE
DIR_knights  := KNIGHTS
GAME    ?= aitd3
RES     ?= 640
ARGS    ?=
FRAMES  ?= 300
TIMEOUT ?= 60

.PHONY: $(addprefix run-,$(GAMES)) check

run: run-$(GAME)

$(addprefix run-,$(GAMES)): run-%: $(TARGET)
	AITD_DATA_DIR=$(CURDIR)/data/$(DIR_$*) AITD_RES=$(RES) ./$(TARGET) $(ARGS)

check: $(TARGET)
	@for g in $(GAMES); do \
	    case $$g in aitd1) d=INDARK;; jack) d=JACK;; aitd2) d=INDARK2;; \
	        aitd3) d=INDARK3;; timegate) d=TIMEGATE;; knights) d=KNIGHTS;; esac; \
	    AITD_DATA_DIR=$(CURDIR)/data/$$d AITD_RES=$(RES) AITD_NO_DIALOGS=1 AITD_SKIP_DOCUMENTS=1 \
	        AITD_FRAMES=$(FRAMES) AITD_TIMEOUT=$(TIMEOUT) \
	        ./$(TARGET) --stage=game > $(BUILD)/check-$$g.log 2>&1; \
	    rc=$$?; \
	    if grep -q "AITD_TIMEOUT reached" $(BUILD)/check-$$g.log; then s="timeout"; \
	    elif [ $$rc -eq 0 ]; then s="ok"; else s="FAILED ($$rc)"; fi; \
	    printf "%-9s %s  (build/check-%s.log)\n" $$g "$$s" $$g; \
	done

clean:
	rm -rf $(BUILD) $(DIST) $(TARGET) pak_dump music_render cd_dump

# Offline tool: exercises the reconstructed .PAK reader against real data.
TOOL_OBJ := $(filter-out $(BUILD)/main.o,$(OBJ))

pak_dump: $(TOOL_OBJ) $(BUILD)/pak_dump.o
	$(CC) $(TOOL_OBJ) $(BUILD)/pak_dump.o $(LDFLAGS) -o $@

music_render: $(TOOL_OBJ) $(BUILD)/music_render.o
	$(CC) $(TOOL_OBJ) $(BUILD)/music_render.o $(LDFLAGS) -o $@

$(BUILD)/music_render.o: tools/music_render.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# Dumps a CD, or a disc image the system has attached, to BIN/CUE.
cd_dump: tools/cd_dump.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/pak_dump.o: tools/pak_dump.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
