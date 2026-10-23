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
