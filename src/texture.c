/*
 * texture.c - Time Gate's textured and Gouraud shaded triangles.
 *
 * TG320.EX_ draws body primitives 8, 9 and 10 (absent from WAITD3.EXE)
 * through one of three filler sets chosen by the detail option
 * (0x41c9b3): flat, medium and high. Only the high set is rebuilt:
 *
 *   8   0x41bc2c  texels from the body's page; texel 0 is transparent
 *   9   0x412e4f  Gouraud: shade[colour][level], level per corner
 *   10  0x4133bc  both: shade[texel][level]
 *
 * Pages come from TEXTURES.PAK, 256 texels a row and as many rows as the
 * entry holds. The shade table is ITD_RESS entry 54, 64 rows of 256
 * colours, kept transposed (0x417cfe) so a colour's 64 levels are
 * adjacent. Corner levels come from the light of the camera view the
 * actor stands in (0x411e2f).
 *
 * The original clips the triangle against the screen (0x41b274..) and
 * steps every edge and span with Bresenham error terms. This version walks
 * edges and spans in 16.16 fixed point and clips spans: the same coverage
 * and mapping, not proven pixel for pixel.
 */

#include <string.h>

#include "aitd.h"
#include "pak.h"
#include "raster.h"
#include "texture.h"

enum {
    TEX_SLOTS = 40,             /* 0x28 */
    TEX_ROW = 256,
    SHADE_LEVELS = 64,
    SHADE_COLOURS = 256,
    SHADE_SIZE = SHADE_LEVELS * SHADE_COLOURS,
    ITD_RESS_SHADE = 54,
    ROWS_MAX = 480
};

struct tex_slot {
    u32 uses;
    s32 index;
    u8 *data;
    u32 size;
};

static struct tex_slot s_slots[TEX_SLOTS];  /* 0x451fd0.. */
static u8 s_shade[SHADE_SIZE];               /* byte_4583A4 */
static int s_shade_loaded;

static void slot_load(struct tex_slot *slot, int index)
{
    slot->uses = 0;
    slot->index = index;
    slot->data = pak_load("TEXTURES.PAK", index, &slot->size);
    if (slot->data == NULL) {
        slot->size = 0;
    }
}

struct tex_page texture_page(int index, const u8 *keep)
{
    struct tex_page page = { NULL, 0 };
    int i, pick = -1;
    u32 most = 0;

    for (i = 0; i < TEX_SLOTS; i++) {
        if (s_slots[i].data != NULL && s_slots[i].index == index) {
            s_slots[i].uses++;
            page.texels = s_slots[i].data;
            page.size = s_slots[i].size;
            return page;
        }
    }
    for (i = 0; i < TEX_SLOTS && pick < 0; i++) {
        if (s_slots[i].data == NULL) {
            pick = i;
        }
    }
    /* 0x41fe9a: no free slot, the most used one goes. */
    if (pick < 0) {
        pick = 0;
        for (i = 0; i < TEX_SLOTS; i++) {
            if (s_slots[i].data != keep && s_slots[i].uses > most) {
                most = s_slots[i].uses;
                pick = i;
            }
        }
        if (s_slots[pick].data == keep) {
            pick = (pick + 1) % TEX_SLOTS;
        }
        mem_free(s_slots[pick].data);
        s_slots[pick].data = NULL;
    }
    slot_load(&s_slots[pick], index);
    page.texels = s_slots[pick].data;
    page.size = s_slots[pick].size;
    return page;
}

const u8 *texture_shade_table(void)
{
    if (!s_shade_loaded) {
        u32 size = 0;
        u8 *src = pak_load("ITD_RESS.PAK", ITD_RESS_SHADE, &size);
        int colour, level;

        s_shade_loaded = 1;
        if (src == NULL || size < SHADE_SIZE) {
            /* No table: every level shows the plain colour. */
            for (colour = 0; colour < SHADE_COLOURS; colour++) {
                memset(s_shade + colour * SHADE_LEVELS, colour, SHADE_LEVELS);
            }
        } else {
            for (colour = 0; colour < SHADE_COLOURS; colour++) {
                for (level = 0; level < SHADE_LEVELS; level++) {
                    s_shade[colour * SHADE_LEVELS + level] = src[level * SHADE_COLOURS + colour];
                }
            }
        }
        if (src != NULL) {
            mem_free(src);
        }
    }
    return s_shade;
}

/* The light's direction and position are in camera space; the y term adds
 * the position where the others subtract it, as the original does. */
s16 texture_light_level(const s16 *point, const s16 *dir, const s16 *pos)
{
    s32 v = (((s32)dir[1] * (s32)(pos[1] + point[1])) >> 16)
          + (((s32)dir[0] * (s32)(point[0] - pos[0])) >> 16)
          + (((s32)dir[2] * (s32)(point[2] - pos[2])) >> 16);

    v = v / 8 + 32;     /* signed divide: truncates toward zero */
    if (v < 0) {
        return 0;
    }
    return (s16)(v >= SHADE_LEVELS ? SHADE_LEVELS - 1 : v);
}

/* ---- the scan converter ------------------------------------------------ */

struct edge_row {
    s32 x, u, v, l;     /* 16.16 */
};

static struct edge_row s_left[ROWS_MAX], s_right[ROWS_MAX];

