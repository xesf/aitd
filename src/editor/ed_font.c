/*
 * ed_font.c - the font viewer: the title's font (ITD_RESS, see the
 * profile's font_entry), every character from 32 to 255 in a grid, and the
 * selected one enlarged with its advance and the font's height.
 */

#include <stdio.h>
#include <string.h>

#include "aitd.h"
#include "font.h"
#include "raster.h"
#include "editor/ed_ui.h"

enum { FIRST = 32, LAST = 255, COLUMNS = 16, ZOOM = 2, CELL = 24 };

static int s_char = 'A';

static void font_key(int sc)
{
    int c = s_char;

    switch (sc) {
    case ED_KEY_LEFT: c--; break;
    case ED_KEY_RIGHT: c++; break;
    case ED_KEY_UP: c -= COLUMNS; break;
    case ED_KEY_DOWN: c += COLUMNS; break;
    default: break;
    }
    if (c >= FIRST && c <= LAST) {
        s_char = c;
    }
}

static void enlarged(s16 left, s16 top)
{
    u8 cell[CELL * CELL];
    char text[2] = { (char)s_char, 0 };
    int x, y;

    memset(cell, g_ed.black, sizeof(cell));
    ed_text_game(-100, -100, " ", g_ed.white);    /* binds the font in white */
    font_draw_string(cell, CELL, CELL, 2, 2 + font_ascent(), text);
    for (y = 0; y < CELL; y++) {
        for (x = 0; x < CELL; x++) {
            ed_rect((s16)(left + x * ZOOM), (s16)(top + y * ZOOM),
                    (s16)(left + x * ZOOM + ZOOM - 1), (s16)(top + y * ZOOM + ZOOM - 1),
                    cell[y * CELL + x]);
        }
    }
}

static void font_draw(void)
{
    s16 panel = (s16)(g_screen_width - CELL * ZOOM - 6);
    s16 top = (s16)(ed_top() + 4), pitch = (s16)(ed_line_game() + 1);
    s16 col = (s16)((panel - 10) / COLUMNS);
    char line[64];
    int c;

    ed_clear(g_ed.black);
    for (c = FIRST; c <= LAST; c++) {
        char text[2] = { (char)c, 0 };
        s16 x = (s16)(6 + ((c - FIRST) % COLUMNS) * col);
        s16 y = (s16)(top + ((c - FIRST) / COLUMNS) * pitch);

        if (c == s_char) {
            ed_rect((s16)(x - 2), (s16)(y - 1), (s16)(x + col - 3), (s16)(y + pitch - 2), g_ed.select);
        }
        ed_text_game(x, y, text, c == s_char ? g_ed.accent : g_ed.white);
    }
    {
        s16 left = panel;

        enlarged(left, top);
        snprintf(line, sizeof(line), "%d", s_char);
        ed_text(left, (s16)(top + CELL * ZOOM + 4), line, g_ed.white);
        snprintf(line, sizeof(line), "adv %d", font_glyph_advance((u8)s_char));
        ed_text(left, (s16)(top + CELL * ZOOM + 4 + ed_line()), line, g_ed.grey);
        snprintf(line, sizeof(line), "h %d", font_height());
        ed_text(left, (s16)(top + CELL * ZOOM + 4 + 2 * ed_line()), line, g_ed.grey);
    }
    ed_bars(2, "Arrows: character  Tab next  F10 back");
}

const struct ed_viewer ed_view_fonts = {
    "Font", NULL, NULL, font_key, font_draw
};
