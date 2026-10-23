# sources.mk - the engine sources every platform builds, relative to the
# repository root. The platform Makefiles and CMake projects read this list;
# each adds its own backend from src/platforms/.

ENGINE_SRC := \
	src/main.c \
	src/engine.c \
	src/resource.c \
	src/pak.c \
	src/inflate.c \
	src/explode.c \
	src/body.c \
	src/font.c \
	src/text.c \
	src/palette.c \
	src/game.c \
	src/harness.c \
	src/menu.c \
	src/menu_backend.c \
	src/menu_styles.c \
	src/frame.c \
	src/boot.c \
	src/input.c \
	src/exe_tables.c \
	src/raster.c \
	src/r3d.c \
	src/texture.c \
	src/anim.c \
	src/hyb.c \
	src/world.c \
	src/draw.c \
	src/actor.c \
	src/life.c \
	src/lifemap.c \
	src/ui.c \
	src/inventory.c \
	src/action.c \
	src/special.c \
	src/sound.c \
	src/music.c \
	src/opl.c \
	src/hmp.c \
	src/loop.c \
	src/savegame.c \
	src/escape.c \
	src/cutscene.c \
	src/reader.c \
	src/map.c \
	src/picture.c \
	src/smacker.c \
	src/mux.c \
	src/cdimage.c \
	src/stubs.c \
	src/platforms/common.c \
	src/platforms/mixer.c

# Shared by the hosts with no system MIDI synthesizer or no keyboard.
SOUNDFONT_SRC := src/platforms/midi_tsf.c
PAD_SRC := src/platforms/pad.c
