# Alone in the Dark 3 - reconstructed engine
#
# Native macOS build: AppKit + CoreGraphics + AudioToolbox, no third party
# dependencies. The engine core (main.c, engine.c, resource.c, stubs.c) is
# plain C99 and platform agnostic; only src/platform_macos.m is host specific.

TARGET  := aitd3
SRCDIR  := src
BUILD   := build

CORE_SRC := $(SRCDIR)/main.c $(SRCDIR)/engine.c $(SRCDIR)/resource.c \
            $(SRCDIR)/pak.c $(SRCDIR)/explode.c $(SRCDIR)/body.c $(SRCDIR)/font.c $(SRCDIR)/text.c \
            $(SRCDIR)/palette.c $(SRCDIR)/game.c $(SRCDIR)/harness.c \
            $(SRCDIR)/menu.c $(SRCDIR)/menu_backend.c $(SRCDIR)/menu_styles.c $(SRCDIR)/frame.c $(SRCDIR)/boot.c $(SRCDIR)/input.c \
            $(SRCDIR)/exe_tables.c $(SRCDIR)/raster.c $(SRCDIR)/r3d.c $(SRCDIR)/texture.c \
            $(SRCDIR)/anim.c $(SRCDIR)/hyb.c $(SRCDIR)/world.c $(SRCDIR)/draw.c \
            $(SRCDIR)/actor.c $(SRCDIR)/life.c $(SRCDIR)/lifemap.c $(SRCDIR)/ui.c $(SRCDIR)/inventory.c \
            $(SRCDIR)/action.c $(SRCDIR)/special.c $(SRCDIR)/sound.c $(SRCDIR)/music.c \
            $(SRCDIR)/loop.c $(SRCDIR)/savegame.c $(SRCDIR)/escape.c $(SRCDIR)/cutscene.c $(SRCDIR)/reader.c $(SRCDIR)/map.c $(SRCDIR)/picture.c $(SRCDIR)/smacker.c $(SRCDIR)/stubs.c
MAC_SRC  := $(SRCDIR)/platform_macos.m

CORE_OBJ := $(patsubst $(SRCDIR)/%.c,$(BUILD)/%.o,$(CORE_SRC))
MAC_OBJ  := $(patsubst $(SRCDIR)/%.m,$(BUILD)/%.o,$(MAC_SRC))
OBJ      := $(CORE_OBJ) $(MAC_OBJ)

CC       := clang
CFLAGS   := -std=c99 -Wall -Wextra -Wno-unused-parameter -g -O1 -I$(SRCDIR)
OBJCFLAGS := -fobjc-arc -Wall -Wextra -Wno-unused-parameter -g -O1 -I$(SRCDIR)
LDFLAGS  := -lz -framework AppKit -framework CoreGraphics -framework AudioToolbox

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $@

$(BUILD)/%.o: $(SRCDIR)/%.c $(wildcard $(SRCDIR)/*.h) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRCDIR)/%.m | $(BUILD)
	$(CC) $(OBJCFLAGS) -c $< -o $@

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
	rm -rf $(BUILD) $(TARGET) pak_dump

# Offline tool: exercises the reconstructed .PAK reader against real data.
TOOL_OBJ := $(filter-out $(BUILD)/main.o,$(OBJ))

pak_dump: $(TOOL_OBJ) $(BUILD)/pak_dump.o
	$(CC) $(TOOL_OBJ) $(BUILD)/pak_dump.o $(LDFLAGS) -o $@

$(BUILD)/pak_dump.o: tools/pak_dump.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
