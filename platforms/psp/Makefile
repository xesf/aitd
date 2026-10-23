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
          -DAITD_VERSION='"$(VERSION)"'
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
