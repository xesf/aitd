/*
 * raster.h - 2D drawing primitives of the hand written renderer.
 *
 * Every routine draws into the current target (0x4351ca, pitch 0x4351c8)
 * through the current clip rectangle (0x445c92..0x445c98) and the current
 * origin offset (0x445c9a / 0x445c9c).
 */

#ifndef AITD_RASTER_H
#define AITD_RASTER_H

#include "aitd.h"

struct raster_state {
    u8 *target;        /* 0x4351ca */
    s16 pitch;         /* 0x4351c8 */
    s16 width;         /* 0x4351c6 */
    s16 height;        /* 0x4351c4 */
    s16 clip_xmin;     /* 0x445c96 */
    s16 clip_xmax;     /* 0x445c98 */
    s16 clip_ymin;     /* 0x445c92 */
    s16 clip_ymax;     /* 0x445c94 */
    s16 origin_x;      /* 0x445c9a */
    s16 origin_y;      /* 0x445c9c */
};

extern struct raster_state g_raster;

/* Byte 0x435060: when clear every polygon is drawn flat. */
extern s16 g_poly_detail;

/* A rectangle of pixels; the layout of g_blit_width/height/source (0x444854). */
struct raster_image {
    u16 width;
    u16 height;
    u8 *pixels;
};

/* A raster_image value; some compilers (Open Watcom) take no run-time
 * values in a structure initialiser. */
static inline struct raster_image raster_image_make(int width, int height, u8 *pixels)
{
    struct raster_image image;

    image.width = (u16)width;
    image.height = (u16)height;
    image.pixels = pixels;
    return image;
}

void raster_init(u8 *screen, int width, int height);
void screen_clear(void);    /* 0x4201ec */
void screen_present(void);  /* 0x41c7cf */

/* 0x421120 - clamps to the target size. */
void raster_set_clip(s16 xmin, s16 ymin, s16 xmax, s16 ymax);
void raster_reset_clip(void);

/* 0x41de6c / 0x41ded6 - redirect drawing into another buffer and back. */
void raster_push_target(u8 *buffer);
void raster_pop_target(void);

/* 0x41b88c - copy a whole image to (x, y) of the target, unclipped. */
void raster_blit(s16 x, s16 y, const struct raster_image *image);

/* 0x401acc - points are (x, y) s16 pairs. */
void raster_poly(const s16 *points, int count, int type, int colour);

void raster_copy_rect(s16 x1, s16 y1, s16 x2, s16 y2, const u8 *picture); /* 0x402cb5 */
void raster_line(s16 x1, s16 y1, s16 x2, s16 y2, int colour);  /* 0x41f72c */
void raster_point(s16 x, s16 y, int colour);                     /* 0x424f78 */
void raster_rect(s16 x1, s16 x2, s16 y1, s16 y2, int colour);    /* 0x425268 */

#endif /* AITD_RASTER_H */
