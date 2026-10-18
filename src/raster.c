/*
 * raster.c - 2D drawing primitives of the hand written renderer.
 *
 * The originals are register-call assembly routines. Each function here
 * follows its routine step by step, including the 16 bit arithmetic and the
 * order in which the clip tests are made, because the output has to match
 * the original pixel for pixel.
 */

#include <string.h>

#include "aitd.h"
#include "exe_tables.h"
#include "platform.h"
#include "raster.h"

struct raster_state g_raster;
s16 g_poly_detail = 1;  /* 0x435060 */

/* 0x41de6c saves exactly one level. */
static u8 *s_saved_target;
static s16 s_saved_pitch;

void raster_init(u8 *screen, int width, int height)
{
    g_raster.target = screen;
    g_raster.pitch = (s16)width;
    g_raster.width = (s16)width;
    g_raster.height = (s16)height;
    g_raster.origin_x = 0;
    g_raster.origin_y = 0;
    raster_reset_clip();
}

/* 0x421120: eax = xmin, edx = ymin, ebx = xmax, ecx = ymax. */
void raster_set_clip(s16 xmin, s16 ymin, s16 xmax, s16 ymax)
{
    if (xmin < 0) {
        xmin = 0;
    }
    if (ymin < 0) {
        ymin = 0;
    }
    if (xmax >= g_raster.width) {
        xmax = (s16)(g_raster.width - 1);
    }
    if (ymax >= g_raster.height) {
        ymax = (s16)(g_raster.height - 1);
    }

    g_raster.clip_xmin = xmin;
    g_raster.clip_ymin = ymin;
    g_raster.clip_xmax = xmax;
    g_raster.clip_ymax = ymax;
}

void raster_reset_clip(void)
{
    raster_set_clip(0, 0, (s16)(g_raster.width - 1), (s16)(g_raster.height - 1));
}

/* 0x41de6c. The original also negates the pitch for a bottom-up DIB; the
 * host buffers here are always top-down. */
void raster_push_target(u8 *buffer)
{
    s_saved_target = g_raster.target;
    s_saved_pitch = g_raster.pitch;
    g_raster.target = buffer;
}

/* 0x41ded6 */
void raster_pop_target(void)
{
    g_raster.target = s_saved_target;
    g_raster.pitch = s_saved_pitch;
}

/* 0x41b88c - eax = x, edx = y, ebx = image. No clipping. */
void raster_blit(s16 x, s16 y, const struct raster_image *image)
{
    u8 *dst = g_raster.target + (size_t)y * g_raster.pitch + x;
    const u8 *src = image->pixels;
    int row;

    for (row = 0; row < image->height; row++) {
        memcpy(dst, src, image->width);
        dst += g_raster.pitch;
        src += image->width;
    }
}

/* ---- polygons (0x401acc) ----------------------------------------------- */

#define POLY_MAX_POINTS 512

/* The two point lists 0x43c018 / 0x43d018 swap roles after every clip pass. */
static s16 s_poly_a[POLY_MAX_POINTS * 2 + 4];
static s16 s_poly_b[POLY_MAX_POINTS * 2 + 4];
static s16 *s_poly_in;   /* 0x42f96c */
static s16 *s_poly_out;  /* 0x42f970 */
static int s_poly_count; /* 0x42f97e */
static int s_poly_clipped; /* 0x42f982 */

/* Edge tables, one x per scanline. 0x43e018 and the spare point list. */
static s16 s_edge_a[1024];
static s16 s_edge_b[1024];

static s16 mul_div16(s16 a, s16 b, s16 c)
{
    /* imul r16 then idiv r16: 32 bit product, 16 bit quotient. */
    return (s16)(((s32)a * (s32)b) / (s32)c);
}

static void poly_swap_lists(void)
{
    s16 *t = s_poly_in;

    s_poly_in = s_poly_out;
    s_poly_out = t;
}