static void walk_edge(const struct tex_vertex *a, const struct tex_vertex *b,
                      s16 ymin, s16 ymax, int *seen)
{
    const struct tex_vertex *t;
    s32 dy, x, u, v, l, sx, su, sv, sl;
    int y;

    if (a->y > b->y) {
        t = a; a = b; b = t;
    }
    dy = b->y - a->y;
    x = (s32)a->x << 16;
    u = (s32)a->u << 16;
    v = (s32)a->v << 16;
    l = (s32)a->level << 16;
    if (dy == 0) {
        sx = su = sv = sl = 0;
    } else {
        sx = (((s32)b->x - a->x) << 16) / dy;
        su = (((s32)b->u - a->u) << 16) / dy;
        sv = (((s32)b->v - a->v) << 16) / dy;
        sl = (((s32)b->level - a->level) << 16) / dy;
    }

    for (y = a->y; y <= b->y; y++, x += sx, u += su, v += sv, l += sl) {
        int row;

        if (y < ymin || y > ymax) {
            continue;
        }
        row = y - ymin;
        if (!seen[row] || x < s_left[row].x) {
            s_left[row].x = x;
            s_left[row].u = u;
            s_left[row].v = v;
            s_left[row].l = l;
        }
        if (!seen[row] || x > s_right[row].x) {
            s_right[row].x = x;
            s_right[row].u = u;
            s_right[row].v = v;
            s_right[row].l = l;
        }
        seen[row] = 1;
        if (dy == 0) {
            /* A flat edge spans from one end to the other. */
            const struct edge_row end = {
                (s32)b->x << 16, (s32)b->u << 16, (s32)b->v << 16, (s32)b->level << 16
            };
            if (end.x < s_left[row].x) s_left[row] = end;
            if (end.x > s_right[row].x) s_right[row] = end;
            break;
        }
    }
}

static u8 texel(const struct tex_page *page, s32 u, s32 v)
{
    s32 index = (u >> 16) + ((v >> 16) << 8);

    if (page->texels == NULL || index < 0 || (u32)index >= page->size) {
        return 0;
    }
    return page->texels[index];
}

void texture_triangle(const struct tex_vertex *in, enum tex_fill fill,
                      const struct tex_page *page, int colour)
{
    struct tex_vertex v[3];
    const u8 *shade = (fill == TEX_FILL_TEXTURE) ? NULL : texture_shade_table();
    int seen[ROWS_MAX];
    s16 ymin = 0x7fff, ymax = (s16)0x8000;
    int i, y;

    for (i = 0; i < 3; i++) {
        v[i] = in[i];
        v[i].x = (s16)(v[i].x + g_raster.origin_x);
        v[i].y = (s16)(v[i].y + g_raster.origin_y);
        if (v[i].y < ymin) ymin = v[i].y;
        if (v[i].y > ymax) ymax = v[i].y;
    }
    if (ymax < g_raster.clip_ymin || ymin > g_raster.clip_ymax) {
        return;
    }
    if (ymin < g_raster.clip_ymin) ymin = g_raster.clip_ymin;
    if (ymax > g_raster.clip_ymax) ymax = g_raster.clip_ymax;
    if (ymax - ymin + 1 > ROWS_MAX) {
        return;
    }

    memset(seen, 0, sizeof(int) * (size_t)(ymax - ymin + 1));
    walk_edge(&v[0], &v[1], ymin, ymax, seen);
    walk_edge(&v[1], &v[2], ymin, ymax, seen);
    walk_edge(&v[2], &v[0], ymin, ymax, seen);

    for (y = ymin; y <= ymax; y++) {
        const struct edge_row *l = &s_left[y - ymin];
        const struct edge_row *r = &s_right[y - ymin];
        s32 xl = l->x >> 16, xr = r->x >> 16, dx = xr - xl;
        s32 u = l->u, tv = l->v, lv = l->l;
        s32 su = 0, sv = 0, sl = 0;
        u8 *dst;
        s32 x;

        if (!seen[y - ymin]) {
            continue;
        }
        if (dx > 0) {
            su = (r->u - l->u) / dx;
            sv = (r->v - l->v) / dx;
            sl = (r->l - l->l) / dx;
        }
        if (xl < g_raster.clip_xmin) {
            s32 skip = g_raster.clip_xmin - xl;
            u += su * skip;
            tv += sv * skip;
            lv += sl * skip;
            xl = g_raster.clip_xmin;
        }
        if (xr > g_raster.clip_xmax) {
            xr = g_raster.clip_xmax;
        }
        dst = g_raster.target + (s32)y * g_raster.pitch;
        for (x = xl; x <= xr; x++, u += su, tv += sv, lv += sl) {
            u8 t;

            switch (fill) {
            case TEX_FILL_TEXTURE:
                t = texel(page, u, tv);
                if (t != 0) {
                    dst[x] = t;
                }
                break;
            case TEX_FILL_GOURAUD:
                dst[x] = shade[(colour * SHADE_LEVELS + ((lv >> 16) & 0xff)) & (SHADE_SIZE - 1)];
                break;
            case TEX_FILL_TEXTURE_LIT:
                t = texel(page, u, tv);
                if (t != 0) {
                    dst[x] = shade[(t * SHADE_LEVELS + (lv >> 16)) & (SHADE_SIZE - 1)];
                }
                break;
            }
        }
    }
}
