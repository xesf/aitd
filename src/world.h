/*
 * world.h - the world model: floors, rooms, cameras, world objects and the
 * active actor table.
 *
 * Floors are SALxx.PAK (one entry per room) and CAMxx.PAK (one entry per
 * camera). Each room lists the cameras that can show it; each camera lists
 * the rooms it can see, with per room mask and view zone data. Backgrounds
 * come from CAMERAxx.PAK, masks from MASKxx.PAK and animated background
 * pieces from ANIMxx.PAK.
 *
 * OBJETS.ITD holds every object of the game (54 bytes each). An object that
 * is near the camera gets an actor slot (182 bytes, 50 slots) and is
 * simulated; leaving the camera's view returns it to its object record.
 */

#ifndef AITD_WORLD_H
#define AITD_WORLD_H

#include <stddef.h>

#include "aitd.h"

/* world_object.zv_type: how actor_create sizes the box. */
enum zv_type {
    ZV_CUBE = 0,                    /* square in x/z on the larger extent */
    ZV_BODY = 1,
    ZV_CENTRED = 2,
    ZV_ROTATED = 3,
    ZV_ZONE = 4                     /* a tagged hard collision box */
};

/* anim_type bits (anim_init's second argument). */
enum anim_type {
    ANIM_ONCE = 0,
    ANIM_REPEAT = 1,
    ANIM_UNINTERRUPTIBLE = 2,
    ANIM_RESET = 4                  /* start from the rest pose */
};

/* anim_action_type: the action state machines of actor_anim_action. */
enum action_type {
    ACTION_NONE = 0,
    ACTION_HIT_START = 1,           /* waiting for the hit animation */
    ACTION_HIT = 2,                 /* the hot point box hurts */
    ACTION_HIT_END = 3,             /* waiting for the animation to change */
    ACTION_FIRE_START = 4,
    ACTION_FIRE = 5,                /* trace the shot on this frame */
    ACTION_THROW_START = 6,
    ACTION_THROW = 7,               /* release the object */
    ACTION_HIT_OBJECT = 8,          /* the whole actor box hurts */
    ACTION_FLY = 9,                 /* a thrown object in flight */
    ACTION_HIT_KEY_FRAME = 10       /* hit animation running, wait for the frame */
};

/* track_mode, set by LIFE_OP_MOVE. */
enum track_mode {
    TRACK_NONE = 0,
    TRACK_MANUAL = 1,               /* the arrow keys */
    TRACK_FOLLOW = 2,               /* follow another object */
    TRACK_SCRIPT = 3                /* a TRAK entry */
};

/* life_mode & 3: when an object's life script runs. */
enum life_mode {
    LIFE_MODE_OFF = 0,
    LIFE_MODE_ALWAYS = 1,
    LIFE_MODE_ROOM = 2,             /* in the current room */
    LIFE_MODE_VISIBLE = 3           /* in a room the camera can see */
};

/* Type word (+14) of a scene zone (room +2). */
enum scene_zone_type {
    ZONE_ROOM_LINK = 0,             /* +12 is the room entered */
    ZONE_MATERIAL = 8,
    ZONE_HARD_DEC = 9,
    ZONE_LIFE = 10                  /* starts the object's +0x24 life */
};

/* Type word (+14) of a hard collision box (room +0). */
enum hardcol_type {
    HARDCOL_BLOCK = 3,              /* reports 255 in HARD_COL */
    HARDCOL_TAGGED = 9,             /* reports +12 in HARD_COL */
    HARDCOL_NOT_PLAYER = 10         /* the player walks through */
};

#define ACTOR_MAX 50          /* 0x238c / 0xb6 */
#define OBJECT_SAVED_ZV_MAX 580 /* 0x1fb8 / 14 */

#pragma pack(push, 1)

