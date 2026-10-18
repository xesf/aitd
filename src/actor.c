/*
 * actor.c - per actor simulation.
 *
 *   actor_process     0x40fe5f  animation step, movement, collisions, gravity
 *   actor_fall        0x40f7d6  scene zones under the actor (room links,
 *                               material, triggers)
 *   actor_do_move     0x411411  DO_MOVE: keyboard, follow and track movement
 *   camera_select     0x40a57a  camera change when the player leaves a view
 */

#include <stdlib.h>
#include <string.h>

#include "actor.h"
#include "aitd.h"
#include "game.h"
#include "anim.h"
#include "draw.h"
#include "life.h"
#include "menu.h"
#include "subsystems.h"
#include "r3d.h"
#include "world.h"

#define POSE_SLOTS 80
#define POSE_BYTES 656   /* 0xcd00 / 80 */

/* Ring of saved poses an actor blends from when its animation changes
 * (0x444374 owner table, 0x4350c5 ring index, 0x444aa4 buffer). */
static s16 s_pose_owner[POSE_SLOTS];
static s16 s_pose_ring;
static int s_pose_init;

const u8 *g_hardcol_hits[32];
static int s_hardcol_count;   /* 0x434f20 */

/* Corrected x/z move written by the slide routine (0x445000 / 0x444ffe). */
static s16 s_move_x, s_move_z;

/* ---- interpolators (0x422608..0x42275b) -------------------------------- */

void interp_init(s16 from, s16 to, s16 duration, s16 *s)
{
    s[0] = from;
    s[1] = to;
    s[2] = duration;
    ticks_update();
    s[3] = (s16)g_ticks;
}

void interp_init_angle(s16 from, s16 to, s16 duration, s16 *s)
{
    interp_init((s16)(from & 0x3ff), (s16)(to & 0x3ff), duration, s);
}

s16 interp_get(s16 *s)
{
    u16 elapsed;

    if (s[2] == 0) {
        return s[1];
    }
    ticks_update();
    elapsed = (u16)((u16)g_ticks - (u16)s[3]);
    if (elapsed >= (u16)s[2]) {
        s[2] = 0;
        return s[1];
    }
    return (s16)((s16)(((s32)(s16)(s[1] - s[0]) * (s16)elapsed) / s[2]) + s[0]);
}

s16 interp_get_angle(s16 *s)
{
    u16 elapsed;
    s16 to, from, diff;

    if (s[2] == 0) {
        return s[1];
    }
    ticks_update();
    elapsed = (u16)((u16)g_ticks - (u16)s[3]);
    if (elapsed >= (u16)s[2]) {
        s[2] = 0;
        return s[1];
    }

    to = (s16)(s[1] & 0x3ff);
    from = (s16)(s[0] & 0x3ff);
    diff = (s16)(to - from);

    if (diff > 0x200) {
        from = (s16)(from + 0x400);
        diff = (s16)(to - from);
    } else if (diff < (s16)0xfe00) {
        diff = (s16)(to + 0x400 - from);
    }
    return (s16)((s16)(((s32)diff * (s16)elapsed) / s[2]) + from);
}

/* ---- small geometry helpers -------------------------------------------- */

/* 0x422a04 */
s16 distance_xz(s16 x1, s16 z1, s16 x2, s16 z2)
{
    s16 dx = (s16)(x1 - x2), dz = (s16)(z1 - z2);
    s32 sum;

    if (dx < 0) dx = (s16)-dx;
    if (dz < 0) dz = (s16)-dz;
    sum = (s32)dx + dz;
    return (sum > 32767 || sum < -32768) ? 0x7d00 : (s16)sum;
}

/* 0x4229bb */
s16 distance_3d(s16 x1, s16 y1, s16 z1, s16 x2, s16 y2, s16 z2)
{
    s16 dx = (s16)(x1 - x2), dy = (s16)(y1 - y2), dz = (s16)(z1 - z2);
    s32 sum;

    if (dx < 0) dx = (s16)-dx;
    if (dy < 0) dy = (s16)-dy;
    if (dz < 0) dz = (s16)-dz;
    sum = (s32)dx + dy;
    if (sum > 32767) return 0x7d00;
    sum += dz;
    if (sum > 32767) return 0x7d00;
    return (s16)sum;
}

/* 0x422a78 - strict overlap of two boxes. */
int zv_overlap(const s16 *a, const s16 *b)
{
    return a[0] < b[1] && b[0] < a[1]
        && a[2] < b[3] && b[2] < a[3]
        && a[4] < b[5] && b[4] < a[5];
}

/* 0x412731 */
void zv_to_room(s16 *zv, s16 from, s16 to)
{
    const u8 *a = g_floor_rooms[from];
    const u8 *b = g_floor_rooms[to];
    s16 dx = (s16)((s16)(ROOM_X(b) - ROOM_X(a)) * 10);
    s16 dy = (s16)((s16)(ROOM_Y(b) - ROOM_Y(a)) * 10);
    s16 dz = (s16)((s16)(ROOM_Z(b) - ROOM_Z(a)) * 10);

    zv[0] = (s16)(zv[0] - dx);
    zv[1] = (s16)(zv[1] - dx);
    zv[2] = (s16)(zv[2] + dy);
    zv[3] = (s16)(zv[3] + dy);
    zv[4] = (s16)(zv[4] + dz);
    zv[5] = (s16)(zv[5] + dz);
}

/* ---- collisions -------------------------------------------------------- */

/* 0x4143e0 - room points at the room blob; its +0 offset leads to the
 * collision boxes (16 bytes: box, parameter, type). */
int hardcol_test(const s16 *zv, const u8 *room)
{
    const u8 *p = room + (u16)blob_s16(room, 0);
    int count = (u16)blob_s16(p, 0);
    int i;

    s_hardcol_count = 0;
    p += 2;

    for (i = 0; i < count; i++, p += 16) {
        if (blob_s16(p, 0) < zv[1] && zv[0] < blob_s16(p, 2)
            && blob_s16(p, 4) < zv[3] && zv[2] < blob_s16(p, 6)
            && blob_s16(p, 8) < zv[5] && zv[4] < blob_s16(p, 10)) {
            if (s_hardcol_count < 32) {
                g_hardcol_hits[s_hardcol_count] = p;
            }
            s_hardcol_count++;
        }
    }
    if (s_hardcol_count > 32) {
        s_hardcol_count = 32;
    }
    return s_hardcol_count;
}

