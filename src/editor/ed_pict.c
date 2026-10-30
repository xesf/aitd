/*
 * ed_pict.c - the picture viewer: every entry of ITD_RESS.PAK, shown full
 * screen in its own palette when it is a picture (picture.c's sizes), or
 * described when it is not (a palette, sprites, a body, a sound).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "pak.h"
#include "picture.h"
#include "raster.h"
#include "editor/ed_ui.h"

static int s_count;
static int s_entry;
static int s_info = 1;
static int s_loaded = -1;
static u8 *s_pixels;
static int s_is_picture;
static int s_has_palette;
static u32 s_size;
static u8 s_palette[AITD_PALETTE_BYTES];

static void pict_open(void)
{
    struct pak pak;

    s_count = 0;
    if (pak_open(&pak, "ITD_RESS.PAK")) {
        s_count = pak_entry_count(&pak);
        pak_close(&pak);
    }
    if (s_pixels == NULL) {
        s_pixels = malloc(SCREEN_PIXELS);
    }
    s_loaded = -1;
}

static void pict_close(void)
{
    free(s_pixels);
    s_pixels = NULL;
}

static void load(void)
{
    u8 *raw;

    if (s_loaded == s_entry || s_pixels == NULL) {
        return;
    }
    s_loaded = s_entry;
    s_size = 0;
    raw = pak_load("ITD_RESS.PAK", s_entry, &s_size);
    if (raw != NULL) {
        mem_free(raw);
    }
    memcpy(s_palette, g_ed_game_palette, sizeof(s_palette));
    memset(s_pixels, 0, SCREEN_PIXELS);
    s_is_picture = s_size >= SCREEN_PIXELS;
    s_has_palette = s_is_picture && picture_load(s_entry, s_pixels, s_palette) == 1;
    if (!s_is_picture && s_size == AITD_PALETTE_BYTES) {
        /* a bare palette: show it as 16 x 16 swatches */
        raw = pak_load("ITD_RESS.PAK", s_entry, &s_size);
        if (raw != NULL) {
            memcpy(s_palette, raw, sizeof(s_palette));
            mem_free(raw);
            s_has_palette = 1;
        }
    }
    ed_use_palette(s_palette);
}

static void pict_key(int sc)
{
    if (sc == ED_KEY_LEFT) {
        sc = ED_KEY_UP;
    } else if (sc == ED_KEY_RIGHT) {
        sc = ED_KEY_DOWN;
    }
    if (ed_list_key(sc, &s_entry, s_count, 10)) {
        return;
    }
    if (sc == ED_KEY_ENTER || sc == ED_KEY_SPACE) {
        s_info ^= 1;
    }
}

static void swatches(void)
{
    s16 top = (s16)(ed_top() + ed_line() + 2), size = (s16)((ed_bottom() - top) / 16);
    s16 left = (s16)((g_screen_width - size * 16) / 2);
    int i;

    for (i = 0; i < 256; i++) {
        s16 x = (s16)(left + (i % 16) * size), y = (s16)(top + (i / 16) * size);

        ed_rect(x, y, (s16)(x + size - 2), (s16)(y + size - 2), (u8)i);
    }
}

static void pict_draw(void)
{
    char line[96];

    load();
    if (s_is_picture) {
        memcpy(g_raster.target, s_pixels, SCREEN_PIXELS);
    } else {
        ed_clear(g_ed.black);
        if (s_size == AITD_PALETTE_BYTES) {
            swatches();
        }
    }
    if (!s_info && s_is_picture) {
        return;
    }
    ed_bars(0, "Up/Dn entry  Enter bars  Tab next");
    snprintf(line, sizeof(line), "ITD_RESS %d/%d  %u b  %s", s_entry, s_count - 1,
             (unsigned)s_size,
             s_is_picture ? (s_has_palette ? "picture+palette" : "picture")
             : s_size == AITD_PALETTE_BYTES ? "palette" : "not a picture");
    ed_rect(0, ed_top(), (s16)(ed_text_width(line) + 6), (s16)(ed_top() + ed_line()), g_ed.black);
    ed_text(3, (s16)(ed_top() + 1), line, g_ed.white);
}

const struct ed_viewer ed_view_pictures = {
    "Images", pict_open, pict_close, pict_key, pict_draw
};