/*
 * 0x4022d0 (left), 0x40238c (right). Clip against a vertical line at bp.
 * The intersection is computed from whichever endpoint has the smaller x.
 */
static int poly_clip_x(s16 bp, int keep_greater)
{
    const s16 *in = s_poly_in;
    s16 *out = s_poly_out;
    int produced = 0;
    s16 cx, bx;
    int n;

    s_poly_clipped = 1;

    cx = *in++;
    bx = *in++;

    for (n = s_poly_count; n > 0; n--) {
        s16 dx = *in++;
        s16 ax = *in++;
        int cur_in = keep_greater ? (cx >= bp) : (cx <= bp);
        int next_in = keep_greater ? (dx >= bp) : (dx <= bp);

        if (cur_in) {
            *out++ = cx;
            *out++ = bx;
            produced++;
        }

        if (cur_in != next_in) {
            s16 x0 = cx, y0 = bx, x1 = dx, y1 = ax;

            if (!(x1 > x0)) {
                s16 t;

                t = x0; x0 = x1; x1 = t;
                t = y0; y0 = y1; y1 = t;
            }

            *out++ = bp;
            *out++ = (s16)(y0 + mul_div16((s16)(bp - x0), (s16)(y1 - y0),
                                          (s16)(x1 - x0)));
            produced++;
        }

        cx = dx;
        bx = ax;
    }

    out[0] = s_poly_out[0];
    out[1] = s_poly_out[1];
    poly_swap_lists();

    return produced;
}

/* 0x402448 (top), 0x4024f5 (bottom). No endpoint reordering here. */
static int poly_clip_y(s16 bp, int keep_greater)
{
    const s16 *in = s_poly_in;
    s16 *out = s_poly_out;
    int produced = 0;
    s16 bx, cx;
    int n;

    s_poly_clipped = 1;

    bx = *in++;
    cx = *in++;

    for (n = s_poly_count; n > 0; n--) {
        s16 dx = *in++;
        s16 ax = *in++;
        int cur_in = keep_greater ? (cx >= bp) : (cx <= bp);
        int next_in = keep_greater ? (ax >= bp) : (ax <= bp);

        if (cur_in) {
            *out++ = bx;
            *out++ = cx;
            produced++;
        }

        if (cur_in != next_in) {
            *out++ = (s16)(bx + mul_div16((s16)(bp - cx), (s16)(dx - bx),
                                          (s16)(ax - cx)));
            *out++ = bp;
            produced++;
        }

        bx = dx;
        cx = ax;
    }

    out[0] = s_poly_out[0];
    out[1] = s_poly_out[1];
    poly_swap_lists();

    return produced;
}

static void fill_span(u8 *dst, int count, u8 colour)
{
    if (count > 0) {
        memset(dst, colour, (size_t)count);
    }
}

