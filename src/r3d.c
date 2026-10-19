/*
 * r3d.c - the 3D library (0x4236da..0x424a18).
 *
 * Rotation steps all have the same shape in the original: two 16x16 signed
 * products are summed as a 32 bit value, the high word is kept and shifted
 * left by one. That drops bit 15 of the result, so it is reproduced exactly
 * rather than written as a >> 15. An axis whose angle is zero is skipped
 * outright, which matters because sin(0) in the table is 4, not 0.
 */

#include <string.h>

#include "aitd.h"
#include "exe_tables.h"
#include "game.h"
#include "r3d.h"
#include "raster.h"
#include "texture.h"

#define R3D_MAX_POINTS 400
#define R3D_MAX_PRIMS 400            /* 0x42488e clamps the draw loop */
#define R3D_STASH_BYTES 0x1f40       /* 0x436988..0x4388c8 */
#define R3D_INVALID ((s16)0xd8f0)    /* -10000 */

struct r3d_axis {
    s16 cos_a, sin_a;
    int active;   /* 0xf8 in the flag byte; 0xf9 means skip */
};

struct r3d_bbox g_r3d_bbox;
s16 g_r3d_x, g_r3d_y, g_r3d_z;
s16 g_r3d_screen_x, g_r3d_screen_y;
s16 g_r3d_hide_box[6];

/* Camera, 0x435468.. and its copies. */
static s16 s_cam_x, s_cam_y, s_cam_z;
static struct r3d_axis s_cam_alpha, s_cam_beta, s_cam_gamma; /* 0x439a6d..f */
static s16 s_cam_angles[3];   /* 0x435488.. */

/* Projection, 0x435484.. */
static s16 s_centre_x, s_centre_y, s_z_offset, s_focal_x, s_focal_y;

static s16 s_clip_y = 10000;  /* 0x439a12 / 0x439a42 */

/* Point rotation matrix for r3d_rotate_point (0x4399da..0x4399f0). */
static struct r3d_axis s_pt_alpha, s_pt_beta, s_pt_gamma;

/* Bone rotation matrix (0x4399f2..0x439a08, flags 0x439a6a..c). */
static struct r3d_axis s_bone_alpha, s_bone_beta, s_bone_gamma;

static s16 s_model[R3D_MAX_POINTS * 3];      /* 0x436028 */
static s16 s_projected[R3D_MAX_POINTS * 3];  /* 0x4356c8 */
static int s_point_count;                    /* 0x439904 */

/* Time Gate bodies (TG320.EX_ 0x4228a9). Flag 0x80: a table of (u, v)
 * corner triples and two texture pages. Flag 0x40: the last points are
 * light normals, one per vertex from s_normal_offset (bytes) on; they
 * follow their vertex's bone rotations and pivots and are never clipped. */
static s16 s_camera[R3D_MAX_POINTS * 3];     /* TG 0x43c4b0, camera space */
static const u8 *s_uv_table;                 /* TG 0x4422a0 */
static struct tex_page s_pages[2];           /* TG 0x45c4d0, 0x45c4cc */
static int s_normal_count;                   /* TG 0x4422b2 */
static int s_normal_offset;                  /* TG 0x4422ae */
static s32 s_light_dir[3], s_light_pos[3];   /* TG 0x43c333.., 0x451d54.. */

/* Object being drawn, 0x4398ec..0x439900. */
static s16 s_obj_x, s_obj_y, s_obj_z, s_obj_alpha, s_obj_beta, s_obj_gamma;
static int s_rejected;  /* 0x4354bc */

struct r3d_sort_entry {
    s16 depth;
    s16 depth2;
    s16 type;
    const u8 *record;
};

static u8 s_stash[R3D_STASH_BYTES];
static struct r3d_sort_entry s_sort[R3D_MAX_PRIMS + 1];
static int s_sort_count;  /* 0x439906 */

static u16 rd16(const u8 *p)
{
    return (u16)(p[0] | (p[1] << 8));
}

static s16 rds16(const u8 *p)
{
    return (s16)rd16(p);
}

static void wr16(u8 *p, s16 v)
{
    p[0] = (u8)v;
    p[1] = (u8)((u16)v >> 8);
}

static s16 table_sin(int angle)
{
    return g_sin_table[angle & 0x3ff];
}

static s16 table_cos(int angle)
{
    return g_sin_table[(angle + 0x100) & 0x3ff];
}

static void axis_set(struct r3d_axis *axis, s16 angle, int skip_zero)
{
    int a = angle & 0x3ff;

    axis->cos_a = table_cos(a);
    axis->sin_a = table_sin(a);
    axis->active = !(skip_zero && a == 0);
}

/* High word of a 32 bit sum or difference, shifted left by one (the
 * "mov ax, di / shl ax, 1" tail of every rotation step). */
static s16 hi_shl(u32 value)
{
    return (s16)(u16)((u16)(value >> 16) << 1);
}

/* (u, v) -> (u cos - v sin, u sin + v cos) */
static void axis_rotate(const struct r3d_axis *axis, s16 *u, s16 *v)
{
    u32 uc = (u32)((s32)*u * axis->cos_a);
    u32 vs = (u32)((s32)*v * axis->sin_a);
    u32 us = (u32)((s32)*u * axis->sin_a);
    u32 vc = (u32)((s32)*v * axis->cos_a);

    *u = hi_shl(uc - vs);
    *v = hi_shl(us + vc);
}

/* Order used by every rotation in the library: beta on (x, z), then alpha
 * on (y, z), then gamma on (x, y). */
static void rotate3(const struct r3d_axis *alpha, const struct r3d_axis *beta,
                    const struct r3d_axis *gamma, s16 *x, s16 *y, s16 *z)
{
    if (beta->active) {
        axis_rotate(beta, x, z);
    }
    if (alpha->active) {
        axis_rotate(alpha, y, z);
    }
    if (gamma->active) {
        axis_rotate(gamma, x, y);
    }
}

