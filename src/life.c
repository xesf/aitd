/*
 * life.c - the life script interpreter (0x4038f6) and its expression
 * evaluator (0x403002).
 *
 * A life script is a stream of 16 bit words from LISTLIFE.PAK. Each opcode
 * may carry bit 15, in which case the next word names the object the
 * instruction acts on. When that object has an actor, the instruction runs
 * on the actor; when it has none, a reduced set of opcodes edits the object
 * record directly (the table at 0x403a02). The script owner is restored
 * after every instruction.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actor.h"
#include "aitd.h"
#include "game.h"
#include "anim.h"
#include "draw.h"
#include "exe_tables.h"
#include "life.h"
#include "savegame.h"
#include "menu.h"
#include "palette.h"
#include "platform.h"
#include "r3d.h"
#include "subsystems.h"
#include "world.h"

/* Script state, 0x441204..0x44120c and 0x435050. */
static struct actor *s_owner;     /* 0x441204 */
static s16 s_owner_index;         /* 0x44120c */
static const u8 *s_pc;            /* 0x441208 */
static s16 s_life_num = -1;       /* 0x435050 */

/* Objects that may be picked up, two inventories of 50 (0x44478c). */
s16 g_inventory[2][50];
s16 g_inventory_count[2];         /* 0x444bbc */
s16 g_in_hand[2] = { -1, -1 };    /* 0x444bb4 */
s16 g_inventory_index;            /* 0x445034 */

static u16 s_rand_lo = 0x349a, s_rand_hi = 0xacdc;  /* 0x43524c */


s16 g_palette_effect;   /* 0x444ff6 */

/* Alone in the Dark 1: the scenery box GET_HARD_CLIP found around the
 * actor, empty (32000 / -32000) when none. */
s16 g_hard_clip[6];

static void get_hard_clip(const struct actor *a)
{
    const u8 *room = g_floor_rooms[a->room];
    const u8 *box = room + (blob_s16(room, 0) & ~1);
    int count = blob_s16(box, 0);
    int i;

    box += 2;
    for (i = 0; i < count; i++, box += 16) {
        s16 zv[6];
        int k;

        for (k = 0; k < 6; k++) {
            zv[k] = blob_s16(box, k * 2);
        }
        if (zv_overlap(a->zv, zv)) {
            memcpy(g_hard_clip, zv, sizeof(g_hard_clip));
            return;
        }
    }
    g_hard_clip[0] = g_hard_clip[2] = g_hard_clip[4] = 32000;
    g_hard_clip[1] = g_hard_clip[3] = g_hard_clip[5] = -32000;
}

/* Alone in the Dark 1 variable TEST_ZV_END_ANIM: would the actor, moved by
 * the whole of `anim` and raised by dy, stand clear of the scenery while
 * the same box 100 lower touches it (i.e. land on something)? */
static s16 test_zv_end_anim(const struct actor *a, s16 anim_index, s16 dy)
{
    const u8 *anim = hqr_get(g_cache_anim, anim_index, NULL);
    const u8 *body = hqr_get(g_cache_body, a->body, NULL);
    s16 zv[6];
    s16 forward = 0;
    int frames, bones, size, f;

    if (anim == NULL || body == NULL) {
        return 0;
    }
    frames = blob_s16(anim, 0);
    bones = blob_s16(anim, 2);
    size = (blob_s16(body, 0) & 8) ? bones * 16 + 8 : (bones + 1) * 8;
    for (f = 0; f < frames; f++) {
        forward = (s16)(forward + blob_s16(anim, 4 + f * size + 6));
    }
    r3d_rotate_2d(0, forward, a->beta);
    memcpy(zv, a->zv, sizeof(zv));
    zv[0] = (s16)(zv[0] + g_r3d_x);
    zv[1] = (s16)(zv[1] + g_r3d_x);
    zv[2] = (s16)(zv[2] + dy);
    zv[3] = (s16)(zv[3] + dy);
    zv[4] = (s16)(zv[4] + g_r3d_y);
    zv[5] = (s16)(zv[5] + g_r3d_y);
    if (hardcol_test(zv, g_floor_rooms[a->room])) {
        return 0;
    }
    zv[2] = (s16)(zv[2] + 100);
    zv[3] = (s16)(zv[3] + 100);
    return hardcol_test(zv, g_floor_rooms[a->room]) ? 1 : 0;
}

/* Time Gate reads extra operands in a dozen opcodes (operand shapes taken
 * from the TG320.EX_ interpreter at 0x418ccb, checked against every
 * LISTLIFE script of the DOS data). */
static int time_gate(void)
{
    return game_id() == GAME_TIMEGATE;
}

/* Titles before Alone in the Dark 2 read some operands differently. */
static int before_aitd2(void)
{
    return game_id() == GAME_AITD1 || game_id() == GAME_JACK;
}

static s16 fetch(void)
{
    s16 v = (s16)(s_pc[0] | (s_pc[1] << 8));

    s_pc += 2;
    return v;
}

/* 0x422bd4 - 32 bit shift register, 16 steps per call. */
u16 rand16(void)
{
    int i;

    for (i = 0; i < 16; i++) {
        u16 ax = (u16)((u16)(s_rand_hi << 1) ^ s_rand_hi);
        u16 carry = (u16)((ax >> 14) & 1);   /* last bit out of shl ax, 2 */
        u16 lo_carry = (u16)(s_rand_lo >> 15);

        s_rand_lo = (u16)((s_rand_lo << 1) | carry);
        s_rand_hi = (u16)((s_rand_hi << 1) | lo_carry);
    }
    return s_rand_lo;
}

static s16 actor_object(s16 actor_index)
{
    if (actor_index == -1) {
        return -1;
    }
    return g_actors[actor_index].object;
}

/* 0x40faef */
static u8 matrix_lookup(s16 table, s16 a1, s16 a2)
{
    const u8 *t;
    u8 n;

    if (a1 == -1) a1 = 0;
    if (a2 == -1) a2 = 0;

    t = hqr_get(g_cache_mat, table, NULL);
    if (t == NULL) {
        return 0;
    }
    n = t[0];
    return t[2 + (u32)n * (u16)(g_actors[a1].material - 1)
             + (u16)(g_actors[a2].material - 1)];
}

/* 0x41425d - where `target` lies relative to the way `a` faces:
 * 1 in front, 2 behind, 4 left, 8 right. */
static s16 position_relative(const struct actor *a, const struct actor *target)
{
    s16 angle = (s16)(384 - a->beta);
    s16 cx = (s16)(((s32)a->zv[1] + a->zv[0]) >> 1);
    s16 cz = (s16)(((s32)a->zv[5] + a->zv[4]) >> 1);
    s16 zv[6];
    s16 dx, dz;
    s16 c, s;
    s32 rx, rz;

    memcpy(zv, target->zv, sizeof(zv));
    if (target->room != a->room && a->stage != -1 && target->stage != -1) {
        zv_to_room(zv, target->room, a->room);
    }

    dx = (s16)((((s32)zv[1] + zv[0]) >> 1) - cx);
    dz = (s16)((((s32)zv[5] + zv[4]) >> 1) - cz);

    c = g_sin_table[(angle + 256) & 0x3ff];
    s = g_sin_table[angle & 0x3ff];

    rx = ((s32)c * dx - (s32)dz * s) >> 15;
    rz = ((s32)s * dx + (s32)dz * c) >> 15;

    if ((s16)rx < 0) {
        return ((s16)rz <= 0) ? 1 : 4;
    }
    if ((s16)rz < 0) {
        return 8;
    }
    return 2;
}