/* 0x412901 */
int actor_collide(s16 index, const s16 *zv)
{
    s16 room = g_actors[index].room;
    int found = 0;
    s16 i;

    for (i = 0; i < ACTOR_MAX; i++) {
        const struct actor *o = &g_actors[i];
        int hit;

        if (o->object == -1 || i == index) {
            continue;
        }
        if (room == o->room) {
            hit = zv_overlap(zv, o->zv);
        } else {
            s16 tmp[6];
            memcpy(tmp, zv, sizeof(tmp));
            zv_to_room(tmp, room, o->room);
            hit = zv_overlap(tmp, o->zv);
        }
        if (hit) {
            g_cur_actor->col[found++] = i;
            if (found == 3) {
                return 3;
            }
        }
    }
    return found;
}

/* 0x4127f4 */
int actor_collide_solid(s16 index, const s16 *zv)
{
    s16 room = g_actors[index].room;
    s16 i;

    for (i = 0; i < ACTOR_MAX; i++) {
        const struct actor *o = &g_actors[i];
        int hit;

        if (o->object == -1 || i == index) {
            continue;
        }
        if (room == o->room) {
            hit = zv_overlap(zv, o->zv);
        } else {
            s16 tmp[6];
            memcpy(tmp, zv, sizeof(tmp));
            zv_to_room(tmp, room, o->room);
            hit = zv_overlap(tmp, o->zv);
        }
        if (hit && !(o->flags & 0x80)) {
            return 1;
        }
    }
    return 0;
}

/* 0x412a4a */
int actor_collide_mark(s16 index, const s16 *zv)
{
    s16 room = g_actors[index].room;
    int count = 0;
    s16 i;

    for (i = 0; i < ACTOR_MAX; i++) {
        struct actor *o = &g_actors[i];
        int hit;

        if (o->object == -1 || i == index) {
            continue;
        }
        if (room == o->room) {
            hit = zv_overlap(zv, o->zv);
        } else {
            s16 tmp[6];
            memcpy(tmp, zv, sizeof(tmp));
            zv_to_room(tmp, room, o->room);
            hit = zv_overlap(tmp, o->zv);
        }
        if (hit) {
            o->col_by = index;
            count++;
        }
    }
    return count;
}

/* 0x412b73 */
static void slide_kill(s16 code)
{
    if ((u16)code < 4) {
        if (code == 1 || code == 2) {
            s_move_z = 0;
        }
    } else if (code == 4 || code == 8) {
        s_move_x = 0;
    }
}

static s16 side_type(s16 code)
{
    if (code == 5 || code == 9 || code == 6 || code == 10) {
        return 2;
    }
    return code != 0 ? 1 : 0;
}

/*
 * 0x412bd8 - keep the component of the move that slides along an obstacle.
 * start is the box before the move, moved after it.
 */
static void slide(const s16 *start, const s16 *moved, const s16 *box)
{
    s16 c1 = 0, c2 = 0, t1, t2;
    s32 cx, cz;

    if (!(start[1] > box[0])) {
        c1 = 4;
    } else if (!(start[0] < box[1])) {
        c1 = 8;
    }
    if (!(start[5] > box[4])) {
        c1 |= 1;
    } else if (!(start[4] < box[5])) {
        c1 |= 2;
    }

    t1 = side_type(c1);
    if (t1 == 0) {
        s_move_x = s_move_z = 0;
        return;
    }

    cx = ((s32)moved[1] + moved[0]) >> 1;
    cz = ((s32)moved[4] + moved[5]) >> 1;

    if ((s16)cx < box[0]) {
        c2 = 4;
    } else if ((s16)cx > box[1]) {
        c2 = 8;
    }
    if ((s16)cz < box[4]) {
        c2 |= 1;
    } else if ((s16)cz > box[5]) {
        c2 |= 2;
    }
    t2 = side_type(c2);

    if (t1 == 1) {
        slide_kill(c1);
    } else if (t2 == 1 && (c2 & c1)) {
        slide_kill(c2);
    } else if (c2 == c1 || c2 + c1 == 15) {
        s32 ax = abs((s32)moved[0] - start[0]);
        s32 az = abs((s32)moved[4] - start[4]);
        if (ax > az) {
            s_move_z = 0;
        } else {
            s_move_x = 0;
        }
    } else if (t2 == 0 || (t2 == 1 && !(c2 & c1))) {
        s_move_x = s_move_z = 0;
    } else {
        slide_kill((s16)(c1 & c2));
    }
}

/* ---- static layer ------------------------------------------------------ */

/* 0x41b428 */
void actor_unstatic(s16 index)
{
    struct actor *a = &g_actors[index];

    a->flags &= ~8;
    g_static_dirty = 1;
}

/* 0x41b3f8 */
void actor_make_static(s16 index)
{
    struct actor *a = &g_actors[index];

    a->flags |= 0x0c;
    a->flags &= ~1;
    g_static_dirty = 1;
}

/* ---- animation control (0x40fc09) -------------------------------------- */

int anim_init(s16 anim, s16 type, s16 info)
{
    struct actor *a = g_cur_actor;
    /* Alone in the Dark 1 keeps the frame counter when an animation starts
     * and refuses any change while either animation is uninterruptible. */
    int aitd1 = game_id() == GAME_AITD1;

    if (a->anim == anim) {
        if (a->flags & 1) {
            a->anim_type = type;
            a->anim_info = info;
            return 0;
        }
        if (a->flags & 8) {
            actor_unstatic(g_cur_actor_index);
        }
        a->flags |= 1;
        anim_set_frame(a->frame, hqr_get(g_cache_anim, anim, NULL),
                       hqr_get(g_cache_body, a->body, NULL));
        a->anim_type = type;
        a->anim_info = info;
        if (!aitd1) {
            a->frame = 0;
        }
        return 1;
    }

    if (anim == -1) {
        a->new_anim = -2;
        return 1;
    }

    if (!(a->flags & 1)) {
        a->flags |= 1;
        if (a->flags & 8) {
            actor_unstatic(g_cur_actor_index);
        }
        anim_set_frame(0, hqr_get(g_cache_anim, anim, NULL),
                       hqr_get(g_cache_body, a->body, NULL));
        a->new_anim = anim;
        a->new_anim_type = type;
        a->new_anim_info = info;
        if (!aitd1) {
            a->frame = 0;
        }
        return 1;
    }

    if (aitd1) {
        if ((a->anim_type & ANIM_UNINTERRUPTIBLE)
            || (a->new_anim_type & ANIM_UNINTERRUPTIBLE)) {
            return 0;
        }
    } else if (a->anim_type & ANIM_UNINTERRUPTIBLE) {
        if (a->new_anim_type & ANIM_UNINTERRUPTIBLE) {
            return 0;
        }
        a->anim_info = anim;
        return 1;
    }

    a->new_anim = anim;
    a->new_anim_type = type;
    a->new_anim_info = info;
    if (!aitd1) {
        a->frame = 0;
    }
    return 1;
}