/* ---- camera ------------------------------------------------------------ */

/* 0x424590 */
void r3d_set_camera_angles(s16 alpha, s16 beta, s16 gamma)
{
    s_cam_angles[0] = (s16)(alpha & 0x3ff);
    s_cam_angles[1] = (s16)(beta & 0x3ff);
    s_cam_angles[2] = (s16)(gamma & 0x3ff);
    axis_set(&s_cam_alpha, alpha, 1);
    axis_set(&s_cam_beta, beta, 1);
    axis_set(&s_cam_gamma, gamma, 1);
}

/* 0x42454b */
void r3d_set_camera_position(s16 x, s16 y, s16 z)
{
    s_cam_x = x;
    s_cam_y = y;
    s_cam_z = z;
}

void r3d_get_camera_position(s16 *x, s16 *y, s16 *z)
{
    *x = s_cam_x;
    *y = s_cam_y;
    *z = s_cam_z;
}

/* 0x4244be */
void r3d_set_projection(s16 centre_x, s16 centre_y, s16 z_offset,
                        s16 focal_x, s16 focal_y)
{
    s_centre_x = centre_x;
    s_centre_y = centre_y;
    s_z_offset = z_offset;
    s_focal_x = focal_x;
    s_focal_y = focal_y;
}

/* 0x42419a */
void r3d_set_clip_y(s16 y)
{
    s_clip_y = y;
}

/* 0x424425 - each product is rounded separately here. */
static void rotate_2d(s16 x, s16 y, s16 angle, s16 *out_x, s16 *out_y)
{
    s16 c, s;
    s16 a, b, d, e;

    if (angle == 0) {
        *out_x = x;
        *out_y = y;
        return;
    }

    c = table_cos(angle);
    s = table_sin(angle);

    a = (s16)(((s32)x * c) >> 15);
    b = (s16)(((s32)y * s) >> 15);
    d = (s16)(((s32)x * s) >> 15);
    e = (s16)(((s32)y * c) >> 15);

    *out_x = (s16)(a - b);
    *out_y = (s16)(d + e);
}

/* 0x42428b */
void r3d_rotate_2d(s16 x, s16 y, s16 angle)
{
    rotate_2d(x, y, angle, &g_r3d_x, &g_r3d_y);
}

/* 0x42469c */
void r3d_set_camera_target(s16 x, s16 y, s16 z, s16 alpha, s16 beta,
                           s16 gamma, s16 distance)
{
    s16 ax, dx, ax2, dx2;

    rotate_2d((s16)-distance, 0, (s16)(alpha + 0x200), &ax, &dx);
    rotate_2d(ax, 0, (s16)(beta + 0x200), &ax2, &dx2);

    r3d_set_camera_position((s16)(dx2 + x), (s16)(-dx + y), (s16)(ax2 + z));
    r3d_set_camera_angles(alpha, beta, gamma);
}

/* 0x4242f3 */
static void camera_rotate(s16 *x, s16 *y, s16 *z)
{
    rotate3(&s_cam_alpha, &s_cam_beta, &s_cam_gamma, x, y, z);
}

/* 0x4241af */
void r3d_world_to_camera(s16 x, s16 y, s16 z)
{
    x = (s16)(x - s_cam_x);
    y = (s16)(y - s_cam_y);
    z = (s16)(z - s_cam_z);
    camera_rotate(&x, &y, &z);
    g_r3d_x = x;
    g_r3d_y = y;
    g_r3d_z = z;
}

/* 0x424236 - 16 bit divide, unlike the body projection. */
void r3d_project(s16 x, s16 y, s16 z)
{
    s16 d = (s16)(z + s_z_offset);

    if (d == 0) {
        return;
    }
    g_r3d_screen_x = (s16)((s16)(((s32)x * s_focal_x) / d) + s_centre_x);
    g_r3d_screen_y = (s16)((s16)(((s32)y * s_focal_y) / d) + s_centre_y);
}

/* 0x424a18 */
void r3d_rotate_point(s16 x, s16 y, s16 z, s16 alpha, s16 beta, s16 gamma)
{
    if (alpha == 0 && beta == 0 && gamma == 0) {
        g_r3d_x = x;
        g_r3d_y = y;
        g_r3d_z = z;
        return;
    }

    /* 0x4237f9 sets every axis unconditionally. */
    axis_set(&s_pt_alpha, alpha, 0);
    axis_set(&s_pt_beta, beta, 0);
    axis_set(&s_pt_gamma, gamma, 0);
    rotate3(&s_pt_alpha, &s_pt_beta, &s_pt_gamma, &x, &y, &z);

    g_r3d_x = x;
    g_r3d_y = y;
    g_r3d_z = z;
}

/* ---- body transform ---------------------------------------------------- */

/* 0x4238be - rotate count points in place with the bone matrix. */
static void bone_rotate_points(s16 *points, int count)
{
    while (count-- > 0) {
        rotate3(&s_bone_alpha, &s_bone_beta, &s_bone_gamma,
                &points[0], &points[1], &points[2]);
        points += 3;
    }
}

/* The normals that mirror the group of n points at byte offset `offset`,
 * clamped to the model. */
static s16 *normal_group(u16 offset, int *n)
{
    int first = (offset + s_normal_offset) / 6;

    if (first >= s_point_count) {
        *n = 0;
    } else if (first + *n > s_point_count) {
        *n = s_point_count - first;
    }
    return &s_model[first * 3];
}

/* 0x423aee - rotates a bone's points, then every later bone whose parent is
 * this bone, recursively, all with the same matrix. */
