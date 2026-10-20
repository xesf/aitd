/*
 * life.h - gameplay subsystems the scene loop drives: life scripts, actor
 * movement and the per frame audio/message services.
 */

#ifndef AITD_LIFE_H
#define AITD_LIFE_H

#include "aitd.h"
#include "world.h"

/* Life script opcodes. The values are the raw opcodes of Alone in the Dark
 * 2 and 3, Jack in the Dark and Time Gate (jump table at 0x4041a3 in
 * WAITD3.EXE); Alone in the Dark 1 numbers them differently and is mapped
 * onto these by life_opcode_map (lifemap.c). Opcodes only AITD1 has get
 * values from 200. Bit 15 makes an opcode act on the object whose index
 * follows; an object without an actor only accepts the subset in
 * object_opcode (0x403a7e). Names are the classic ones. */
enum life_op {
    LIFE_OP_DO_MOVE = 0,            /* run the track / manual movement */
    LIFE_OP_ANIM_ONCE = 1,
    LIFE_OP_ANIM_ALL_ONCE = 2,      /* once, cannot be interrupted */
    LIFE_OP_BODY = 3,
    LIFE_OP_IF_EQUAL = 4,           /* IF_xx run on when true, else jump */
    LIFE_OP_IF_DIFFERENT = 5,
    LIFE_OP_IF_SUP_EQUAL = 6,
    LIFE_OP_IF_SUP = 7,
    LIFE_OP_IF_INF_EQUAL = 8,
    LIFE_OP_IF_INF = 9,
    LIFE_OP_GOTO = 10,
    LIFE_OP_RETURN = 11,
    LIFE_OP_END = 12,
    LIFE_OP_ANIM_REPEAT = 13,
    LIFE_OP_ANIM_MOVE = 14,
    LIFE_OP_MOVE = 15,              /* track mode and track number */
    LIFE_OP_HIT = 16,
    LIFE_OP_MESSAGE = 17,
    LIFE_OP_VAR = 18,
    LIFE_OP_INC = 19,
    LIFE_OP_DEC = 20,
    LIFE_OP_ADD = 21,
    LIFE_OP_SUB = 22,
    LIFE_OP_LIFE_MODE = 23,
    LIFE_OP_SWITCH = 24,
    LIFE_OP_CASE = 25,
    LIFE_OP_START_CHRONO = 26,
    LIFE_OP_MULTI_CASE = 27,
    LIFE_OP_FOUND = 28,
    LIFE_OP_LIFE = 29,
    LIFE_OP_DELETE = 30,
    LIFE_OP_TAKE = 31,
    LIFE_OP_IN_HAND = 32,
    LIFE_OP_READ = 33,              /* read_document, 0x4160b6 */
    LIFE_OP_ANIM_SAMPLE = 34,
    LIFE_OP_SPECIAL = 35,
    LIFE_OP_DO_REAL_ZV = 36,
    LIFE_OP_SAMPLE = 37,
    LIFE_OP_TYPE = 38,
    LIFE_OP_GAME_OVER = 39,
    LIFE_OP_MANUAL_ROT = 40,
    LIFE_OP_RND_FREQ = 41,          /* reads one word, does nothing */
    LIFE_OP_MUSIC = 42,
    LIFE_OP_SET_BETA = 43,
    LIFE_OP_DO_ROT_ZV = 44,
    LIFE_OP_STAGE = 45,
    LIFE_OP_FOUND_NAME = 46,
    LIFE_OP_FOUND_FLAG = 47,
    LIFE_OP_FOUND_LIFE = 48,
    LIFE_OP_CAMERA_TARGET = 49,
    LIFE_OP_DROP = 50,              /* start_fall, 0x40e1f0 */
    LIFE_OP_FIRE = 51,
    LIFE_OP_TEST_COL = 52,
    LIFE_OP_FOUND_BODY = 53,
    LIFE_OP_SET_ALPHA = 54,
    LIFE_OP_DO_MAX_ZV = 55,
    LIFE_OP_PUT = 56,               /* 0x40e366 */
    LIFE_OP_DO_NORMAL_ZV = 57,
    LIFE_OP_DO_CARRE_ZV = 58,
    LIFE_OP_SAMPLE_THEN = 59,
    LIFE_OP_LIGHT = 60,
    LIFE_OP_SHAKING = 61,           /* g_shaking */
    LIFE_OP_INVENTORY = 62,         /* enables the inventory */
    LIFE_OP_FOUND_WEIGHT = 63,
    LIFE_OP_PUT_AT = 64,            /* 0x40e41b */
    LIFE_OP_DEF_ZV = 65,
    LIFE_OP_HIT_OBJECT = 66,
    LIFE_OP_ANGLE = 67,
    LIFE_OP_REP_SAMPLE = 68,
    LIFE_OP_THROW = 69,
    LIFE_OP_WATER = 70,             /* no-op in AITD3 */
    LIFE_OP_PICTURE = 71,
    LIFE_OP_STOP_SAMPLE = 72,
    LIFE_OP_NEXT_MUSIC = 73,
    LIFE_OP_FADE_MUSIC = 74,
    LIFE_OP_STOP_HIT_OBJECT = 75,
    LIFE_OP_COPY_ANGLE = 76,
    LIFE_OP_SAMPLE_THEN_REPEAT = 77,
    LIFE_OP_WAIT_GAME_OVER = 78,
    LIFE_OP_GET_MATRICE = 79,       /* no-op in AITD3 */
    LIFE_OP_STAGE_LIFE = 80,        /* object +0x24, the life a zone of type 10 starts */
    LIFE_OP_CONTINUE_TRACK = 81,    /* step over a TRACK_OP_STOP */
    LIFE_OP_ANIM_RESET = 82,        /* once, from the rest pose */
    LIFE_OP_RESET_MOVE_MANUAL = 83,
    LIFE_OP_PLUIE = 84,             /* rain; no-op in AITD3 */
    LIFE_OP_ANIM_HYBRIDE_ONCE = 85,
    LIFE_OP_ANIM_HYBRIDE_REPEAT = 86,
    LIFE_OP_MODIF_C_VAR = 87,       /* set a DEFINES word */
    LIFE_OP_CALL_INVENTORY = 88,
    LIFE_OP_BODY_RESET = 89,
    LIFE_OP_DEL_INVENTORY = 90,
    LIFE_OP_SET_INVENTORY = 91,
    LIFE_OP_PLAY_SEQUENCE = 92,     /* 0x4025a3 */
    LIFE_OP_2D_ANIM_SAMPLE = 93,
    LIFE_OP_SET_GROUND = 94,
    LIFE_OP_PROTECT = 95,           /* no-op in AITD3 */
    LIFE_OP_DEF_ABS_ZV = 96,
    LIFE_OP_DEF_SEQUENCE_SAMPLE = 97,
    LIFE_OP_READ_ON_PICTURE = 98,
    LIFE_OP_FIRE_UP_DOWN = 99,
    LIFE_OP_DO_ROT_CLUT = 100,
    LIFE_OP_STOP_CLUT = 101,
    /* Time Gate. */
    LIFE_OP_IF_IN = 102,
    LIFE_OP_IF_OUT = 103,
    LIFE_OP_SET_VOLUME_SAMPLE = 105,
    LIFE_OP_FADE_IN_MUSIC = 108,
    LIFE_OP_SET_MUSIC_VOLUME = 109,
    LIFE_OP_MUSIC_AND_LOOP = 110,
    LIFE_OP_MUSIC_THEN = 111,
    LIFE_OP_MUSIC_THEN_LOOP = 112,
    LIFE_OP_START_FADE_IN_MUSIC = 113,
    LIFE_OP_START_FADE_IN_MUSIC_THEN = 114,
    LIFE_OP_START_FADE_IN_MUSIC_LOOP = 115,
    LIFE_OP_FADE_OUT_MUSIC_STOP = 116,
    LIFE_OP_MUSIC_ALTER_TEMPO = 117,
    LIFE_OP_REP_SAMPLE_N_TIME = 118, /* not handled by the TG interpreter */
    LIFE_OP_TG_VALUE_LIST = 121,    /* count, then that many expressions */
    LIFE_OP_TG_122 = 122,
    /* Alone in the Dark 1 only. */
    LIFE_OP_MESSAGE_VALUE = 200,
    LIFE_OP_CAMERA = 201,
    LIFE_OP_STOP_BETA = 202,
    LIFE_OP_C_VAR = 203,
    LIFE_OP_UP_COOR_Y = 204,
    LIFE_OP_SPEED = 205,
    LIFE_OP_GET_HARD_CLIP = 206,
    LIFE_OP_END_SEQUENCE = 207,
    LIFE_OP_INVALID = -1
};