/* 0x403002 */
static s16 eval(void)
{
    s16 op = fetch();
    struct actor *a;
    s16 a_index;

    if (op == LIFE_VAR_CONSTANT) {
        return fetch();
    }
    if (op == LIFE_VAR_VARS) {
        return g_vars[fetch()];
    }

    if (op & 0x8000) {
        s16 obj_index = fetch();
        struct world_object *o = object_get(obj_index);

        a_index = o->actor;
        if (a_index == -1) {
            /* 0x4030d9: the object has no actor. */
            switch (life_var_map((s16)(op & 0x7fff))) {
            case LIFE_VAR_DIST:
                if (o->room != g_cur_room) {
                    return 32000;
                }
                return distance_3d(s_owner->world_x, s_owner->world_y,
                                   s_owner->world_z, o->x, o->y, o->z);
            case LIFE_VAR_ROOM:
                return o->room;
            case LIFE_VAR_LIFE:
                return o->life;
            case LIFE_VAR_STAGE:
                return o->stage;
            case LIFE_VAR_MATRIX: {
                s16 table = fetch();
                s16 other = fetch();
                return matrix_lookup(table, -1, object_get(other)->actor);
            }
            case LIFE_VAR_COL:
            case LIFE_VAR_HARD_DEC:
            case LIFE_VAR_HARD_COL:
            case LIFE_VAR_HIT:
            case LIFE_VAR_HIT_BY:
            case LIFE_VAR_COL_BY:
                /* Alone in the Dark 1: an absent object touches nothing.
                 * With WAITD3's 0 the hall's scripts took the missing
                 * player for one standing at zone 0 during the intro and
                 * handed out the lighter and the soup pot. */
                return game_current()->life_dialect == LIFE_DIALECT_AITD1 ? -1 : 0;
            default:
                return 0;
            }
        }
        a = &g_actors[a_index];
    } else {
        a_index = s_owner_index;
        a = s_owner;
    }

    switch (life_var_map((s16)(op & 0x7fff))) {
    case LIFE_VAR_COL: return actor_object(a->col[0]);
    case LIFE_VAR_HARD_DEC: return a->hard_dec;
    case LIFE_VAR_HARD_COL: return a->hard_col;
    case LIFE_VAR_HIT: return actor_object(a->hit);
    case LIFE_VAR_HIT_BY: return actor_object(a->hit_by);
    case LIFE_VAR_ANIM: return a->anim;
    case LIFE_VAR_END_ANIM: return a->end_anim;
    case LIFE_VAR_FRAME: return a->frame;
    case LIFE_VAR_END_FRAME: return a->end_frame;
    case LIFE_VAR_BODY: return a->body;
    case LIFE_VAR_MARK: return a->mark;
    case LIFE_VAR_NUM_TRACK: return a->track_number;
    case LIFE_VAR_CHRONO: return (s16)(timer_elapsed_u32(&a->chrono) / 60);
    case LIFE_VAR_ROOM_CHRONO: return (s16)(timer_elapsed_u32(&a->room_chrono) / 60);
    case LIFE_VAR_DIST: {
        struct world_object *o = object_get(fetch());
        s16 x, y, z;

        if (o->actor == -1) {
            if (o->room != g_cur_room) {
                return 32000;
            }
            x = o->x;
            y = o->y;
            z = o->z;
        } else {
            const struct actor *t = &g_actors[o->actor];
            x = t->world_x;
            y = t->world_y;
            z = t->world_z;
        }
        return distance_3d(a->world_x, a->world_y, a->world_z, x, y, z);
    }
    case LIFE_VAR_COL_BY: return actor_object(a->col_by);
    case LIFE_VAR_IN_INVENTORY: {
        s16 obj = eval();
        return (object_get(obj)->flags2 & 0x8000) ? 1 : 0;
    }
    case LIFE_VAR_ACTION: return g_action;
    case LIFE_VAR_POSREL: {
        s16 target = object_get(time_gate() ? eval() : fetch())->actor;
        if (target == -1) {
            return 0;
        }
        return position_relative(a, &g_actors[target]);
    }
    case LIFE_VAR_KEYBOARD_INPUT:
        if (g_scene_dir_flags & DIR_LEFT) return DIR_LEFT;
        if (g_scene_dir_flags & DIR_RIGHT) return DIR_RIGHT;
        if (g_scene_dir_flags & DIR_UP) return DIR_UP;
        if (g_scene_dir_flags & DIR_DOWN) return DIR_DOWN;
        return 0;
    case LIFE_VAR_BUTTON: return g_scene_confirm;
    case LIFE_VAR_COL_ANY: {
        s16 c = a->col[0];
        if (c == -1) {
            c = a->col_by;
            if (c == -1) {
                return -1;
            }
        }
        return g_actors[c].object;
    }
    case LIFE_VAR_ALPHA: return a->alpha;
    case LIFE_VAR_BETA: return a->beta;
    case LIFE_VAR_GAMMA: return a->gamma;
    case LIFE_VAR_IN_HAND: return g_in_hand[g_inventory_index];
    case LIFE_VAR_HIT_FORCE: return a->action_force;
    case LIFE_VAR_CAMERA: return blob_s16(g_room_data, 12 + g_cur_camera * 2);
    case LIFE_VAR_RAND: {
        s16 range = fetch();
        if (range == 0) {
            return 0;
        }
        return (s16)((s32)rand16() % range);
    }
    case LIFE_VAR_FALLING: return a->falling;
    case LIFE_VAR_ROOM: return a->room;
    case LIFE_VAR_LIFE: return a->life;
    case LIFE_VAR_OBJECT_TAKEN: return (object_get(fetch())->flags2 & 0xc000) ? 1 : 0;
    case LIFE_VAR_MUSIC:
        if (time_gate()) {
            fetch();    /* a music channel */
        }
        return g_music_pending;
    case LIFE_VAR_DIV_BY_2: return (s16)(eval() / 2);
    case LIFE_VAR_END_WAY: return 0;
    case LIFE_VAR_DEFINE: {
        s16 index = fetch();
        return (index >= 0 && index < DEFINES_WORDS_MAX) ? (s16)g_defines_itd[index] : 0;
    }
    case LIFE_VAR_STAGE: return a->stage;
    case LIFE_VAR_THROW: return (object_get(fetch())->flags2 & 0x1000) ? 1 : 0;
    case LIFE_VAR_MATRIX: {
        s16 table = fetch();
        s16 other = fetch();
        return matrix_lookup(table, a_index, object_get(other)->actor);
    }
    case LIFE_VAR_MATERIAL: return a->material;
    case LIFE_VAR_ROOM_Y: return a->room_y;
    case LIFE_VAR_TEST_ZV_END_ANIM: {
        s16 anim = fetch(), dy = fetch();
        return test_zv_end_anim(a, anim, dy);
    }
    default:
        return 0;
    }
}

/* ---- opcodes on an object without an actor (0x403a7e) ------------------- */