static void bone_rotate_tree(const u8 *bone, int bone_count)
{
    int index = bone[7];
    int remaining;
    const u8 *p = bone;

    bone_rotate_points(&s_model[rd16(bone) / 2], rds16(bone + 2));
    if (s_normal_count > 0) {   /* TG 0x421cd8 */
        int n = rds16(bone + 2);
        s16 *normals = normal_group(rd16(bone), &n);
        bone_rotate_points(normals, n);
    }

    for (remaining = bone_count - index; remaining > 0; remaining--) {
        if (p[6] == index) {
            bone_rotate_tree(p, bone_count);
        }
        p += 0x10;
    }
}

/* Projects the camera relative points in s_projected, 0x423d6f / 0x4240e1. */
static void project_point(s16 *out, s16 x, s16 y, s16 z, s16 min_z)
{
    s16 d = (s16)(z + s_z_offset);
    s32 sx, sy;

    if (d <= min_z) {
        out[0] = out[1] = out[2] = R3D_INVALID;
        return;
    }

    sx = ((s32)(s16)(u16)(s_focal_x) * x) / d;
    sy = ((s32)(s16)(u16)(s_focal_y) * y) / d;
    /* INDARK.EXE projects with a 16 bit idiv (0x157b3), which cannot
     * return a quotient outside a word. WAITD3 divides in 32 bits and keeps
     * the low word, so a point beside the camera at the near plane wrapped
     * to the other side of the screen; the seated zombie of floor 1 then
     * filled the corridor view. Such a point is dropped instead. */
    if (game_current()->life_dialect == LIFE_DIALECT_AITD1
        && (sx < -32768 || sx > 32767 || sy < -32768 || sy > 32767)) {
        out[0] = out[1] = out[2] = R3D_INVALID;
        return;
    }
    sx = (s16)((s16)sx + s_centre_x);
    sy = (s16)((s16)sy + s_centre_y);

    if (sx < g_r3d_bbox.xmin) g_r3d_bbox.xmin = (s16)sx;
    if (sx > g_r3d_bbox.xmax) g_r3d_bbox.xmax = (s16)sx;
    if (sy < g_r3d_bbox.ymin) g_r3d_bbox.ymin = (s16)sy;
    if (sy > g_r3d_bbox.ymax) g_r3d_bbox.ymax = (s16)sy;

    out[0] = (s16)sx;
    out[1] = (s16)sy;
    out[2] = d;
}

/* 0x423cff..0x423d6f: s_model to world, clip at s_clip_y, camera, then
 * project (z <= 50 is dropped). */
static void project_model(s16 rel_x, s16 rel_y, s16 rel_z)
{
    int regular = s_point_count - s_normal_count;
    int i;

    for (i = 0; i < s_point_count; i++) {
        s16 x = (s16)(s_model[i * 3] + rel_x);
        s16 y = (s16)(s_model[i * 3 + 1] + rel_y);
        s16 z;

        if (y > s_clip_y && i < regular) {
            s_projected[i * 3] = s_projected[i * 3 + 1] = s_projected[i * 3 + 2] = R3D_INVALID;
            s_camera[i * 3] = s_camera[i * 3 + 1] = s_camera[i * 3 + 2] = R3D_INVALID;
            continue;
        }
        y = (s16)(y - s_cam_y);
        z = (s16)(s_model[i * 3 + 2] + rel_z);
        camera_rotate(&x, &y, &z);
        s_projected[i * 3] = s_camera[i * 3] = x;
        s_projected[i * 3 + 1] = s_camera[i * 3 + 1] = y;
        s_projected[i * 3 + 2] = s_camera[i * 3 + 2] = z;
    }

    /* 0x423d6f: project, z <= 50 is dropped. */
    for (i = 0; i < s_point_count; i++) {
        s16 *pt = &s_projected[i * 3];
        project_point(pt, pt[0], pt[1], pt[2], 50);
    }

}

/*
 * 0x423ba9 - animated body. body points just past the extra-data block.
 * Returns the pointer to the primitive list.
 */
