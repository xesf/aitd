/*
 * action.c - animation driven actions: hits, gunshots, throws and the
 * flight of a thrown object (0x41317b), with the shot trace (0x40e756) and
 * the landing of a thrown object (0x40e9e0).
 *
 * anim_action_type steps through small state machines:
 *   1 -> 10 -> 2   hand to hand: wait for the anim, then the key frame,
 *                  then hit whatever the hot point box touches
 *   4 -> 5         fire: on the key frame trace a shot along the heading
 *   6 -> 7 -> 9    throw: release the object on the key frame, then fly it
 *   8              a box around the actor hits everything it touches
 */

#include <string.h>

#include "actor.h"
#include "aitd.h"
#include "draw.h"
#include "life.h"
#include "r3d.h"
#include "world.h"

/* Damage dealt to `target`, adjusted for difficulty; written to the
 * target's action_force where its life script reads it (HIT_FORCE). */
static void deal_damage(struct actor *a, s16 index, s16 target, int floor_at_zero)
{
    struct actor *t = &g_actors[target];
    s16 level;

    a->hit = target;
    t->hit_by = index;

    level = (t->object == g_hit_player_object) ? g_difficulty[2] : g_difficulty[1];
    if (level == 0) {
        a->action_force = (s16)(a->action_force - 2);
    } else if (level == 2) {
        a->action_force = (s16)(a->action_force + 3);
    }
    if (floor_at_zero ? a->action_force <= 0 : a->action_force < 0) {
        a->action_force = 1;
    }
    t->action_force = a->action_force;
}

/* 0x40e756 - march a box of half size `size` along (angle, vertical) until
 * it meets a wall, the floor, or an actor. Returns the actor or -1; the
 * last free position is left in g_r3d_x / y / z. */
static s16 shot_trace(s16 index, s16 x, s16 z, s16 y, s16 vertical, s16 angle,
                      s16 room, s16 size)
{
    s16 box[6];
    s16 sx, sz, sy;
    s16 px = x, py = y, pz = z;
    s16 result = -2;

    box[0] = (s16)(x - size);
    box[1] = (s16)(x + size);
    box[2] = (s16)(y - size);
    box[3] = (s16)(y + size);
    box[4] = (s16)(z - size);
    box[5] = (s16)(z + size);

    r3d_rotate_2d((s16)(2 * size), 0, angle);
    sx = g_r3d_x;
    sz = g_r3d_y;
    if (vertical) {
        r3d_rotate_2d((s16)(2 * size), 0, vertical);
        sy = g_r3d_x;
    } else {
        sy = 0;
    }

    while (result == -2) {
        s16 i;

        box[0] = (s16)(box[0] + sx);
        box[1] = (s16)(box[1] + sx);
        box[2] = (s16)(box[2] + sy);
        box[3] = (s16)(box[3] + sy);
        box[4] = (s16)(box[4] + sz);
        box[5] = (s16)(box[5] + sz);

        px = x;
        py = y;
        pz = z;
        x = (s16)(x + sx);
        y = (s16)(y + sy);
        z = (s16)(z + sz);

        if (x > 20000 || z > 20000 || y > 0 || x < -20000 || z < -20000 || y < -20000) {
            if (y > 0) {
                py = -1;
            }
            result = -1;
            break;
        }

        if (hardcol_test(box, g_floor_rooms[room]) > 0) {
            result = -1;
            break;
        }

        for (i = 0; i < ACTOR_MAX; i++) {
            const struct actor *o = &g_actors[i];
            s16 test[6];

            if (o->object == -1 || i == index || (o->flags & 0x20)) {
                continue;
            }
            memcpy(test, box, sizeof(test));
            if (room != o->room) {
                zv_to_room(test, room, o->room);
            }
            if (test[0] < o->zv[1] && o->zv[0] < test[1]
                && test[2] < o->zv[3] && o->zv[2] < test[3]
                && test[4] < o->zv[5] && o->zv[4] < test[5]) {
                result = i;
                break;
            }
        }
    }

    g_r3d_x = px;
    g_r3d_y = py;
    g_r3d_z = pz;
    return result;
}

/* 0x40e9e0 - put a thrown object down near (x, z): back off along the
 * reverse heading until its box is free, then drop it floor by floor. */
