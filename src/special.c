/*
 * special.c - short lived effect actors (object == -2): dust, blood and
 * sparks, and one-shot impact bodies.
 *
 *   special_create  0x40c9ef
 *   special_draw    0x40cf8d   (moves the particles too)
 *
 * Particle clouds live in blocks of the unnamed cache (0x407581), indexed
 * by the actor's frame field; the block is a fake body: a colour word, a
 * point count, the points, then one (life, speed) or (angle, speed) pair
 * per point.
 */

#include <stdlib.h>
#include <string.h>

#include "actor.h"
#include "aitd.h"
#include "draw.h"
#include "life.h"
#include "r3d.h"
#include "raster.h"
#include "world.h"

#define PARTICLES 30
#define BLOCK_BYTES (4 + PARTICLES * 6 + PARTICLES * 4)
#define BLOCK_MAX 50

static s16 *s_blocks[BLOCK_MAX];
static u32 s_rand_next = 1;   /* Watcom's per thread seed */

/* 0x424f44 - the C library rand(). */
static s16 crt_rand(void)
{
    s_rand_next = s_rand_next * 0x41c64e6du + 0x3039u;
    return (s16)((s_rand_next >> 16) & 0x7fff);
}

/* 0x40c9a6 */
static s16 special_rand_between(s16 lo, s16 hi)
{
    s16 span = (s16)(hi - lo);

    if (span == 0) {
        return lo;
    }
    return (s16)(lo + crt_rand() % span);
}

/* 0x4199bf / 0x419a75 / 0x419aea */
static s16 special_block_alloc(void)
{
    int i;

    for (i = 0; i < BLOCK_MAX; i++) {
        if (s_blocks[i] == NULL) {
            s_blocks[i] = calloc(1, BLOCK_BYTES);
            return s_blocks[i] != NULL ? (s16)i : -1;
        }
    }
    return -1;
}

static s16 *block_get(s16 id)
{
    if (id < 0 || id >= BLOCK_MAX) {
        return NULL;
    }
    return s_blocks[id];
}

void special_release(s16 id)
{
    if (id >= 0 && id < BLOCK_MAX && s_blocks[id] != NULL) {
        free(s_blocks[id]);
        s_blocks[id] = NULL;
    }
}