static const u8 *transform_animated(u8 *body)
{
    u8 *p = body;
    u8 *bones;
    int bone_count;
    int i;
    s16 rel_x, rel_y, rel_z;
    u16 bone_offsets[64];

    rel_x = (s16)(s_obj_x - s_cam_x);
    rel_y = s_obj_y;
    rel_z = (s16)(s_obj_z - s_cam_z);

    s_point_count = rds16(p);
    p += 2;
    if (s_point_count > R3D_MAX_POINTS) {
        s_point_count = R3D_MAX_POINTS;
    }
    for (i = 0; i < s_point_count * 3; i++) {
        s_model[i] = rds16(p + i * 2);
    }
    p += rds16(body) * 6;

    bone_count = rds16(p);
    p += 2;
    for (i = 0; i < bone_count && i < 64; i++) {
        bone_offsets[i] = rd16(p + i * 2);
    }
    p += bone_count * 2;
    bones = p;

    /* 0x423c1b: the object's own angles become the root bone's rotation. */
    wr16(bones + 0x0a, s_obj_alpha);
    wr16(bones + 0x0c, s_obj_beta);
    wr16(bones + 0x0e, s_obj_gamma);

    for (i = 0; i < bone_count && i < 64; i++) {
        u8 *bone = bones + bone_offsets[i];
        s16 a = rds16(bone + 0x0a), b = rds16(bone + 0x0c), c = rds16(bone + 0x0e);

        if ((a | b | c) == 0 || rds16(bone + 2) == 0) {
            continue;
        }

        switch (rds16(bone + 8)) {
        case BONE_ROTATE:
            axis_set(&s_bone_alpha, a, 1);
            axis_set(&s_bone_beta, b, 1);
            axis_set(&s_bone_gamma, c, 1);
            bone_rotate_tree(bone, bone_count);
            break;
        case BONE_TRANSLATE: {
            s16 *pt = &s_model[rd16(bone) / 2];
            int n = rds16(bone + 2);
            while (n-- > 0) {
                pt[0] = (s16)(pt[0] + a);
                pt[1] = (s16)(pt[1] + b);
                pt[2] = (s16)(pt[2] + c);
                pt += 3;
            }
            break;
        }
        case BONE_ZOOM: {
            /* 0x423b55: factor is param + 256, keep bits 8..23. */
            s16 *pt = &s_model[rd16(bone) / 2];
            int n = rds16(bone + 2);
            s16 fa = (s16)(a + 0x100), fb = (s16)(b + 0x100), fc = (s16)(c + 0x100);
            while (n-- > 0) {
                pt[0] = (s16)(((s32)pt[0] * fa) >> 8);
                pt[1] = (s16)(((s32)pt[1] * fb) >> 8);
                pt[2] = (s16)(((s32)pt[2] * fc) >> 8);
                pt += 3;
            }
            break;
        }
        default:
            break;
        }
    }

    /* 0x423ca8: move every bone to its pivot, in raw record order. */
    for (i = 0; i < bone_count; i++) {
        const u8 *bone = bones + i * 0x10;
        const s16 *pivot = &s_model[rd16(bone + 4) / 2];
        s16 *pt = &s_model[rd16(bone) / 2];
        s16 px = pivot[0], py = pivot[1], pz = pivot[2];
        int n = rds16(bone + 2);

        while (n-- > 0) {
            pt[0] = (s16)(pt[0] + px);
            pt[1] = (s16)(pt[1] + py);
            pt[2] = (s16)(pt[2] + pz);
            pt += 3;
        }
        if (s_normal_count > 0) {   /* TG 0x421f0d */
            n = rds16(bone + 2);
            pt = normal_group(rd16(bone), &n);
            while (n-- > 0) {
                pt[0] = (s16)(pt[0] + px);
                pt[1] = (s16)(pt[1] + py);
                pt[2] = (s16)(pt[2] + pz);
                pt += 3;
            }
        }
    }

    project_model(rel_x, rel_y, rel_z);

    return bones + bone_count * 0x10;
}

/*
 * Bodies with flag 8 (Jack in the Dark) keep 24 byte group records: the
 * usual 16, then a rotation (x, y, z) and a pad word, and keyframes carry
 * both. WAITD3.EXE has no such bodies and no code for them; worked out
 * from the data: each group in table order is translated or zoomed,
 * rotated about its own origin by its rotation, then every group is moved
 * to its parent's vertex and the whole model takes the object's angles.
 */
static const u8 *transform_optimised(u8 *body)
{
    u8 *p = body;
    u8 *bones;
    int bone_count;
    int i;
    u16 bone_offsets[64];

    s_point_count = rds16(p);
    p += 2;
    if (s_point_count > R3D_MAX_POINTS) {
        s_point_count = R3D_MAX_POINTS;
    }
    for (i = 0; i < s_point_count * 3; i++) {
        s_model[i] = rds16(p + i * 2);
    }
    p += rds16(body) * 6;

    bone_count = rds16(p);
    p += 2;
    for (i = 0; i < bone_count && i < 64; i++) {
        bone_offsets[i] = rd16(p + i * 2);
    }
    p += bone_count * 2;
    bones = p;

    for (i = 0; i < bone_count && i < 64; i++) {
        u8 *bone = bones + bone_offsets[i];
        s16 *pt = &s_model[rd16(bone) / 2];
        int n = rds16(bone + 2);
        s16 a = rds16(bone + 0x0a), b = rds16(bone + 0x0c), c = rds16(bone + 0x0e);
        int k;

        if (rds16(bone + 8) == BONE_TRANSLATE && (a | b | c)) {
            for (k = 0; k < n; k++) {
                pt[k * 3] = (s16)(pt[k * 3] + a);
                pt[k * 3 + 1] = (s16)(pt[k * 3 + 1] + b);
                pt[k * 3 + 2] = (s16)(pt[k * 3 + 2] + c);
            }
        } else if (rds16(bone + 8) == BONE_ZOOM && (a | b | c)) {
            for (k = 0; k < n; k++) {
                pt[k * 3] = (s16)(((s32)pt[k * 3] * (a + 256)) / 256);
                pt[k * 3 + 1] = (s16)(((s32)pt[k * 3 + 1] * (b + 256)) / 256);
                pt[k * 3 + 2] = (s16)(((s32)pt[k * 3 + 2] * (c + 256)) / 256);
            }
        }
        if (bone[7] != 0) {
            axis_set(&s_bone_alpha, rds16(bone + 0x10), 1);
            axis_set(&s_bone_beta, rds16(bone + 0x12), 1);
            axis_set(&s_bone_gamma, rds16(bone + 0x14), 1);
            bone_rotate_points(pt, n);
        }
    }

    for (i = 0; i < bone_count; i++) {
        const u8 *bone = bones + i * 0x18;
        const s16 *pivot = &s_model[rd16(bone + 4) / 2];
        s16 *pt = &s_model[rd16(bone) / 2];
        s16 px = pivot[0], py = pivot[1], pz = pivot[2];
        int n = rds16(bone + 2);

        while (n-- > 0) {
            pt[0] = (s16)(pt[0] + px);
            pt[1] = (s16)(pt[1] + py);
            pt[2] = (s16)(pt[2] + pz);
            pt += 3;
        }
    }

    axis_set(&s_bone_alpha, s_obj_alpha, 1);
    axis_set(&s_bone_beta, s_obj_beta, 1);
    axis_set(&s_bone_gamma, s_obj_gamma, 1);
    bone_rotate_points(s_model, s_point_count);

    project_model((s16)(s_obj_x - s_cam_x), s_obj_y, (s16)(s_obj_z - s_cam_z));
    return bones + bone_count * 0x18;
}

