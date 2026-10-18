/*
 * actor.h - per actor simulation: animation stepping, movement, collision,
 * scene zones, tracks and the camera choice that follows the player.
 */

#ifndef AITD_ACTOR_H
#define AITD_ACTOR_H

#include "aitd.h"
#include "world.h"

/* Opcodes of a TRAK entry (actor_do_move, 0x411411). */
enum track_op {
    TRACK_OP_WARP = 0,
    TRACK_OP_GOTO_POS = 1,
    TRACK_OP_END = 2,
    TRACK_OP_REWIND = 3,
    TRACK_OP_MARK = 4,
    TRACK_OP_STOP = 5,                 /* waits for LIFE_OP_CONTINUE_TRACK */
    TRACK_OP_ROTATE = 6,
    TRACK_OP_COL_OFF = 7,
    TRACK_OP_COL_ON = 8,
    TRACK_OP_SKIP_WORD = 9,
    TRACK_OP_FALL_OFF = 10,
    TRACK_OP_FALL_ON = 11,
    TRACK_OP_GOTO_POS_Y = 12,          /* walk while climbing to the point's height */
    TRACK_OP_STORE_POS = 13,
    TRACK_OP_STAIRS_X = 14,
    TRACK_OP_STAIRS_Z = 15,
    TRACK_OP_ANGLES = 16
};

/* Time based interpolator, 8 bytes: from, to, duration, start tick. */
void interp_init(s16 from, s16 to, s16 duration, s16 *s);      /* 0x422608 */
void interp_init_angle(s16 from, s16 to, s16 duration, s16 *s); /* 0x422636 */
s16 interp_get(s16 *s);                                         /* 0x42266c */
s16 interp_get_angle(s16 *s);                                   /* 0x4226c0 */

/* 0x40fc09 - returns 1 when the animation was taken. */
int anim_init(s16 anim, s16 type, s16 info);
/* 0x41b428 - drop a static actor out of the background layer. */
void actor_unstatic(s16 index);
/* 0x41b3f8 - freeze an actor into the background layer. */
void actor_make_static(s16 index);

/* 0x4143e0 - collision boxes of a room overlapping zv. */
int hardcol_test(const s16 *zv, const u8 *room);
extern const u8 *g_hardcol_hits[32];  /* 0x444414 */

/* 0x412901 - actors overlapping zv, written to g_cur_actor->col. */
int actor_collide(s16 index, const s16 *zv);
/* 0x4127f4 - any solid actor overlapping zv. */
int actor_collide_solid(s16 index, const s16 *zv);
/* 0x412a4a - actors overlapping zv get col_by = index. */
int actor_collide_mark(s16 index, const s16 *zv);

/* 0x412731 - move a box from one room's frame into another's. */
void zv_to_room(s16 *zv, s16 from, s16 to);
int zv_overlap(const s16 *a, const s16 *b);   /* 0x422a78 */

/* 0x411233 - turn with the left/right keys. */
void actor_turn_keys(s16 speed);
/* 0x4125c0 - pick the walk/run/stop animations from the move state. */
void actor_anim_move(s16 stand, s16 walk, s16 run, s16 stop, s16 back,
                     s16 turn_right, s16 turn_left);
/* 0x411411 - DO_MOVE: manual, follow and track movement. */
void actor_do_move(void);
/* 0x40ab13 - put the current actor somewhere else. */
void actor_teleport(s16 floor, s16 room, s16 x, s16 y, s16 z);

/* 0x422a04 / 0x4229bb */
s16 distance_xz(s16 x1, s16 z1, s16 x2, s16 z2);
s16 distance_3d(s16 x1, s16 y1, s16 z1, s16 x2, s16 y2, s16 z2);

#endif /* AITD_ACTOR_H */
