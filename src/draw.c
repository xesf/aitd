/*
 * draw.c - scene composition (0x41a215..0x41b31f, 0x40c290, 0x4069f2).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actor.h"
#include "aitd.h"
#include "anim.h"
#include "draw.h"
#include "game.h"
#include "hyb.h"
#include "life.h"
#include "palette.h"
#include "platform.h"
#include "r3d.h"
#include "raster.h"
#include "subsystems.h"
#include "text.h"
#include "world.h"

u8 *g_screen;
s16 g_clip_y_default = 10000;  /* 0x4350e1 */
s16 g_fade_request;            /* 0x43505c */
s16 g_black_screen;            /* 0x43505a */
s32 g_force_redraw;            /* 0x435138 */

/* Animated background pieces, 24 bytes each at 0x441210 in the original. */
struct bganim {
    u16 frame;          /* +0x00 */
    u32 time;           /* +0x02 */
    const u8 *cursor;   /* +0x08 current frame record */
    const u8 *anim;     /* +0x0c */
    s16 screen[4];      /* +0x10 */
};

#define BGANIM_MAX 32
static struct bganim s_bganims[BGANIM_MAX];
static u16 s_bganim_count;  /* 0x430d98 */

void draw_init(void)
{
    if (g_screen == NULL) {
        g_screen = mem_alloc_checked((size_t)g_screen_width * (size_t)g_screen_height
                                     + AITD_FRAME_EXTRA);
        memset(g_screen, 0, (size_t)g_screen_width * (size_t)g_screen_height);
    }
    raster_init(g_screen, g_screen_width, g_screen_height);
}

void draw_present(void)
{
    platform_video_present(g_screen, g_screen_width, g_screen_height);
}

/* ---- sorting (0x40c290) ------------------------------------------------ */

static int overlaps(s16 a1, s16 a2, s16 b1, s16 b2)
{
    return (a1 > b1 && a1 < b2) || (a2 > b1 && a2 < b2)
        || (b1 > a1 && b1 < a2) || (b2 > a1 && b2 < a2);
}

static s16 nearest(s16 cam, s16 v1, s16 v2)
{
    s32 d1 = abs((s32)cam - v1);
    s32 d2 = abs((s32)cam - v2);

    return (s16)(d1 < d2 ? d1 : d2);
}

/* 0x40c290 - farther first. */
static int compare_actors(s16 ia, s16 ib)
{
    const struct actor *a = &g_actors[ia];
    const struct actor *b = &g_actors[ib];
    s16 za[6], zb[6];
    s16 cam_x, cam_y, cam_z;
    s32 layer_a, layer_b;
    s16 da = 0, db = 0;
    int flags = 0;

    r3d_get_camera_position(&cam_x, &cam_y, &cam_z);

    memcpy(za, a->zv, sizeof(za));
    memcpy(zb, b->zv, sizeof(zb));
    if (a->room != g_cur_room) {
        zv_to_room(za, a->room, g_cur_room);
    }
    if (b->room != g_cur_room) {
        zv_to_room(zb, b->room, g_cur_room);
    }

    layer_a = (((s32)za[2] + za[3]) >> 1) - 2000;
    layer_a = (layer_a / 2000) * 2000;
    layer_b = (((s32)zb[2] + zb[3]) >> 1) - 2000;
    layer_b = (layer_b / 2000) * 2000;

    if ((s16)layer_a != (s16)layer_b) {
        da = (s16)abs((s32)cam_y - 2000 - (s16)layer_a);
        db = (s16)abs((s32)cam_y - 2000 - (s16)layer_b);
    } else {
        if (overlaps(za[0], za[1], zb[0], zb[1])) flags |= 1;
        if (overlaps(za[4], za[5], zb[4], zb[5])) flags |= 2;

        if (flags == 0) {
            da = distance_xz(cam_x, cam_z, (s16)((za[0] >> 1) + (za[1] >> 1)),
                             (s16)((za[4] >> 1) + (za[5] >> 1)));
            db = distance_xz(cam_x, cam_z, (s16)((zb[0] >> 1) + (zb[1] >> 1)),
                             (s16)((zb[4] >> 1) + (zb[5] >> 1)));
        } else {
            if (flags & 2) {
                da = nearest(cam_x, za[0], za[1]);
                db = nearest(cam_x, zb[0], zb[1]);
            }
            if (flags & 1) {
                da = (s16)(da + nearest(cam_z, za[4], za[5]));
                db = (s16)(db + nearest(cam_z, zb[4], zb[5]));
            }
        }
    }

    if (da > db) return -1;
    if (da < db) return 1;
    return 0;
}