/* 0x423e59 - static body. Returns NULL when a point falls behind the eye. */
static const u8 *transform_static(const u8 *body)
{
    const u8 *p = body;
    struct r3d_axis alpha, beta, gamma;
    int rotate = (s_obj_alpha | s_obj_beta | s_obj_gamma) != 0;
    s16 rel_x = (s16)(s_obj_x - s_cam_x);
    s16 rel_y = s_obj_y;
    s16 rel_z = (s16)(s_obj_z - s_cam_z);
    int i;

    if (rotate) {
        axis_set(&alpha, s_obj_alpha, 0);
        axis_set(&beta, s_obj_beta, 0);
        axis_set(&gamma, s_obj_gamma, 0);
    }

    s_point_count = rds16(p);
    p += 2;
    if (s_point_count > R3D_MAX_POINTS) {
        s_point_count = R3D_MAX_POINTS;
    }

    for (i = 0; i < s_point_count; i++) {
        s16 x = rds16(p), y = rds16(p + 2), z = rds16(p + 4);
        s16 *out = &s_projected[i * 3];

        p += 6;
        s_model[i * 3] = x;
        s_model[i * 3 + 1] = y;
        s_model[i * 3 + 2] = z;

        if (rotate) {
            rotate3(&alpha, &beta, &gamma, &x, &y, &z);
        }

        x = (s16)(x + rel_x);
        y = (s16)(y + rel_y);
        if (y > s_clip_y && i < s_point_count - s_normal_count) {
            out[0] = out[1] = out[2] = R3D_INVALID;
            s_camera[i * 3] = s_camera[i * 3 + 1] = s_camera[i * 3 + 2] = R3D_INVALID;
            continue;
        }
        y = (s16)(y - s_cam_y);
        z = (s16)(z + rel_z);
        camera_rotate(&x, &y, &z);
        s_camera[i * 3] = x;
        s_camera[i * 3 + 1] = y;
        s_camera[i * 3 + 2] = z;

        if ((s16)(z + s_z_offset) <= 0) {
            s_rejected = 2;
            return NULL;
        }
        project_point(out, x, y, z, 0);
    }

    return p;
}

/* Skips the 16 bit flags, the 12 byte box and the extra data block. */
static u8 *body_points(u8 *body)
{
    s16 extra = rds16(body + 0x0e);

    return body + 0x10 + extra;
}

static void set_object(s16 x, s16 y, s16 z, s16 alpha, s16 beta, s16 gamma)
{
    s_obj_x = x;
    s_obj_y = y;
    s_obj_z = z;
    s_obj_alpha = alpha;
    s_obj_beta = beta;
    s_obj_gamma = gamma;
}

/* ---- primitives -------------------------------------------------------- */

static int point_valid_z(const s16 *pt)
{
    return pt[2] > 0;
}

static void sort_push(s16 depth, s16 type, const u8 *record)
{
    if (s_sort_count >= R3D_MAX_PRIMS) {
        return;
    }
    s_sort[s_sort_count].depth = depth;
    s_sort[s_sort_count].depth2 = depth;
    s_sort[s_sort_count].type = type;
    s_sort[s_sort_count].record = record;
    s_sort_count++;
}

static const s16 *proj_at(u16 byte_offset)
{
    return &s_projected[byte_offset / 2];
}

/* TG 0x4214aa: the sign of the determinant of the three camera space
 * corners, each scaled down by 8, in 32 bit wrapping products. */
static int tg_front_facing(const s16 *a, const s16 *b, const s16 *c)
{
    u32 x0 = (u32)(s32)(a[0] >> 3), y0 = (u32)(s32)(a[1] >> 3), z0 = (u32)(s32)(a[2] >> 3);
    u32 x1 = (u32)(s32)(b[0] >> 3), y1 = (u32)(s32)(b[1] >> 3), z1 = (u32)(s32)(b[2] >> 3);
    u32 x2 = (u32)(s32)(c[0] >> 3), y2 = (u32)(s32)(c[1] >> 3), z2 = (u32)(s32)(c[2] >> 3);
    u32 d = (y1 * z2 - y2 * z1) * x0 - (y0 * z2 - y2 * z0) * x1 + (y0 * z1 - y1 * z0) * x2;

    return (s32)d >= 0;
}

/*
 * Time Gate primitives 8, 9 and 10 (TG 0x4213ec, 0x421620): a corner count
 * (always 3; the original reads three), a material word, three vertex
 * offsets and, for 9 and 10, three normal offsets. The material's low six
 * bits pick the page (any nonzero value: the second) and its upper ten the
 * UV triple; for 9 the high byte is the colour. Records keep the type, the
 * material and per corner x, y, u, v, light level.
 */