/* Variables read by the evaluator (0x403002). -1 is a constant and 0 a
 * VARS.ITD entry; bit 15 reads the value off another object. */
enum life_var {
    LIFE_VAR_CONSTANT = -1,
    LIFE_VAR_VARS = 0,
    LIFE_VAR_COL = 1,
    LIFE_VAR_HARD_DEC = 2,
    LIFE_VAR_HARD_COL = 3,
    LIFE_VAR_HIT = 4,
    LIFE_VAR_HIT_BY = 5,
    LIFE_VAR_ANIM = 6,
    LIFE_VAR_END_ANIM = 7,
    LIFE_VAR_FRAME = 8,
    LIFE_VAR_END_FRAME = 9,
    LIFE_VAR_BODY = 10,
    LIFE_VAR_MARK = 11,
    LIFE_VAR_NUM_TRACK = 12,
    LIFE_VAR_CHRONO = 13,
    LIFE_VAR_ROOM_CHRONO = 14,
    LIFE_VAR_DIST = 15,
    LIFE_VAR_COL_BY = 16,
    LIFE_VAR_IN_INVENTORY = 17,     /* flags2 0x8000 */
    LIFE_VAR_ACTION = 18,
    LIFE_VAR_POSREL = 19,
    LIFE_VAR_KEYBOARD_INPUT = 20,
    LIFE_VAR_BUTTON = 21,
    LIFE_VAR_COL_ANY = 22,          /* COL, else COL_BY */
    LIFE_VAR_ALPHA = 23,
    LIFE_VAR_BETA = 24,
    LIFE_VAR_GAMMA = 25,
    LIFE_VAR_IN_HAND = 26,
    LIFE_VAR_HIT_FORCE = 27,
    LIFE_VAR_CAMERA = 28,
    LIFE_VAR_RAND = 29,
    LIFE_VAR_FALLING = 30,
    LIFE_VAR_ROOM = 31,
    LIFE_VAR_LIFE = 32,
    LIFE_VAR_OBJECT_TAKEN = 33,     /* flags2 0xc000 */
    LIFE_VAR_MUSIC = 34,
    LIFE_VAR_DEFINE = 35,
    LIFE_VAR_STAGE = 36,
    LIFE_VAR_THROW = 37,            /* flags2 0x1000 */
    LIFE_VAR_MATRIX = 38,
    LIFE_VAR_MATERIAL = 39,
    LIFE_VAR_TEST_PROTECT = 40,
    LIFE_VAR_END_WAY = 41,          /* Time Gate */
    LIFE_VAR_DIV_BY_2 = 42,         /* Time Gate: an expression halved */
    /* Alone in the Dark 1 only. */
    LIFE_VAR_ROOM_Y = 200,
    LIFE_VAR_TEST_ZV_END_ANIM = 201
};