void raster_poly(const s16 *points, int count, int type, int colour)
{
    s16 xmin = 0x7fff, ymin = 0x7fff;
    s16 xmax = (s16)0x8000, ymax = (s16)0x8000;
    s16 *dst;
    s16 *table_a, *table_b, *table_cur;
    u8 col = (u8)colour;
    int i, rows;
    u8 *row;

    if (count <= 0 || count > POLY_MAX_POINTS) {
        return;
    }

    /* 0x401b4e: offset the points and take the bounds. */
    dst = s_poly_a;
    for (i = 0; i < count; i++) {
        s16 x = (s16)(points[i * 2] + g_raster.origin_x);
        s16 y = (s16)(points[i * 2 + 1] + g_raster.origin_y);

        if (x < xmin) xmin = x;
        if (x > xmax) xmax = x;
        if (y < ymin) ymin = y;
        if (y > ymax) ymax = y;

        *dst++ = x;
        *dst++ = y;
    }
    dst[0] = s_poly_a[0];
    dst[1] = s_poly_a[1];

    if (ymax == ymin) {
        return;  /* 0x401ba8 */
    }

    s_poly_in = s_poly_a;
    s_poly_out = s_poly_b;
    s_poly_count = count;
    s_poly_clipped = 0;

    /* 0x401bcb..0x401c5d: clip only against the edges that are crossed. */
    if (xmin < g_raster.clip_xmin) {
        int n = poly_clip_x(g_raster.clip_xmin, 1);
        if (n <= 2) return;
        s_poly_count = n;
    }
    if (xmax > g_raster.clip_xmax) {
        int n = poly_clip_x(g_raster.clip_xmax, 0);
        if (n <= 2) return;
        s_poly_count = n;
    }
    if (ymin < g_raster.clip_ymin) {
        int n = poly_clip_y(g_raster.clip_ymin, 1);
        if (n <= 2) return;
        s_poly_count = n;
    }
    if (ymax > g_raster.clip_ymax) {
        int n = poly_clip_y(g_raster.clip_ymax, 0);
        if (n <= 2) return;
        s_poly_count = n;
    }

    /* 0x401c80: clipping moved the vertical extent. */
    if (s_poly_clipped) {
        ymin = 0x7fff;
        ymax = (s16)0x8000;
        for (i = 0; i < s_poly_count; i++) {
            s16 y = s_poly_in[i * 2 + 1];
            if (y < ymin) ymin = y;
            if (y > ymax) ymax = y;
        }
        if (ymin == ymax) {
            return;
        }
    }

    /* 0x401cbd: walk the edges into the two scanline tables. Every time an
     * edge starts on the top or bottom row the walker switches table. */
    table_a = s_edge_a;
    table_b = s_edge_b;
    table_cur = table_a;
    {
        const s16 *p = s_poly_in;
        s16 bx = *p++;
        s16 cx = *p++;

        for (i = 0; i < s_poly_count; i++) {
            s16 dx = *p++;
            s16 ax = *p++;
            s16 nx = dx, ny = ax;

            if (ax != cx) {
                s16 *out;
                int step;
                s16 x, count_y, run, err;
                int k;

                if (cx == ymin || cx == ymax) {
                    s16 *t = table_a;
                    table_a = table_b;
                    table_b = t;
                    table_cur = table_a;
                }

                if (!(bx < dx)) {
                    s16 t;
                    t = dx; dx = bx; bx = t;
                    t = cx; cx = ax; ax = t;
                }

                out = table_cur + cx;
                if (cx < ax) {
                    step = 1;
                    count_y = (s16)(ax - cx);
                } else {
                    step = -1;
                    count_y = (s16)(cx - ax);
                }

                x = bx;
                *out = x;
                out += step;
                run = (s16)(dx - bx);
                err = (s16)((u16)count_y >> 1);

                for (k = 0; k < count_y; k++) {
                    err = (s16)(err + run);
                    while (err >= count_y) {
                        err = (s16)(err - count_y);
                        x++;
                    }
                    *out = x;
                    out += step;
                }
            }

            bx = nx;
            cx = ny;
        }
    }

    /* 0x401d7d: fill. */
    rows = (u8)(ymax - ymin + 1);
    row = g_raster.target + (size_t)ymin * g_raster.pitch;
    {
        const s16 *right = table_b + ymin;
        const s16 *left = table_a + ymin;

        if (!g_poly_detail || type == 0 || type > 6) {
            for (i = 0; i < rows; i++) {
                s16 a = right[i], c = left[i];
                if (a > c) { s16 t = a; a = c; c = t; }
                fill_span(row + a, c - a + 1, col);
                row += g_raster.pitch;
            }
        } else if (type == 1) {
            /* 0x402243: noise from the routine's own code bytes. */
            int noise = POLY_NOISE_ORIGIN;
            for (i = 0; i < rows; i++) {
                s16 a = right[i], c = left[i];
                int w, k;
                u8 *d;
                if (a > c) { s16 t = a; a = c; c = t; }
                w = c - a + 1;
                d = row + a;
                for (k = 0; k < w; k++) {
                    int idx = noise;
                    if (idx < 0) idx = 0;
                    if (idx >= (int)sizeof(g_poly_noise)) idx = sizeof(g_poly_noise) - 1;
                    *d++ = (u8)(col + (g_poly_noise[idx] & 3));
                    noise++;
                }
                noise -= 10;
                row += g_raster.pitch;
            }
        } else if (type == 2) {
            /* 0x40228e: transparent, keeps the low nibble of the pixel. */
            for (i = 0; i < rows; i++) {
                s16 a = right[i], c = left[i];
                int w, k;
                u8 *d;
                if (a > c) { s16 t = a; a = c; c = t; }
                w = c - a + 1;
                d = row + a;
                for (k = 0; k < w; k++, d++) {
                    *d = (u8)((*d & 0x0f) + col);
                }
                row += g_raster.pitch;
            }
        } else if (type == 3 || type == 6) {
            /* 0x401e5f / 0x401fbc: horizontal ramp over 16 shades. */
            for (i = 0; i < rows; i++) {
                s16 a = right[i], c = left[i];
                int w, k;
                const u8 *ramp;
                u8 *d;
                u8 shade = (type == 3) ? col : (u8)(col + 0x0f);
                if (a > c) { s16 t = a; a = c; c = t; }
                w = c - a;
                ramp = g_poly_ramp + (size_t)w * 16;
                d = row + a;
                w++;
                if (w < 16) {
                    for (k = 0; k < w; k++) {
                        *d++ = (type == 3) ? (u8)(ramp[k] + shade)
                                           : (u8)(shade - ramp[k]);
                    }
                } else {
                    for (k = 0; k < 16; k++) {
                        fill_span(d, ramp[k], shade);
                        d += ramp[k];
                        shade = (type == 3) ? (u8)(shade + 1) : (u8)(shade - 1);
                    }
                }
                row += g_raster.pitch;
            }
        } else {
            /* 0x40211e (4) / 0x4021a3 (5): vertical ramp, bouncing inside
             * the 16 shade block; type 5 steps every second row. */
            s8 step = 1;
            int every = (type == 5) ? 2 : 1;
            int counter = every;
            u8 shade = col;
            u8 low = (u8)(col & 0x0f);

            if (low == 0x0f) {
                step = -1;
            }
            for (i = 0; i < rows; i++) {
                s16 a = right[i], c = left[i];
                if (a > c) { s16 t = a; a = c; c = t; }
                fill_span(row + a, c - a + 1, shade);
                if (--counter == 0) {
                    counter = every;
                    shade = (u8)(shade + step);
                    low = (u8)(low + step);
                    if (low == 0 || low == 0x0f) {
                        step = (s8)-step;
                    }
                }
                row += g_raster.pitch;
            }
        }
    }
}