static const u8 *collect_textured(int type, const u8 *p, u8 *rec, u8 **stash)
{
    int n = *p++;
    u16 material = rd16(p);
    const u8 *vertex = p + 2;
    const u8 *normal = vertex + n * 2;
    const u8 *next = normal + (type == PRIM_TEXTURED ? 0 : n * 2);
    s16 select = (s16)(type == PRIM_GOURAUD ? material >> 8 : material & 0x3f);
    const u8 *uv = (s_uv_table != NULL && type != PRIM_GOURAUD)
                 ? s_uv_table + (material >> 6) * 12 : NULL;
    const s16 *cam[3];
    s16 zmax = (s16)0x8300;
    int k;

    if (n != 3) {
        return next;
    }
    for (k = 0; k < 3; k++) {
        const s16 *pt = proj_at(rd16(vertex + k * 2));
        const s16 *c = &s_camera[rd16(vertex + k * 2) / 2];

        if (pt[2] <= 0 || c[0] == R3D_INVALID) {   /* 0x4227cb */
            return next;
        }
        cam[k] = c;
        if (pt[2] > zmax) zmax = pt[2];
    }
    if (!tg_front_facing(cam[0], cam[1], cam[2])) {
        return next;
    }

    rec[0] = (u8)type;
    rec[1] = (u8)material;
    rec[2] = (u8)(material >> 8);
    rec[3] = 0;
    for (k = 0; k < 3; k++) {
        const s16 *pt = proj_at(rd16(vertex + k * 2));
        u8 *out = rec + 4 + k * 10;
        s16 level = 0;

        wr16(out, pt[0]);
        wr16(out + 2, pt[1]);
        wr16(out + 4, uv != NULL ? rds16(uv + k * 4) : 0);
        wr16(out + 6, uv != NULL ? rds16(uv + k * 4 + 2) : 0);
        if (type != PRIM_TEXTURED) {
            /* 0x412e4f: the level, with the selector's upper nibble. */
            level = texture_light_level(&s_camera[rd16(normal + k * 2) / 2],
                                        s_light_dir, s_light_pos);
            level = (s16)(level | (select & 0xf0));
        }
        wr16(out + 8, level);
    }
    sort_push(zmax, PRIM_TEXTURED, rec);
    *stash = rec + 4 + 3 * 10;
    return next;
}

/*
 * 0x4247be..0x4247de - reads the primitive list and stashes everything that
 * survives depth and backface tests. Records hold the primitive's header
 * bytes and the projected points.
 */
static void collect_primitives(const u8 *p)
{
    u8 *stash = s_stash;
    u8 *stash_end = s_stash + sizeof(s_stash) - 64;
    int count = rds16(p);

    p += 2;

    while (count-- > 0 && stash < stash_end) {
        int type = *p++;
        u8 *rec = stash;

        switch (type) {
        case PRIM_LINE: { /* 0x42328d */
            const s16 *a, *b;
            s16 za, zb;

            rec[0] = p[0];
            rec[1] = p[1];
            a = proj_at(rd16(p + 3));
            b = proj_at(rd16(p + 5));
            p += 7;
            wr16(rec + 2, a[0]);
            wr16(rec + 4, a[1]);
            wr16(rec + 6, b[0]);
            wr16(rec + 8, b[1]);
            za = a[2];
            zb = b[2];
            if (za > zb) { s16 t = za; za = zb; zb = t; }
            if (za > 0) {
                sort_push(zb, 0, rec);
                stash += 10;
            }
            break;
        }
        case PRIM_TEXTURED:
        case PRIM_GOURAUD:
        case PRIM_TEXTURED_LIT:
            p = collect_textured(type, p, rec, &stash);
            break;
        case PRIM_POLY: /* 0x4231b8 */ {
            int n = *p++;
            s16 zmin = 0x7d00, zmax = (s16)0x8300;
            u8 *pts;
            int k;

            rec[0] = (u8)n;
            rec[1] = p[0];
            rec[2] = p[1];
            rec[3] = 0;
            p += 2;
            pts = rec + 4;
            for (k = 0; k < n; k++) {
                const s16 *pt = proj_at(rd16(p));
                p += 2;
                wr16(pts + k * 4, pt[0]);
                wr16(pts + k * 4 + 2, pt[1]);
                if (pt[2] < zmin) zmin = pt[2];
                if (pt[2] > zmax) zmax = pt[2];
            }
            if (zmin > 0 && n >= 3) {
                /* 0x423216: screen space winding of the first three points. */
                s16 x0 = rds16(pts), y0 = rds16(pts + 2);
                s16 x1 = rds16(pts + 4), y1 = rds16(pts + 6);
                s16 x2 = rds16(pts + 8), y2 = rds16(pts + 10);
                s32 a = (s32)(s16)(x1 - x0) * (s16)(y0 - y2);
                s32 b = (s32)(s16)(x0 - x2) * (s16)(y1 - y0);

                if (b - a >= 0) {
                    sort_push(zmax, 1, rec);
                    stash = pts + n * 4;
                }
            } else if (zmin > 0) {
                sort_push(zmax, 1, rec);
                stash = pts + n * 4;
            }
            break;
        }
        case PRIM_POINT: { /* 0x42338a */
            const s16 *pt;

            rec[0] = p[0];
            rec[1] = p[1];
            pt = proj_at(rd16(p + 3));
            p += 5;
            wr16(rec + 2, pt[0]);
            wr16(rec + 4, pt[1]);
            wr16(rec + 6, pt[2]);
            if (point_valid_z(pt)) {
                sort_push(pt[2], 2, rec);
                stash += 8;
            }
            break;
        }
        case PRIM_SPHERE: { /* 0x42331d */
            const s16 *pt;

            rec[0] = p[0];
            rec[1] = p[1];
            rec[2] = p[3];
            rec[3] = p[4];
            pt = proj_at(rd16(p + 5));
            p += 7;
            wr16(rec + 4, pt[0]);
            wr16(rec + 6, pt[1]);
            wr16(rec + 8, pt[2]);
            if (point_valid_z(pt)) {
                sort_push(pt[2], 3, rec);
                stash += 10;
            }
            break;
        }
        case PRIM_BIG_POINT:
        case PRIM_ZIXEL: { /* 0x4233f7 big point / zixel */
            u16 off = rd16(p + 3);
            const s16 *model = &s_model[off / 2];
            const s16 *pt = proj_at(off);
            s16 wx = (s16)(model[0] + s_obj_x);
            s16 wy = (s16)(model[1] + s_obj_y);
            s16 wz = (s16)(model[2] + s_obj_z);
            int hidden = wx > g_r3d_hide_box[0] && wx < g_r3d_hide_box[1]
                      && wy > g_r3d_hide_box[2] && wy < g_r3d_hide_box[3]
                      && wz > g_r3d_hide_box[4] && wz < g_r3d_hide_box[5];

            rec[0] = p[0];
            rec[1] = p[1];
            p += 5;
            if (hidden) {
                break;
            }
            wr16(rec + 2, pt[0]);
            wr16(rec + 4, pt[1]);
            wr16(rec + 6, pt[2]);
            if (point_valid_z(pt)) {
                sort_push(pt[2], (s16)type, rec);
                stash += 8;
            }
            break;
        }
        default:
            /* 0x4234c9: types 4 and 5 consume nothing and draw nothing. */
            break;
        }
    }
}