/* 0x40c9ef - returns the actor slot or -1. */
s16 special_create(s16 type, s16 x, s16 z, s16 y, s16 stage, s16 room,
                   s16 alpha, s16 beta, s16 gamma, const s16 *zv)
{
    static const s16 dust_colours[4] = { 0x90, 0xc0, 0x30, 0x70 };
    struct actor *a = NULL;
    s16 slot;
    s16 *data;
    int j;

    if (!world_camera_sees_room(room)) {
        return -1;
    }
    for (slot = 0; slot < ACTOR_MAX; slot++) {
        if (g_actors[slot].object == -1) {
            a = &g_actors[slot];
            break;
        }
    }
    if (a == NULL) {
        return -1;
    }

    a->flags = 0x20;
    a->object = -2;
    a->life = -1;
    a->life_mode = LIFE_MODE_VISIBLE;
    a->body = 0;
    a->room_x = a->world_x = x;
    a->room_y = a->world_y = y;
    a->room_z = a->world_z = z;
    a->stage = stage;
    a->room = room;
    if (room != g_cur_room) {
        const u8 *r = g_floor_rooms[room];
        a->world_x = (s16)(a->world_x - (s16)((s16)(ROOM_X(g_room_data) - ROOM_X(r)) * 10));
        a->world_y = (s16)(a->world_y + (s16)((s16)(ROOM_Y(g_room_data) - ROOM_Y(r)) * 10));
        a->world_z = (s16)(a->world_z + (s16)((s16)(ROOM_Z(g_room_data) - ROOM_Z(r)) * 10));
    }
    a->alpha = alpha;
    a->beta = beta;
    a->gamma = gamma;
    a->step_x = a->step_y = a->step_z = 0;
    if (zv != NULL) {
        memcpy(a->zv, zv, sizeof(a->zv));
    }

    if (type == SPECIAL_DUST) {
        /* Dust: points spread inside the given box. */
        a->zv[0] = (s16)(a->zv[0] - x);
        a->zv[1] = (s16)(a->zv[1] - x);
        a->zv[2] = (s16)(a->zv[2] - y);
        a->zv[3] = (s16)(a->zv[3] - y);
        a->zv[4] = (s16)(a->zv[4] - z);
        a->zv[5] = (s16)(a->zv[5] - z);
        a->frame = special_block_alloc();
        data = block_get(a->frame);
        if (data == NULL) {
            a->object = -1;
            return -1;
        }
        a->anim = SPECIAL_DUST;
        data[0] = dust_colours[crt_rand() & 3];
        data[1] = PARTICLES;
        for (j = 0; j < PARTICLES; j++) {
            data[2 + j * 3] = special_rand_between(a->zv[0], a->zv[1]);
            data[3 + j * 3] = special_rand_between(a->zv[2], a->zv[3]);
            data[4 + j * 3] = special_rand_between(a->zv[4], a->zv[5]);
        }
        for (j = 0; j < PARTICLES; j++) {
            data[2 + PARTICLES * 3 + j * 2] = special_rand_between(150, 300);
            data[3 + PARTICLES * 3 + j * 2] = special_rand_between(30, 80);
        }
        a->zv[0] = (s16)(x - 10);
        a->zv[1] = (s16)(x + 10);
        a->zv[2] = y;
        a->zv[3] = (s16)(y - 1);
        a->zv[4] = (s16)(z - 10);
        a->zv[5] = (s16)(z + 10);
    } else if (type == SPECIAL_BLOOD || type == SPECIAL_SPARKS) {
        /* Blood (1) and sparks (2): a burst falling under gravity. */
        a->zv[0] = a->zv[1] = x;
        a->zv[2] = a->zv[3] = y;
        a->zv[4] = a->zv[5] = z;
        a->frame = special_block_alloc();
        data = block_get(a->frame);
        if (data == NULL) {
            a->object = -1;
            return -1;
        }
        a->anim = SPECIAL_BLOOD;   /* sparks fly the same way */
        data[0] = (type == SPECIAL_BLOOD) ? 10 : 15;
        data[1] = PARTICLES;
        for (j = 0; j < PARTICLES; j++) {
            data[2 + j * 3] = special_rand_between(-100, 100);
            data[3 + j * 3] = (s16)(special_rand_between(-100, 100) + y);
            data[4 + j * 3] = special_rand_between(-100, 100);
        }
        for (j = 0; j < PARTICLES; j++) {
            data[2 + PARTICLES * 3 + j * 2] = special_rand_between(0, 1023);
            data[3 + PARTICLES * 3 + j * 2] = special_rand_between(-50, 10);
        }
    } else if (type <= SPECIAL_BODY_DEFINE_10) {
        a->anim = type;
        a->zv[0] = a->zv[1] = x;
        a->zv[2] = a->zv[3] = y;
        a->zv[4] = a->zv[5] = z;
    }

    g_actors_dirty = 1;
    return slot;
}

/* Raw body view of a particle block for the static transform: the count
 * word followed by the points. */
static u8 *block_body(s16 *data)
{
    return (u8 *)&data[1];
}

static void release(struct actor *a)
{
    special_release(a->frame);
    a->object = -1;
    g_actors_dirty = 1;
}

