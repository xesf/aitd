# PlayStation Portable build, driven by the PSP SDK's build.mak.
#
#   make -C platforms/psp            EBOOT.PBP in platforms/psp/
#   make psp                         the same, from the repository root
#
# Needs the pspdev toolchain (psp-gcc, psp-config) on PATH.

ROOT    := ../..
include $(ROOT)/sources.mk
VERSION := $(shell cat $(ROOT)/VERSION 2>/dev/null || echo dev)

TARGET  = aitd
OBJDIR  := obj
SRC     := $(ENGINE_SRC) src/platforms/psp.c $(SOUNDFONT_SRC) $(PAD_SRC)
OBJS    := $(patsubst src/%.c,$(OBJDIR)/%.o,$(SRC))

INCDIR  = $(ROOT)/src
# -G0: the SDK's objects and ours do not share a small-data area.
# -fsigned-char: the engine was written for x86, where char is signed.
CFLAGS  = -O2 -G0 -std=gnu99 -Wall -fsigned-char -fwrapv -Wno-unused-parameter \
          -DMIXER_MUSIC_DIVISOR=4 -DAITD_VERSION='"$(VERSION)"'
ASFLAGS = $(CFLAGS)
LIBS    = -lpspgu -lpspge -lpspdisplay -lpspctrl -lpspaudio -lpsppower -lpsprtc \
          -lpthread -lm

EXTRA_TARGETS   = EBOOT.PBP
PSP_EBOOT_TITLE = Alone in the Dark
PSP_LARGE_MEMORY = 1

PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

$(OBJDIR)/%.o: $(ROOT)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(addprefix -I,$(INCDIR)) $(CPPFLAGS) -c $< -o $@

# One EBOOT per game, each with its own title and, when `make art` has made
# it from the player's data, its own icon and background:
# ../../build/psp/<GAME>/EBOOT.PBP.
GAMES      := INDARK JACK INDARK2 INDARK3 TIMEGATE KNIGHTS
TITLE_INDARK   := Alone in the Dark
TITLE_JACK     := Jack in the Dark
TITLE_INDARK2  := Alone in the Dark 2
TITLE_INDARK3  := Alone in the Dark 3
TITLE_TIMEGATE := Time Gate: Knight's Chase
TITLE_KNIGHTS  := Time Gate (Windows)
ART        := $(ROOT)/build/art
GAMES_OUT  := $(ROOT)/build/psp

.PHONY: games
games: $(TARGET).elf
	$(STRIP) $(TARGET).elf -o $(TARGET)_strip.elf
	@for g in $(GAMES); do \
	    out=$(GAMES_OUT)/$$g; mkdir -p $$out; \
	    case $$g in INDARK) t="$(TITLE_INDARK)";; JACK) t="$(TITLE_JACK)";; \
	        INDARK2) t="$(TITLE_INDARK2)";; INDARK3) t="$(TITLE_INDARK3)";; \
	        TIMEGATE) t="$(TITLE_TIMEGATE)";; KNIGHTS) t="$(TITLE_KNIGHTS)";; esac; \
	    $(MKSFO) -d MEMSIZE=1 "$$t" $$out/PARAM.SFO; \
	    icon=NULL; pic=NULL; \
	    [ -f $(ART)/$$g/psp/ICON0.PNG ] && icon=$(ART)/$$g/psp/ICON0.PNG; \
	    [ -f $(ART)/$$g/psp/PIC1.PNG ] && pic=$(ART)/$$g/psp/PIC1.PNG; \
	    $(PACK_PBP) $$out/EBOOT.PBP $$out/PARAM.SFO $$icon NULL NULL $$pic NULL \
	        $(TARGET)_strip.elf NULL > /dev/null && echo "$$out/EBOOT.PBP"; \
	    rm -f $$out/PARAM.SFO; \
	done
	rm -f $(TARGET)_strip.elf