static void land_object(s16 x, s16 z)
{
    struct actor *a = g_cur_actor;
    struct world_object *o = object_get(a->object);
    const u8 *body = hqr_get(g_cache_body, a->body, NULL);
    s16 base[6], box[6];
    s16 px = x, pz = z;
    s16 y = (s16)(2000 * (a->room_y / 2000));
    s16 back = 0;
    int placed = 0;
    int i;

    for (i = 0; i < 6; i++) {
        base[i] = blob_s16(body, 2 + i * 2);
    }

    while (!placed) {
        r3d_rotate_2d(0, (s16)-back, (s16)(a->beta + 0x200));
        memcpy(box, base, sizeof(box));
        px = (s16)(g_r3d_x + x);
        pz = (s16)(g_r3d_y + z);
        box[0] = (s16)(box[0] + px);
        box[1] = (s16)(box[1] + px);
        box[2] = (s16)(box[2] + y);
        box[3] = (s16)(box[3] + y);
        box[4] = (s16)(box[4] + pz);
        box[5] = (s16)(box[5] + pz);

        if (!hardcol_test(box, g_floor_rooms[a->room])) {
            s16 k;

            placed = 1;
            for (k = 0; k < ACTOR_MAX; k++) {
                const struct actor *t = &g_actors[k];
                s16 test[6];

                if (t->object == -1) {
                    continue;
                }
                memcpy(test, box, sizeof(test));
                if (a->room != t->room) {
                    zv_to_room(test, a->room, t->room);
                }
                if (test[0] < t->zv[1] && t->zv[0] < test[1]
                    && test[2] < t->zv[3] && t->zv[2] < test[3]
                    && test[4] < t->zv[5] && t->zv[4] < test[5]) {
                    if (t->object != o->alpha && !(t->flags & 0x80)) {
                        placed = 0;
                    }
                    break;
                }
            }
        }

        if (placed) {
            if (y < -500) {
                box[2] = (s16)(box[2] + 100);
                box[3] = (s16)(box[3] + 100);
                if (!hardcol_test(box, g_floor_rooms[a->room])) {
                    y = (s16)(y + 2000);
                    placed = 0;
                }
            }
        } else {
            back = (s16)(back + 100);
        }
    }

    a->room_x = a->world_x = px;
    a->room_y = a->world_y = y;
    a->room_z = a->world_z = pz;
    a->step_x = 0;
    a->step_z = 0;
    a->anim_action_type = ACTION_NONE;
    a->speed = 0;
    a->gamma = 0;

    for (i = 0; i < 6; i++) {
        a->zv[i] = base[i];
    }
    a->zv[0] = (s16)(a->zv[0] + px);
    a->zv[1] = (s16)(a->zv[1] + px);
    a->zv[2] = (s16)(a->zv[2] + y);
    a->zv[3] = (s16)(a->zv[3] + y);
    a->zv[4] = (s16)(a->zv[4] + pz);
    a->zv[5] = (s16)(a->zv[5] + pz);

    o->flags2 |= 0x4000;
    o->flags2 &= ~0x1000;
    actor_make_static(g_cur_actor_index);
}

static const u8 *scene_zone_at(s16 room, s16 x, s16 y, s16 z)
{
    const u8 *r = g_floor_rooms[room];
    const u8 *p = r + (blob_s16(r, 2) & ~1);
    int count = blob_s16(p, 0);

    p += 2;
    while (count-- > 0) {
        if (x >= blob_s16(p, 0) && x <= blob_s16(p, 2)
            && y >= blob_s16(p, 4) && y <= blob_s16(p, 6)
            && z >= blob_s16(p, 8) && z <= blob_s16(p, 10)) {
            return p;
        }
        p += 16;
    }
    return NULL;
}