/* 0x40c7da - the original runs the Watcom qsort; an insertion sort keeps
 * the order deterministic on every host. */
void draw_sort_actors(void)
{
    int i, j;

    for (i = 1; i < g_visible_count; i++) {
        s16 v = g_visible_actors[i];

        for (j = i; j > 0 && compare_actors(g_visible_actors[j - 1], v) > 0; j--) {
            g_visible_actors[j] = g_visible_actors[j - 1];
        }
        g_visible_actors[j] = v;
    }
}

/* ---- helpers ----------------------------------------------------------- */

/* Time Gate (TG 0x40b95c, 0x40b767): an actor takes the light of the
 * current camera's view of its room, direction at +10 and position at +16
 * of the 22 byte view record, and keeps it while the camera does not see
 * that room. Other titles have no light in their view records. */
enum { TG_VIEW_SIZE = 22, TG_VIEW_LIGHT = 10 };

static s16 s_actor_light[ACTOR_MAX][6];   /* TG actor +0xbc..+0xc6 */

static void actor_light(const struct actor *a)
{
    s16 *light = s_actor_light[a - g_actors];
    const u8 *cam;
    int count, i, k;

    if (g_camera_view_size < TG_VIEW_SIZE || g_cur_camera < 0) {
        return;
    }
    cam = g_room_cameras[g_cur_camera];
    count = CAMERA_VIEW_COUNT(cam);
    for (i = 0; i < count; i++) {
        const u8 *view = CAMERA_VIEW(cam, i);

        if (blob_s16(view, 0) == a->room) {
            for (k = 0; k < 6; k++) {
                light[k] = blob_s16(view, TG_VIEW_LIGHT + k * 2);
            }
            break;
        }
    }
    r3d_set_light(light, light + 3);
}

static void clamp_bbox(void)
{
    if (g_r3d_bbox.xmin < 0) g_r3d_bbox.xmin = 0;
    if (g_r3d_bbox.xmax >= g_screen_width) g_r3d_bbox.xmax = (s16)(g_screen_width - 1);
    if (g_r3d_bbox.ymin < 0) g_r3d_bbox.ymin = 0;
    if (g_r3d_bbox.ymax >= g_screen_height) g_r3d_bbox.ymax = (s16)(g_screen_height - 1);
}

static int bbox_on_screen(void)
{
    return g_r3d_bbox.xmin < g_screen_width && g_r3d_bbox.ymin < g_screen_height
        && g_r3d_bbox.xmax >= 0 && g_r3d_bbox.ymax >= 0;
}

static void store_bbox(struct actor *a)
{
    a->screen[0] = g_r3d_bbox.xmin;
    a->screen[1] = g_r3d_bbox.ymin;
    a->screen[2] = g_r3d_bbox.xmax;
    a->screen[3] = g_r3d_bbox.ymax;
}

/* The body of an actor, re-posed when it was just (re)loaded. */
static u8 *actor_body(struct actor *a)
{
    u8 *body = hqr_get(g_cache_body, a->body, NULL);

    if (body != NULL && g_hqr_loaded && a->anim != -1) {
        const u8 *anim = hqr_get(g_cache_anim, a->anim, NULL);
        if (anim != NULL) {
            anim_set_frame(a->frame, anim, body);
        }
    }
    return body;
}

static void reset_clip(void)
{
    raster_set_clip(0, 0, (s16)(g_screen_width - 1), (s16)(g_screen_height - 1));
}

/* ---- masks (0x419360, 0x41a215) ---------------------------------------- */

static u16 rd16(const u8 *p)
{
    return (u16)(p[0] | (p[1] << 8));
}

/* 0x419360 - restore the background pixels a mask covers, from g_back_buffer
 * into the current target. Rows are clipped, columns are not. */