static void object_opcode(s16 op, struct world_object *o)
{
    switch (op) {
    case LIFE_OP_ANIM_ONCE: o->anim = fetch(); o->anim_info = fetch(); o->anim_type = ANIM_ONCE; o->frame = 0; break;
    case LIFE_OP_ANIM_ALL_ONCE: o->anim = fetch(); o->anim_info = fetch(); o->anim_type = ANIM_UNINTERRUPTIBLE; o->frame = 0; break;
    case LIFE_OP_BODY: o->body = eval(); break;
    case LIFE_OP_ANIM_REPEAT: o->anim = fetch(); o->anim_info = -1; o->anim_type = ANIM_REPEAT; o->frame = 0; break;
    case LIFE_OP_MOVE:
        o->track_mode = fetch();
        o->track_number = fetch();
        o->position_in_track = 0;
        o->mark = -1;
        break;
    case LIFE_OP_LIFE_MODE: {
        s16 mode = fetch();
        if (mode != (o->life_mode & 3)) {
            o->life_mode = mode;
            g_objects_dirty = 1;
        }
        break;
    }
    case LIFE_OP_LIFE: o->life = fetch(); break;
    case LIFE_OP_DELETE: {
        s16 obj = before_aitd2() ? fetch() : eval();
        struct world_object *t;

        object_delete(obj);
        t = object_get(obj);
        if (t->found_body != -1) {
            t->flags2 |= 0x4000;
        }
        break;
    }
    case LIFE_OP_TYPE: {
        s16 v = (s16)(fetch() & 0x5d1);
        o->flags = (u16)((o->flags & 0xfa2e) + v);
        break;
    }
    case LIFE_OP_SET_BETA: { s16 v = fetch(); fetch(); o->beta = v; break; }
    case LIFE_OP_STAGE:
        o->stage = fetch();
        o->room = fetch();
        o->x = fetch();
        o->y = fetch();
        o->z = fetch();
        g_objects_dirty = 1;
        break;
    case LIFE_OP_FOUND_NAME: o->found_name = fetch(); break;
    case LIFE_OP_FOUND_FLAG: o->flags2 = (u16)((o->flags2 & 0xe000) | (u16)fetch()); break;
    case LIFE_OP_FOUND_LIFE: o->found_life = fetch(); break;
    case LIFE_OP_TEST_COL:
        if (fetch()) o->flags |= 0x20; else o->flags &= ~0x20;
        break;
    case LIFE_OP_FOUND_BODY: o->found_body = fetch(); break;
    case LIFE_OP_SET_ALPHA: { s16 v = fetch(); fetch(); o->alpha = v; break; }
    case LIFE_OP_FOUND_WEIGHT: o->position_in_track = fetch(); break;
    case LIFE_OP_ANGLE: o->alpha = fetch(); o->beta = fetch(); o->gamma = fetch(); break;
    case LIFE_OP_COPY_ANGLE: {
        struct world_object *src = object_get(fetch());
        if (src->actor == -1) {
            o->alpha = src->alpha;
            o->beta = src->beta;
            o->gamma = src->gamma;
        } else {
            const struct actor *t = &g_actors[src->actor];
            o->alpha = t->alpha;
            o->beta = t->beta;
            o->gamma = t->gamma;
        }
        break;
    }
    case LIFE_OP_STAGE_LIFE: o->field_24 = fetch(); break;
    case LIFE_OP_CONTINUE_TRACK: {
        const u8 *track = hqr_get(g_cache_trak, o->track_number, NULL);
        if (blob_s16(track, o->position_in_track * 2) == TRACK_OP_STOP) {
            o->position_in_track++;
        }
        break;
    }
    case LIFE_OP_ANIM_RESET: o->anim = fetch(); o->anim_info = fetch(); o->anim_type = ANIM_RESET; o->frame = 0; break;
    case LIFE_OP_BODY_RESET: o->body = eval(); o->anim = eval(); break;
    default:
        break;
    }
}

/* ---- helpers for the actor opcodes ------------------------------------- */

static void zv_add_room(struct actor *a)
{
    a->zv[0] = (s16)(a->zv[0] + a->room_x);
    a->zv[1] = (s16)(a->zv[1] + a->room_x);
    a->zv[2] = (s16)(a->zv[2] + a->room_y);
    a->zv[3] = (s16)(a->zv[3] + a->room_y);
    a->zv[4] = (s16)(a->zv[4] + a->room_z);
    a->zv[5] = (s16)(a->zv[5] + a->room_z);
}

/* 0x40b8e3 - box from the animated body's current pose. */
static void zv_from_pose(struct actor *a)
{
    u8 *body = hqr_get(g_cache_body, a->body, NULL);

    r3d_measure_body(0, 0, 0, a->alpha, a->beta, a->gamma, body);
    r3d_points_bounds(a->zv);
    a->zv[0] = (s16)(a->zv[0] + a->room_x);
    a->zv[1] = (s16)(a->zv[1] + a->room_x);
    a->zv[2] = (s16)(a->zv[2] + a->room_y);
    a->zv[3] = (s16)(a->zv[3] + a->room_y);
    a->zv[4] = (s16)(a->zv[4] + a->room_z);
    a->zv[5] = (s16)(a->zv[5] + a->room_z);
    if (g_r3d_bbox.xmin < 0) g_r3d_bbox.xmin = 0;
    if (g_r3d_bbox.xmax > g_screen_width - 1) g_r3d_bbox.xmax = (s16)(g_screen_width - 1);
    if (g_r3d_bbox.ymin < 0) g_r3d_bbox.ymin = 0;
    if (g_r3d_bbox.ymax > g_screen_height - 1) g_r3d_bbox.ymax = (s16)(g_screen_height - 1);
    a->screen[0] = g_r3d_bbox.xmin;
    a->screen[1] = g_r3d_bbox.ymin;
    a->screen[2] = g_r3d_bbox.xmax;
    a->screen[3] = g_r3d_bbox.ymax;
    object_save_zv((s16 *)a);
}

/* 0x406150.. shared by the sprite animation opcodes 85 and 86. */
static void sprite_anim(struct actor *a, s16 anim, s16 body, s16 type)
{
    if (a->anim == anim && a->body == body) {
        return;
    }
    a->anim = anim;
    a->body = body;
    ticks_update();
    a->sprite_time = g_ticks;
    a->frame = 0;

    if (a->anim != -1 && a->body != -1) {
        const u8 *hyb = hqr_get(g_cache_hyb, a->anim, NULL);
        const u8 *rec;

        hyb += blob_u32(hyb, 8);
        rec = hyb + blob_u32(hyb, a->body * 4);
        a->frame_count = rec[1];
        a->anim_info = -1;
        a->anim_type = type;
        a->flags |= 0x200;
    } else {
        a->frame_count = 0;
        a->flags = 0;
    }
}

/* Inventory list of the current inventory (0x40d78e). */
static int inventory_find(s16 object)
{
    int i;

    for (i = 0; i < g_inventory_count[g_inventory_index]; i++) {
        if (g_inventory[g_inventory_index][i] == object) {
            return i;
        }
    }
    return -1;
}

/* 0x40d806 */
void inventory_remove(s16 object)
{
    int i = inventory_find(object);

    if (i != -1) {
        for (; i < 49; i++) {
            g_inventory[g_inventory_index][i] = g_inventory[g_inventory_index][i + 1];
        }
        g_inventory_count[g_inventory_index]--;
        object_get(object)->flags2 &= ~0x8000;
    }
}

/* 0x40d8a2 - into the current inventory, at the front. */
void object_take(s16 object)
{
    struct world_object *o = object_get(object);
    s16 *list = g_inventory[g_inventory_index];
    int i;

    /* Time Gate (TG320.EX_ 0x414dc2) keeps the first entry, the hero's
     * own actions, in place and takes new objects in behind it. */
    int first = (game_current()->inventory_style == INVENTORY_ICONS
                 && g_inventory_count[g_inventory_index] > 0) ? 1 : 0;

    for (i = g_inventory_count[g_inventory_index]; i > first; i--) {
        if (i < 50) {
            list[i] = list[i - 1];
        }
    }
    list[first] = object;
    g_inventory_count[g_inventory_index]++;
    g_action = 0x800;
    life_run_global(object);

    if (o->actor != -1) {
        world_remove_actor(o->actor);
    }
    o->flags2 &= ~0x4000;
    o->flags2 |= 0x8000;
    o->room = -1;
    o->stage = -1;
}

/* Opcode 49 - hand control to another object, following it to its floor
 * or room. */
