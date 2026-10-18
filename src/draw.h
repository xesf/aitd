/*
 * draw.h - scene composition: sorting, the static background layer, the
 * per frame redraw and the occlusion masks.
 *
 * Three buffers take part:
 *   g_front_buffer  the clean camera picture (0x435064)
 *   g_back_buffer   the picture plus every static actor (0x444bac)
 *   g_screen        what is presented: back buffer plus moving actors
 *                   (the WinG surface at 0x445c8e in the original)
 */

#ifndef AITD_DRAW_H
#define AITD_DRAW_H

#include "aitd.h"

/* special_create types; 3..8 draw one body once, chosen by a DEFINES word. */
enum special_type {
    SPECIAL_DUST = 0,
    SPECIAL_BLOOD = 1,
    SPECIAL_SPARKS = 2,
    SPECIAL_BODY_DEFINE_1 = 3,  /* the muzzle flash */
    SPECIAL_BODY_DEFINE_5 = 4,
    SPECIAL_BODY_DEFINE_6 = 5,
    SPECIAL_BODY_DEFINE_7 = 6,
    SPECIAL_BODY_DEFINE_8 = 7,
    SPECIAL_BODY_DEFINE_10 = 8
};
#include "world.h"

extern u8 *g_screen;
extern s16 g_clip_y_default;    /* 0x4350e1 */
extern s16 g_fade_request;      /* 0x43505c */
extern s16 g_black_screen;      /* 0x43505a */
extern s32 g_force_redraw;      /* 0x435138 */

void draw_init(void);
void draw_present(void);

void draw_sort_actors(void);          /* 0x40c7da */
void draw_measure_actors(void);       /* 0x41a615 */
void draw_static_actors(void);        /* 0x41a94a */
void draw_mark_overlaps(void);        /* 0x41a3f6 */
void draw_frame(s16 full);            /* 0x41ac57 */
void draw_masks(struct actor *a);     /* 0x41a215 */

/* Animated background pieces (0x406cea, 0x406d8c, 0x4069f2). */
void bganim_reset(void);
void bganim_add(u16 index);
void bganim_insert(void);
void special_release(s16 id);
void bganim_draw(s16 index);

#endif /* AITD_DRAW_H */
