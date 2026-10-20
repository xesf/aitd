/*
 * frame.c - the window frames of Alone in the Dark 1, AITD2 and Jack.
 *
 * Their sprite bank keeps 16 bit offsets; a sprite is four bytes, width,
 * height, then pixels. A frame is corners 0..3, top 4, bottom 5, left 6,
 * right 7 in 20 pixel steps and the title plate 8, with a black inside.
 * Worked out from the data; the original routines are not rebuilt.
 */

#include "aitd.h"
#include "frame.h"
#include "raster.h"
#include "subsystems.h"

s16 g_window[4];

static u16 rd16(const u8 *p)
{
    return (u16)(p[0] | (p[1] << 8));
}

void old_sprite_draw(s16 x, s16 y, int index, const u8 *bank, int transparent)
{
    const u8 *p = bank + rd16(bank + index * 2) + 4;
    int width = rd16(p), height = rd16(p + 2);
    int row, col;

    p += 4;
    for (row = 0; row < height; row++, p += width) {
        int ty = y + row;

        if (ty < 0 || ty >= g_raster.height) {
            continue;
        }
        for (col = 0; col < width; col++) {
            int tx = x + col;

            if (tx >= 0 && tx < g_raster.width && (!transparent || p[col] != 0)) {
                g_raster.target[ty * g_raster.pitch + tx] = p[col];
            }
        }
    }
}

static void sprite_size(const u8 *bank, int index, int *w, int *h)
{
    const u8 *p = bank + rd16(bank + index * 2) + 4;

    *w = rd16(p);
    *h = rd16(p + 2);
}

/* Part of a sprite: columns c0..c0+cols, rows r0..r0+rows, zero skipped. */
static void sprite_part(s16 x, s16 y, int index, const u8 *bank, int c0, int cols, int r0, int rows)
{
    const u8 *p = bank + rd16(bank + index * 2) + 4;
    int width = rd16(p);
    int row, col;

    p += 4;
    for (row = 0; row < rows; row++) {
        int ty = y + row;
        const u8 *line = p + (r0 + row) * width + c0;

        if (ty < 0 || ty >= g_raster.height) {
            continue;
        }
        for (col = 0; col < cols; col++) {
            int tx = x + col;
            if (tx >= 0 && tx < g_raster.width && line[col] != 0) {
                g_raster.target[ty * g_raster.pitch + tx] = line[col];
            }
        }
    }
}

/* How many columns from the left (or the right) of a side piece, and rows
 * from the top (or the bottom) of a top piece, carry the border. */
static int border_cols(const u8 *bank, int index, int from_right)
{
    const u8 *p = bank + rd16(bank + index * 2) + 4;
    int w = rd16(p), n = 0;

    p += 4;
    while (n < w && p[from_right ? w - 1 - n : n] != 0) {
        n++;
    }
    return n;
}

static int border_rows(const u8 *bank, int index, int from_bottom)
{
    const u8 *p = bank + rd16(bank + index * 2) + 4;
    int w = rd16(p), h = rd16(p + 2), n = 0;

    p += 4;
    while (n < h && p[(from_bottom ? h - 1 - n : n) * w] != 0) {
        n++;
    }
    return n;
}

/*
 * AITD2's bank has a larger frame: 30 x 30 corners, 30 x 17 top and
 * bottom pieces, 11 pixel tall side pieces, and no title plate (its
 * sprite 8 is a placeholder square). The pieces carry padding (and a stray
 * pixel) beside the border, so only the border part of each is drawn; the
 * corners go on top, their zero pixels left alone. Worked out from the
 * data.
 */
