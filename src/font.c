/*
 * font.c - bitmap font rendering.
 *
 * Reconstructed from:
 *   font_set_data     0x420eea  header parse
 *   font_draw_string  0x420c78  glyph walk and pen advance
 *   font_blit_glyph   0x420e6f  the inner copy
 *   font_set_colour   0x4029cb  fills the remap table
 *
 * Blob layout, confirmed against itd_ress.pak entry 1 (206 glyphs, 8761
 * bytes): an 8 byte header, then one 10 byte record per glyph from
 * first_char to last_char, then the pixel data.
 *
 *   +0 u32 absolute offset of the pixel data within the blob, 0 for blank
 *   +4 u8  width
 *   +5 u8  height
 *   +6 s8  x offset, subtracted from the pen
 *   +7 s8  y offset, subtracted from the pen
 *   +8 u8  advance, added to the inter-character spacing
 *   +9 u8  padding, always zero in the shipped font
 *
 * The offsets are absolute because the blitter recovers the base as
 * glyph_table - 8, i.e. the blob start (0x420e4d). Space has offset 0 with
 * width and height 0 and an advance of 7, which is why the drawing branch is
 * skipped for it (0x420d03).
 *
 * Pixels are one byte each and zero is transparent. With no colour table the
 * bytes are palette indices; with one set they are remapped through it first.
 */

#include <string.h>

#include "aitd.h"
#include "font.h"

#define GLYPH_RECORD_SIZE 10
#define FONT_FLAG_HIDDEN 0x02  /* 0x43522e bit 1 suppresses drawing */

/* Mirrors of the globals font_set_data writes. */
static const u8 *s_blob;         /* 0x43521c */
static const u8 *s_glyphs;       /* 0x435228, blob + 8 */
static u8 s_first_char;          /* 0x43522c */
static u8 s_last_char;           /* 0x43522d */
static u8 s_flags;               /* 0x43522e */
static const u8 *s_colour_table; /* 0x435220 */
static u16 s_spacing;            /* 0x435226 */

/* 0x420eea. Returns the previous blob so callers can save and restore it.
 * Glyph records start at blob + 8. */
void *font_set_data(void *blob)
{
    void *previous = (void *)s_blob;
    const u8 *bytes = blob;

    s_blob = bytes;
    if (bytes == NULL) {
        s_glyphs = NULL;
        return previous;
    }
    s_flags = bytes[0];
    s_first_char = bytes[2];
    s_last_char = bytes[3];
    s_glyphs = bytes + 8;
    return previous;
}

/* 0x420f47 - xchg eax, ds:0x435220 */
void *font_set_color_table(void *table)
{
    void *previous = (void *)s_colour_table;

    s_colour_table = table;
    return previous;
}

/* 0x420f4e - xchg ax, ds:0x435226 */
u16 font_set_spacing(u16 spacing)
{
    u16 previous = s_spacing;

    s_spacing = spacing;
    return previous;
}

void font_bind(const void *blob, const u8 *colour_table, u16 spacing)
{
    const u8 *bytes = blob;

    s_blob = bytes;
    s_colour_table = colour_table;
    s_spacing = spacing;

    if (bytes == NULL) {
        s_glyphs = NULL;
        return;
    }

    s_flags = bytes[0];
    s_first_char = bytes[2];
    s_last_char = bytes[3];
    s_glyphs = bytes + 8;
}

static const u8 *glyph_record(u8 ch)
{
    if (s_glyphs == NULL || ch < s_first_char || ch > s_last_char) {
        return NULL;  /* 0x420cae, 0x420cb6 */
    }

    return s_glyphs + (size_t)(ch - s_first_char) * GLYPH_RECORD_SIZE;
}

static u32 read_u32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

int font_glyph_advance(u8 ch)
{
    const u8 *record = glyph_record(ch);

    if (record == NULL) {
        return 0;
    }

    return record[8] + s_spacing;  /* 0x420cee */
}

int font_string_width(const char *text)
{
    int width = 0;

    for (; *text != '\0'; text++) {
        width += font_glyph_advance((u8)*text);
    }

    return width;
}

int font_height(void)
{
    int tallest = 0;
    int ch;

    for (ch = s_first_char; ch <= s_last_char; ch++) {
        const u8 *record = glyph_record((u8)ch);

        if (record != NULL && record[5] > tallest) {
            tallest = record[5];
        }
    }

    return tallest;
}

/*
 * 0x420e6f. Clipping is handled by the caller in the original through the
 * rectangles at 0x445c92 and 0x445c96; here the target bounds do the same job.
 */
static void blit_glyph(u8 *target, int pitch, int height,
                       const u8 *pixels, int width, int rows,
                       int x, int y)
{
    int row, col;

    for (row = 0; row < rows; row++) {
        int ty = y + row;

        if (ty < 0 || ty >= height) {
            continue;
        }

        for (col = 0; col < width; col++) {
            int tx = x + col;
            u8 value = pixels[row * width + col];

            if (tx < 0 || tx >= pitch) {
                continue;
            }

            if (s_colour_table != NULL) {
                value = s_colour_table[value];  /* 0x420ec7 */
            }

            if (value != 0) {                   /* 0 stays transparent */
                target[ty * pitch + tx] = value;
            }
        }
    }
}

/*
 * 0x420c78
 */
void font_draw_string(u8 *target, int pitch, int height,
                      int x, int y, const char *text)
{
    if (s_glyphs == NULL || target == NULL || text == NULL) {
        return;
    }

    if (s_flags & FONT_FLAG_HIDDEN) {  /* 0x420e82 */
        return;
    }

    for (; *text != '\0'; text++) {    /* 0x420ca6: NUL terminates */
        u8 ch = (u8)*text;
        const u8 *record = glyph_record(ch);
        u32 offset;
        int width, rows;

        if (record == NULL) {
            continue;
        }

        offset = read_u32(record);
        width = record[4];
        rows = record[5];

        /* 0x420d03: a null data offset means nothing to draw, e.g. space. */
        if (offset != 0 && width > 0 && rows > 0) {
            blit_glyph(target, pitch, height,
                       s_blob + offset, width, rows,
                       x - (s8)record[6],   /* 0x420cd4 */
                       y - (s8)record[7]);  /* 0x420da4 */
        }

        x += record[8] + s_spacing;  /* 0x420cee */
    }
}

/*
 * 0x4029cb - builds the remap table so glyph pixels come out in one colour.
 * Index 0 stays 0 so transparency survives the remap.
 */
void font_set_colour(u8 *table, u8 colour)
{
    int i;

    table[0] = 0;
    for (i = 1; i < 256; i++) {
        table[i] = colour;
    }
}