/* ---- actor_process (0x40fe5f) ------------------------------------------ */

static void commit_step_xz(struct actor *a)
{
    a->world_x = (s16)(a->world_x + a->step_x);
    a->world_z = (s16)(a->world_z + a->step_z);
    a->room_x = (s16)(a->room_x + a->step_x);
    a->room_z = (s16)(a->room_z + a->step_z);
    a->step_x = 0;
    a->step_z = 0;
}

static void pose_store(struct actor *a)
{
    int wrapped = 0;
    u8 *body;

    if (!s_pose_init) {
        int i;
        for (i = 0; i < POSE_SLOTS; i++) {
            s_pose_owner[i] = -1;
        }
        s_pose_init = 1;
    }

    while (s_pose_owner[s_pose_ring] != -1) {
        struct world_object *o = object_get(s_pose_owner[s_pose_ring]);

        if (o->actor == -1 || g_actors[o->actor].pose_slot != s_pose_ring) {
            break;
        }
        if (++s_pose_ring == POSE_SLOTS) {
            s_pose_ring = 0;
            if (wrapped) {
                fatal_error(0, "pose ring full");
            }
            wrapped = 1;
        }
    }

    body = hqr_get(g_cache_body, a->body, NULL);
    anim_save_pose((u8 *)g_anim_buffer + POSE_BYTES * s_pose_ring, body);

    if (a->pose_slot != -1) {
        s_pose_owner[a->pose_slot] = -1;
    }
    s_pose_owner[s_pose_ring] = a->object;
    a->pose_slot = s_pose_ring++;
    if (s_pose_ring == POSE_SLOTS) {
        s_pose_ring = 0;
    }
}

static void process_sprite(struct actor *a)
{
    const u8 *hyb;
    const u8 *rec;

    if (a->anim == -1 || a->body == -1) {
        return;
    }

    hyb = hqr_get(g_cache_hyb, a->anim, NULL);
    hyb += blob_u32(hyb, 8);
    rec = hyb + blob_u32(hyb, a->body * 4);

    ticks_update();
    if (g_ticks - a->sprite_time < (u16)blob_s16(rec, a->frame * 8 + 4)) {
        a->end_frame = 0;
        return;
    }

    if (++a->frame >= a->frame_count) {
        if (a->anim_type & ANIM_REPEAT) {
            a->frame = 0;
        } else {
            a->anim = -1;
            a->anim_type = 0;
            a->anim_info = -1;
        }
        a->end_anim = 1;
    }
    a->sprite_time = g_ticks;
    a->end_frame = 1;
}