/* Raw opcode / variable of the title in force -> life_op / life_var
 * (lifemap.c). */
s16 life_opcode_map(s16 raw);
s16 life_var_map(s16 raw);

/* 0x4038f6 - run one life script for g_cur_actor. */
void life_run(s16 life, int flag);
/* 0x406735 - run an object's life without an actor of its own. */
void life_run_global(s16 object);

/* 0x40fe5f - animation, movement and collision for g_cur_actor. */
void actor_process(int visible);
/* 0x40f7d6 - gravity. */
void actor_fall(void);
/* 0x41317b - hits, throws and other animation driven actions. */
void actor_anim_action(void);

/* 0x40a57a - switch cameras when the player leaves the current one. */
int camera_select(void);

/* 0x40b452 - per frame sample and music bookkeeping. */
void audio_frame(void);
/* 0x40b2e8 */
void sound_stop_all(void);

/* Objects and the inventory. */
void object_take(s16 object);              /* 0x40d8a2 */
void object_found(s16 object, int mode);   /* 0x40dc7d */
void object_delete(s16 object);            /* 0x40983a */
void inventory_remove(s16 object);         /* 0x40d806 */
void object_put(const s16 *args);          /* 0x40e366 */
void object_drop(s16 object, s16 target);  /* 0x40e41b */

/* Animation driven actions started by life scripts. */
void actor_hit(s16 anim, s16 frame, s16 group, s16 size, s16 force, s16 next); /* 0x412e49 */
void actor_fire(s16 anim, s16 frame, s16 hot, s16 w4, s16 w5, s16 w6, s16 w7,
                s16 next, int variant);    /* 0x412ef1 / 0x412fbc */
void actor_throw(s16 anim, s16 frame, s16 hot, s16 object, s16 w5, s16 w6,
                 s16 next);                /* 0x413088 */