/* ---- lines and points -------------------------------------------------- */

/* 0x425cfc - ax = x1, bx = x2, dx = y. */
static void raster_hline(s16 x1, s16 x2, s16 y, u8 colour)
{
    x1 = (s16)(x1 + g_raster.origin_x);
    x2 = (s16)(x2 + g_raster.origin_x);
    y = (s16)(y + g_raster.origin_y);

    if (x1 > x2) {
        s16 t = x1; x1 = x2; x2 = t;
    }
    if (x1 > g_raster.clip_xmax || x2 < g_raster.clip_xmin
        || y < g_raster.clip_ymin || y > g_raster.clip_ymax) {
        return;
    }
    if (x1 < g_raster.clip_xmin) x1 = g_raster.clip_xmin;
    if (x2 > g_raster.clip_xmax) x2 = g_raster.clip_xmax;

    fill_span(g_raster.target + (size_t)y * g_raster.pitch + x1, x2 - x1 + 1, colour);
}

/* 0x425c4c - ax = x, dx = y1, bx = y2. */
static void raster_vline(s16 x, s16 y1, s16 y2, u8 colour)
{
    u8 *d;
    int n;

    x = (s16)(x + g_raster.origin_x);
    y1 = (s16)(y1 + g_raster.origin_y);
    y2 = (s16)(y2 + g_raster.origin_y);

    if (!(y1 < y2)) {
        s16 t = y1; y1 = y2; y2 = t;
    }
    if (x < g_raster.clip_xmin || x > g_raster.clip_xmax
        || y1 > g_raster.clip_ymax || y2 < g_raster.clip_ymin) {
        return;
    }
    if (y1 < g_raster.clip_ymin) y1 = g_raster.clip_ymin;
    if (y2 > g_raster.clip_ymax) y2 = g_raster.clip_ymax;

    d = g_raster.target + (size_t)y1 * g_raster.pitch + x;
    for (n = y2 - y1 + 1; n > 0; n--) {
        *d = colour;
        d += g_raster.pitch;
    }
}