/* OBJETS.ITD record, 0x36 bytes. */
struct world_object {
    s16 actor;          /* 0x00  actor slot, -1 when not instantiated */
    s16 body;           /* 0x02 */
    u16 flags;          /* 0x04 */
    s16 zv_type;        /* 0x06 */
    s16 found_body;     /* 0x08 */
    s16 found_name;     /* 0x0a  also the zone id for zv_type 4 */
    u16 flags2;         /* 0x0c */
    s16 found_life;     /* 0x0e */
    s16 x, y, z;        /* 0x10 */
    s16 alpha, beta, gamma; /* 0x16 */
    s16 stage;          /* 0x1c  floor */
    s16 room;           /* 0x1e */
    s16 life_mode;      /* 0x20 */
    s16 life;           /* 0x22 */
    s16 field_24;       /* 0x24 */
    s16 anim;           /* 0x26 */
    s16 frame;          /* 0x28 */
    s16 anim_type;      /* 0x2a */
    s16 anim_info;      /* 0x2c */
    s16 track_mode;     /* 0x2e */
    s16 track_number;   /* 0x30 */
    s16 position_in_track; /* 0x32 */
    s16 mark;           /* 0x34 */
};

/* Active actor, 0xb6 bytes. */
struct actor {
    s16 object;         /* 0x00  owning world object, -1 free, -2 special */
    s16 body;           /* 0x02 */
    u16 flags;          /* 0x04 */
    s16 dyn_flags;      /* 0x06 */
    s16 zv[6];          /* 0x08  world space box: x1 x2 y1 y2 z1 z2 */
    s16 screen[4];      /* 0x14  xmin ymin xmax ymax */
    s16 room_x, room_y, room_z;     /* 0x1c  position in its own room */
    s16 world_x, world_y, world_z;  /* 0x22  position in the current room */
    s16 alpha, beta, gamma;         /* 0x28 */
    s16 stage;          /* 0x2e */
    s16 room;           /* 0x30 */
    s16 life_mode;      /* 0x32 */
    s16 life;           /* 0x34 */
    u32 chrono;         /* 0x36 */
    u32 room_chrono;    /* 0x3a */
    s16 anim;           /* 0x3e */
    s16 anim_type;      /* 0x40  bit 0 repeat, bit 1 uninterruptible */
    s16 anim_info;      /* 0x42  animation to chain to */
    s16 pose_slot;      /* 0x44  slot in the saved pose ring, -1 none */
    s16 anim_flag;      /* 0x46  passed to the interpolator */
    s16 new_anim;       /* 0x48  queued animation, -1 none, -2 stop */
    s16 new_anim_type;  /* 0x4a */
    s16 new_anim_info;  /* 0x4c */
    s16 frame;          /* 0x4e */
    s16 frame_count;    /* 0x50 */
    s16 end_frame;      /* 0x52  keyframe reached this tick */
    s16 end_anim;       /* 0x54  animation finished */
    u32 sprite_time;    /* 0x56  hybrid sprite frame timer */
    s16 track_mode;     /* 0x5a */
    s16 track_number;   /* 0x5c */
    s16 mark;           /* 0x5e */
    s16 position_in_track; /* 0x60 */
    s16 step_x, step_y, step_z;     /* 0x62  animation displacement */
    s16 slide[3];       /* 0x68  collision corrections this keyframe */
    s16 fall[4];        /* 0x6e  y interpolator: from, to, duration, time */
    s16 falling;        /* 0x76 */
    s16 rot[4];         /* 0x78  beta interpolator: from, to, duration, time */
    s16 rot_dir;        /* 0x80  1 left, -1 right, 0 none */
    s16 speed;          /* 0x82  manual move state: 10 walk, 16 run, -1 back */
    s16 speed_interp[4]; /* 0x84 */
    s16 col[3];         /* 0x8c */
    s16 col_by;         /* 0x92 */
    s16 hard_dec;       /* 0x94 */
    s16 hard_col;       /* 0x96 */
    s16 hit;            /* 0x98 */
    s16 hit_by;         /* 0x9a */
    s16 anim_action_type; /* 0x9c */
    s16 action_anim;    /* 0x9e */
    s16 action_frame;   /* 0xa0 */
    s16 action_param;   /* 0xa2  hit box size / thrown object */
    s16 action_force;   /* 0xa4 */
    s16 hot_point_id;   /* 0xa6 */
    s16 action_param2;  /* 0xa8 */
    s16 hot_point[3];   /* 0xaa */
    s16 material;       /* 0xb0 */
    s16 visible;        /* 0xb2 */
    s16 action_param3;  /* 0xb4 */
};

#pragma pack(pop)