/* Case 9: one frame of flight. */
static void fly(struct actor *a, s16 index)
{
    struct world_object *o = object_get(a->object);
    s16 cur_x = (s16)(a->step_x + a->room_x);
    s16 cur_y = (s16)(a->step_y + a->room_y);
    s16 cur_z = (s16)(a->step_z + a->room_z);
    s16 box[6], rel[6];
    s16 start_x = o->x, start_y = o->y, start_z = o->z;
    s16 nx = start_x, ny = start_y, nz = start_z;
    s16 last_x = start_x, last_z = start_z;
    s16 dist = 0;
    int i;

    memcpy(box, a->zv, sizeof(box));
    memcpy(rel, a->zv, sizeof(rel));
    rel[0] = (s16)(rel[0] - cur_x);
    rel[1] = (s16)(rel[1] - cur_x);
    rel[2] = (s16)(rel[2] - cur_y);
    rel[3] = (s16)(rel[3] - cur_y);
    rel[4] = (s16)(rel[4] - cur_z);
    rel[5] = (s16)(rel[5] - cur_z);

    do {
        int n = actor_collide(index, box);

        if (n) {
            int hits = n;

            a->hot_point[0] = a->hot_point[1] = a->hot_point[2] = 0;
            for (i = 0; i < n; i++) {
                s16 c = a->col[i];

                if (g_actors[c].object == o->alpha) {
                    goto pass;   /* 0x41397e: passes through its thrower */
                }
                deal_damage(a, index, c, 1);
            }
            if (hits) {
                goto landed;
            }
        }

        r3d_rotate_2d(0, (s16)-dist, a->beta);
        dist = (s16)(dist + 10);
        nx = (s16)(g_r3d_x + start_x);
        ny = start_y;
        nz = (s16)(g_r3d_y + start_z);

        memcpy(box, rel, sizeof(box));
        box[0] = (s16)(box[0] + nx - 10);
        box[1] = (s16)(box[1] + nx + 10);
        box[2] = (s16)(box[2] + ny - 10);
        box[3] = (s16)(box[3] + ny + 10);
        box[4] = (s16)(box[4] + nz - 10);
        box[5] = (s16)(box[5] + nz + 10);

        {
            const u8 *zone = scene_zone_at(a->room, nx, ny, nz);
            if (zone != NULL && (blob_s16(zone, 14) == ZONE_ROOM_LINK || blob_s16(zone, 14) == ZONE_LIFE)) {
                goto landed;
            }
        }
        if (hardcol_test(box, g_floor_rooms[a->room])) {
            a->hot_point[0] = a->hot_point[1] = a->hot_point[2] = 0;
            goto landed;
        }
        last_x = nx;
        last_z = nz;
    } while (nx < a->zv[0] - 100 || nx > a->zv[1] + 100
             || nz < a->zv[4] - 100 || nz > a->zv[5] + 100);

pass:
    o->x = cur_x;
    o->y = cur_y;
    o->z = cur_z;
    object_save_zv((s16 *)a);
    return;

landed:
    sample_play((s16)g_defines_itd[3]);
    land_object(last_x, last_z);
    object_save_zv((s16 *)a);
}