void change_player(s16 object)
{
    struct world_object *o;
    s16 act;

    if (object == g_player_object) {
        return;
    }
    o = object_get(object);
    act = o->actor;
    g_player_object = object;

    if (act != -1) {
        struct actor *a = &g_actors[act];

        if (a->stage != g_cur_floor) {
            g_change_floor = 1;
            g_new_floor = a->stage;
            g_new_room = a->room;
        } else {
            g_player_actor = act;
            if (a->room != g_cur_room) {
                g_change_room = 1;
                g_new_room = a->room;
            }
        }
    } else if (o->stage != g_cur_floor) {
        g_change_floor = 1;
        g_new_floor = o->stage;
        g_new_room = o->room;
    } else if (o->room != g_cur_room) {
        g_change_room = 1;
        g_new_room = o->room;
    }
}

/* 0x412e49 */
void actor_hit(s16 anim, s16 frame, s16 group, s16 size, s16 force, s16 next)
{
    struct actor *a = g_cur_actor;

    if (anim_init(anim, ANIM_ONCE, next)) {
        a->action_anim = anim;
        a->action_frame = frame;
        a->anim_action_type = ACTION_HIT_START;
        a->action_param = size;
        a->hot_point_id = group;
        a->action_force = (force < 3) ? 3 : force;
    }
}

/* 0x412ef1 (variant 0) and 0x412fbc (variant 1). */
void actor_fire(s16 anim, s16 frame, s16 hot, s16 w4, s16 w5, s16 w6, s16 w7,
                s16 next, int variant)
{
    struct actor *a = g_cur_actor;

    if (anim_init(anim, ANIM_ONCE, next)) {
        a->action_anim = anim;
        a->action_frame = frame;
        a->anim_action_type = ACTION_FIRE_START;
        a->action_param = w5;
        a->hot_point_id = hot;
        if (variant) {
            a->action_force = (w6 < 3) ? 3 : w6;
            a->action_param3 = w7;
        } else {
            a->action_force = ((u16)w6 < 3) ? 3 : w6;
            a->action_param3 = 0;
        }
        a->action_param2 = w4;
    }
}

/* 0x40d9bf - the most objects any camera of `room` can see on `floor`. */
static s16 objects_in_view(s16 floor, s16 room)
{
    const u8 *r = g_floor_rooms[room];
    s16 best = 0;
    int i, j, k;

    for (i = 0; i < blob_s16(r, 10); i++) {
        const u8 *cam = g_floor_cameras[blob_s16(r, 12 + i * 2)];
        s16 count = 0;

        for (j = 0; j < CAMERA_VIEW_COUNT(cam); j++) {
            s16 viewed = blob_s16(CAMERA_VIEW(cam, j), 0);
            for (k = 0; k < g_object_count; k++) {
                if (g_objects[k].stage == floor && g_objects[k].room == viewed) {
                    count++;
                }
            }
        }
        if (count > best) {
            best = count;
        }
    }
    return best;
}

/* Mark an object as lying in the world again: out of the inventory, its
 * saved box forgotten, the scene rebuilt. */
static void object_placed(s16 index)
{
    g_objects_dirty = 1;
    g_static_dirty = 1;
    g_game_vars[index * 7] = 0;
}

/* 0x40dfcf - somewhere in front of an actor for a body to land: the box of
 * `body` is pushed out along `beta` until it clears the actor box `zv`,
 * then must not hit the scenery or another solid actor. The spot is left
 * in g_r3d_x/y/z. */
static int find_drop_spot(const s16 *zv, s16 x, s16 z, s16 y, s16 beta, s16 room,
                          s16 body)
{
    s16 base[6];
    s16 box[6];
    s16 px = 0, pz = 0;
    int found = 0;
    s16 step;
    int i;

    zv_from_body(hqr_get(g_cache_body, body, NULL), base);
    for (step = 0; step <= 1500; step = (s16)(step + 10)) {
        r3d_rotate_2d(step, 0, (s16)(beta - 256));
        memcpy(box, base, sizeof(box));
        px = (s16)(x + g_r3d_x);
        pz = (s16)(z + g_r3d_y);
        box[0] = (s16)(box[0] + px);
        box[1] = (s16)(box[1] + px);
        box[2] = (s16)(box[2] + y);
        box[3] = (s16)(box[3] + y);
        box[4] = (s16)(box[4] + pz);
        box[5] = (s16)(box[5] + pz);
        if (!zv_overlap(box, zv)) {
            found = 1;
            break;
        }
    }
    if (!found || hardcol_test(box, g_floor_rooms[room])) {
        return 0;
    }
    for (i = 0; i < ACTOR_MAX; i++) {
        const struct actor *other = &g_actors[i];

        if (other->object == -1) {
            continue;
        }
        if (other->room != room) {
            s16 moved[6];

            memcpy(moved, box, sizeof(moved));
            zv_to_room(moved, room, other->room);
            if (zv_overlap(moved, other->zv) && !(other->flags & 0x80)) {
                return 0;
            }
        } else if (zv_overlap(box, other->zv) && !(other->flags & 0x80)) {
            return 0;
        }
    }
    g_r3d_x = px;
    g_r3d_y = y;
    g_r3d_z = pz;
    return 1;
}

/* 0x40e1f0 - opcode 50: drop `object` in front of the actor of `holder`;
 * it falls from there (flags 0x184, fall clock in +0x30). */
void start_fall(s16 object, s16 holder)
{
    struct world_object *o = object_get(object);
    struct actor *h;

    if (object_get(holder)->actor == -1) {
        return;
    }
    h = &g_actors[object_get(holder)->actor];
    if (objects_in_view(h->stage, h->room) > 46) {
        return;
    }
    if (!find_drop_spot(h->zv, h->room_x, h->room_z, h->room_y, h->beta, h->room, o->body)) {
        return;
    }
    o->x = g_r3d_x;
    o->y = g_r3d_y;
    o->z = g_r3d_z;
    inventory_remove(object);
    o->flags2 |= 0x4000;
    o->flags = 0x184;
    o->alpha = o->beta = o->gamma = 0;
    o->stage = h->stage;
    o->room = h->room;
    ticks_update();
    o->track_number = (s16)g_ticks;   /* reused as the fall clock */
    object_placed(object);
}

/* 0x40e366 - opcode 56: object, x, y, z, room, floor, alpha, beta, gamma. */
void object_put(const s16 *w)
{
    struct world_object *o = object_get(w[0]);

    o->x = w[1];
    o->y = w[2];
    o->z = w[3];
    o->room = w[4];
    o->stage = w[5];
    o->alpha = w[6];
    o->beta = w[7];
    o->gamma = w[8];
    inventory_remove(w[0]);
    o->flags2 |= 0x4000;
    object_placed(w[0]);
}

/* 0x40e41b - opcode 64: put `object` where `target` is. When `object` has
 * an actor the original moves the script's current actor instead. */
void object_drop(s16 object, s16 target)
{
    struct world_object *o = object_get(object);
    const struct world_object *t = object_get(target);
    s16 x, y, z, room, stage, alpha, beta, gamma;

    if (t->actor == -1) {
        x = t->x; y = t->y; z = t->z;
        room = t->room; stage = t->stage;
        alpha = t->alpha; beta = t->beta; gamma = t->gamma;
    } else {
        const struct actor *ta = &g_actors[t->actor];

        x = ta->room_x; y = ta->room_y; z = ta->room_z;
        room = ta->room; stage = ta->stage;
        alpha = ta->alpha; beta = ta->beta; gamma = ta->gamma;
    }
    inventory_remove(object);
    if (o->actor == -1) {
        o->x = x; o->y = y; o->z = z;
        o->room = room; o->stage = stage;
        o->alpha = alpha; o->beta = beta; o->gamma = gamma;
        o->flags2 |= 0x4000;
        o->flags |= 0x80;
    } else {
        struct actor *a = g_cur_actor;

        a->room_x = x; a->room_y = y; a->room_z = z;
        a->room = room; a->stage = stage;
        a->alpha = alpha; a->beta = beta; a->gamma = gamma;
        object_get(a->object)->flags2 |= 0x4000;
        a->flags |= 0x80;
    }
    object_placed(object);
    g_init_view = 1;
}