static void draw_mask(const u8 *mask, const u8 *source)
{
    const u8 *src;
    u8 *dst;
    s16 rewind = (s16)rd16(mask + 8);
    u16 rows = rd16(mask + 0x0a);
    u16 ymin = rd16(mask + 2);
    u16 ymax = rd16(mask + 6);
    const u8 *p = mask + 0x0c;
    u16 clip_ymin = (u16)g_raster.clip_ymin;
    u16 clip_ymax = (u16)g_raster.clip_ymax;
    u16 skip;
    int r;

    src = source + rd16(mask);
    dst = g_raster.target + rd16(mask);

    if (ymax > clip_ymax) {
        rows = (u16)(rows - (ymax - clip_ymax));
    }
    if (ymax < clip_ymin) {
        return;
    }
    if (ymin > clip_ymax) {
        return;
    }

    if (clip_ymin > ymin) {
        skip = (u16)(clip_ymin - ymin);
        rows = (u16)(rows - skip);
        while (skip-- > 0) {
            p += 2 + rd16(p) * 2;
            dst += g_raster.pitch;
            src += 320;
        }
    }

    dst += (s32)ymin * g_raster.pitch;
    src += (s32)ymin * 320;

    for (r = 0; r < rows; r++) {
        u16 spans = rd16(p);

        p += 2;
        while (spans-- > 0) {
            src += p[0];
            dst += p[0];
            memcpy(dst, src, p[1]);
            src += p[1];
            dst += p[1];
            p += 2;
        }
        dst -= rewind;
        src -= rewind;
        src += 320;
        dst += g_raster.pitch;
    }
}

static const u8 *s_mask_source;

/* Alone in the Dark 1 keeps no mask blobs: view +2 lists cover entries
 * (rectangle count, offset from the list to its polygons, rectangles of
 * x1, z1, x2, z2), and when the actor's box lies inside a rectangle the
 * background inside each polygon (point count, then x, y screen words) is
 * put back over it. Worked out from the data; the original routine is not
 * rebuilt. */
static u8 s_cover[64000];

static void draw_cover(const u8 *list, const struct actor *a, const u8 *source)
{
    s16 x1 = (s16)(a->zv[0] / 10), x2 = (s16)(a->zv[1] / 10);
    s16 z1 = (s16)(a->zv[4] / 10), z2 = (s16)(a->zv[5] / 10);
    const u8 *entry = list + 2;
    int count = blob_s16(list, 0);
    int i;

    for (i = 0; i < count; i++) {
        int rects = blob_s16(entry, 0);
        const u8 *r = entry + 4;
        int k, hit = 0;

        for (k = 0; k < rects && !hit; k++, r += 8) {
            hit = x1 >= blob_s16(r, 0) && z1 >= blob_s16(r, 2)
               && x2 <= blob_s16(r, 4) && z2 <= blob_s16(r, 6);
        }
        if (hit) {
            const u8 *poly = list + (u16)blob_s16(entry, 2);
            int polys = blob_s16(poly, 0);
            u8 *target = g_raster.target;
            s16 pts[2 * 64];
            int x, y, n, j;

            poly += 2;
            g_raster.target = s_cover;
            for (j = 0; j < polys; j++) {
                n = blob_s16(poly, 0);
                for (k = 0; k < n && k < 64; k++) {
                    pts[k * 2] = blob_s16(poly, 2 + k * 4);
                    pts[k * 2 + 1] = blob_s16(poly, 4 + k * 4);
                }
                raster_poly(pts, n < 64 ? n : 64, 0, 0xff);
                poly += 2 + n * 4;
            }
            g_raster.target = target;

            for (y = g_raster.clip_ymin; y <= g_raster.clip_ymax; y++) {
                for (x = g_raster.clip_xmin; x <= g_raster.clip_xmax; x++) {
                    if (s_cover[y * 320 + x]) {
                        target[y * g_raster.pitch + x] = source[y * 320 + x];
                        s_cover[y * 320 + x] = 0;
                    }
                }
            }
        }
        entry += 4 + rects * 8;
    }
}

