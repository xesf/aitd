/*
 * ed_ui.c - drawing for the editor's viewers.
 *
 * Everything goes into the raster target the editor set up (the buffer the
 * input poll puts on the window), in an 8 x 8 font (ed_font8.c) that
 * leaves room on a 320 x 200 screen; text from the game itself can use
 * the title's own font (ed_text_game). The palette is
 * whatever the viewer shows (a picture's, the game's), so the editor's
 * colours are the entries of that palette nearest to black, white, greys,
 * an amber accent and a blue for the selection.
 */

#include <stdio.h>
#include <string.h>

#include "aitd.h"
#include "font.h"
#include "life.h"
#include "palette.h"
#include "raster.h"
#include "subsystems.h"
#include "editor/ed_ui.h"

const struct ed_viewer *const g_ed_viewers[] = {
    &ed_view_pictures, &ed_view_models, &ed_view_fonts, &ed_view_texts, &ed_view_life
};
const int g_ed_viewer_count = (int)(sizeof(g_ed_viewers) / sizeof(g_ed_viewers[0]));

struct ed_colours g_ed;
u8 g_ed_game_palette[AITD_PALETTE_BYTES];

static u8 nearest(const u8 *palette, int r, int g, int b)
{
    int i, best = 0;
    long best_d = 0x7fffffffL;

    for (i = 0; i < 256; i++) {
        long dr = palette[i * 3] - r, dg = palette[i * 3 + 1] - g, db = palette[i * 3 + 2] - b;
        long d = dr * dr + dg * dg + db * db;

        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return (u8)best;
}

void ed_use_palette(const u8 *palette)
{
    if (palette != g_palette_buffer) {
        memcpy(g_palette_buffer, palette, AITD_PALETTE_BYTES);
    }
    palette_set_range(g_palette_buffer, 0, 256);
    g_ed.black = nearest(palette, 0, 0, 0);
    g_ed.white = nearest(palette, 255, 255, 255);
    g_ed.grey = nearest(palette, 150, 150, 150);
    g_ed.dim = nearest(palette, 60, 60, 60);
    g_ed.accent = nearest(palette, 255, 200, 40);
    g_ed.select = nearest(palette, 40, 70, 160);
}

s16 ed_line(void)
{
    return 10;
}

s16 ed_top(void)
{
    return (s16)(ed_line() + 1);
}

s16 ed_bottom(void)
{
    return (s16)(g_screen_height - ed_line() - 2);
}

void ed_clear(u8 colour)
{
    raster_reset_clip();
    memset(g_raster.target, colour, SCREEN_PIXELS);
}

void ed_rect(s16 x1, s16 y1, s16 x2, s16 y2, u8 colour)
{
    raster_rect(x1, x2, y1, y2, colour);
}

/* 8 x 8, through the raster clip; characters outside 32..126 show as ?. */
void ed_text(s16 x, s16 y, const char *text, u8 colour)
{
    x = (s16)(x + g_raster.origin_x);
    y = (s16)(y + g_raster.origin_y);
    for (; *text != '\0'; text++, x = (s16)(x + 8)) {
        u8 c = (u8)*text;
        const u8 *glyph = g_ed_font8[(c >= 32 && c <= 126 ? c : '?') - 32];
        int row, col;

        if (x > g_raster.clip_xmax) {
            break;
        }
        for (row = 0; row < 8; row++) {
            int py = y + row;

            if (py < g_raster.clip_ymin || py > g_raster.clip_ymax || glyph[row] == 0) {
                continue;
            }
            for (col = 0; col < 8; col++) {
                int px = x + col;

                if ((glyph[row] >> col) & 1 && px >= g_raster.clip_xmin && px <= g_raster.clip_xmax) {
                    g_raster.target[py * g_raster.pitch + px] = colour;
                }
            }
        }
    }
}

s16 ed_text_width(const char *text)
{
    return (s16)(8 * strlen(text));
}

/* In the title's own font, y the top of the line. */
void ed_text_game(s16 x, s16 y, const char *text, u8 colour)
{
    text_draw_string(x, (s16)(y + font_ascent()), text, colour);
}

s16 ed_text_game_width(const char *text)
{
    return (s16)font_string_width(text);
}

s16 ed_line_game(void)
{
    int h = font_height();

    return (s16)((h > 0 ? h : 8) + 3);
}

void ed_bars(int current, const char *help)
{
    s16 x = 4;
    int i;

    raster_reset_clip();
    ed_rect(0, 0, (s16)(g_screen_width - 1), (s16)(ed_line() - 1), g_ed.dim);
    for (i = 0; i < g_ed_viewer_count; i++) {
        char label[32];

        snprintf(label, sizeof(label), "%d %s", i + 1, g_ed_viewers[i]->name);
        if (i == current) {
            ed_rect((s16)(x - 2), 0, (s16)(x + ed_text_width(label) + 1), (s16)(ed_line() - 1),
                    g_ed.select);
        }
        ed_text(x, 1, label, i == current ? g_ed.accent : g_ed.white);
        x = (s16)(x + ed_text_width(label) + 6);
    }
    ed_rect(0, (s16)(ed_bottom() + 1), (s16)(g_screen_width - 1), (s16)(g_screen_height - 1),
            g_ed.dim);
    ed_text(4, (s16)(ed_bottom() + 2), help, g_ed.white);
}

void ed_list(s16 x1, s16 y1, s16 x2, s16 y2, int count, int selected, int *first,
             void (*row)(int index, char *out, size_t size))
{
    int rows = (y2 - y1 + 1) / ed_line(), i;

    if (rows < 1) {
        rows = 1;
    }
    if (selected < *first) {
        *first = selected;
    }
    if (selected >= *first + rows) {
        *first = selected - rows + 1;
    }
    if (*first < 0) {
        *first = 0;
    }
    raster_set_clip(x1, y1, x2, y2);
    for (i = 0; i < rows && *first + i < count; i++) {
        char text[256];
        s16 y = (s16)(y1 + i * ed_line());
        int index = *first + i;

        row(index, text, sizeof(text));
        if (index == selected) {
            ed_rect(x1, y, x2, (s16)(y + ed_line() - 1), g_ed.select);
        }
        ed_text((s16)(x1 + 2), (s16)(y + 1), text, index == selected ? g_ed.accent : g_ed.grey);
    }
    raster_reset_clip();
}

int ed_list_key(int scancode, int *selected, int count, int page)
{
    int s = *selected;

    switch (scancode) {
    case ED_KEY_UP: s--; break;
    case ED_KEY_DOWN: s++; break;
    case ED_KEY_PGUP: s -= page; break;
    case ED_KEY_PGDN: s += page; break;
    case ED_KEY_HOME: s = 0; break;
    case ED_KEY_END: s = count - 1; break;
    default: return 0;
    }
    if (s >= count) {
        s = count - 1;
    }
    if (s < 0) {
        s = 0;
    }
    *selected = s;
    return 1;
}