/*
 * 0x40d6eb, reached through the pointer at 0x444eda. Records a colour
 * cycle over [first, last] in the 0x316 byte state at 0x444bcc:
 *   +0 timer, +4 period (10 x speed), +8 direction (first >= last),
 *   +10 first, +12 last, +14 the palette, +782/+786 the handler pointers.
 * The stepping handler (0x40d598, rotate the range by one colour) is never
 * called anywhere in this build, so the cycle stays recorded but inert.
 */
void palette_cycle_init(s16 first, s16 last, s16 speed)
{
    u8 *st = g_palette_effect_state;
    s32 period = 10 * speed;
    s16 reverse = (s16)(first >= last);
    u32 now;

    memcpy(st + 10, &first, 2);
    memcpy(st + 12, &last, 2);
    memcpy(st + 4, &period, 4);
    memcpy(st + 14, g_palette_buffer, 0x300);
    memcpy(st + 8, &reverse, 2);
    timer_reset_u32(&now);
    memcpy(st, &now, 4);
}

/* 0x413088 */
void actor_throw(s16 anim, s16 frame, s16 hot, s16 object, s16 w5, s16 w6,
                 s16 next)
{
    struct actor *a = g_cur_actor;

    if (!anim_init(anim, ANIM_UNINTERRUPTIBLE, next)) {
        return;
    }
    if (objects_in_view(a->stage, a->room) > 46) {
        return;
    }
    a->action_anim = anim;
    a->action_frame = frame;
    a->anim_action_type = ACTION_THROW_START;
    a->hot_point_id = hot;
    a->action_param = object;
    a->action_force = ((u16)w6 < 3) ? 3 : w6;
    if (!w5) {
        object_get(object)->gamma = (s16)(object_get(object)->gamma - 256);
    }
    object_get(object)->flags2 |= 0x1000;
}

/* 0x40983a - take an object out of the world. */
void object_delete(s16 object)
{
    struct world_object *o = object_get(object);

    if (o->actor != -1) {
        struct actor *a = &g_actors[o->actor];

        a->room = -1;
        a->stage = -1;
        a->life = -1;
        g_objects_dirty = 1;
        if (a->flags & 8) {
            actor_unstatic(o->actor);
        }
    }
    o->stage = -1;
    o->room = -1;
    inventory_remove(object);
}

/* Turn a rotation angle with a speed through the actor's interpolator
 * (opcodes 43 and 54). */
static void turn_angle(struct actor *a, s16 *angle, s16 target, s16 duration)
{
    if (target == *angle) {
        return;
    }
    if (!a->rot[2] || target != a->rot[1]) {
        interp_init(*angle, target, duration, a->rot);
    }
    *angle = interp_get_angle(a->rot);
}

/* ---- the interpreter (0x4038f6) ---------------------------------------- */

static int s_trace = -1;

