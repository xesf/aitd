/*
 * anim.c - skeletal animation (0x41fb52..0x4200dd).
 *
 * The original keeps a 32 bit pointer to the keyframe being left inside the
 * body block itself (body + 0x10). A host pointer does not fit in 4 bytes, so
 * the slot holds a handle instead: a 1-based index into a table of the
 * keyframe addresses seen so far. The meaning of the slot, including the
 * "zero means no previous pose" test at 0x41fe52, is unchanged.
 */

#include <stdlib.h>
#include <string.h>

#include "aitd.h"
#include "anim.h"
#include "platform.h"
#include "r3d.h"

s16 g_anim_step_x, g_anim_step_y, g_anim_step_z;
s16 g_anim_duration, g_anim_elapsed;
u32 g_ticks;

static u32 s_tick_base;  /* 0x4351bf */

static u16 rd16(const u8 *p)
{
    return (u16)(p[0] | (p[1] << 8));
}

static void wr16(u8 *p, u16 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void wr32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

/* ---- keyframe handles -------------------------------------------------- */

#define HANDLE_BUCKETS 4096

struct handle_entry {
    const u8 *ptr;
    u32 next;  /* 1-based chain within s_handles, 0 ends */
};

static struct handle_entry *s_handles;
static u32 s_handle_count;
static u32 s_handle_capacity;
static u32 s_buckets[HANDLE_BUCKETS];

static u32 ptr_bucket(const u8 *p)
{
    uintptr_t v = (uintptr_t)p;

    return (u32)((v >> 3) ^ (v >> 15)) & (HANDLE_BUCKETS - 1);
}

static u32 ptr_to_handle(const u8 *p)
{
    u32 b, h;

    if (p == NULL) {
        return 0;
    }

    b = ptr_bucket(p);
    for (h = s_buckets[b]; h != 0; h = s_handles[h - 1].next) {
        if (s_handles[h - 1].ptr == p) {
            return h;
        }
    }

    if (s_handle_count == s_handle_capacity) {
        u32 cap = s_handle_capacity ? s_handle_capacity * 2 : 1024;
        struct handle_entry *grown = realloc(s_handles, cap * sizeof(*grown));

        if (grown == NULL) {
            return 0;
        }
        s_handles = grown;
        s_handle_capacity = cap;
    }

    s_handles[s_handle_count].ptr = p;
    s_handles[s_handle_count].next = s_buckets[b];
    s_handle_count++;
    s_buckets[b] = s_handle_count;
    return s_handle_count;
}

static const u8 *handle_to_ptr(u32 h)
{
    if (h == 0 || h > s_handle_count) {
        return NULL;
    }
    return s_handles[h - 1].ptr;
}

/* ---- ticks ------------------------------------------------------------- */

/* 0x41fab8 - GetTickCount() / 17, so roughly 60 per second. */
void ticks_update(void)
{
    g_ticks = platform_get_ticks() / 17u - s_tick_base;
}

void ticks_set_base(u32 base)
{
    s_tick_base = base;
}

static u32 s_frozen_at;  /* 0x4351a4 */
u8 g_ticks_frozen;       /* 0x4351a8 */

/* Saved with the game (block 19). */
u32 *ticks_frozen_at(void)
{
    return &s_frozen_at;
}

/* 0x41faf8 - stop the game clock (menus, dialogs, cutscenes). */
void ticks_freeze(void)
{
    if (g_ticks_frozen != 1) {
        ticks_update();
        s_frozen_at = g_ticks;
        g_ticks_frozen = 1;
    }
}

/* 0x41fb18 - restart it where it stopped. */
void ticks_unfreeze(void)
{
    if (g_ticks_frozen == 1) {
        s_tick_base = 0;
        ticks_update();
        s_tick_base = g_ticks - s_frozen_at;
        g_ticks = s_frozen_at;
        g_ticks_frozen = 0;
    }
}

/* ---- body layout ------------------------------------------------------- */

/* Pointer to the first bone record + 8 (its type field), as the frame
 * writers compute it, and the bone count. */
static u8 *bone_params(u8 *body, int *bone_count)
{
    u8 *p = body + 0x0e;
    u16 points;

    p += rd16(p) + 2;      /* skip the extra-data block */
    points = rd16(p);
    p += points * 6 + 2;   /* skip the points */
    *bone_count = rd16(p);
    p += *bone_count * 2 + 2;
    return p + 8;
}

/* 0x41fbe0 */
int anim_frame_count(const u8 *anim)
{
    return (s16)rd16(anim);
}

static int frame_size(int bones, u16 body_flags)
{
    return (body_flags & 8) ? bones * 16 + 8 : (bones + 1) * 8;
}

/* 0x41fbee - jump straight to a keyframe. */
int anim_set_frame(int frame, const u8 *anim, u8 *body)
{
    u16 flags = rd16(body);
    int anim_bones, body_bones, n, stride;
    const u8 *key;
    const u8 *src;
    u8 *dst;

    if ((u16)frame >= rd16(anim)) {
        return -1;
    }

    anim_bones = rd16(anim + 2);
    key = anim + 4 + (u16)((u16)frame_size(anim_bones, flags) * (u16)frame);

    g_anim_duration = (s16)rd16(key);
    g_anim_elapsed = (s16)rd16(key);

    if (!(flags & 2)) {
        return 0;
    }

    /* 0x41fc63: remember the keyframe and when it was entered. */
    wr32(body + 0x10, ptr_to_handle(key));
    ticks_update();
    wr16(body + 0x14, (u16)g_ticks);

    dst = bone_params(body, &body_bones);
    n = anim_bones;
    if (n > body_bones) {
        n = body_bones;
    }
    stride = (flags & 8) ? 16 : 8;

    src = key + 8;
    while (n-- > 0) {
        memcpy(dst, src, (size_t)stride);
        src += stride;
        dst += stride + 8;
    }

    g_anim_step_x = (s16)rd16(key + 2);
    g_anim_step_y = (s16)rd16(key + 4);
    g_anim_step_z = (s16)rd16(key + 6);
    return 1;
}

/* 0x41fd54 */
static s16 lerp_value(s16 from, s16 to, s16 elapsed, s16 duration)
{
    if (to == from) {
        return to;
    }
    return (s16)((s16)(((s32)(s16)(to - from) * elapsed) / duration) + from);
}

/* 0x41fcde - takes the short way round when the difference passes half a
 * turn. The result is not masked. */
static s16 lerp_angle(s16 from, s16 to, s16 elapsed, s16 duration)
{
    s16 diff = (s16)(to - from);

    if (from == to) {
        return from;
    }
    if (diff > 0x200) {
        from = (s16)(from + 0x400);
        return (s16)((s16)(((s32)(s16)(to - from) * elapsed) / duration) + from);
    }
    if (diff < (s16)0xfe00) {
        to = (s16)(to + 0x400);
        return (s16)((s16)(((s32)(s16)(to - from) * elapsed) / duration) + from);
    }
    return (s16)((s16)(((s32)diff * elapsed) / duration) + from);
}

/*
 * 0x41fd9f - interpolate from the remembered pose towards keyframe `frame`.
 * Returns 1 once the keyframe has been reached (and remembers it), 0 while
 * still blending, -1 for a body without bones.
 */
int anim_set_inter(int frame, const u8 *anim, u8 *body, int flag)
{
    u16 flags = rd16(body);
    int frame_total = rd16(anim);
    int anim_bones = rd16(anim + 2);
    int fsize = frame_size(anim_bones, flags);
    const u8 *key = anim + 4 + (u16)((u16)fsize * (u16)frame);
    const u8 *prev;
    u16 duration = rd16(key);
    u16 start;
    u16 elapsed;
    int body_bones, n;
    u8 *dst;

    (void)flag;

    if (!(flags & 2)) {
        return -1;
    }

    /* 0x41fe1c: frame 0 blends from whatever pose was saved; any other
     * frame (or an empty animation) blends from the keyframe before it. */
    if (frame_total != 0 && frame == 0) {
        prev = handle_to_ptr(rd32(body + 0x10));
    } else {
        prev = key - fsize;
        wr32(body + 0x10, ptr_to_handle(prev));
    }

    start = rd16(body + 0x14);
    if (prev == NULL) {
        start = duration;
        prev = key;
    }

    dst = bone_params(body, &body_bones);
    n = anim_bones;
    if (n > body_bones) {
        n = body_bones;
    }

    ticks_update();
    elapsed = (u16)((u16)g_ticks - start);

    if (elapsed >= duration) {
        /* 0x41ffd6: keyframe reached. */
        const u8 *src = key + 8;
        int stride = (flags & 8) ? 16 : 8;

        while (n-- > 0) {
            memcpy(dst, src, (size_t)stride);
            src += stride;
            dst += stride + 8;
        }

        wr32(body + 0x10, ptr_to_handle(key));
        ticks_update();
        wr16(body + 0x14, (u16)g_ticks);

        g_anim_duration = (s16)duration;
        g_anim_elapsed = (s16)duration;
        g_anim_step_x = (s16)rd16(key + 2);
        g_anim_step_y = (s16)rd16(key + 4);
        g_anim_step_z = (s16)rd16(key + 6);
        return 1;
    }

    {
        const u8 *a = prev + 8;
        const u8 *b = key + 8;
        s16 e = (s16)elapsed, d = (s16)duration;

        while (n-- > 0) {
            u16 type = rd16(b);

            /* 0x41fd87: the type always comes from the target frame. */
            wr16(dst, type);

            if (!(flags & 8)) {
                if (type == 0) {
                    wr16(dst + 2, (u16)lerp_angle((s16)rd16(a + 2), (s16)rd16(b + 2), e, d));
                    wr16(dst + 4, (u16)lerp_angle((s16)rd16(a + 4), (s16)rd16(b + 4), e, d));
                    wr16(dst + 6, (u16)lerp_angle((s16)rd16(a + 6), (s16)rd16(b + 6), e, d));
                } else if (type == 1 || type == 2) {
                    wr16(dst + 2, (u16)lerp_value((s16)rd16(a + 2), (s16)rd16(b + 2), e, d));
                    wr16(dst + 4, (u16)lerp_value((s16)rd16(a + 4), (s16)rd16(b + 4), e, d));
                    wr16(dst + 6, (u16)lerp_value((s16)rd16(a + 6), (s16)rd16(b + 6), e, d));
                }
                a += 8;
                b += 8;
                dst += 16;
            } else {
                /* 0x41ff1b: translation or zoom, then always a rotation. */
                if (type == 1 || type == 2) {
                    wr16(dst + 2, (u16)lerp_value((s16)rd16(a + 2), (s16)rd16(b + 2), e, d));
                    wr16(dst + 4, (u16)lerp_value((s16)rd16(a + 4), (s16)rd16(b + 4), e, d));
                    wr16(dst + 6, (u16)lerp_value((s16)rd16(a + 6), (s16)rd16(b + 6), e, d));
                }
                wr16(dst + 8, (u16)lerp_angle((s16)rd16(a + 8), (s16)rd16(b + 8), e, d));
                wr16(dst + 10, (u16)lerp_angle((s16)rd16(a + 10), (s16)rd16(b + 10), e, d));
                wr16(dst + 12, (u16)lerp_angle((s16)rd16(a + 12), (s16)rd16(b + 12), e, d));
                a += 16;
                b += 16;
                dst += 24;
            }
        }

        g_anim_step_x = (s16)(((s32)(s16)rd16(key + 2) * e) / d);
        g_anim_step_y = (s16)(((s32)(s16)rd16(key + 4) * e) / d);
        g_anim_step_z = (s16)(((s32)(s16)rd16(key + 6) * e) / d);
        g_anim_duration = d;
        g_anim_elapsed = e;
    }

    return 0;
}

/* 0x42003c - copy the body's current bone state into buffer as a pseudo
 * keyframe and make it the pose to blend from. */
void anim_save_pose(u8 *buffer, u8 *body)
{
    u16 flags = rd16(body);
    int bones, stride, n;
    u8 *src;
    u8 *dst = buffer + 8;

    if (!(flags & 2)) {
        return;
    }

    ticks_update();
    wr16(body + 0x14, (u16)g_ticks);
    wr32(body + 0x10, ptr_to_handle(buffer));

    src = bone_params(body, &bones);
    stride = (flags & 8) ? 16 : 8;
    for (n = bones; n > 0; n--) {
        memcpy(dst, src, (size_t)stride);
        dst += stride;
        src += stride + 8;
    }
}

/* 0x4200b4 */
void anim_reset(u8 *body)
{
    if (rd16(body) & 2) {
        ticks_update();
        wr16(body + 0x14, (u16)g_ticks);
        wr32(body + 0x10, 0);
    }
}

/* 0x41fb52 - pivot of bone `index` in the last transformed model points. */
void anim_hot_point(int index, const u8 *body, s16 *out)
{
    u16 flags = rd16(body);
    const u8 *p;
    int points, bones;

    if (flags & 2) {
        p = body + 0x0e;
        p += rd16(p);
        points = rd16(p + 2);
        p += points * 6;
        bones = rd16(p + 4);
        p += bones * 2 + 6;

        if ((u16)index < (u16)bones) {
            const u8 *bone = p + index * ((flags & 8) ? 0x18 : 0x10);
            const s16 *pt = r3d_model_points() + rd16(bone + 4) / 2;

            out[0] = pt[0];
            out[1] = pt[1];
            out[2] = pt[2];
            return;
        }
    }

    out[0] = out[1] = out[2] = 0;
}