/* 0x41f72c - eax = x1, edx = y1, ebx = x2, ecx = y2, colour on the stack. */
void raster_line(s16 x1, s16 y1, s16 x2, s16 y2, int colour)
{
    s16 dxl, dyl;
    s16 cx, bx;
    u8 *d;
    int step;
    u8 c = (u8)colour;

    if (y1 == y2) {
        raster_hline(x1, x2, y1, c);
        return;
    }
    if (x1 == x2) {
        raster_vline(x1, y1, y2, c);
        return;
    }
    if (!(x1 < x2)) {
        s16 t;
        t = x1; x1 = x2; x2 = t;
        t = y1; y1 = y2; y2 = t;
    }

    x1 = (s16)(x1 + g_raster.origin_x);
    y1 = (s16)(y1 + g_raster.origin_y);
    x2 = (s16)(x2 + g_raster.origin_x);
    y2 = (s16)(y2 + g_raster.origin_y);

    if (x2 < g_raster.clip_xmin || x1 > g_raster.clip_xmax) {
        return;
    }

    dxl = (s16)(x2 - x1);
    dyl = (s16)(y2 - y1);

    if (x2 > g_raster.clip_xmax) {
        y2 = (s16)(mul_div16((s16)(g_raster.clip_xmax - x1), dyl, dxl) + y1);
        x2 = g_raster.clip_xmax;
        dxl = (s16)(x2 - x1);
        dyl = (s16)(y2 - y1);
    }
    if (x1 < g_raster.clip_xmin) {
        y1 = (s16)(mul_div16((s16)(g_raster.clip_xmin - x1), dyl, dxl) + y1);
        x1 = g_raster.clip_xmin;
        dxl = (s16)(x2 - x1);
        dyl = (s16)(y2 - y1);
    }

    if (y1 < g_raster.clip_ymin && y2 < g_raster.clip_ymin) {
        return;
    }
    if (y1 > g_raster.clip_ymax && y2 > g_raster.clip_ymax) {
        return;
    }

    if (y2 < g_raster.clip_ymin) {
        x2 = (s16)(mul_div16((s16)(g_raster.clip_ymin - y1), dxl, dyl) + x1);
        y2 = g_raster.clip_ymin;
        dxl = (s16)(x2 - x1);
        dyl = (s16)(y2 - y1);
    } else if (y2 > g_raster.clip_ymax) {
        x2 = (s16)(mul_div16((s16)(g_raster.clip_ymax - y1), dxl, dyl) + x1);
        y2 = g_raster.clip_ymax;
        dxl = (s16)(x2 - x1);
        dyl = (s16)(y2 - y1);
    }

    if (y1 < g_raster.clip_ymin) {
        x1 = (s16)(mul_div16((s16)(g_raster.clip_ymin - y1), dxl, dyl) + x1);
        y1 = g_raster.clip_ymin;
    } else if (y1 > g_raster.clip_ymax) {
        x1 = (s16)(mul_div16((s16)(g_raster.clip_ymax - y1), dxl, dyl) + x1);
        y1 = g_raster.clip_ymax;
    }

    /* 0x41f98a: Bresenham from (x1, y1). */
    bx = (s16)(y2 - y1);
    cx = (s16)(x2 - x1);
    d = g_raster.target + (s32)y1 * g_raster.pitch + x1;
    step = g_raster.pitch;
    if (!(bx > 0)) {
        bx = (s16)-bx;
        step = -step;
    }

    if (cx >= bx) {
        u16 err = (u16)((u16)cx >> 1);
        u16 len = (u16)cx;
        int n = (u16)cx + 1;

        *d++ = c;
        while (--n > 0) {
            err = (u16)(err + (u16)bx);
            if (!(err < len)) {
                err = (u16)(err - len);
                d += step;
            }
            *d++ = c;
        }
    } else {
        u16 err = (u16)((u16)bx >> 1);
        u16 len = (u16)bx;
        int n = (u16)bx + 1;

        *d = c;
        d += step;
        while (--n > 0) {
            err = (u16)(err + (u16)cx);
            if (!(err < len)) {
                err = (u16)(err - len);
                d++;
            }
            *d = c;
            d += step;
        }
    }
}