/* 0x40cf8d */
void special_draw(s16 index, s16 visible)
{
    struct actor *a = &g_actors[index];
    s16 *data;
    int dead = 1;
    int j;

    switch (a->anim) {
    case SPECIAL_DUST:
        data = block_get(a->frame);
        if (data == NULL) {
            release(a);
            return;
        }
        a->beta = (s16)(a->beta + 8);
        if (r3d_transform_points(a->world_x, a->world_y, a->world_z, a->alpha,
                                 a->beta, a->gamma, block_body(data))) {
            s16 *pair = &data[2 + PARTICLES * 3];
            for (j = 0; j < data[1]; j++, pair += 2) {
                if (pair[0] > 0) {
                    dead = 0;
                    data[3 + j * 3] = (s16)(data[3 + j * 3] - pair[1]);
                    pair[0] = (s16)(pair[0] - 5);
                }
            }
        }
        if (dead) {
            release(a);
        }
        break;

    case SPECIAL_BLOOD:
        data = block_get(a->frame);
        if (data == NULL) {
            release(a);
            return;
        }
        if (r3d_transform_points(a->world_x, 0, a->world_z, a->alpha, a->beta,
                                 a->gamma, block_body(data))) {
            const s16 *proj = r3d_projected_points();
            s16 *pair = &data[2 + PARTICLES * 3];

            for (j = 0; j < data[1]; j++, pair += 2, proj += 3) {
                s16 *pt = &data[2 + j * 3];

                if (pt[1] >= 0) {
                    continue;
                }
                dead = 0;
                if (proj[2] > 100 && visible) {
                    raster_point(proj[0], proj[1], (u8)data[0]);
                }
                pair[1] = (s16)(pair[1] + 6);
                pt[1] = (s16)(pt[1] + pair[1]);
                r3d_rotate_2d(10, 0, pair[0]);
                pt[0] = (s16)(pt[0] + g_r3d_x);
                pt[2] = (s16)(pt[2] + g_r3d_y);
            }
        }
        if (dead) {
            release(a);
        }
        break;

    case SPECIAL_BODY_DEFINE_1: case SPECIAL_BODY_DEFINE_5: case SPECIAL_BODY_DEFINE_6:
    case SPECIAL_BODY_DEFINE_7: case SPECIAL_BODY_DEFINE_8: case SPECIAL_BODY_DEFINE_10: {
        static const int define_word[9] = { 0, 0, 0, 1, 5, 6, 7, 8, 10 };
        u8 *body = hqr_get(g_cache_body, (s16)g_defines_itd[define_word[a->anim]], NULL);

        if (body != NULL && visible) {
            r3d_set_clip_y(10000);
            r3d_draw_body(a->world_x, a->world_y, a->world_z, 0, a->beta, 0, body);
            a->object = -1;
            g_actors_dirty = 1;
        }
        break;
    }
    default:
        break;
    }
}

/* Opcode 35 (0x405314): an effect at the current actor. */
void special_spawn(s16 type)
{
    struct actor *a = g_cur_actor;

    switch (type) {
    case SPECIAL_DUST:
        special_create(SPECIAL_DUST, (s16)(a->room_x + a->step_x), (s16)(a->room_z + a->step_z),
                       (s16)(a->room_y + a->step_y), a->stage, a->room, a->alpha,
                       a->beta, a->gamma, a->zv);
        break;
    case SPECIAL_BLOOD: {
        /* At the hot point of whoever hit this actor. */
        struct actor *saved = g_cur_actor;
        struct actor *h = &g_actors[a->hit_by];

        g_cur_actor = h;
        special_create(SPECIAL_BLOOD, (s16)(h->hot_point[0] + h->room_x + h->step_x),
                       (s16)(h->hot_point[2] + h->room_z + h->step_z),
                       (s16)(h->hot_point[1] + h->room_y + h->step_y),
                       h->stage, h->room, 0, (s16)-h->beta, 0, NULL);
        g_cur_actor = saved;
        break;
    }
    case SPECIAL_BODY_DEFINE_5:
        special_create(SPECIAL_BODY_DEFINE_5, (s16)(a->room_x + a->step_x), (s16)(a->room_z + a->step_z),
                       (s16)(a->room_y + a->step_y), a->stage, a->room, a->alpha,
                       a->beta, a->gamma, a->zv);
        break;
    default:
        break;
    }
}