static void frame_draw_sized(const u8 *bank, s16 left, s16 top, s16 right, s16 bottom)
{
    int cw, ch, tw, th, bw, bh, lw, lh, rw, rh;
    int bl = border_cols(bank, 6, 0), br = border_cols(bank, 7, 1);
    int bt = border_rows(bank, 4, 0), bb = border_rows(bank, 5, 1);
    s16 x, y;

    sprite_size(bank, 0, &cw, &ch);
    sprite_size(bank, 4, &tw, &th);
    sprite_size(bank, 5, &bw, &bh);
    sprite_size(bank, 6, &lw, &lh);
    sprite_size(bank, 7, &rw, &rh);

    g_window[0] = (s16)(left + bl);
    g_window[1] = (s16)(top + bt);
    g_window[2] = (s16)(right - br - 1);
    g_window[3] = (s16)(bottom - bb - 1);
    raster_rect(g_window[0], g_window[2], g_window[1], g_window[3], 0);

    for (x = (s16)(left + cw); x < right - cw; x = (s16)(x + tw)) {
        s16 at = (s16)(x + tw > right - cw ? right - cw - tw : x);
        sprite_part(at, top, 4, bank, 0, tw, 0, bt);
        sprite_part(at, (s16)(bottom - bb), 5, bank, 0, bw, bh - bb, bb);
    }
    for (y = (s16)(top + ch); y < bottom - ch; y = (s16)(y + lh)) {
        s16 at = (s16)(y + lh > bottom - ch ? bottom - ch - lh : y);
        sprite_part(left, at, 6, bank, 0, bl, 0, lh);
        sprite_part((s16)(right - br), at, 7, bank, rw - br, br, 0, rh);
    }
    sprite_part(left, top, 0, bank, 0, cw, 0, ch);
    sprite_part((s16)(right - cw), top, 1, bank, 0, cw, 0, ch);
    sprite_part(left, (s16)(bottom - ch), 2, bank, 0, cw, 0, ch);
    sprite_part((s16)(right - cw), (s16)(bottom - ch), 3, bank, 0, cw, 0, ch);
}

void frame_draw(s16 x, s16 y, s16 width, s16 height)
{
    const u8 *bank = engine_resource(ITD_RESS_UI_SPRITES);
    s16 left = (s16)(x - width / 2), top = (s16)(y - height / 2);
    s16 right = (s16)(x + width / 2), bottom = (s16)(y + height / 2);
    s16 cx, cy;
    int cw = 20, ch = 20;

    raster_set_clip(0, 0, 319, 199);
    if (bank != NULL) {
        sprite_size(bank, 0, &cw, &ch);
    }
    if (bank != NULL && (cw != 20 || ch != 20)) {
        frame_draw_sized(bank, left, top, right, bottom);
        raster_set_clip(g_window[0], g_window[1], g_window[2], g_window[3]);
        return;
    }
    if (bank != NULL) {
        old_sprite_draw(left, top, 0, bank, 0);
        for (cx = (s16)(left + 20); right - 20 > cx; cx = (s16)(cx + 20)) {
            old_sprite_draw(cx, top, 4, bank, 0);
        }
        old_sprite_draw(cx, top, 1, bank, 0);
        for (cy = (s16)(top + 20); bottom - 20 > cy; cy = (s16)(cy + 20)) {
            old_sprite_draw(left, cy, 6, bank, 0);
        }
        for (cy = (s16)(top + 20); bottom - 20 > cy; cy = (s16)(cy + 20)) {
            old_sprite_draw((s16)(right - 8), cy, 7, bank, 0);
        }
        old_sprite_draw(left, cy, 2, bank, 0);
        for (cx = (s16)(left + 20); right - 20 > cx; cx = (s16)(cx + 20)) {
            old_sprite_draw(cx, (s16)(cy + 12), 5, bank, 0);
        }
        old_sprite_draw(cx, cy, 3, bank, 0);
        old_sprite_draw((s16)(x - 20), (s16)(cy + 12), 8, bank, 0);
    }

    g_window[0] = (s16)(left + 8);
    g_window[1] = (s16)(top + 8);
    g_window[2] = (s16)(right - 9);
    g_window[3] = (s16)(bottom - 9);
    raster_rect(g_window[0], g_window[2], g_window[1], g_window[3], 0);
    raster_set_clip(g_window[0], g_window[1], g_window[2], g_window[3]);
}
