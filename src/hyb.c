/*
 * hyb.c - "hybrid" 2D sprites: LISTHYB.PAK, the animated background pieces
 * of ANIMxx.PAK and the inventory frames all use this vector format
 * (0x424ff8 renderer, 0x40706b bounding box, 0x42925c RLE bitmaps).
 *
 * File layout, offsets relative to the start:
 *   +0  u32  sprite table   (u32 offsets relative to the table)
 *   +4  u32  bitmap table   (u32 offsets relative to the table)
 *   +8  u32  animation table (u32 offsets relative to the table)
 *
 * A sprite is u8 flags (bit 7 mirror), u8 element count, then elements of
 * u8 type, u8 count, u8 colour, u8 colour, count (x, y) s16 points:
 *   0  another sprite: index in bytes 2-3, at point 0
 *   1  a bitmap: index in bytes 2-3, bottom-left corner at point 0
 *   4  filled polygon in colour b3, outlined in b2 unless b2 is 0xff
 *   5  filled rectangle between two points, likewise outlined
 *   6  polyline in colour b3
 *   7  points in colour b3
 */

#include <string.h>

#include "aitd.h"
#include "hyb.h"
#include "raster.h"

static u16 rd16(const u8 *p)
{
    return (u16)(p[0] | (p[1] << 8));
}

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static const u8 *sprite_record(const u8 *data, int index)
{
    const u8 *table = data + rd32(data);

    return table + rd32(table + (index & 0x3fff) * 4);
}

static const u8 *bitmap_record(const u8 *data, int index)
{
    const u8 *table = data + rd32(data + 4);

    return table + rd32(table + (index & 0x3fff) * 4);
}

/* ---- bitmaps (0x42925c) ------------------------------------------------ */

/* Expands one RLE row into line[] / covered[]; returns the next row. */
static const u8 *bitmap_row(const u8 *p, int width, u8 *line, u8 *covered)
{
    int runs = *p++;
    int x = 0;

    memset(line, 0, (size_t)width);
    memset(covered, 0, (size_t)width);

    while (runs-- > 0) {
        int skip = *p++;
        int n = p[0] * 4 + p[1];

        p += 2;
        x += skip;
        while (n-- > 0) {
            if (x >= 0 && x < width) {
                line[x] = *p;
                covered[x] = 1;
            }
            p++;
            x++;
        }
    }
    p++;  /* trailing skip */
    return p;
}

static void draw_bitmap(int index, s16 x, s16 y, const u8 *data)
{
    const u8 *rec = bitmap_record(data, index);
    int width = rd16(rec);
    int height = rd16(rec + 2);
    int mirror = (index & 0x8000) != 0;
    const u8 *p = rec + 4;
    u8 line[1024], covered[1024];
    int row;

    if (width <= 0 || width > 1024) {
        return;
    }

    x = (s16)(x + g_raster.origin_x);
    y = (s16)(y - height + 1 + g_raster.origin_y);

    if (x > g_raster.clip_xmax || y > g_raster.clip_ymax) {
        return;
    }

    for (row = 0; row < height; row++) {
        s16 ty = (s16)(y + row);
        int col;

        p = bitmap_row(p, width, line, covered);
        if (ty < g_raster.clip_ymin || ty > g_raster.clip_ymax) {
            continue;
        }
        for (col = 0; col < width; col++) {
            int src = mirror ? width - 1 - col : col;
            s16 tx = (s16)(x + col);

            if (!covered[src] || tx < g_raster.clip_xmin || tx > g_raster.clip_xmax) {
                continue;
            }
            if (line[src] != 0) {
                g_raster.target[(s32)ty * g_raster.pitch + tx] = line[src];
            }
        }
    }
}

/* ---- the element walker (0x424ff8) ------------------------------------- */