_Static_assert(sizeof(struct world_object) == 0x36, "world_object size");
_Static_assert(offsetof(struct world_object, x) == 0x10, "world_object.x");
_Static_assert(offsetof(struct world_object, stage) == 0x1c, "world_object.stage");
_Static_assert(offsetof(struct world_object, anim) == 0x26, "world_object.anim");
_Static_assert(offsetof(struct world_object, mark) == 0x34, "world_object.mark");

_Static_assert(sizeof(struct actor) == 0xb6, "actor size");
_Static_assert(offsetof(struct actor, zv) == 0x08, "actor.zv");
_Static_assert(offsetof(struct actor, screen) == 0x14, "actor.screen");
_Static_assert(offsetof(struct actor, room_x) == 0x1c, "actor.room_x");
_Static_assert(offsetof(struct actor, world_x) == 0x22, "actor.world_x");
_Static_assert(offsetof(struct actor, alpha) == 0x28, "actor.alpha");
_Static_assert(offsetof(struct actor, stage) == 0x2e, "actor.stage");
_Static_assert(offsetof(struct actor, life_mode) == 0x32, "actor.life_mode");
_Static_assert(offsetof(struct actor, chrono) == 0x36, "actor.chrono");
_Static_assert(offsetof(struct actor, anim) == 0x3e, "actor.anim");
_Static_assert(offsetof(struct actor, frame) == 0x4e, "actor.frame");
_Static_assert(offsetof(struct actor, track_mode) == 0x5a, "actor.track_mode");
_Static_assert(offsetof(struct actor, fall) == 0x6e, "actor.fall");
_Static_assert(offsetof(struct actor, rot) == 0x78, "actor.rot");
_Static_assert(offsetof(struct actor, speed) == 0x82, "actor.speed");
_Static_assert(offsetof(struct actor, hard_col) == 0x96, "actor.hard_col");
_Static_assert(offsetof(struct actor, step_x) == 0x62, "actor.step_x");
_Static_assert(offsetof(struct actor, col) == 0x8c, "actor.col");
_Static_assert(offsetof(struct actor, anim_action_type) == 0x9c, "actor.anim_action_type");
_Static_assert(offsetof(struct actor, hot_point_id) == 0xa6, "actor.hot_point_id");
_Static_assert(offsetof(struct actor, hot_point) == 0xaa, "actor.hot_point");
_Static_assert(offsetof(struct actor, visible) == 0xb2, "actor.visible");

/* ---- loaded data ------------------------------------------------------- */

extern u8 **g_floor_rooms;      /* 0x4350af, SALxx.PAK entries */
extern int g_floor_room_count;
extern u8 **g_floor_cameras;    /* 0x4350b3, CAMxx.PAK entries */
extern int g_floor_camera_count;

extern u8 *g_room_data;         /* 0x444ab8, current room */
extern u8 *g_room_cameras[64];  /* 0x444ac8, cameras of the current room */
extern s16 g_camera_zone[64];   /* 0x444f38 */
extern u8 *g_room_hardcol;      /* 0x444ab0 */
extern s16 g_room_hardcol_count; /* 0x445008 */
extern u8 *g_room_scezone;      /* 0x444abc */
extern s16 g_room_scezone_count; /* 0x445012 */
extern s16 g_room_camera_count; /* 0x445014 */

extern u8 *g_camera_masks;      /* 0x4350d9, MASKxx.PAK entry */
extern u8 *g_camera_bganims;    /* 0x4350dd, ANIMxx.PAK entry */

extern s16 g_cur_floor;         /* 0x445030 */
extern s16 g_cur_room;          /* 0x445032 */
extern s16 g_cur_camera;        /* 0x44502e */
extern s16 g_new_camera;        /* 0x445006 */
extern s16 g_viewed_room;       /* 0x4413f0 */
extern s8 g_visible_rooms[64];  /* 0x444ee8, -1 terminated */

extern struct world_object *g_objects; /* 0x444b1c */
extern s16 g_object_count;      /* 0x445024 */
extern struct actor g_actors[ACTOR_MAX]; /* 0x444b14 */
extern struct actor *g_cur_actor;  /* 0x444aa0 */
extern s16 g_cur_actor_index;      /* 0x44500e */