/* 0x41317b */
void actor_anim_action(void)
{
    struct actor *a = g_cur_actor;
    s16 index = g_cur_actor_index;
    int i;

    switch (a->anim_action_type) {
    case ACTION_HIT_START:
        if (a->anim == a->action_anim) {
            a->anim_action_type = ACTION_HIT_KEY_FRAME;
        }
        /* fall through */
    case ACTION_HIT_KEY_FRAME:
        if (a->anim == a->action_anim) {
            if (a->frame == a->action_frame) {
                a->anim_action_type = ACTION_HIT;
            }
        } else {
            a->anim_action_type = ACTION_NONE;
        }
        break;

    case ACTION_HIT: {
        s16 box[6];
        s16 x, y, z, size;
        int n;

        if (a->anim != a->action_anim) {
            a->anim_action_type = ACTION_NONE;
        }
        x = (s16)(a->hot_point[0] + a->room_x + a->step_x);
        y = (s16)(a->hot_point[1] + a->room_y + a->step_y);
        z = (s16)(a->hot_point[2] + a->room_z + a->step_z);
        size = a->action_param;
        box[0] = (s16)(x - size);
        box[1] = (s16)(size + x);
        box[2] = (s16)(y - size);
        box[3] = (s16)(size + y);
        box[4] = (s16)(z - size);
        box[5] = (s16)(size + z);

        n = actor_collide(index, box);
        for (i = 0; i < n; i++) {
            s16 target = a->col[i];

            deal_damage(a, index, target, 0);
            if (g_actors[target].flags & 1) {
                a->anim_action_type = ACTION_NONE;
                break;
            }
        }
        break;
    }

    case ACTION_HIT_END:
        if (a->anim != a->action_anim) {
            a->anim_action_type = ACTION_NONE;
        }
        break;

    case ACTION_FIRE_START:
        if (a->anim == a->action_anim && a->frame == a->action_frame) {
            a->anim_action_type = ACTION_FIRE;
        }
        break;

    case ACTION_FIRE: {
        s16 hx = (s16)(a->room_x + a->hot_point[0]);
        s16 hy = (s16)(a->room_y + a->hot_point[1]);
        s16 hz = (s16)(a->room_z + a->hot_point[2]);
        s16 target;

        if (a->visible) {
            special_create(a->action_param2, hx, hz, hy, a->stage, a->room, 0,
                           a->beta, 0, NULL);
        }
        target = shot_trace(index, hx, hz, hy, a->action_param3,
                            (s16)(a->beta - 0x100), a->room, a->action_param);
        if (target == -1) {
            special_create(SPECIAL_SPARKS, g_r3d_x, g_r3d_z, g_r3d_y, a->stage, a->room, 0,
                           (s16)-a->beta, 0, NULL);
        } else {
            a->hot_point[0] = (s16)(g_r3d_x - a->room_x);
            a->hot_point[1] = (s16)(g_r3d_y - a->room_y);
            a->hot_point[2] = (s16)(g_r3d_z - a->room_z);
            deal_damage(a, index, target, 1);
        }
        a->anim_action_type = ACTION_NONE;
        break;
    }

    case ACTION_THROW_START:
        if (a->anim == a->action_anim && a->frame == a->action_frame) {
            s16 obj = a->action_param;
            struct world_object *o = object_get(obj);

            a->anim_action_type = ACTION_THROW;
            inventory_remove(obj);
            o->x = (s16)(a->hot_point[0] + a->room_x + a->step_x);
            o->y = (s16)(a->hot_point[1] + a->room_y + a->step_y);
            o->z = (s16)(a->hot_point[2] + a->room_z + a->step_z);
            o->room = a->room;
            o->stage = a->stage;
            o->alpha = a->alpha;
            o->beta = a->beta;
            o->flags2 &= ~0x4000;
            o->flags |= (o->found_life == -1) ? 5 : 0x85;
            o->flags &= ~0x20;
            g_objects_dirty = 1;
        }
        break;

    case ACTION_THROW: {
        s16 x = (s16)(a->hot_point[0] + a->room_x + a->step_x);
        s16 y = (s16)(a->hot_point[1] + a->room_y + a->step_y);
        s16 z = (s16)(a->hot_point[2] + a->room_z + a->step_z);
        struct world_object *o = object_get(a->action_param);
        s16 t_index = o->actor;

        a->anim_action_type = ACTION_NONE;
        if (t_index != -1) {
            struct actor *t = &g_actors[t_index];

            t->room_x = x;
            t->room_y = y;
            t->room_z = z;
            zv_rotated(hqr_get(g_cache_body, t->body, NULL), t->zv, t->alpha,
                       t->beta, t->gamma, t);
            t->zv[0] = (s16)(t->zv[0] + x);
            t->zv[1] = (s16)(t->zv[1] + x);
            t->zv[2] = (s16)(t->zv[2] + y);
            t->zv[3] = (s16)(t->zv[3] + y);
            t->zv[4] = (s16)(t->zv[4] + z);
            t->zv[5] = (s16)(t->zv[5] + z);
            t->flags |= 1;
            t->flags &= ~8;

            o->x = x;
            o->y = y;
            o->z = z;
            o->alpha = a->object;   /* the thrower, so the flight skips it */

            t->dyn_flags = 0;
            t->anim_action_type = ACTION_FLY;
            t->action_param = 50;
            {
                s16 level = (t->object == g_hit_player_object) ? g_difficulty[2] : g_difficulty[1];
                if (level == 0) {
                    a->action_force = (s16)(a->action_force - 2);
                } else if (level == 2) {
                    a->action_force = (s16)(a->action_force + 3);
                }
                if (a->action_force <= 0) {
                    a->action_force = 1;
                }
            }
            t->action_force = a->action_force;
            t->hot_point_id = -1;
            t->speed = 10000;
            interp_init(0, t->speed, 60, t->speed_interp);
        }
        break;
    }

    case ACTION_HIT_OBJECT: {
        s16 box[6];
        int n;

        memcpy(box, a->zv, sizeof(box));
        box[0] = (s16)(box[0] - 10);
        box[1] = (s16)(box[1] + 10);
        box[2] = (s16)(box[2] - 10);
        box[3] = (s16)(box[3] + 10);
        box[4] = (s16)(box[4] - 10);
        box[5] = (s16)(box[5] + 10);
        n = actor_collide(index, box);
        for (i = 0; i < n; i++) {
            a->hot_point[0] = a->hot_point[1] = a->hot_point[2] = 0;
            deal_damage(a, index, a->col[i], 1);
        }
        break;
    }

    case ACTION_FLY:
        fly(a, index);
        break;

    default:
        a->anim_action_type = ACTION_NONE;
        break;
    }
}