/* 0x4247e0 - selection sort, deepest first; ties keep their order. */
static void sort_primitives(void)
{
    int i, j;

    for (i = 0; i + 1 < s_sort_count; i++) {
        int best = -1;
        s16 depth = s_sort[i].depth;

        for (j = i + 1; j < s_sort_count; j++) {
            if (s_sort[j].depth > depth) {
                best = j;
                depth = s_sort[j].depth;
            } else if (s_sort[j].depth == depth) {
                const struct r3d_sort_entry *ref = (best >= 0) ? &s_sort[best] : &s_sort[i];
                if (ref->depth2 < s_sort[j].depth2) {
                    best = j;
                }
            }
        }
        if (best >= 0) {
            struct r3d_sort_entry t = s_sort[i];
            s_sort[i] = s_sort[best];
            s_sort[best] = t;
        }
    }
}

/* 0x423518 - 32 point circle, then a polygon. */
static void draw_sphere(const u8 *rec)
{
    u8 type = rec[0];
    u8 colour = rec[1];
    s16 size = rds16(rec + 2);
    s16 cx = rds16(rec + 4), cy = rds16(rec + 6), z = rds16(rec + 8);
    s16 rx, ry;
    s16 pts[64];
    int i, angle;

    if (z == 0) {
        return;
    }
    rx = (s16)(((s32)size * s_focal_x) / z);
    ry = (s16)(((s32)size * s_focal_y) / z);

    for (i = 0, angle = 0; i < 32; i++, angle += 0x20) {
        s16 x = (s16)((s16)(((s32)table_sin(angle) * (s16)rx) >> 15) + cx);
        s16 y = (s16)((s16)(((s32)table_cos(angle) * (s16)ry) >> 15) + cy);

        if (x < g_r3d_bbox.xmin) g_r3d_bbox.xmin = x;
        if (x > g_r3d_bbox.xmax) g_r3d_bbox.xmax = x;
        if (y < g_r3d_bbox.ymin) g_r3d_bbox.ymin = y;
        if (y > g_r3d_bbox.ymax) g_r3d_bbox.ymax = y;
        pts[i * 2] = x;
        pts[i * 2 + 1] = y;
    }

    raster_poly(pts, 32, type, colour);
}

static void draw_primitives(void)
{
    int count = s_sort_count;
    int i;

    if (count > R3D_MAX_PRIMS) {
        count = R3D_MAX_PRIMS;
    }

    for (i = 0; i < count; i++) {
        const u8 *rec = s_sort[i].record;

        switch (s_sort[i].type) {
        case PRIM_LINE: /* 0x4234f3 */
            raster_line(rds16(rec + 2), rds16(rec + 4), rds16(rec + 6),
                        rds16(rec + 8), rec[1]);
            break;
        case PRIM_POLY:
        case PRIM_POLY_ALT: { /* 0x4234ce */
            s16 pts[2 * 256];
            int n = rec[0], k;
            for (k = 0; k < n; k++) {
                pts[k * 2] = rds16(rec + 4 + k * 4);
                pts[k * 2 + 1] = rds16(rec + 6 + k * 4);
            }
            raster_poly(pts, n, rec[1], rec[2]);
            break;
        }
        case PRIM_TEXTURED: { /* TG 0x4218bc, 0x42197c, 0x4219c4 */
            struct tex_vertex v[3];
            int type = rec[0], k;
            enum tex_fill fill = type == PRIM_TEXTURED ? TEX_FILL_TEXTURE
                               : type == PRIM_GOURAUD ? TEX_FILL_GOURAUD
                               : TEX_FILL_TEXTURE_LIT;

            for (k = 0; k < 3; k++) {
                const u8 *in = rec + 4 + k * 10;
                v[k].x = rds16(in);
                v[k].y = rds16(in + 2);
                v[k].u = rds16(in + 4);
                v[k].v = rds16(in + 6);
                v[k].level = rds16(in + 8);
            }
            texture_triangle(v, fill, &s_pages[(rec[1] & 0x3f) != 0], rec[2]);
            break;
        }
        case PRIM_POINT: /* 0x423635 */
            raster_point(rds16(rec + 2), rds16(rec + 4), rec[1]);
            break;
        case PRIM_SPHERE:
            draw_sphere(rec);
            break;
        case PRIM_BIG_POINT: { /* 0x42364f */
            s16 x = rds16(rec + 2), y = rds16(rec + 4);
            raster_point(x, y, rec[1]);
            raster_point((s16)(x + 1), y, rec[1]);
            raster_point((s16)(x + 1), (s16)(y + 1), rec[1]);
            raster_point(x, (s16)(y + 1), rec[1]);
            break;
        }
        case PRIM_ZIXEL: { /* 0x42369d */
            s16 x = rds16(rec + 2), y = rds16(rec + 4), z = rds16(rec + 6);
            s16 r = (z != 0) ? (s16)((0x40 * (s32)s_focal_x) / z) : 0;
            raster_rect((s16)(x - r), (s16)(x + r), (s16)(y - r), (s16)(y + r), rec[1]);
            break;
        }
        default:
            break;
        }
    }
}