void actor_process(int flag)
{
    struct actor *a = g_cur_actor;
    s16 index = g_cur_actor_index;
    int nb_hard = 0, nb_col = 0;
    s16 sx = 0, sy = 0, sz = 0;
    s16 dx = 0, dy = 0, dz = 0;
    s16 saved_col[3];
    int k;

    if (a->flags & 0x200) {
        process_sprite(a);
        return;
    }

    /* 0x40ff9b: switch to a queued animation. */
    if (a->new_anim != -1) {
        s16 next = a->new_anim;
        const u8 *anim;

        if (next == -2) {
            actor_make_static(index);
            a->new_anim = -1;
            a->new_anim_type = 0;
            a->new_anim_info = -1;
            a->end_anim = 1;
            return;
        }

        if (a->new_anim_type & ANIM_RESET) {
            anim_reset(hqr_get(g_cache_body, a->body, NULL));
            a->new_anim_type &= ~ANIM_RESET;
        } else {
            if (!a->end_frame) {
                commit_step_xz(a);
                a->slide[0] = a->slide[1] = a->slide[2] = 0;
            }
            pose_store(a);
        }

        a->anim = next;
        a->anim_type = a->new_anim_type;
        a->anim_info = a->new_anim_info;
        a->new_anim = -1;
        a->new_anim_type = 0;
        a->new_anim_info = -1;
        a->end_anim = 0;
        a->frame = 0;
        anim = hqr_get(g_cache_anim, next, NULL);
        a->frame_count = (s16)anim_frame_count(anim);
    }

    if (a->anim == -1) {
        a->end_frame = 0;
        if (a->speed) {
            sx = a->step_x;
            sy = a->step_y;
            sz = a->step_z;
            g_anim_step_x = 0;
            g_anim_step_y = 0;
            g_anim_step_z = (s16)-interp_get(a->speed_interp);
            r3d_rotate_2d(0, g_anim_step_z, a->beta);
            dx = (s16)(g_r3d_x - sx);
            dz = (s16)(g_r3d_y - sz);
            dy = 0;
        } else {
            nb_col = actor_collide(index, a->zv);
            for (k = 0; k < nb_col; k++) {
                g_actors[a->col[k]].col_by = index;
            }
            for (k = nb_col; k < 3; k++) {
                a->col[k] = -1;
            }
        }
    } else {
        const u8 *anim = hqr_get(g_cache_anim, a->anim, NULL);
        u8 *body = hqr_get(g_cache_body, a->body, NULL);

        sx = a->step_x;
        sy = a->step_y;
        sz = a->step_z;

        if (a->frame < (int)(u16)blob_s16(anim, 0)) {
            a->end_frame = (s16)anim_set_inter(a->frame, anim, body, flag);
        } else {
            a->end_frame = 1;
        }
        a->anim_flag = 0;
        if (a->end_frame == -1) {
            a->end_frame = 0;
        }

        r3d_rotate_2d(g_anim_step_x, g_anim_step_z, a->beta);
        dx = (s16)(a->slide[0] + g_r3d_x - sx);
        dz = (s16)(a->slide[2] + g_r3d_y - sz);
    }

    /* 0x41056d: vertical motion from the fall interpolator. */
    if (a->fall[2]) {
        if (a->fall[2] == -1) {
            dy = (s16)(a->fall[1] - sy);
            a->fall[2] = 0;
            a->fall[1] = 0;
            a->fall[0] = 0;
        } else {
            dy = (s16)(interp_get(a->fall) - sy);
        }
    } else {
        dy = 0;
    }

    memcpy(saved_col, a->col, sizeof(saved_col));

    if (dx || dz || dy) {
        s16 moved[6];
        int blocked = 0;

        memcpy(moved, a->zv, sizeof(moved));
        moved[0] = (s16)(moved[0] + dx);
        moved[1] = (s16)(moved[1] + dx);
        moved[2] = (s16)(moved[2] + dy);
        moved[3] = (s16)(moved[3] + dy);
        moved[4] = (s16)(moved[4] + dz);
        moved[5] = (s16)(moved[5] + dz);

        if (a->dyn_flags & 1) {
            nb_hard = hardcol_test(moved, g_floor_rooms[a->room]);
            for (k = 0; k < nb_hard; k++) {
                const u8 *h = g_hardcol_hits[k];
                s16 type = blob_s16(h, 14);

                if (type == HARDCOL_TAGGED) {
                    a->hard_col = blob_s16(h, 12);
                }
                if (type == HARDCOL_BLOCK) {
                    a->hard_col = 255;
                }
                if ((type != HARDCOL_NOT_PLAYER || index != g_player_actor) && (dx || dz)) {
                    s16 box[6];
                    int j;

                    for (j = 0; j < 6; j++) {
                        box[j] = blob_s16(h, j * 2);
                    }
                    s_move_x = dx;
                    s_move_z = dz;
                    slide(a->zv, moved, box);
                    a->slide[0] = (s16)(a->slide[0] + (s_move_x - dx));
                    a->slide[2] = (s16)(a->slide[2] + (s_move_z - dz));
                    moved[0] = (s16)(moved[0] + (s_move_x - dx));
                    moved[1] = (s16)(moved[1] + (s_move_x - dx));
                    moved[4] = (s16)(moved[4] + (s_move_z - dz));
                    moved[5] = (s16)(moved[5] + (s_move_z - dz));
                    dx = s_move_x;
                    dz = s_move_z;
                }
            }
        } else {
            a->hard_col = hardcol_test(moved, g_floor_rooms[a->room]) ? 1 : 0;
        }

        if (a->dyn_flags & 1) {
            nb_col = actor_collide(index, moved);
            for (k = 0; k < nb_col; k++) {
                s16 other = a->col[k];
                struct actor *o = &g_actors[other];

                o->col_by = index;

                if (o->flags & 0x80) {
                    if (a->track_mode == TRACK_MANUAL && !g_tab_locked) {
                        object_found(o->object, 0);   /* 0x40dc7d */
                    }
                    continue;
                }

                if (o->flags & 0x10) {
                    s16 pushed[6];
                    int push_ok = 1;

                    memcpy(pushed, o->zv, sizeof(pushed));
                    pushed[0] = (s16)(pushed[0] + dx);
                    pushed[1] = (s16)(pushed[1] + dx);
                    pushed[4] = (s16)(pushed[4] + dz);
                    pushed[5] = (s16)(pushed[5] + dz);

                    if (hardcol_test(pushed, g_floor_rooms[o->room])) {
                        push_ok = 0;
                    } else if (actor_collide(other, pushed)) {
                        /* 0x41094c: the contacts land in the pusher's own
                         * col[] slots, as in the original. */
                        push_ok = 0;
                    }

                    if (push_ok) {
                        if (o->flags & 8) {
                            actor_unstatic(other);
                        }
                        o->flags |= 1;
                        o->world_x = (s16)(o->world_x + dx);
                        o->world_z = (s16)(o->world_z + dz);
                        o->room_x = (s16)(o->room_x + dx);
                        o->room_z = (s16)(o->room_z + dz);
                        memcpy(o->zv, pushed, sizeof(pushed));
                    } else if (dx || dz) {
                        s16 box[6];

                        memcpy(box, o->zv, sizeof(box));
                        if (o->room != a->room) {
                            zv_to_room(box, o->room, a->room);
                        }
                        s_move_x = dx;
                        s_move_z = dz;
                        slide(a->zv, moved, box);
                        dx = s_move_x;
                        dz = s_move_z;
                    }
                } else if ((a->dyn_flags & 1) && (dx || dz)) {
                    s16 box[6];

                    memcpy(box, o->zv, sizeof(box));
                    if (o->room != a->room) {
                        zv_to_room(box, o->room, a->room);
                    }
                    s_move_x = dx;
                    s_move_z = dz;
                    slide(a->zv, moved, box);
                    a->slide[0] = (s16)(a->slide[0] + (s_move_x - dx));
                    a->slide[2] = (s16)(a->slide[2] + (s_move_z - dz));
                    moved[0] = (s16)(moved[0] + (s_move_x - dx));
                    moved[1] = (s16)(moved[1] + (s_move_x - dx));
                    moved[4] = (s16)(moved[4] + (s_move_z - dz));
                    moved[5] = (s16)(moved[5] + (s_move_z - dz));
                    dx = s_move_x;
                    dz = s_move_z;
                }
            }
        }

        /* 0x410c2a: still stuck after sliding? */
        if (nb_hard || nb_col) {
            int n = hardcol_test(moved, g_floor_rooms[a->room]);

            for (k = 0; k < n; k++) {
                if (blob_s16(g_hardcol_hits[k], 14) != 10 || index != g_player_actor) {
                    blocked = 1;
                }
            }
            if (actor_collide(index, moved) && actor_collide_solid(index, a->zv)) {
                blocked = 1;
            }
        }

        if (!blocked) {
            a->step_x = (s16)(dx + sx);
            a->step_y = (s16)(dy + sy);
            a->step_z = (s16)(dz + sz);
            a->zv[0] = (s16)(a->zv[0] + dx);
            a->zv[1] = (s16)(a->zv[1] + dx);
            a->zv[2] = (s16)(a->zv[2] + dy);
            a->zv[3] = (s16)(a->zv[3] + dy);
            a->zv[4] = (s16)(a->zv[4] + dz);
            a->zv[5] = (s16)(a->zv[5] + dz);
        }
    }

    /* 0x410d69: gravity. */
    if (a->fall[2]) {
        if (a->fall[2] != -1 && (a->flags & 0x100)) {
            a->falling = 1;
        }
    } else {
        a->world_y = (s16)(a->world_y + a->step_y);
        a->room_y = (s16)(a->room_y + a->step_y);
        a->step_y = 0;

        if (a->flags & 0x100) {
            s16 below[6];

            memcpy(below, a->zv, sizeof(below));
            below[3] = (s16)(below[3] + 100);
            if (a->room_y >= g_clip_y_default
                || hardcol_test(below, g_floor_rooms[a->room])
                || actor_collide_mark(index, below)) {
                a->falling = 0;
            } else {
                interp_init(0, (s16)(g_clip_y_default - a->room_y), 40, a->fall);
            }
        }
    }

    for (k = nb_col; k < 3; k++) {
        a->col[k] = -1;
    }

    /* 0x410e9f: pushed objects that are no longer touched freeze again. */
    for (k = 0; k < 3; k++) {
        s16 other = saved_col[k];

        if (other != -1 && (g_actors[other].flags & 0x10)) {
            int j;
            for (j = 0; j < 3 && a->col[j] != other; j++) {
            }
            if (j == 3) {
                g_actors[other].flags &= ~1;
                actor_make_static(other);
            }
        }
    }

    if (a->end_frame) {
        if (a->frame == a->frame_count - 1) {
            a->end_anim = 1;
            a->end_frame = 0;
            if (a->anim_type & ANIM_REPEAT) {
                a->frame = 0;
            } else if (a->new_anim == -1) {
                a->anim_type &= ~ANIM_UNINTERRUPTIBLE;
                anim_init(a->anim_info, ANIM_REPEAT, -1);
            } else {
                a->frame = 0;
            }
        } else {
            a->frame++;
        }
        commit_step_xz(a);
        a->slide[0] = a->slide[1] = a->slide[2] = 0;
    } else {
        if (a->anim == -1 && a->speed && !a->speed_interp[2]) {
            commit_step_xz(a);
            interp_init(0, a->speed, 60, a->speed_interp);
        }
        a->end_anim = 0;
    }
}

