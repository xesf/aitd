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
            $(SRCDIR)/menu.c $(SRCDIR)/menu_backend.c $(SRCDIR)/input.c \
            $(SRCDIR)/exe_tables.c $(SRCDIR)/raster.c $(SRCDIR)/r3d.c $(SRCDIR)/texture.c \
            $(SRCDIR)/anim.c $(SRCDIR)/hyb.c $(SRCDIR)/world.c $(SRCDIR)/draw.c \
            $(SRCDIR)/actor.c $(SRCDIR)/life.c $(SRCDIR)/lifemap.c $(SRCDIR)/ui.c $(SRCDIR)/inventory.c \
            $(SRCDIR)/action.c $(SRCDIR)/special.c $(SRCDIR)/sound.c \
            $(SRCDIR)/loop.c $(SRCDIR)/savegame.c $(SRCDIR)/escape.c $(SRCDIR)/cutscene.c $(SRCDIR)/reader.c $(SRCDIR)/map.c $(SRCDIR)/picture.c $(SRCDIR)/stubs.c
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

# AITD_DATA_DIR points at the directory holding itd_ress.pak, DEFINES.ITD and
# the rest of the original assets.
run: $(TARGET)
	AITD_DATA_DIR=$(PWD)/data ./$(TARGET)

clean:
	rm -rf $(BUILD) $(TARGET) pak_dump

# Offline tool: exercises the reconstructed .PAK reader against real data.
TOOL_OBJ := $(filter-out $(BUILD)/main.o,$(OBJ))

pak_dump: $(TOOL_OBJ) $(BUILD)/pak_dump.o
	$(CC) $(TOOL_OBJ) $(BUILD)/pak_dump.o $(LDFLAGS) -o $@

$(BUILD)/pak_dump.o: tools/pak_dump.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
