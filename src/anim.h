/*
 * anim.h - skeletal animation (0x41fb52..0x4200dd).
 *
 * An animation is u16 frame_count, u16 bone_count, then frame_count
 * keyframes. A keyframe is an 8 byte header (u16 duration in ticks, s16
 * step x/y/z) followed by one 8 byte entry per bone (u16 type, s16 a, b, c),
 * or 16 bytes per bone when the body carries flag 8.
 *
 * Applying a frame writes the bone entries straight into the body, at bone
 * record + 8. The body's extra-data block keeps the interpolation state:
 * +0x10 the keyframe being left (a pointer in the original) and +0x14 the
 * tick at which it was entered.
 */

#ifndef AITD_ANIM_H
#define AITD_ANIM_H

#include "aitd.h"

/* Step of the last frame applied, 0x43547a..0x43547e. */
extern s16 g_anim_step_x, g_anim_step_y, g_anim_step_z;
/* Duration and elapsed ticks of the current keyframe, 0x435480 / 0x435482. */
extern s16 g_anim_duration, g_anim_elapsed;

/* 0x430da0 - 60 Hz tick counter, refreshed by anim_update_ticks (0x41fab8). */
extern u32 g_ticks;
void ticks_update(void);   /* 0x41fab8 */
void ticks_set_base(u32 base);
extern u8 g_ticks_frozen;  /* 0x4351a8 */
void ticks_freeze(void);    /* 0x41faf8 */
u32 *ticks_frozen_at(void);  /* 0x4351a4 */
void ticks_unfreeze(void);  /* 0x41fb18 */

int anim_frame_count(const u8 *anim);                     /* 0x41fbe0 */
int anim_set_frame(int frame, const u8 *anim, u8 *body);  /* 0x41fbee */
int anim_set_inter(int frame, const u8 *anim, u8 *body, int flag); /* 0x41fd9f */
void anim_save_pose(u8 *buffer, u8 *body);                /* 0x42003c */
void anim_reset(u8 *body);                                /* 0x4200b4 */
void anim_hot_point(int index, const u8 *body, s16 *out); /* 0x41fb52 */

#endif /* AITD_ANIM_H */