/* 0x41a215 */
void draw_masks(struct actor *a)
{
    const u8 *cam = g_room_cameras[g_cur_camera];
    int views = CAMERA_VIEW_COUNT(cam);
    const u8 *view = CAMERA_VIEW(cam, 0);
    const u8 *zones;
    int overlap = game_current()->mask_layout == MASK_SLOT_FILES
               || game_current()->mask_layout == MASK_CAMERA_INDEXED;
    int i, j, count;

    a->screen[0] = g_r3d_bbox.xmin;
    a->screen[1] = g_r3d_bbox.ymin;
    a->screen[2] = g_r3d_bbox.xmax;
    a->screen[3] = g_r3d_bbox.ymax;
    raster_set_clip(g_r3d_bbox.xmin, g_r3d_bbox.ymin, g_r3d_bbox.xmax, g_r3d_bbox.ymax);

    for (i = 0; views > 0; i++, views--, view += g_camera_view_size) {
        if (blob_s16(view, 0) == a->room) {
            break;
        }
    }
    if (views == 0) {
        return;
    }

    if (game_current()->mask_layout == MASK_COVER_ZONES) {
        draw_cover(cam + (blob_s16(view, 2) & ~1), a, s_mask_source);
        reset_clip();
        return;
    }

    zones = cam + (blob_s16(view, 2) & ~1);
    count = blob_s16(zones, 0);
    zones += 2;

    for (j = 0; j < count; j++) {
        int rects = blob_s16(zones, 0);
        const u8 *r = zones + 2;
        s16 x1 = (s16)(a->zv[0] / 10), x2 = (s16)(a->zv[1] / 10);
        s16 z1 = (s16)(a->zv[4] / 10), z2 = (s16)(a->zv[5] / 10);
        int k;

        for (k = 0; k < rects; k++, r += 8) {
            /* AITD3 wants the box inside a rectangle; Time Gate any
             * overlap (TG 0x42a422). */
            int hit = overlap
                ? x1 <= blob_s16(r, 4) && x2 > blob_s16(r, 0)
                  && z1 <= blob_s16(r, 6) && z2 > blob_s16(r, 2)
                : blob_s16(r, 0) <= x1 && blob_s16(r, 2) <= z1
                  && blob_s16(r, 4) >= x2 && blob_s16(r, 6) >= z2;
            const u8 *mask = hit ? world_mask(i, j) : NULL;

            if (mask != NULL) {
                draw_mask(mask, s_mask_source);
            }
            if (hit) {
                break;
            }
        }
        zones += 2 + rects * 8;
    }

    reset_clip();
}

/* ---- sprite actors (flag 0x200) ---------------------------------------- */

static void sprite_bbox(struct actor *a, int draw)
{
    const u8 *data;
    int sprite;
    s16 box[4];

    if (a->anim == -1 || a->body == -1) {
        return;
    }
    data = hqr_get(g_cache_hyb, a->anim, NULL);
    sprite = hyb_anim_sprite(data, a->body, a->frame);
    if (draw) {
        hyb_render(sprite, 0, 0, data);
    }
    if (hyb_bbox(box, sprite, data)) {
        g_r3d_bbox.xmin = box[0];
        g_r3d_bbox.ymin = box[1];
        g_r3d_bbox.xmax = box[2];
        g_r3d_bbox.ymax = box[3];
    } else {
        g_r3d_bbox.xmin = 0;
        g_r3d_bbox.ymin = 0;
        g_r3d_bbox.xmax = 1;
        g_r3d_bbox.ymax = 1;
    }
}

/* 0x41a615, flag 0x200 branch: measure only. */
static void sprite_measure(struct actor *a)
{
    sprite_bbox(a, 0);
    clamp_bbox();
    a->screen[0] = g_r3d_bbox.xmin;
    a->screen[1] = g_r3d_bbox.ymin;
    a->screen[2] = g_r3d_bbox.xmax;
    a->screen[3] = g_r3d_bbox.ymax;
}

/* 0x41af3b */
static void sprite_draw(struct actor *a)
{
    sprite_bbox(a, 1);
    a->screen[0] = g_r3d_bbox.xmin;
    a->screen[1] = g_r3d_bbox.ymin;
    a->screen[2] = g_r3d_bbox.xmax;
    a->screen[3] = g_r3d_bbox.ymax;
}

/* ---- passes ------------------------------------------------------------ */

/* 0x41a615 - screen boxes before anything is drawn. */
void draw_measure_actors(void)
{
    int i;

    for (i = 0; i < g_visible_count; i++) {
        struct actor *a = &g_actors[g_visible_actors[i]];

        if (a->flags & 0x200) {
            sprite_measure(a);
            continue;
        }
        if (a->flags & (1 | 8)) {
            u8 *body = actor_body(a);

            if (body == NULL) {
                continue;
            }
            r3d_measure_body((s16)(a->step_x + a->world_x),
                             (s16)(a->step_y + a->world_y),
                             (s16)(a->step_z + a->world_z),
                             a->alpha, a->beta, a->gamma, body);
            clamp_bbox();
            store_bbox(a);
        }
    }
}