void life_run(s16 life, int flag)
{
    int done = 0;

    if (s_trace == -1) {
        s_trace = getenv("AITD_LIFETRACE") != NULL;
    }
    s16 switch_value = 0;

    s_owner_index = g_cur_actor_index;
    s_owner = g_cur_actor;
    s_life_num = life;
    s_pc = hqr_get(g_cache_life, life, NULL);
    if (s_pc == NULL) {
        s_life_num = -1;
        return;
    }

    while (!done) {
        s16 opcode = fetch();
        s16 op = life_opcode_map((s16)(opcode & 0x7fff));
        int targeted = 0;
        struct actor *a;

        if (opcode & 0x8000) {
            s16 obj_index = fetch();
            struct world_object *o = object_get(obj_index);

            targeted = 1;
            g_cur_actor_index = o->actor;
            g_cur_actor = &g_actors[o->actor < 0 ? 0 : o->actor];
            if (o->actor == -1) {
                object_opcode(op, o);
                goto next;
            }
        }

        a = g_cur_actor;

        if (s_trace) {
            fprintf(stderr, "  life %d actor %d op %d%s\n", life, g_cur_actor_index, op,
                    targeted ? " (targeted)" : "");
        }

        switch (op) {
        case LIFE_OP_DO_MOVE: actor_do_move(); break;
        case LIFE_OP_ANIM_ONCE: {
            s16 anim = fetch(), info = fetch();
            if (anim == -1) {
                a->anim = -1;
                a->new_anim = -2;
            } else {
                anim_init(anim, ANIM_ONCE, info);
            }
            break;
        }
        case LIFE_OP_ANIM_ALL_ONCE: { s16 anim = fetch(), info = fetch(); anim_init(anim, ANIM_UNINTERRUPTIBLE, info); break; }
        case LIFE_OP_BODY: {
            s16 body = eval();
            object_get(a->object)->body = body;
            if (a->body != body) {
                a->body = body;
                if (a->flags & 1) {
                    if (a->anim != -1 && a->body != -1) {
                        const u8 *anim = hqr_get(g_cache_anim, a->anim, NULL);
                        u8 *b = hqr_get(g_cache_body, a->body, NULL);
                        anim_set_inter(a->frame, anim, b, 1);
                        a->anim_flag = 0;
                    }
                } else {
                    g_init_view = 1;
                }
            }
            break;
        }
        case LIFE_OP_IF_EQUAL: case LIFE_OP_IF_DIFFERENT: case LIFE_OP_IF_SUP_EQUAL: case LIFE_OP_IF_SUP: case LIFE_OP_IF_INF_EQUAL: case LIFE_OP_IF_INF: {   /* IF_xx */
            s16 l = eval(), r = eval();
            int jump;
            switch (op) {
            case LIFE_OP_IF_EQUAL: jump = l != r; break;
            case LIFE_OP_IF_DIFFERENT: jump = l == r; break;
            case LIFE_OP_IF_SUP_EQUAL: jump = l < r; break;
            case LIFE_OP_IF_SUP: jump = l <= r; break;
            case LIFE_OP_IF_INF_EQUAL: jump = l > r; break;
            default: jump = l >= r; break;
            }
            if (jump) {
                s16 off = fetch();
                s_pc += off * 2;
            } else {
                s_pc += 2;
            }
            break;
        }
        case LIFE_OP_GOTO: { s16 off = fetch(); s_pc += off * 2; break; }
        case LIFE_OP_RETURN: case LIFE_OP_END: done = 1; break;
        case LIFE_OP_ANIM_REPEAT: anim_init(fetch(), ANIM_REPEAT, -1); break;
        case LIFE_OP_ANIM_MOVE: {
            s16 w[7];
            int i;
            for (i = 0; i < 7; i++) w[i] = fetch();
            actor_anim_move(w[0], w[1], w[2], w[3], w[4], w[5], w[6]);
            break;
        }
        case LIFE_OP_MOVE: { s16 mode = fetch(), num = fetch(); actor_set_move_mode(mode, num); break; }
        case LIFE_OP_HIT: {
            s16 anim = fetch(), frame = fetch(), group = fetch(), size = fetch();
            s16 force = eval();
            s16 next = fetch();
            actor_hit(anim, frame, group, size, force, next);
            break;
        }
        case LIFE_OP_MESSAGE: message_show(fetch()); break;
        case LIFE_OP_VAR: { s16 v = fetch(); g_vars[v] = eval(); break; }
        case LIFE_OP_INC: g_vars[fetch()]++; break;
        case LIFE_OP_DEC: g_vars[fetch()]--; break;
        case LIFE_OP_ADD: { s16 v = fetch(); g_vars[v] = (s16)(g_vars[v] + eval()); break; }
        case LIFE_OP_SUB: { s16 v = fetch(); g_vars[v] = (s16)(g_vars[v] - eval()); break; }
        case LIFE_OP_LIFE_MODE: {
            s16 mode = fetch();
            if (mode != (before_aitd2() ? a->life_mode : (a->life_mode & 3))) {
                a->life_mode = mode;
                g_objects_dirty = 1;
            }
            break;
        }
        case LIFE_OP_SWITCH: switch_value = eval(); break;
        case LIFE_OP_CASE: {
            if (fetch() == switch_value) {
                s_pc += 2;
            } else {
                s16 off = fetch();
                s_pc += off * 2;
            }
            break;
        }
        case LIFE_OP_START_CHRONO: timer_reset_u32(&a->chrono); break;
        case LIFE_OP_MULTI_CASE: {
            s16 n = fetch();
            int match = 0;
            for (; n > 0; n--) {
                if (fetch() == switch_value) {
                    match = 1;
                }
            }
            if (match) {
                s_pc += 2;
            } else {
                s16 off = fetch();
                s_pc += off * 2;
            }
            break;
        }
        case LIFE_OP_FOUND: object_found(fetch(), (flag && game_id() != GAME_AITD1) ? 2 : 1); break;
        case LIFE_OP_LIFE: a->life = fetch(); break;
        case LIFE_OP_DELETE: {
            s16 obj = before_aitd2() ? fetch() : eval();
            struct world_object *t;
            object_delete(obj);
            t = object_get(obj);
            if (t->found_body != -1) {
                t->flags2 |= 0x4000;
            }
            break;
        }
        case LIFE_OP_TAKE: object_take(fetch()); break;
        case LIFE_OP_IN_HAND: g_in_hand[g_inventory_index] = before_aitd2() ? fetch() : eval(); break;
        case LIFE_OP_READ: {
            s16 a1 = fetch(), a2 = fetch();
            if (game_id() == GAME_AITD1) {
                g_reader_voc = fetch();    /* the CD release's voice */
            }
            palette_fade_out(0x20, 0);
            read_document((s16)(a2 + 1), a1);
            g_init_view = 2;
            break;
        }
        case LIFE_OP_ANIM_SAMPLE: {
            s16 sample = eval();
            s16 anim, frame;

            if (time_gate()) {
                fetch();    /* volume */
            }
            anim = fetch();
            frame = fetch();
            if (a->end_frame && a->anim == anim && a->frame == frame) {
                sample_play(sample);
            }
            break;
        }
        case LIFE_OP_SPECIAL: special_spawn(fetch()); break;
        case LIFE_OP_DO_REAL_ZV: zv_from_pose(a); break;
        case LIFE_OP_SAMPLE: {
            s16 s = (before_aitd2() || time_gate()) ? eval() : fetch();
            if (time_gate()) {
                fetch();    /* volume */
            }
            sample_play(s);
            break;
        }
        case LIFE_OP_TYPE: {
            s16 v = (s16)(fetch() & 0x5d1);
            u16 old = a->flags;
            a->flags = (u16)((a->flags & 0xfa2e) + v);
            if (!(old & 1)) {
                if ((v & 1) && (old & 8)) {
                    actor_unstatic(g_cur_actor_index);
                }
            } else if (!(v & 1)) {
                actor_make_static(g_cur_actor_index);
            }
            break;
        }
        case LIFE_OP_GAME_OVER: {
            u32 t;
            timer_reset_u32(&t);
            while (timer_elapsed_u32(&t) < 120) {
                input_poll();
            }
            g_quit_requested = 1;
            done = 1;
            break;
        }
        case LIFE_OP_MANUAL_ROT: actor_turn_keys(game_id() == GAME_AITD1 ? 240 : 0x5a); break;
        case LIFE_OP_RND_FREQ: fetch(); break;
        case LIFE_OP_MUSIC: {
            s16 m = fetch();
            if (time_gate()) {
                fetch();
            }
            music_play(m);
            break;
        }
        case LIFE_OP_SET_BETA: {
            s16 v = fetch(), speed = fetch();
            turn_angle(a, &a->beta, v, speed);
            break;
        }
        case LIFE_OP_DO_ROT_ZV: zv_rotated(hqr_get(g_cache_body, a->body, NULL), a->zv, a->alpha, a->beta, a->gamma, a); zv_add_room(a); break;
        case LIFE_OP_STAGE: {
            s16 f = fetch(), r = fetch(), x = fetch(), y = fetch(), z = fetch();
            actor_teleport(f, r, x, y, z);
            break;
        }
        case LIFE_OP_FOUND_NAME: object_get(a->object)->found_name = fetch(); break;
        case LIFE_OP_FOUND_FLAG: {
            struct world_object *o = object_get(a->object);
            o->flags2 = (u16)((o->flags2 & 0xe000) | (u16)fetch());
            break;
        }
        case LIFE_OP_FOUND_LIFE: object_get(a->object)->found_life = fetch(); break;
        case LIFE_OP_CAMERA_TARGET: change_player(fetch()); break;
        case LIFE_OP_DROP: { s16 v = eval(); s16 w = fetch(); start_fall(v, w); break; }
        case LIFE_OP_FIRE: {
            if (game_id() == GAME_AITD1) {
                /* No emitter model: anim, frame, hot point, box, force, next. */
                s16 anim = fetch(), frame = fetch(), hot = fetch(), size = fetch();
                s16 force = fetch(), next = fetch();
                actor_fire(anim, frame, hot, -1, size, force, 0, next, 0);
            } else {
                s16 anim = eval(), frame = fetch(), hot = fetch(), w4 = fetch();
                s16 w5 = fetch(), w6 = fetch();
                s16 next = eval();
                actor_fire(anim, frame, hot, w4, w5, w6, 0, next, 0);
            }
            break;
        }
        case LIFE_OP_TEST_COL:
            if (fetch()) a->dyn_flags |= 1; else a->dyn_flags &= ~1;
            break;
        case LIFE_OP_FOUND_BODY: object_get(a->object)->found_body = fetch(); break;
        case LIFE_OP_SET_ALPHA: {
            s16 v = fetch(), speed = fetch();
            turn_angle(a, &a->alpha, v, speed);
            break;
        }
        case LIFE_OP_DO_MAX_ZV: {
            const u8 *body = hqr_get(g_cache_body, a->body, NULL);
            s16 size;
            zv_from_body(body, a->zv);
            size = (s16)(a->zv[1] - a->zv[0]);
            if (size < (s16)(a->zv[5] - a->zv[4])) size = (s16)(a->zv[5] - a->zv[4]);
            a->zv[0] = (s16)(size / -2);
            a->zv[1] = (s16)(size / 2);
            a->zv[4] = (s16)(size / -2);
            a->zv[5] = (s16)(size / 2);
            object_save_zv((s16 *)a);
            zv_add_room(a);
            break;
        }
        case LIFE_OP_PUT: {
            s16 w[9];
            int i;
            for (i = 0; i < 9; i++) w[i] = fetch();
            object_put(w);                                       /* 0x40e366 */
            break;
        }
        case LIFE_OP_DO_NORMAL_ZV: {
            zv_from_body(hqr_get(g_cache_body, a->body, NULL), a->zv);
            zv_add_room(a);
            break;
        }
        case LIFE_OP_DO_CARRE_ZV: {
            s16 half;
            zv_from_body(hqr_get(g_cache_body, a->body, NULL), a->zv);
            half = (s16)((u16)(a->zv[1] + a->zv[5]) >> 1);
            a->zv[1] = half;
            a->zv[5] = half;
            a->zv[0] = (s16)-half;
            a->zv[4] = (s16)-half;
            zv_add_room(a);
            break;
        }
        case LIFE_OP_SAMPLE_THEN: {
            s16 s;

            if (time_gate()) {
                s = eval();
                fetch();
                g_sample_pending = eval();
                fetch();
            } else {
                s = before_aitd2() ? eval() : fetch();
                g_sample_pending = before_aitd2() ? eval() : fetch();
            }
            sample_play(s);
            break;
        }
        case LIFE_OP_LIGHT: {
            s16 v = (s16)(2 - fetch() * 2);
            if (v != g_black_screen) {
                g_black_screen = v;
                g_fade_request = 1;
            }
            break;
        }
        case LIFE_OP_SHAKING: {
            s16 v = fetch();
            g_shaking = v;
            break;
        }
        case LIFE_OP_INVENTORY: g_inventory_enabled = fetch(); break;
        case LIFE_OP_FOUND_WEIGHT: object_get(a->object)->position_in_track = fetch(); break;
        case LIFE_OP_PUT_AT: {
            s16 v = fetch();
            s16 w = time_gate() ? eval() : fetch();
            object_drop(v, w);
            break;
        }
        case LIFE_OP_DEF_ZV: {
            a->zv[0] = (s16)(fetch() + a->room_x + a->step_x);
            a->zv[1] = (s16)(fetch() + a->room_x + a->step_x);
            a->zv[2] = (s16)(fetch() + a->room_y + a->step_y);
            a->zv[3] = (s16)(fetch() + a->room_y + a->step_y);
            a->zv[4] = (s16)(fetch() + a->room_z + a->step_z);
            a->zv[5] = (s16)(fetch() + a->room_z + a->step_z);
            break;
        }
        case LIFE_OP_HIT_OBJECT:
            a->anim_action_type = ACTION_HIT_OBJECT;
            a->action_param = fetch();
            a->action_force = fetch();
            a->hot_point_id = -1;
            break;
        case LIFE_OP_ANGLE: a->alpha = fetch(); a->beta = fetch(); a->gamma = fetch(); break;
        case LIFE_OP_REP_SAMPLE: {
            s16 s = (game_id() == GAME_AITD1 || time_gate()) ? eval() : fetch();
            s16 n = fetch();
            sample_repeat(s, (u16)n);
            break;
        }
        case LIFE_OP_THROW: {
            s16 w[7];
            int i;
            for (i = 0; i < 7; i++) w[i] = fetch();
            actor_throw(w[0], w[1], w[2], w[3], w[4], w[5], w[6]);
            break;
        }
        case LIFE_OP_WATER: break;
        case LIFE_OP_PICTURE: {
            s16 entry = fetch(), time = fetch();
            s16 sample = time_gate() ? eval() : fetch();
            if (time_gate()) {
                fetch();
            }
            show_picture(entry, time, sample);
            break;
        }
        case LIFE_OP_STOP_SAMPLE:
            if (time_gate()) {
                fetch();
            }
            sound_stop_all();
            break;
        case LIFE_OP_NEXT_MUSIC: {
            s16 m = fetch();
            if (time_gate()) {
                fetch();
                fetch();
            }
            if (g_music_pending != -1) {
                g_music_next = m;
            } else {
                music_play(m);
            }
            break;
        }
        case LIFE_OP_FADE_MUSIC: {
            s16 m = fetch();
            if (time_gate()) {
                fetch();
                fetch();
            }
            music_play(m);
            break;
        }
        case LIFE_OP_STOP_HIT_OBJECT:
            if (a->anim_action_type == ACTION_HIT_OBJECT) {
                a->anim_action_type = ACTION_NONE;
                a->action_param = 0;
                a->action_force = 0;
                a->hot_point_id = -1;
            }
            break;
        case LIFE_OP_COPY_ANGLE: {
            struct world_object *src = object_get(fetch());
            if (src->actor == -1) {
                a->alpha = src->alpha;
                a->beta = src->beta;
                a->gamma = src->gamma;
            } else {
                const struct actor *t = &g_actors[src->actor];
                a->alpha = t->alpha;
                a->beta = t->beta;
                a->gamma = t->gamma;
            }
            break;
        }
        case LIFE_OP_SAMPLE_THEN_REPEAT: {
            int evals = game_id() == GAME_AITD1 || time_gate();
            s16 s = evals ? eval() : fetch();
            s16 next;

            if (time_gate()) {
                fetch();
            }
            next = evals ? eval() : fetch();
            if (time_gate()) {
                fetch();
            }
            g_sample_pending = (s16)(next | 0x4000);
            sample_play(s);
            break;
        }
        case LIFE_OP_WAIT_GAME_OVER:
            while ((g_last_scancode & 0xff) || g_key_dir_flags || g_key_confirm) {
                input_poll();
            }
            while (!(g_last_scancode & 0xff) && !g_key_dir_flags && !g_key_confirm) {
                input_poll();
            }
            g_quit_requested = 1;
            done = 1;
            break;
        case LIFE_OP_STAGE_LIFE: object_get(a->object)->field_24 = fetch(); break;
        case LIFE_OP_CONTINUE_TRACK: {
            const u8 *track = hqr_get(g_cache_trak, a->track_number, NULL);
            if (blob_s16(track, a->position_in_track * 2) == TRACK_OP_STOP) {
                a->position_in_track++;
            }
            break;
        }
        case LIFE_OP_ANIM_RESET: {
            s16 anim = fetch(), info = fetch();
            if (anim == -1) {
                a->anim = -1;
                a->new_anim = -2;
            } else {
                anim_init(anim, ANIM_RESET, info);
            }
            break;
        }
        case LIFE_OP_RESET_MOVE_MANUAL:
        case LIFE_OP_STOP_BETA: a->rot[2] = 0; break;
        /* Alone in the Dark 1 only. */
        case LIFE_OP_MESSAGE_VALUE: { s16 text = fetch(); fetch(); message_show(text); break; }
        case LIFE_OP_C_VAR: {
            s16 index = fetch(), value = eval();
            if (index >= 0 && index < DEFINES_WORDS_MAX) {
                g_defines_itd[index] = (u16)value;
            }
            break;
        }
        case LIFE_OP_UP_COOR_Y: interp_init(0, -2000, -1, a->fall); break;
        case LIFE_OP_GET_HARD_CLIP: get_hard_clip(a); break;
        case LIFE_OP_CAMERA:
        case LIFE_OP_SPEED: fetch(); break;
        case LIFE_OP_END_SEQUENCE: break;
        case LIFE_OP_ANIM_HYBRIDE_ONCE: { s16 anim = fetch(), body = fetch(); sprite_anim(a, anim, body, 0); break; }
        case LIFE_OP_ANIM_HYBRIDE_REPEAT: { s16 anim = fetch(), body = fetch(); sprite_anim(a, anim, body, 1); break; }
        case LIFE_OP_MODIF_C_VAR: {
            s16 index = fetch();
            s16 value = eval();
            if (index < 0 || index >= DEFINES_WORDS_MAX) {
                break;
            }
            g_defines_itd[index] = (u16)value;
            if (index == 2 && game_id() == GAME_AITD3) {
                s16 *v = &g_vars[(s16)g_defines_itd[2]];
                if (g_difficulty[0] == 2) {
                    *v = (s16)(*v << 1);
                } else if (g_difficulty[0] == 0) {
                    *v = (s16)(*v / 2);
                }
            }
            break;
        }
        case LIFE_OP_CALL_INVENTORY: inventory_open(); break;
        case LIFE_OP_BODY_RESET: {
            s16 body = eval(), anim = eval();
            struct world_object *o = object_get(a->object);
            o->body = body;
            o->anim = anim;
            a->body = body;
            if (a->flags & 1) {
                a->anim = anim;
                anim_set_frame(0, hqr_get(g_cache_anim, a->anim, NULL),
                               hqr_get(g_cache_body, a->body, NULL));
                a->new_anim = anim;
                a->new_anim_type = 4;
                a->new_anim_info = -1;
                a->frame = 0;
            } else {
                g_init_view = 1;
            }
            break;
        }
        case LIFE_OP_DEL_INVENTORY: {
            int i;
            for (i = 0; i < g_inventory_count[g_inventory_index]; i++) {
                object_get(g_inventory[g_inventory_index][i])->flags2 &= ~0x8000;
            }
            g_in_hand[g_inventory_index] = -1;
            g_inventory_count[g_inventory_index] = 0;
            g_action = 0;
            break;
        }
        case LIFE_OP_SET_INVENTORY: {
            s16 v = fetch();
            if (v != g_inventory_index && v < 2) {
                int i;
                for (i = 0; i < g_inventory_count[g_inventory_index]; i++) {
                    object_get(g_inventory[g_inventory_index][i])->flags2 &= ~0x8000;
                }
                g_inventory_index = v;
                for (i = 0; i < g_inventory_count[g_inventory_index]; i++) {
                    object_get(g_inventory[g_inventory_index][i])->flags2 |= 0x8000;
                }
            }
            break;
        }
        case LIFE_OP_PLAY_SEQUENCE: {
            /* Jack in the Dark gives only the sequence. */
            s16 number = fetch();
            s16 flags = game_id() == GAME_JACK ? 0 : fetch();
            s16 repeat = game_id() == GAME_JACK ? 1 : fetch();
            ticks_freeze();
            play_cutscene(number, flags, repeat);
            ticks_unfreeze();
            break;
        }
        case LIFE_OP_2D_ANIM_SAMPLE: {
            s16 sample = eval(), idx = fetch(), frame = fetch();
            if (bganim_frame_is(idx, frame)) {
                sample_play(sample);
            }
            break;
        }
        case LIFE_OP_SET_GROUND: g_clip_y_default = fetch(); break;
        case LIFE_OP_DEF_ABS_ZV: {
            int i;
            for (i = 0; i < 6; i++) a->zv[i] = fetch();
            break;
        }
        case LIFE_OP_DEF_SEQUENCE_SAMPLE: {
            s16 n = fetch();
            s16 used = (s16)(n > 30 ? 30 : n);
            memcpy(g_cutscene_samples, s_pc, (size_t)used * 4);
            s_pc += n * 4;
            g_cutscene_sample_count = used;
            break;
        }
        case LIFE_OP_READ_ON_PICTURE: {
            s16 w[8];
            int i;
            for (i = 0; i < 8; i++) w[i] = fetch();
            read_book(w);
            g_init_view = 2;
            break;
        }
        case LIFE_OP_FIRE_UP_DOWN: {
            s16 anim = eval(), frame = fetch(), hot = fetch(), w4 = fetch();
            s16 w5 = fetch(), w6 = fetch(), w7 = fetch();
            s16 next = eval();
            if (time_gate()) {
                fetch();
                fetch();
            }
            actor_fire(anim, frame, hot, w4, w5, w6, w7, next, 1);
            break;
        }
        case LIFE_OP_DO_ROT_CLUT: {
            s16 first = fetch(), last = fetch(), speed = fetch();
            g_palette_effect = 1;
            palette_cycle_init(first, last, speed);
            break;
        }
        case LIFE_OP_STOP_CLUT: g_palette_effect = 0; break;
        /* Time Gate. The music and volume opcodes only consume their
         * operands until the music system is rebuilt for this title. */
        case LIFE_OP_IF_IN:
        case LIFE_OP_IF_OUT: {
            s16 v = eval(), lo = eval(), hi = eval();
            int inside = v >= lo && v <= hi;

            if (inside == (op == LIFE_OP_IF_IN)) {
                s_pc += 2;
            } else {
                s16 off = fetch();
                s_pc += off * 2;
            }
            break;
        }
        case LIFE_OP_MUSIC_AND_LOOP: { s16 m = fetch(); fetch(); music_play(m); break; }
        case LIFE_OP_MUSIC_THEN:
        case LIFE_OP_MUSIC_THEN_LOOP: { s16 m = fetch(); fetch(); g_music_next = fetch(); music_play(m); break; }
        case LIFE_OP_FADE_OUT_MUSIC_STOP: fetch(); fetch(); music_play(-1); break;
        case LIFE_OP_SET_VOLUME_SAMPLE:
        case LIFE_OP_SET_MUSIC_VOLUME:
        case LIFE_OP_MUSIC_ALTER_TEMPO: fetch(); fetch(); break;
        case LIFE_OP_FADE_IN_MUSIC: { s16 m = fetch(); fetch(); fetch(); music_play(m); break; }
        case LIFE_OP_START_FADE_IN_MUSIC:
        case LIFE_OP_START_FADE_IN_MUSIC_LOOP: { s16 m = fetch(); fetch(); fetch(); fetch(); fetch(); music_play(m); break; }
        case LIFE_OP_START_FADE_IN_MUSIC_THEN: { s16 m = fetch(); fetch(); fetch(); fetch(); fetch(); g_music_next = fetch(); music_play(m); break; }
        case LIFE_OP_TG_VALUE_LIST: {
            s16 n = fetch();
            while (n-- > 0) {
                eval();
            }
            break;
        }
        case LIFE_OP_TG_122: break;
        default:
            break;                                               /* 79, 84, 95 */
        }

    next:
        /* 0x4066c6: Escape ends the intro. */
        if (g_transition_flag && (g_last_scancode & 0xff) == 1) {
            done = 1;
            g_quit_requested = 1;
        }
        if (targeted) {
            g_cur_actor_index = s_owner_index;
            g_cur_actor = s_owner;
        }
    }

    s_life_num = -1;
}