void special_spawn(s16 type);              /* opcode 35 */
s16 special_create(s16 type, s16 x, s16 z, s16 y, s16 stage, s16 room,
                   s16 alpha, s16 beta, s16 gamma, const s16 *zv); /* 0x40c9ef */
void start_fall(s16 object, s16 holder);   /* 0x40e1f0 */
void change_player(s16 object);            /* opcode 49 */

/* Sound and music (0x40b1d6, 0x40b26c, 0x40b31e). */
void sample_play(s16 sample);
void sample_repeat(s16 sample, u16 count);
void music_play(s16 music);

/* Full screen sequences. */
int movie_play(const char *name);          /* cutscene.c: .SMK or .MUX */
void read_document(s16 text, s16 type);     /* 0x4160b6, opcode 33 */
/* An ITD_RESS picture (and its palette) as the reader's backdrop. */
void reader_set_backdrop(s16 entry);
extern s16 g_page_turn_style;
extern s16 g_reader_voc;   /* AITD1 CD: voice read aloud, -1 none */
s16 text_reader(s16 text, s16 xmin, s16 ymin, s16 xmax, s16 ymax, s16 mode,
                s16 colour, s16 shadow);    /* 0x4156a6 */
void play_cutscene(s16 number, s16 flags, s16 repeat); /* 0x4025a3 */
void show_picture(s16 entry, s16 time, s16 sample); /* 0x405db1, opcode 71 */
void read_book(const s16 *args);           /* 0x40584e, opcode 98 */
void palette_cycle_init(s16 first, s16 last, s16 speed); /* 0x40d6eb, opcode 100 */
int bganim_frame_is(s16 index, s16 frame); /* opcode 93 */

u16 rand16(void);                          /* 0x422bd4 */

extern s16 g_inventory[2][50];             /* 0x44478c */
extern s16 g_inventory_count[2];           /* 0x444bbc */
extern s16 g_in_hand[2];                   /* 0x444bb4 */
extern s16 g_inventory_index;              /* 0x445034 */
extern s16 g_sample_priority;               /* 0x444ffc */
extern s16 g_sample_current;                /* 0x445004 */
extern s16 g_music_pending;                /* 0x4350cd */
extern s16 g_music_next;                   /* 0x4350cf */
extern s16 g_sample_pending;               /* 0x444ffa */
extern s16 g_difficulty[3];                /* 0x444ee2 */

/* Sprites (flag 0x200) and special actors (object == -2). */
void special_draw(s16 index, s16 visible);

/* On-screen messages (0x4153f6, 0x41512f, 0x4154a6). */
void message_show(int text_id);
extern s16 g_cutscene_samples[30 * 2];  /* 0x44232c */
extern s16 g_cutscene_sample_count;     /* 0x444ff4 */
extern s16 g_preview_body;   /* 0x4350a9 */
extern s16 g_preview_angle;  /* 0x4350a7 */
int message_draw(void);
void message_reset(void);

/* Text drawing into the current raster target (0x4150bb, 0x41502f). */
void text_draw_string(s16 x, s16 y, const char *text, u8 colour);
void text_draw_centered(s16 x, s16 y, s16 id, u8 colour);
void text_draw_centered_shadowed(s16 x, s16 y, s16 id, u8 colour, u8 shadow);

/* System dialogs reached from the scene loop. */
int scene_escape_menu(void);        /* 0x408545 */
void music_toggle(void);            /* 0x40b3f6 */
void inventory_open(void);          /* 0x40f1c3 */
void scene_tab_action(void);        /* 0x4019ff */

extern s16 g_scene_scancode;   /* 0x445026 */
extern s16 g_scene_dir_flags;  /* 0x445028 */
extern s16 g_scene_confirm;    /* 0x44501e */
extern s16 g_message_shown;    /* 0x445022 */
extern s16 g_action;           /* 0x44502c, the ACTION variable */
extern s16 g_new_floor;        /* 0x44500c */
extern s16 g_new_room;         /* 0x44500a */
extern s16 g_inventory_enabled; /* 0x435056 */
void shaking_save(void);     /* 0x40b59d */
void shaking_restore(void);  /* 0x40b5d6 */
extern s16 g_shaking;      /* 0x430da4 */
extern s16 g_saved_shaking; /* 0x445020 */
extern u8 g_frame_flag;        /* 0x435137 */

#endif /* AITD_LIFE_H */