/* 0x41a94a - draw the static actors into the background layer. */
void draw_static_actors(void)
{
    struct raster_image front;
    int i;

    raster_push_target(g_back_buffer);

    front.width = (u16)g_screen_width;
    front.height = (u16)g_screen_height;
    front.pixels = g_front_buffer;
    raster_blit(0, 0, &front);
    reset_clip();

    for (i = 0; i < g_visible_count; i++) {
        struct actor *a = &g_actors[g_visible_actors[i]];
        u8 *body;

        if ((a->flags & 0x200) || !(a->flags & 8)) {
            continue;
        }

        body = actor_body(a);
        if (body == NULL) {
            continue;
        }

        r3d_set_clip_y((a->flags & 0x400) ? 10000 : g_clip_y_default);
        actor_light(a);
        r3d_draw_body((s16)(a->step_x + a->world_x), (s16)(a->step_y + a->world_y),
                      (s16)(a->step_z + a->world_z), a->alpha, a->beta, a->gamma,
                      body);
        clamp_bbox();
        store_bbox(a);

        if (bbox_on_screen()) {
            s_mask_source = g_front_buffer;
            draw_masks(a);
        }
    }

    g_static_dirty = 0;
    raster_pop_target();
}

static int rect_overlap(const s16 *a, const s16 *b)   /* 0x41b320 */
{
    return !(a[0] > b[2] || a[2] < b[0] || a[1] > b[3] || a[3] < b[1]);
}

/* 0x41a3f6 - anything drawn later over a static actor forces it to be
 * redrawn this frame (flag 4). */
void draw_mark_overlaps(void)
{
    int total = g_visible_count + s_bganim_count;
    int i, j;

    for (i = 0; i < total - 1; i++) {
        s16 entry = g_draw_list[i];
        const s16 *box;

        if (entry >= 0) {
            const struct actor *a = &g_actors[entry];
            if (!(a->flags & 0x225)) {
                continue;
            }
            box = a->screen;
        } else {
            box = s_bganims[entry & 0x7fff].screen;
        }

        for (j = i + 1; j < total; j++) {
            s16 other = g_draw_list[j];
            struct actor *b;

            if (other < 0) {
                continue;
            }
            b = &g_actors[other];
            if ((b->flags & 8) && b->screen[0] < g_screen_width
                && b->screen[1] < g_screen_height && b->screen[2] >= 0
                && b->screen[3] >= 0 && rect_overlap(box, b->screen)) {
                b->flags |= 4;
            }
        }
    }
}

/* 0x41ac57 */
void draw_frame(s16 full)
{
    struct raster_image back;
    int total, i;

    if (g_force_redraw) {
        full = 1;
    }
    if (!g_black_screen && g_fade_request) {
        full = 1;
    }

    /* The original only restores the dirty rectangles of the last frame
     * unless a full redraw is due; copying the whole layer is equivalent. */
    back.width = (u16)g_screen_width;
    back.height = (u16)g_screen_height;
    back.pixels = g_back_buffer;
    raster_blit(0, 0, &back);
    reset_clip();

    total = g_visible_count + s_bganim_count;
    for (i = 0; i < total; i++) {
        s16 entry = g_draw_list[i];

        if (entry >= 0) {
            struct actor *a = &g_actors[entry];
            u8 *body;

            if (!(a->flags & 0x225)) {
                continue;
            }
            a->flags &= ~4;

            if (a->flags & 0x20) {
                special_draw(entry, a->visible);
                if (!a->visible) {
                    continue;
                }
            } else if (a->flags & 0x200) {
                sprite_draw(a);
            } else {
                body = actor_body(a);
                if (body == NULL) {
                    continue;
                }
                r3d_set_clip_y((a->flags & 0x400) ? 10000 : g_clip_y_default);
                actor_light(a);
                r3d_draw_body((s16)(a->step_x + a->world_x),
                              (s16)(a->step_y + a->world_y),
                              (s16)(a->step_z + a->world_z),
                              a->alpha, a->beta, a->gamma, body);
                if (a->anim_action_type && a->hot_point_id != -1) {
                    anim_hot_point(a->hot_point_id, body, a->hot_point);
                }
            }

            clamp_bbox();
            if (bbox_on_screen()) {
                s_mask_source = g_back_buffer;
                draw_masks(a);
            }
        } else {
            bganim_draw((s16)(entry & 0x7fff));
        }
    }

    if (message_draw()) {
        /* 0x41b1e2: the message area joins the dirty list. */
    }

    if (g_black_screen) {
        memset(g_screen, 0, (size_t)g_screen_width * (size_t)g_screen_height);
        if (full) {
            palette_set_range(g_palette_buffer, 0, 256);
            draw_present();
        }
    } else if (full) {
        if (full == 2 || g_fade_request) {
            palette_blackout();
            draw_present();
            palette_fade(0, 0);
            g_fade_request = 0;
        } else {
            palette_set_range(g_palette_buffer, 0, 256);
            draw_present();
        }
    } else {
        draw_present();
    }

    g_redraw_state = 0;
}

