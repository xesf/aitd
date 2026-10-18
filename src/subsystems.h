/*
 * subsystems.h - routines reached from game_main and engine_init whose exact
 * role is established by their call sites but whose bodies have not been
 * reconstructed yet.
 *
 * These names are working names. Each carries the IDA address it maps to so
 * the mapping can be corrected in one place as analysis continues.
 */

#ifndef AITD_SUBSYSTEMS_H
#define AITD_SUBSYSTEMS_H

#include "aitd.h"

/* Single instance guard, FindWindowA based. */
int sys_is_already_running(void);        /* 0x41a152 */

/* Called on every new game and on quit, before the engine tears down. */
void actor_table_reset(s16 mode);        /* 0x40ad85 */

/* Post-restore fix-ups and re-entry into the world. */
extern s16 g_menu_state;                 /* 0x435062 */

/* Builds the save file name; cdecl in the original (arguments pushed at
 * 0x407a7b and 0x407a81). */

extern s16 g_init_view;             /* 0x4350b9 */
void game_scene_loop(s16 flag);     /* 0x407c7c */

/* Timer, input and window plumbing installed by engine_init at
 * 0x4072d1..0x4072e0. */
void input_init(void);               /* 0x419c9f */
void window_show(void);              /* 0x41a197 */
void timer_init(void);               /* 0x41d945 */
void video_mode_query(void);         /* 0x419c2c */

/* Further engine_init stages, 0x407300..0x407314. */
void heap_init(void);                /* 0x422bc9 */
void rand_init(void);                /* 0x422fa1 */
void rand_seed_buffer(void *state);  /* 0x422f54 */
void palette_init(void);             /* 0x41fadb */

/* Screen buffer bookkeeping performed after both frame buffers exist. */
void video_buffers_ready(void);      /* 0x40adc6 */

/* Audio bring-up wrappers; both are thin over the platform layer. */
int sound_init(void);                /* 0x402d9b */
int cdaudio_init(void);              /* 0x401010 */

/* Text table bring-up; text_get_by_id (0x414fcb) is useless before this. */
void text_table_init(void);          /* 0x414e53 */

/* Constructor dispatched through the pointer stored at object+0x2b7e. */
void save_screens_init(void *object_table);  /* 0x419129 */

/* Accessors over the mode record at 0x435160: word[0] and word[1]. */
s32 video_mode_width(void);
s32 video_mode_height(void);

extern u8 g_font_color_table[];  /* 0x44485c */
extern u8 g_rand_state[];        /* 0x4350e5 */

extern u8 *g_menu_picture;      /* 0x444364, ITD_RESS 13 */
extern u8 *g_escape_picture;    /* 0x444360, ITD_RESS 17 */
extern u8 *g_tab_picture;       /* 0x444368, ITD_RESS 48 */
extern u8 g_palette_buffer[];   /* 0x44448c, 768 bytes */
extern u8 g_palette_overlay[];  /* 0x435068, first 16 colours */

/* Shutdown steps invoked by engine_shutdown (0x407687). */
void music_shutdown(void);           /* 0x40b3d2 */
void sound_shutdown(void);           /* 0x402e4f */
void sound_release(void);            /* 0x402df6 */
void cdaudio_shutdown(void);         /* 0x4029a7 */
void video_shutdown(void);           /* 0x41945e */
void window_destroy(void);           /* 0x41dae9 */

/* Resident ITD_RESS entries loaded by engine_init (0x407608..). */
const u8 *engine_resource(int index);

#endif /* AITD_SUBSYSTEMS_H */