/* ---- scene zones (0x40f7d6) -------------------------------------------- */

static const u8 *zone_find(s16 x, s16 y, s16 z, const u8 *p, int count)
{
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

void actor_fall(void)
{
    struct actor *a = g_cur_actor;
    int life_set = 0;
    int moved;

    do {
        const u8 *room = g_floor_rooms[a->room];
        const u8 *p = room + (blob_s16(room, 2) & ~1);
        int count = blob_s16(p, 0);
        const u8 *end;

        moved = 0;
        p += 2;
        end = p + count * 16;

        while (!moved && count > 0) {
            const u8 *z = zone_find((s16)(a->step_x + a->room_x),
                                    (s16)(a->step_y + a->room_y),
                                    (s16)(a->step_z + a->room_z), p, count);
            s16 type;

            if (z == NULL) {
                break;
            }
            count = (int)((end - z) / 16) - 1;
            p = z + 16;
            type = blob_s16(z, 14);

            switch (type) {
            case ZONE_ROOM_LINK: {
                /* Room link: carry the actor into the neighbour's frame. */
                const u8 *from = g_floor_rooms[a->room];
                const u8 *to;
                s16 dx, dy, dz;

                a->room = blob_s16(z, 12);
                to = g_floor_rooms[a->room];
                dx = (s16)((s16)(ROOM_X(to) - ROOM_X(from)) * 10);
                dy = (s16)((s16)(ROOM_Y(to) - ROOM_Y(from)) * 10);
                dz = (s16)((s16)(ROOM_Z(to) - ROOM_Z(from)) * 10);
                a->room_x = (s16)(a->room_x - dx);
                a->room_y = (s16)(a->room_y + dy);
                a->room_z = (s16)(a->room_z + dz);
                a->zv[0] = (s16)(a->zv[0] - dx);
                a->zv[1] = (s16)(a->zv[1] - dx);
                a->zv[2] = (s16)(a->zv[2] + dy);
                a->zv[3] = (s16)(a->zv[3] + dy);
                a->zv[4] = (s16)(a->zv[4] + dz);
                a->zv[5] = (s16)(a->zv[5] + dz);
                moved = 1;

                if (g_cur_actor_index == g_player_actor) {
                    g_change_room = 1;
                    g_new_room = blob_s16(z, 12);
                    world_set_room(g_new_room);
                } else {
                    g_actors_dirty = 1;
                }
                timer_reset_u32(&a->room_chrono);
                break;
            }
            case ZONE_MATERIAL:
                a->material = blob_s16(z, 12);
                break;
            case ZONE_HARD_DEC:
                if (!life_set) {
                    a->hard_dec = blob_s16(z, 12);
                }
                break;
            case ZONE_LIFE: {
                s16 life = object_get(a->object)->field_24;
                if (life != -1) {
                    a->life = life;
                    a->hard_dec = blob_s16(z, 12);
                    life_set = 1;
                }
                break;
            }
            default:
                break;
            }
        }
    } while (moved);
}

/* ---- turning and DO_MOVE ----------------------------------------------- */

/* 0x411233 */
void actor_turn_keys(s16 speed)
{
    struct actor *a = g_cur_actor;

    if (g_scene_dir_flags & DIR_LEFT) {
        if (a->rot_dir != 1) {
            a->rot[2] = 0;
        }
        a->rot_dir = 1;
        if (!a->rot[2]) {
            interp_init(a->beta, (s16)(a->beta + 0x100),
                        a->speed ? speed : (s16)(speed / 2), a->rot);
        }
        a->beta = interp_get(a->rot);
    }
    if (g_scene_dir_flags & DIR_RIGHT) {
        if (a->rot_dir != -1) {
            a->rot[2] = 0;
        }
        a->rot_dir = -1;
        if (!a->rot[2]) {
            interp_init(a->beta, (s16)(a->beta - 0x100),
                        a->speed ? speed : (s16)(speed / 2), a->rot);
        }
        a->beta = interp_get(a->rot);
    }
    if (!(g_scene_dir_flags & (DIR_LEFT | DIR_RIGHT))) {
        a->rot_dir = 0;
        a->rot[2] = 0;
    }
}

/* 0x4125c0 */
void actor_anim_move(s16 stand, s16 walk, s16 run, s16 stop, s16 back,
                     s16 turn_right, s16 turn_left)
{
    struct actor *a = g_cur_actor;

    if (a->speed == 10) {
        anim_init(walk, ANIM_REPEAT, -1);
    }
    if (a->speed == 16) {
        anim_init(run, ANIM_REPEAT, -1);
    }
    if (a->speed == -1) {
        if (a->anim == walk) {
            anim_init(stand, ANIM_ONCE, back);
        } else if (a->anim == run) {
            anim_init(stop, ANIM_ONCE, stand);
        } else {
            anim_init(back, ANIM_REPEAT, -1);
        }
    }
    if (a->speed == 0) {
        if (a->anim == run || a->anim == walk) {
            anim_init(stop, ANIM_ONCE, stand);
        } else {
            if (a->rot_dir == 0) {
                anim_init(stand, ANIM_REPEAT, -1);
            }
            if (a->rot_dir == 1) {
                anim_init(turn_left, ANIM_ONCE, stand);
            }
            if (a->rot_dir == -1) {
                anim_init(turn_right, ANIM_ONCE, stand);
            }
        }
    }
}

/* 0x422892 - which side of the heading the target lies on. */
static s16 s_dir_dx, s_dir_dz;

static s16 heading_side(s16 angle)
{
    s16 rx, rz;
    s32 cross;

    r3d_rotate_2d(0, 1000, angle);
    rx = g_r3d_x;
    rz = g_r3d_y;

    cross = (s32)rx * s_dir_dz - (s32)rz * s_dir_dx;
    if (cross < 0) return -1;
    if (cross == 0) return 0;
    return 1;
}

/* 0x4228e1 */
static s16 turn_towards(s16 x, s16 z, s16 beta, s16 tx, s16 tz)
{
    s16 s1, s2;

    s_dir_dx = (s16)(tx - x);
    s_dir_dz = (s16)(tz - z);
    s1 = heading_side((s16)(beta - 4));
    s2 = heading_side((s16)(beta + 4));

    if (s2 == -1 && s1 == 1) {
        return heading_side(beta);
    }
    return (s16)((u16)(s16)(s1 + s2 + 1) >> 1);
}

/* 0x4229a1 - (b - a) * d / c + a with the 16 bit divide of the original. */
static s16 lerp_track(s16 a, s16 b, s16 c, s16 d)
{
    s32 product = (s32)(b - a) * d;
    s32 dividend = (s32)(((u32)(u16)((u32)product >> 16) << 16) | (u16)product);

    if (c == 0) {
        return a;
    }
    return (s16)((s16)(dividend / c) + a);
}

static void turn_to(struct actor *a, s16 dir, int shift, s16 duration)
{
    if (!a->rot[2] || dir != a->rot_dir) {
        interp_init(a->beta, (s16)(a->beta - (s16)((u16)dir << shift)), duration, a->rot);
    }
    a->rot_dir = dir;
    if (dir) {
        a->beta = interp_get_angle(a->rot);
    } else {
        a->rot[2] = 0;
    }
}

static void zv_shift(struct actor *a, int sign)
{
    s16 x = (s16)(a->step_x + a->room_x);
    s16 y = (s16)(a->step_y + a->room_y);
    s16 z = (s16)(a->step_z + a->room_z);

    if (sign < 0) {
        x = (s16)-x;
        y = (s16)-y;
        z = (s16)-z;
    }
    a->zv[0] = (s16)(a->zv[0] + x);
    a->zv[1] = (s16)(a->zv[1] + x);
    a->zv[2] = (s16)(a->zv[2] + y);
    a->zv[3] = (s16)(a->zv[3] + y);
    a->zv[4] = (s16)(a->zv[4] + z);
    a->zv[5] = (s16)(a->zv[5] + z);
}

/* Target position given in `room`, expressed in the actor's room. */
static void track_point(struct actor *a, s16 room, s16 *x, s16 *y, s16 *z)
{
    if (a->room != room) {
        const u8 *t = g_floor_rooms[room];
        const u8 *r = g_floor_rooms[a->room];

        *x = (s16)(*x - (s16)((s16)(ROOM_X(r) - ROOM_X(t)) * 10));
        if (y != NULL) {
            *y = (s16)(*y + (s16)((s16)(ROOM_Y(r) - ROOM_Y(t)) * 10));
        }
        *z = (s16)(*z + (s16)((s16)(ROOM_Z(r) - ROOM_Z(t)) * 10));
    }
}

void actor_do_move(void)
{
    struct actor *a = g_cur_actor;

    switch (a->track_mode) {
    case TRACK_MANUAL:
        /* Manual: the arrow keys. */
        actor_turn_keys(30);
        if (g_scene_dir_flags & DIR_UP) {
            if (a->speed == 0 || a->speed == -1) {
                a->speed = 10;
            }
            if (a->speed > 0 && a->speed < 10) {
                a->speed = 16;
            }
        } else if (a->speed > 0 && a->speed <= 10) {
            a->speed--;
        } else {
            a->speed = 0;
        }
        if (g_scene_dir_flags & DIR_DOWN) {
            if (a->speed == 0 || a->speed >= 10) {
                a->speed = -1;
            }
            if (a->speed == 16) {
                a->speed = 0;
            }
        }
        break;

    case TRACK_FOLLOW:
        /* Follow another object. */
        if (a->track_number != -1) {
            s16 target = object_get(a->track_number)->actor;

            if (target == -1) {
                a->rot_dir = 0;
                a->speed = 0;
            } else {
                struct actor *t = &g_actors[target];

                if (t->room != -1 && a->room != -1) {
                    const u8 *r = g_floor_rooms[a->room];
                    const u8 *tr = g_floor_rooms[t->room];
                    s16 tx = (s16)(t->room_x - (s16)((s16)(ROOM_X(r) - ROOM_X(tr)) * 10));
                    s16 tz = (s16)((s16)((s16)(ROOM_Z(r) - ROOM_Z(tr)) * 10) + t->room_z);
                    s16 dir = turn_towards((s16)(a->step_x + a->room_x),
                                           (s16)(a->step_z + a->room_z),
                                           a->beta, tx, tz);

                    turn_to(a, dir, 8, 60);
                    a->speed = 10;
                }
            }
        }
        break;

    case TRACK_SCRIPT: {
        const u8 *track = hqr_get(g_cache_trak, a->track_number, NULL);
        const u8 *p = track + a->position_in_track * 2;
        s16 op = blob_s16(p, 0);

        switch (op) {
        case TRACK_OP_WARP: {
            /* Warp to a point, possibly in another room. */
            s16 room = blob_s16(p, 2);
            const u8 *r;

            if (a->room != room) {
                if (g_player_actor == g_cur_actor_index) {
                    g_change_room = 1;
                    g_new_room = room;
                }
                a->room = room;
            }
            zv_shift(a, -1);
            a->room_x = a->world_x = blob_s16(p, 4);
            a->room_y = a->world_y = blob_s16(p, 6);
            a->room_z = a->world_z = blob_s16(p, 8);
            r = g_floor_rooms[a->room];
            a->world_x = (s16)(a->world_x - (s16)((s16)(ROOM_X(g_room_data) - ROOM_X(r)) * 10));
            a->world_y = (s16)(a->world_y + (s16)((s16)(ROOM_Y(g_room_data) - ROOM_Y(r)) * 10));
            a->world_z = (s16)(a->world_z + (s16)((s16)(ROOM_Z(g_room_data) - ROOM_Z(r)) * 10));
            zv_shift(a, 1);
            a->speed = 0;
            a->rot_dir = 0;
            a->rot[2] = 0;
            a->position_in_track = (s16)(a->position_in_track + 5);
            break;
        }
        case TRACK_OP_GOTO_POS: {
            /* Walk to a point. */
            s16 tx = blob_s16(p, 4);
            s16 tz = blob_s16(p, 6);

            track_point(a, blob_s16(p, 2), &tx, NULL, &tz);
            if (distance_xz((s16)(a->step_x + a->room_x), (s16)(a->step_z + a->room_z),
                            tx, tz) >= 200) {
                s16 dir = turn_towards((s16)(a->step_x + a->room_x),
                                       (s16)(a->step_z + a->room_z), a->beta, tx, tz);
                turn_to(a, dir, 6, 15);
            } else {
                a->position_in_track = (s16)(a->position_in_track + 4);
            }
            break;
        }
        case TRACK_OP_END:
            a->speed = 0;
            a->track_number = -1;
            actor_set_move_mode(TRACK_NONE, 0);
            break;
        case TRACK_OP_REWIND:
            a->position_in_track = 0;
            break;
        case TRACK_OP_MARK:
            a->mark = blob_s16(p, 2);
            a->position_in_track = (s16)(a->position_in_track + 2);
            break;
        case TRACK_OP_ROTATE: {
            s16 target = blob_s16(p, 2);

            a->rot_dir = (((a->beta - target) & 0x3ff) <= 0x200) ? -1 : 1;
            if (!a->rot[2]) {
                interp_init(a->beta, target, 120, a->rot);
            }
            a->beta = interp_get_angle(a->rot);
            if (a->beta == target) {
                a->rot_dir = 0;
                a->position_in_track = (s16)(a->position_in_track + 2);
            }
            break;
        }
        case TRACK_OP_COL_OFF:
            a->dyn_flags &= ~1;
            a->position_in_track++;
            break;
        case TRACK_OP_COL_ON:
            a->dyn_flags |= 1;
            a->position_in_track++;
            break;
        case TRACK_OP_SKIP_WORD:
            a->position_in_track = (s16)(a->position_in_track + 2);
            break;
        case TRACK_OP_FALL_OFF:
            a->flags &= ~0x40;
            a->position_in_track++;
            break;
        case TRACK_OP_FALL_ON:
            a->flags |= 0x40;
            a->position_in_track++;
            break;
        case TRACK_OP_GOTO_POS_Y: {
            /* Walk to a point while climbing to its height. */
            s16 tx = blob_s16(p, 4), ty = blob_s16(p, 6), tz = blob_s16(p, 8);
            s16 duration = blob_s16(p, 10);

            track_point(a, blob_s16(p, 2), &tx, &ty, &tz);
            if (a->room_y == ty
                && distance_xz((s16)(a->step_x + a->room_x), (s16)(a->step_z + a->room_z),
                               tx, tz) < 200) {
                a->position_in_track = (s16)(a->position_in_track + 6);
            } else {
                s16 dir = turn_towards((s16)(a->step_x + a->room_x),
                                       (s16)(a->step_z + a->room_z), a->beta, tx, tz);
                if (!a->fall[2]) {
                    interp_init(0, (s16)(ty - (s16)(a->step_y + a->room_y)), duration, a->fall);
                }
                turn_to(a, dir, 8, 60);
            }
            break;
        }
        case TRACK_OP_STORE_POS: {
            struct world_object *o = object_get(a->object);

            o->x = (s16)(a->step_x + a->room_x);
            o->y = (s16)(a->step_y + a->room_y);
            o->z = (s16)(a->step_z + a->room_z);
            a->position_in_track++;
            break;
        }
        case TRACK_OP_STAIRS_X:
        case TRACK_OP_STAIRS_Z: {
            /* Stairs: height follows x (14) or z (15) between two marks. */
            s16 target = blob_s16(p, 2);
            s16 ty = blob_s16(p, 4);
            s16 tz = blob_s16(p, 6);
            struct world_object *o = object_get(a->object);
            s16 cur_y = (s16)(a->step_y + a->room_y);

            if (cur_y >= ty - 100 && cur_y <= ty + 100) {
                s16 d = (s16)(ty - a->world_y);
                a->step_y = 0;
                a->world_y = (s16)(a->world_y + d);
                a->room_y = (s16)(a->room_y + d);
                a->zv[2] = (s16)(a->zv[2] + d);
                a->zv[3] = (s16)(a->zv[3] + d);
                a->position_in_track = (s16)(a->position_in_track + 4);
            } else {
                s16 d;
                s16 dir;

                if (op == TRACK_OP_STAIRS_X) {
                    d = (s16)(lerp_track(o->y, ty, (s16)(target - o->x),
                                         (s16)(a->step_x + a->room_x - o->x)) - a->world_y);
                } else {
                    d = (s16)(lerp_track(o->y, ty, (s16)(tz - o->z),
                                         (s16)(a->step_z + a->room_z - o->z)) - a->world_y);
                }
                a->world_y = (s16)(a->world_y + d);
                a->room_y = (s16)(a->room_y + d);
                a->zv[2] = (s16)(a->zv[2] + d);
                a->zv[3] = (s16)(a->zv[3] + d);

                dir = turn_towards((s16)(a->step_x + a->room_x),
                                   (s16)(a->step_z + a->room_z), a->beta,
                                   target, tz);
                turn_to(a, dir, 8, 60);
            }
            break;
        }
        case TRACK_OP_ANGLES:
            a->alpha = blob_s16(p, 2);
            a->beta = blob_s16(p, 4);
            a->gamma = blob_s16(p, 6);
            a->rot_dir = 0;
            a->position_in_track = (s16)(a->position_in_track + 4);
            break;
        default:
            break;
        }
        break;
    }
    default:
        break;
    }

    /* 0x4125b6 */
    a->beta = (s16)(a->beta & 0x3ff);
}

/* ---- teleport (0x40ab13) ----------------------------------------------- */

void actor_teleport(s16 floor, s16 room, s16 x, s16 y, s16 z)
{
    struct actor *a = g_cur_actor;
    s16 cx, cy, cz;
    int i;

    ticks_freeze();

    for (i = 0; i < ACTOR_MAX; i++) {
        object_save_zv((s16 *)&g_actors[i]);
    }

    a->stage = floor;
    a->room = room;
    a->material = 255;

    cx = (s16)(a->step_x + a->room_x);
    cy = (s16)(a->step_y + a->room_y);
    cz = (s16)(a->step_z + a->room_z);
    a->zv[0] = (s16)(a->zv[0] + (x - cx));
    a->zv[1] = (s16)(a->zv[1] + (x - cx));
    a->zv[2] = (s16)(a->zv[2] + (y - cy));
    a->zv[3] = (s16)(a->zv[3] + (y - cy));
    a->zv[4] = (s16)(a->zv[4] + (z - cz));
    a->zv[5] = (s16)(a->zv[5] + (z - cz));

    a->room_x = a->world_x = x;
    a->room_y = a->world_y = y;
    a->room_z = a->world_z = z;
    a->step_x = a->step_y = a->step_z = 0;

    if (g_player_actor == g_cur_actor_index) {
        if (g_cur_floor == floor) {
            if (g_cur_room != room) {
                g_change_room = 1;
                g_new_room = room;
            }
        } else {
            g_change_floor = 1;
            g_new_floor = floor;
            g_new_room = room;
        }
    } else {
        if (room != g_cur_room) {
            const u8 *r = g_floor_rooms[a->room];
            a->world_x = (s16)(a->world_x - (s16)((s16)(ROOM_X(g_room_data) - ROOM_X(r)) * 10));
            a->world_y = (s16)(a->world_y + (s16)((s16)(ROOM_Y(g_room_data) - ROOM_Y(r)) * 10));
            a->world_z = (s16)(a->world_z + (s16)((s16)(ROOM_Z(g_room_data) - ROOM_Z(r)) * 10));
        }
        g_objects_dirty = 1;
    }

    ticks_unfreeze();
}

/* ---- camera selection (0x40a57a, 0x40a41f, 0x41b65a) -------------------- */

/* 0x41b562 - do two segments intersect. */
static int segments_cross(s16 a1, s16 a2, s16 a3, s16 a4, s16 a5, s16 a6,
                          s16 a7, s16 a8)
{
    s32 d1 = (s32)a1 - a3, d2 = (s32)a6 - a8, d3 = (s32)a5 - a7;
    s32 d4 = (s32)a2 - a4, d5 = (s32)a1 - a5, d6 = (s32)a2 - a6;
    s32 den = d1 * d2 - d3 * d4;
    s32 t, u;

    if (den == 0) {
        return 0;
    }
    t = d5 * d2 - d3 * d6;
    u = -d1 * d6 + d5 * d4;
    if (den < 0) {
        den = -den;
        t = -t;
        u = -u;
    }
    return t >= 0 && u >= 0 && den >= t && den >= u;
}

/* 0x41b65a - centre of the box inside one of the camera's zone polygons:
 * edges have to cross rays to both the left and the right. */
static int in_camera_zone(s16 x1, s16 x2, s16 z1, s16 z2, const u8 *polys, int count)
{
    s16 cx = (s16)(((s32)x1 + x2) / 2);
    s16 cz = (s16)(((s32)z1 + z2) / 2);
    const u8 *p = polys;
    int i, k;

    for (i = 0; i < count; i++) {
        int n = blob_s16(p, 0);
        const u8 *pts = p + 2;
        s16 fx = blob_s16(pts, 0), fz = blob_s16(pts, 2);
        int sides = 0;

        p += 2 + n * 4;
        for (k = 0; k < n; k++) {
            s16 ex1 = blob_s16(pts, k * 4), ez1 = blob_s16(pts, k * 4 + 2);
            s16 ex2, ez2;

            if (k < n - 1) {
                ex2 = blob_s16(pts, (k + 1) * 4);
                ez2 = blob_s16(pts, (k + 1) * 4 + 2);
            } else {
                ex2 = fx;
                ez2 = fz;
            }
            if (segments_cross(cx, cz, (s16)(cx - 10000), cz, ex1, ez1, ex2, ez2)) {
                sides |= 1;
            }
            if (segments_cross(cx, cz, (s16)(cx + 10000), cz, ex1, ez1, ex2, ez2)) {
                sides |= 2;
            }
        }
        if (sides == 3) {
            return 1;
        }
    }
    return 0;
}

static int player_in_camera(int cam)
{
    const struct actor *p = &g_actors[g_player_actor];
    const u8 *zone = g_room_cameras[cam] + g_camera_zone[cam] * 2;

    return in_camera_zone((s16)(p->zv[0] / 10), (s16)(p->zv[1] / 10),
                          (s16)(p->zv[4] / 10), (s16)(p->zv[5] / 10),
                          zone + 2, blob_s16(zone, 0));
}

/* 0x40a41f - among the cameras whose zone holds the player, the one facing
 * most nearly the way the player faces. */
static s16 camera_best(void)
{
    const struct actor *p = &g_actors[g_player_actor];
    s16 best = 32000;
    s16 found = -1;
    s16 i;

    for (i = 0; i < g_room_camera_count; i++) {
        if (player_in_camera(i)) {
            u16 cam_beta = (u16)blob_s16(g_room_cameras[i], 2);
            s16 d;

            cam_beta = (u16)((cam_beta & 0xff) | ((((cam_beta >> 8) + 2) & 3) << 8));
            d = (s16)(cam_beta + p->beta);
            if (d < 0) {
                d = (s16)-d;
            }
            if (d < best) {
                best = d;
                found = i;
            }
        }
    }
    return found;
}

/* 0x40a57a */
int camera_select(void)
{
    s16 cam = g_cur_camera;
    s16 best;

    if (g_cur_camera != -1 && player_in_camera(g_cur_camera)) {
        return 0;
    }
    /* No camera target until the player object has an actor (the AITD1
     * hero only gets one once a camera shows it). */
    if (g_player_actor < 0 || g_actors[g_player_actor].object != g_player_object) {
        return 0;
    }

    best = camera_best();
    if (best != -1) {
        cam = best;
    }
    if (g_cur_camera == cam) {
        return 0;
    }
    g_new_camera = cam;
    g_init_view = 1;
    return 1;
}
