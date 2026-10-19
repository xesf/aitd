/*
 * frame.c - the window frames of Alone in the Dark 1, AITD2 and Jack.
 *
 * Their sprite bank keeps 16 bit offsets; a sprite is four bytes, width,
 * height, then pixels. A frame is corners 0..3, top 4, bottom 5, left 6,
 * right 7 in 20 pixel steps and the title plate 8, with a black inside.
 * Facts from FITD's AffBigCadre and AffSpf / AffSpfI; the original routines
 * are not rebuilt.
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

void frame_draw(s16 x, s16 y, s16 width, s16 height)
{
    const u8 *bank = engine_resource(ITD_RESS_UI_SPRITES);
    s16 left = (s16)(x - width / 2), top = (s16)(y - height / 2);
    s16 right = (s16)(x + width / 2), bottom = (s16)(y + height / 2);
    s16 cx, cy;

    raster_set_clip(0, 0, 319, 199);
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