/* 0x424f78 - eax = x, edx = y, bl = colour. */
void raster_point(s16 x, s16 y, int colour)
{
    x = (s16)(x + g_raster.origin_x);
    y = (s16)(y + g_raster.origin_y);

    if (x < g_raster.clip_xmin || x > g_raster.clip_xmax
        || y < g_raster.clip_ymin || y > g_raster.clip_ymax) {
        return;
    }

    g_raster.target[(s32)y * g_raster.pitch + x] = (u8)colour;
}

/* 0x425268 - eax = x1, ebx = x2, edx = y1, ecx = y2, colour on the stack. */
void raster_rect(s16 x1, s16 x2, s16 y1, s16 y2, int colour)
{
    u8 *d;
    int rows;

    x1 = (s16)(x1 + g_raster.origin_x);
    x2 = (s16)(x2 + g_raster.origin_x);
    y2 = (s16)(y2 + g_raster.origin_y);
    y1 = (s16)(y1 + g_raster.origin_y);

    if (x1 > x2) { s16 t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { s16 t = y1; y1 = y2; y2 = t; }

    if (x1 > g_raster.clip_xmax || x2 < g_raster.clip_xmin
        || y1 > g_raster.clip_ymax || y2 < g_raster.clip_ymin) {
        return;
    }
    if (x1 < g_raster.clip_xmin) x1 = g_raster.clip_xmin;
    if (y1 < g_raster.clip_ymin) y1 = g_raster.clip_ymin;
    if (x2 > g_raster.clip_xmax) x2 = g_raster.clip_xmax;
    if (y2 > g_raster.clip_ymax) y2 = g_raster.clip_ymax;

    d = g_raster.target + (s32)y1 * g_raster.pitch + x1;
    for (rows = y2 - y1 + 1; rows > 0; rows--) {
        fill_span(d, x2 - x1 + 1, (u8)colour);
        d += g_raster.pitch;
    }
}

/* 0x402cb5 - eax = x1, edx = y1, ebx = x2, ecx = y2, then a full screen
 * picture: copy that rectangle of it to the same place on the target. */
void raster_copy_rect(s16 x1, s16 y1, s16 x2, s16 y2, const u8 *picture)
{
    int y;

    for (y = y1; y <= y2; y++) {
        memcpy(g_raster.target + (size_t)y * g_raster.pitch + x1,
               picture + (size_t)y * 320 + x1, (size_t)(x2 - x1 + 1));
    }
}

/* 0x4201ec - clear the whole target. */
void screen_clear(void)
{
    memset(g_raster.target, 0, (size_t)g_raster.pitch * (size_t)g_raster.height);
}

/* 0x41c7cf - put the target on the window (WinGBitBlt in the original). */
void screen_present(void)
{
    platform_video_present(g_raster.target, g_raster.width, g_raster.height);
}