/*
 * 0x406735 - run the life script of an object, giving it a temporary actor
 * slot when it has none (the weapon in hand, for instance).
 */
void life_run_global(s16 object)
{
    s16 life = object_get(object)->found_life;
    struct actor *saved = g_cur_actor;
    s16 saved_index = g_cur_actor_index;
    struct actor *saved_owner = s_owner;
    s16 saved_owner_index = s_owner_index;
    s16 saved_life = s_life_num;
    const u8 *saved_pc = s_pc;
    int borrowed = 0;

    if (life == -1) {
        return;
    }

    if (object_get(object)->actor == -1) {
        s16 i;
        struct actor *a;

        for (i = ACTOR_MAX - 1; i >= 0 && g_actors[i].object != -1; i--) {
        }
        if (i == -1) {
            i = ACTOR_MAX - 1;
        }
        a = &g_actors[i];
        borrowed = 1;
        g_cur_actor = a;
        g_cur_actor_index = i;
        a->object = object;
        a->life = -1;
        a->body = -1;
        a->flags = 0;
        a->stage = -1;
        a->room = -1;
        a->life_mode = 0;
        a->anim = -1;
    }

    life_run(life, 1);

    if (borrowed) {
        g_cur_actor->object = -1;
    }

    g_cur_actor = saved;
    g_cur_actor_index = saved_index;
    s_owner = saved_owner;
    s_owner_index = saved_owner_index;
    if (saved_life != -1) {
        s_life_num = saved_life;
        s_pc = saved_pc;
    }
}