extern s16 *g_vars;             /* 0x444984, VARS.ITD */
extern u32 g_vars_size;         /* 0x4449f0 */

/* Saved world space boxes, 7 words per object (0x4423a4). Word 0 marks a
 * valid entry. Shares storage with g_game_vars. */
#define OBJECT_SAVED_ZV(index) (&g_game_vars[(index) * 7])

extern s16 g_visible_count;          /* 0x445010 */
extern s16 g_visible_actors[ACTOR_MAX]; /* 0x444988 */
extern s16 g_draw_list[ACTOR_MAX + 32]; /* 0x4449f4 */

extern s16 g_player_object;     /* 0x435052 */
extern s16 g_player_actor;      /* 0x435054 */

/* Change requests, serviced by the scene loop. */
extern s16 g_change_floor;      /* 0x4350c1 */
extern s16 g_change_room;       /* 0x4350bf */
extern s16 g_init_view;         /* 0x4350b9 (g_restore_mode) */
extern s16 g_redraw_state;      /* 0x4350c3 */
extern s16 g_actors_dirty;      /* 0x4350bb */
extern s16 g_objects_dirty;     /* 0x4350bd */
extern s16 g_static_dirty;      /* 0x4350b7 */
extern s16 g_new_floor;         /* 0x44500c, set by life scripts */

/* ---- helpers on the loaded blobs --------------------------------------- */

static inline s16 blob_s16(const u8 *p, int offset)
{
    return (s16)(p[offset] | (p[offset + 1] << 8));
}

static inline void blob_set_s16(u8 *p, int offset, s16 v)
{
    p[offset] = (u8)v;
    p[offset + 1] = (u8)((u16)v >> 8);
}

static inline u32 blob_u32(const u8 *p, int offset)
{
    return (u32)p[offset] | ((u32)p[offset + 1] << 8)
         | ((u32)p[offset + 2] << 16) | ((u32)p[offset + 3] << 24);
}

/* The room's world position, in tenths of the actor unit. */
#define ROOM_X(room) blob_s16((room), 4)
#define ROOM_Y(room) blob_s16((room), 6)
#define ROOM_Z(room) blob_s16((room), 8)

/* Camera viewed-room records start at +0x14, 16 bytes each. */
#define CAMERA_VIEW_COUNT(cam) blob_s16((cam), 0x12)
#define CAMERA_VIEW(cam, i) ((cam) + 0x14 + (i) * 16)

struct world_object *object_get(int index);    /* 0x407b1b */

void world_set_floor(s16 floor);   /* 0x40a9b3 */
void world_set_room(s16 room);     /* 0x40a6cb */
void world_setup_camera(void);     /* 0x40a123 */
void world_load_background(s16 camera); /* 0x414883 */
void world_free(void);             /* 0x4095a3 */
void world_reset_flags(void);      /* 0x407bae */
void world_load_objects(void);     /* 0x409490 */
void zv_from_body(const u8 *body, s16 *zv);  /* 0x4227a7 */
void zv_rotated(const u8 *body, s16 *zv, s16 alpha, s16 beta, s16 gamma,
                struct actor *a);   /* 0x40b601 */

void world_build_visible_rooms(void);          /* 0x409ac4 */
int world_room_is_visible(s16 room);           /* 0x4098e9 */
int world_camera_sees_room(s16 room);          /* 0x409943 */
int world_in_view_zone(s16 x, s16 z);          /* 0x4099d6 */
void world_update_objects(void);               /* 0x409bdb */
void world_build_actor_list(void);             /* 0x409fd2 */
void world_remove_actor(s16 index);            /* 0x409654 */

/* 0x40bab7 */
s16 actor_create(s16 body, s16 zv_type, s16 flags, s16 zone, s16 x, s16 y,
                 s16 z, s16 stage, s16 room, s16 alpha, s16 beta, s16 gamma,
                 s16 anim, s16 frame, s16 anim_type, s16 anim_info,
                 s16 object);
void actor_set_move_mode(s16 mode, s16 param);  /* 0x411160 */
void object_save_zv(s16 *zv_owner);              /* 0x40c814 */
void object_restore_zv(s16 *zv_owner);           /* 0x40c8e1 */

#endif /* AITD_WORLD_H */