static void bbox_reset(s16 lo, s16 hi)
{
    g_r3d_bbox.xmin = lo;
    g_r3d_bbox.ymin = lo;
    g_r3d_bbox.xmax = hi;
    g_r3d_bbox.ymax = hi;
}

/* TG 0x4228a9: the extra block of a Time Gate body. Animated bodies keep
 * the UV table offset at +6, the normals at +8 (count) and +0x0a (byte
 * offset) and the pages at +0x0c, +0x0e; static ones at +0, +2, +4 and
 * +6, +8, and only when the block is there. */
static void body_extras(const u8 *body, int load_pages)
{
    u16 flags = rd16(body);
    const u8 *extra = body + 0x10;
    int animated = (flags & 2) != 0;

    s_uv_table = NULL;
    s_normal_count = 0;
    s_normal_offset = 0;
    if (!(flags & (BODY_TEXTURED | BODY_NORMALS)) || (!animated && rds16(body + 0x0e) == 0)) {
        return;
    }
    if ((flags & BODY_TEXTURED) && load_pages) {
        s_uv_table = body + rd16(extra + (animated ? 6 : 0));
        s_pages[0] = texture_page(rd16(extra + (animated ? 0x0c : 6)), NULL);
        s_pages[1] = texture_page(rd16(extra + (animated ? 0x0e : 8)), s_pages[0].texels);
    }
    if (flags & BODY_NORMALS) {
        s_normal_count = rd16(extra + (animated ? 8 : 2));
        s_normal_offset = rd16(extra + (animated ? 0x0a : 4));
    }
}

/* A light value as the original leaves it: the 32 bit global holds the
 * sign extended world value, and the camera transform (TG 0x40c787) then
 * stores only its low word. */
static s32 light_value(s16 world, s16 camera)
{
    return (s32)(((world < 0 ? 0xffff0000u : 0u)) | (u16)camera);
}

/* TG 0x40cebf: both vectors go through the camera transform (0x422256),
 * the direction included. */
void r3d_set_light(const s16 *dir, const s16 *pos)
{
    r3d_world_to_camera(dir[0], dir[1], dir[2]);
    s_light_dir[0] = light_value(dir[0], g_r3d_x);
    s_light_dir[1] = light_value(dir[1], g_r3d_y);
    s_light_dir[2] = light_value(dir[2], g_r3d_z);
    r3d_world_to_camera(pos[0], pos[1], pos[2]);
    s_light_pos[0] = light_value(pos[0], g_r3d_x);
    s_light_pos[1] = light_value(pos[1], g_r3d_y);
    s_light_pos[2] = light_value(pos[2], g_r3d_z);
}

/* 0x424710 */
int r3d_draw_body(s16 x, s16 y, s16 z, s16 alpha, s16 beta, s16 gamma,
                  u8 *body)
{
    u16 flags = rd16(body);
    const u8 *prims;

    set_object(x, y, z, alpha, beta, gamma);
    bbox_reset(0x7fff, (s16)-0x7fff);
    s_rejected = 0;
    s_sort_count = 0;
    body_extras(body, 1);

    if ((flags & 2) && (flags & 8)) {
        prims = transform_optimised(body_points(body));
    } else if (flags & 2) {
        prims = transform_animated(body_points(body));
    } else if (!(flags & 4)) {
        prims = transform_static(body_points(body));
    } else {
        prims = NULL;
    }

    if (prims == NULL) {
        bbox_reset(0x7d00, (s16)0x8300);  /* 0x4248e5 */
        return 2;
    }

    collect_primitives(prims);

    if (s_sort_count == 0) {
        bbox_reset(0x7d00, (s16)0x8300);  /* 0x4248bc */
        return 1;
    }

    if (flags & 1) {
        sort_primitives();
    }
    draw_primitives();
    return 0;
}

/* 0x42497a */
void r3d_measure_body(s16 x, s16 y, s16 z, s16 alpha, s16 beta, s16 gamma,
                      u8 *body)
{
    set_object(x, y, z, alpha, beta, gamma);
    bbox_reset(0x7d00, (s16)0x8300);
    s_rejected = 0;
    s_sort_count = 0;
    body_extras(body, 0);

    if ((rd16(body) & 0xa) == 0xa) {
        transform_optimised(body_points(body));
    } else if (rd16(body) & 2) {
        transform_animated(body_points(body));
    }
}

/* 0x42490e - the blob is a point count and points, no body header.
 * Returns 0 when a point fell behind the eye. */
int r3d_transform_points(s16 x, s16 y, s16 z, s16 alpha, s16 beta, s16 gamma,
                         const u8 *points)
{
    set_object(x, y, z, alpha, beta, gamma);
    bbox_reset(0x7d00, (s16)0x8300);
    s_rejected = 0;
    return transform_static(points) != NULL;
}

/* 0x4227f4 */
void r3d_points_bounds(s16 *zv)
{
    int i;

    zv[0] = zv[2] = zv[4] = 0x7d00;
    zv[1] = zv[3] = zv[5] = (s16)-0x7d00;

    for (i = 0; i < s_point_count; i++) {
        const s16 *pt = &s_model[i * 3];

        if (zv[0] > pt[0]) zv[0] = pt[0];
        if (zv[1] < pt[0]) zv[1] = pt[0];
        if (zv[2] > pt[1]) zv[2] = pt[1];
        if (zv[3] < pt[1]) zv[3] = pt[1];
        if (zv[4] > pt[2]) zv[4] = pt[2];
        if (zv[5] < pt[2]) zv[5] = pt[2];
    }
}

const s16 *r3d_model_points(void)
{
    return s_model;
}

const s16 *r3d_projected_points(void)
{
    return s_projected;
}