void hyb_render(int index, s16 x, s16 y, const u8 *data)
{
    const u8 *sprite = sprite_record(data, index & 0x7fff);
    u8 mirror = (u8)(((index >> 8) & 0x80) ^ (sprite[0] & 0x80));
    int count = sprite[1];
    const u8 *p = sprite + 2;

    while (count-- > 0) {
        int type = p[0];
        int n = p[1];
        u8 c_outline = p[2];
        u8 c_fill = p[3];
        s16 pts[2 * 256 + 2];
        int i;

        for (i = 0; i < n && i < 256; i++) {
            s16 px = (s16)rd16(p + 4 + i * 4);
            s16 py = (s16)rd16(p + 6 + i * 4);

            pts[i * 2] = (s16)((mirror ? (s16)-px : px) + x);
            pts[i * 2 + 1] = (s16)(py + y);
        }

        switch (type) {
        case HYB_SUB_SPRITE: { /* 0x4250cd */
            int sub = (int)rd16(p + 2) ^ (mirror << 8);
            hyb_render(sub, pts[0], pts[1], data);
            break;
        }
        case HYB_BITMAP: { /* 0x4250e8 */
            int bmp = (int)rd16(p + 2) ^ (mirror << 8);
            s16 bx = pts[0];
            if (mirror) {
                bx = (s16)(bx - (s16)rd16(bitmap_record(data, bmp)));
            }
            draw_bitmap(bmp, bx, pts[1], data);
            break;
        }
        case HYB_POLYGON: /* 0x425118 */
            raster_poly(pts, n, 0, c_fill);
            if (c_outline != 0xff) {
                for (i = 0; i < n; i++) {
                    int j = (i + 1) % n;
                    raster_line(pts[i * 2], pts[i * 2 + 1], pts[j * 2], pts[j * 2 + 1], c_outline);
                }
            }
            break;
        case HYB_RECT: /* 0x425152 */
            raster_rect(pts[0], pts[2], pts[1], pts[3], c_fill);
            if (c_outline != 0xff) {
                raster_line(pts[0], pts[1], pts[0], pts[3], c_outline);
                raster_line(pts[2], pts[1], pts[2], pts[3], c_outline);
                raster_line(pts[0], pts[1], pts[2], pts[1], c_outline);
                raster_line(pts[0], pts[3], pts[2], pts[3], c_outline);
            }
            break;
        case HYB_POLYLINE: /* 0x42518c */
            for (i = 0; i + 1 < n; i++) {
                raster_line(pts[i * 2], pts[i * 2 + 1], pts[i * 2 + 2], pts[i * 2 + 3], c_fill);
            }
            break;
        case HYB_POINTS: /* 0x4251b5 */
            for (i = 0; i < n; i++) {
                raster_point(pts[i * 2], pts[i * 2 + 1], c_fill);
            }
            break;
        default:
            break;
        }

        p += 4 + n * 4;
    }
}

/* ---- bounding box (0x40706b, 0x406e4b) --------------------------------- */

static void box_union(s16 *box, const s16 *other)
{
    if (other[0] < box[0]) box[0] = other[0];
    if (other[1] < box[1]) box[1] = other[1];
    if (other[2] > box[2]) box[2] = other[2];
    if (other[3] > box[3]) box[3] = other[3];
}

int hyb_bbox(s16 *box, int index, const u8 *data)
{
    const u8 *sprite = sprite_record(data, index);
    int count = sprite[1];
    const u8 *p = sprite + 2;
    int i;

    box[0] = box[1] = box[2] = box[3] = 0;
    if (count == 0) {
        return 0;
    }

    for (i = 0; i < count; i++) {
        s16 e[4] = { 0, 0, 0, 0 };
        int n = p[1];

        if (n == 0) {
            break;
        }

        if (p[0] == 0) {
            hyb_bbox(e, rd16(p + 2), data);
            e[0] = (s16)(e[0] + (s16)rd16(p + 4));
            e[2] = (s16)(e[2] + (s16)rd16(p + 4));
            e[1] = (s16)(e[1] + (s16)rd16(p + 6));
            e[3] = (s16)(e[3] + (s16)rd16(p + 6));
            p += 8;
        } else if (p[0] == 1) {
            const u8 *bmp = bitmap_record(data, rd16(p + 2));
            e[0] = (s16)rd16(p + 4);
            e[1] = (s16)((s16)rd16(p + 6) - (s16)rd16(bmp + 2) + 1);
            e[2] = (s16)(rd16(bmp) + e[0]);
            e[3] = (s16)rd16(p + 6);
            p += 8;
        } else {
            const u8 *pt = p + 4;
            int k;

            for (k = 0; k < n; k++, pt += 4) {
                s16 x = (s16)rd16(pt), y = (s16)rd16(pt + 2);

                if (e[0] || e[2]) {
                    if (x < e[0]) e[0] = x;
                    if (y < e[1]) e[1] = y;
                    if (x > e[2]) e[2] = x;
                    if (y > e[3]) e[3] = y;
                } else {
                    e[0] = e[2] = x;
                    e[1] = e[3] = y;
                }
            }
            p = pt;
        }

        if (box[0] || box[2]) {
            box_union(box, e);
        } else {
            memcpy(box, e, sizeof(e));
        }
    }
    return 1;
}

/* Sprite index of frame `frame` of animation `anim` (body) in a hyb file. */
int hyb_anim_sprite(const u8 *data, int anim, int frame)
{
    const u8 *table = data + rd32(data + 8);
    const u8 *rec = table + rd32(table + anim * 4);

    return rd16(rec + 2 + frame * 8);
}