/* ---- background animations (0x406cea, 0x406d8c, 0x4069f2) --------------- */

/* 0x406d8c */
void bganim_reset(void)
{
    s_bganim_count = 0;
}

/* 0x406cea - record `index` of the camera's ANIMxx entry. */
void bganim_add(u16 index)
{
    struct bganim *b;
    const u8 *table;
    const u8 *rec;

    if (g_camera_bganims == NULL || s_bganim_count >= BGANIM_MAX) {
        return;
    }

    table = g_camera_bganims + blob_u32(g_camera_bganims, 8);
    rec = table + blob_u32(table, index * 4);

    b = &s_bganims[s_bganim_count];
    b->anim = rec;
    b->cursor = rec + 2;
    b->frame = 0;
    ticks_update();
    b->time = g_ticks;
    s_bganim_count++;
}

/* Opcode 93: is background animation `index` on `frame` this tick. */
int bganim_frame_is(s16 index, s16 frame)
{
    if (index < 0 || index >= s_bganim_count) {
        return 0;
    }
    return s_bganims[index].time != 0 && s_bganims[index].frame == (u16)frame;
}


/*
 * 0x4069f2 - step every background animation and slot it into the draw
 * list in front of the last actor it covers.
 */
void bganim_insert(void)
{
    const u8 *cam = g_room_cameras[g_cur_camera];
    int i;

    memmove(g_draw_list, g_visible_actors, sizeof(s16) * (size_t)g_visible_count);

    for (i = 0; i < s_bganim_count; i++) {
        struct bganim *b = &s_bganims[i];
        int j;

        ticks_update();
        if (g_ticks - b->time >= rd16(b->cursor + 2)) {
            b->frame++;
            b->cursor += 8;
            if (b->frame == b->anim[1]) {
                b->frame = 0;
                b->cursor = b->anim + 2;
            }
            b->time = g_ticks;
        }

        for (j = i + g_visible_count - 1; j >= 0; j--) {
            const struct actor *a;
            const u8 *view;
            int views, k;
            const u8 *zones;

            if (g_draw_list[j] < 0) {
                continue;
            }
            a = &g_actors[g_draw_list[j]];

            view = CAMERA_VIEW(cam, 0);
            views = CAMERA_VIEW_COUNT(cam);
            while (views > 0 && blob_s16(view, 0) != a->room) {
                view += g_camera_view_size;
                views--;
            }
            if (views == 0) {
                continue;
            }

            zones = cam + 2 + (blob_s16(view, 6) & ~1);
            for (k = 0; k < i; k++) {
                zones += (blob_s16(zones, 0) * 4 + 1) * 2;
            }
            {
                int rects = blob_s16(zones, 0);
                const u8 *r = zones + 2;
                s16 x1 = (s16)(a->zv[0] / 10), x2 = (s16)(a->zv[1] / 10);
                s16 z1 = (s16)(a->zv[4] / 10), z2 = (s16)(a->zv[5] / 10);
                int hit = 0;

                for (k = 0; k < rects; k++, r += 8) {
                    if (blob_s16(r, 0) <= x1 && blob_s16(r, 2) <= z1
                        && blob_s16(r, 4) >= x2 && blob_s16(r, 6) >= z2) {
                        hit = 1;
                        break;
                    }
                }
                if (hit) {
                    break;
                }
            }
        }

        memmove(&g_draw_list[j + 2], &g_draw_list[j + 1],
                sizeof(s16) * (size_t)(i + g_visible_count - j - 1));
        g_draw_list[j + 1] = (s16)(0x8000 | i);
    }
}

/* 0x41ad2d */
void bganim_draw(s16 index)
{
    struct bganim *b = &s_bganims[index];
    int sprite = rd16(b->cursor);
    s16 box[4];

    reset_clip();
    hyb_render(sprite, 0, 0, g_camera_bganims);
    if (hyb_bbox(box, sprite, g_camera_bganims)) {
        b->screen[0] = box[0] < 0 ? 0 : box[0];
        b->screen[1] = box[1] < 0 ? 0 : box[1];
        b->screen[2] = box[2] >= g_screen_width ? (s16)(g_screen_width - 1) : box[2];
        b->screen[3] = box[3] >= g_screen_height ? (s16)(g_screen_height - 1) : box[3];
    }
}
