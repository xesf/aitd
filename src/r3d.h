/*
 * r3d.h - the 3D library: camera, rotations, projection and body rendering
 * (0x4236da..0x424a18, hand written assembly in the original).
 *
 * Angles are 10 bit (1024 per turn). Coordinates are s16 throughout.
 */

#ifndef AITD_R3D_H
#define AITD_R3D_H

#include "aitd.h"

/* Primitive types in a LISTBODY body and in the depth sort. */
enum prim_type {
    PRIM_LINE = 0,
    PRIM_POLY = 1,
    PRIM_POINT = 2,
    PRIM_SPHERE = 3,
    PRIM_POLY_ALT = 4,          /* drawn as a polygon; no collector emits it */
    PRIM_BIG_POINT = 6,
    PRIM_ZIXEL = 7,
    /* Time Gate: polygons with a texture (material byte), 9 and 10 with a
     * u, v byte pair per vertex after the points. */
    PRIM_POLY_TEXTURE = 8,
    PRIM_POLY_TEXTURE_UV = 9,
    PRIM_POLY_TEXTURE_UV2 = 10
};

/* Type word (+8) of an animated bone record. */
enum bone_type {
    BONE_ROTATE = 0,
    BONE_TRANSLATE = 1,
    BONE_ZOOM = 2               /* factor = value + 256, 8.8 fixed point */
};

/* Screen bounding box of the last body drawn or measured, 0x4398b8..0x4398be. */
struct r3d_bbox {
    s16 xmin, ymin, xmax, ymax;
};

extern struct r3d_bbox g_r3d_bbox;

/* Result of the point helpers: 0x43545c / 0x43545e / 0x435460. */
extern s16 g_r3d_x, g_r3d_y, g_r3d_z;

/* Result of r3d_project: 0x43548e / 0x435490. */
extern s16 g_r3d_screen_x, g_r3d_screen_y;

/* Box a zixel/bigpoint must lie outside of to be drawn, 0x4398c0..0x4398ca. */
extern s16 g_r3d_hide_box[6];

/* 0x424590 */
void r3d_set_camera_angles(s16 alpha, s16 beta, s16 gamma);
/* 0x42454b */
void r3d_set_camera_position(s16 x, s16 y, s16 z);
/* 0x435468..0x43546c */
void r3d_get_camera_position(s16 *x, s16 *y, s16 *z);
/* 0x4244be - centre x/y, z offset added before the divide, x/y focal. */
void r3d_set_projection(s16 centre_x, s16 centre_y, s16 z_offset,
                        s16 focal_x, s16 focal_y);
/* 0x42469c - camera orbiting a target point at a distance. */
void r3d_set_camera_target(s16 x, s16 y, s16 z, s16 alpha, s16 beta,
                           s16 gamma, s16 distance);
/* 0x42419a - points below this world y (camera relative) are dropped. */
void r3d_set_clip_y(s16 y);

/* 0x424710 - returns 0 when drawn, 1 when nothing was visible, 2 when the
 * body was rejected. Leaves the screen box in g_r3d_bbox. */
int r3d_draw_body(s16 x, s16 y, s16 z, s16 alpha, s16 beta, s16 gamma,
                  u8 *body);
/* 0x42497a - measures an animated body without drawing it. */
void r3d_measure_body(s16 x, s16 y, s16 z, s16 alpha, s16 beta, s16 gamma,
                      u8 *body);
/* 0x42490e - transforms a bare point list (count, points) without drawing. */
int r3d_transform_points(s16 x, s16 y, s16 z, s16 alpha, s16 beta, s16 gamma,
                         const u8 *points);

/* 0x424a18 - rotate a point; result in g_r3d_x/y/z. */
void r3d_rotate_point(s16 x, s16 y, s16 z, s16 alpha, s16 beta, s16 gamma);
/* 0x4241af - world to camera space; result in g_r3d_x/y/z. */
void r3d_world_to_camera(s16 x, s16 y, s16 z);
/* 0x424236 - camera space to screen; result in g_r3d_screen_x/y. */
void r3d_project(s16 x, s16 y, s16 z);
/* 0x42428b - 2D rotation; result in g_r3d_x / g_r3d_y. */
void r3d_rotate_2d(s16 x, s16 y, s16 angle);

/* 0x4227f4 - bounding box of the transformed model space points. */
void r3d_points_bounds(s16 *out_zv);

/* Accessors of the transformed points, used by the hot point lookup. */
const s16 *r3d_model_points(void);       /* 0x436028 */
const s16 *r3d_projected_points(void);   /* 0x4356c8 */

#endif /* AITD_R3D_H */
